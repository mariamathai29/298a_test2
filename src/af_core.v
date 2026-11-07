`default_nettype none

// Adaptive filter core (clk domain), 4-tap prototype v5: FIR with a serial
// radix-4 Booth multiplier, sign-sign LMS CUE, output sequencing and readout.
//
// Taps x[] and weights w[] are circular shift registers. The MAC, the update
// and the readout only ever look at position 0, and every step rotates both
// rings by one, so there is no read mux and no write decoder: each register
// bit only talks to its neighbour.
//
// v5 is built against a 50 MHz target and signed off at the 40 MHz operating
// spec at all corners (worst: 125 C, 3.0 V). Timing techniques:
//   - the Booth partial product is registered (pp_q)
//   - every ring / accumulator / serializer control is a flip-flop, decided a
//     cycle early from the next-state logic; the ring controls are replicated
//     (ctl_ff) so each copy drives one ring segment
//   - the rounding constant is preloaded into acc, and the Horner shift sits
//     after the adder, so neither adds to a critical path
//   - acc and pp_q have no enable: acc is only read in FIN, and pp_q is
//     cleared at step start, so they simply run every cycle
//   - FIN computes r - y, and sign(r - y) by comparison, in parallel with
//     y's saturation check
//   - the weight update forms w0 + mu and w0 - mu in parallel (registered
//     one-hot mu, saturation from the sign bits) and picks one late
//   - the serializer loads on a registered pulse: no start/ready handshake
//
// One step per input sample, started by d_stb (new word in the SCLK_IN holding
// registers); d_pre is the same strobe one cycle earlier.
//   IDLE : on d_stb, shift u into the delay line, latch r, acc = 2
//   MAC  : 17 cycles in Horner order, Booth digit outer (3 down to 0), tap
//          inner. Cycles 0-15 form digit * w[0][13:4] for the tap at position
//          0 into pp_q and rotate; cycles 1-16 add pp_q into acc, and the sum
//          that ends digit groups 3, 2 and 1 is shifted left by 2. Horner
//          scales the initial 2 by 4^3, which adds the 2^7 rounding constant.
//   FIN  : y = sat8(acc >> 8), e = sat8(r - y) into y_q / e_q
//   UPD  : 4 cycles, w[0] updated (if adapting) as it rotates to position 3;
//          the y/e frame is loaded on the first UPD cycle
// 23 cycles per step: at clk = 40 MHz the input may run up to 1.74 MS/s
// (spec: 13 SCLK_IN periods at 20 MHz per sample = 26 clk, 1.54 MS/s).
// A strobe while a step or readout is in progress is dropped and sets
// ovf_flag.
//
// Readout: rising edge of debug_i1 while adapt_en = 0. Steps stop; four
// 20-cycle slots, each loading one frame (lane a = w[k] top 8 bits, Q2.6;
// lane b = x[k]) and then rotating the rings once.
//
// mu = 2^mu_sel weight-register LSBs (mu_sel = 0: 2^-12 ... 7: 2^-5).
// Weights and taps are cleared by rst_n only.
module af_core (
    input  wire        clk,
    input  wire        rst_n,

    // Control pins (already synchronized to clk)
    input  wire        adapt_en,
    input  wire [2:0]  mu_sel,
    input  wire        ro_trig,

    // New sample (one-cycle strobe; data from the SCLK_IN holding registers)
    input  wire        d_pre,        // d_stb one cycle early
    input  wire        d_stb,
    input  wire [7:0]  d_u,
    input  wire [7:0]  d_r,

    // Output serializer (idle whenever tx_load is pulsed)
    output wire        tx_load,
    output wire [7:0]  tx_a,         // y, or weight during readout
    output wire [7:0]  tx_b,         // e, or tap during readout

    output reg         sat_flag,
    output reg         ovf_flag
);
    localparam S_IDLE = 2'd0, S_MAC = 2'd1, S_FIN = 2'd2, S_UPD = 2'd3;

    reg        [1:0]  state;
    reg        [4:0]  p;              // MAC: {digit group, tap}, 16 = drain; UPD: tap
    reg        [1:0]  dig_q;          // Booth digit for the next issue (3 .. 0)
    reg signed [13:0] w [0:3];        // Q2.12, ring
    reg signed [7:0]  x [0:3];        // Q1.7, ring; x[0] = newest between steps
    reg signed [19:0] acc;            // Q5.15, plus the rounding constant
    reg signed [11:0] pp_q;           // registered Booth partial product
    reg               gend_q;         // pp_q ends digit group 3, 2 or 1
    reg               xp_q, xn_q;     // sign of the tap at position 0 (UPD)
    reg        [7:0]  mu_q;           // one-hot mu, in weight-register LSBs
    reg signed [7:0]  r_q;
    reg signed [7:0]  y_q, e_q;
    reg               se_pos, se_neg, adapt_q;
    reg               ro_trig_d, ro_req, ro_act;
    reg        [1:0]  ro_idx;
    reg        [4:0]  ro_cnt;         // cycle within a 20-cycle readout slot
    reg               tx_load_q;

    wire signed [7:0]  x0 = x[0];
    wire signed [13:0] w0 = w[0];

    // ---------------- Booth radix-4 partial product ----------------
    wire        mac_issue = (state == S_MAC) && !p[4];
    wire [1:0]  dig  = dig_q;                       // digit 3, 2, 1, 0
    wire [8:0]  xe   = {x0, 1'b0};                  // x with implicit x[-1] = 0
    wire [2:0]  bth  = xe[{1'b0, dig, 1'b0} +: 3];  // {x[2d+1], x[2d], x[2d-1]}
    wire signed [9:0]  wm  = w0[13:4];              // Q2.8
    reg  signed [11:0] pp;
    always @(*) begin
        case (bth)
            3'b001, 3'b010: pp =  {{2{wm[9]}}, wm};
            3'b011:         pp =  {wm[9], wm, 1'b0};
            3'b100:         pp = -{wm[9], wm, 1'b0};
            3'b101, 3'b110: pp = -{{2{wm[9]}}, wm};
            default:        pp = 12'sd0;
        endcase
    end
    // Horner: the sum that ends a digit group (except the last) is shifted
    wire signed [19:0] acc_sum = acc + {{8{pp_q[11]}}, pp_q};
    wire signed [19:0] acc_n   = gend_q ? (acc_sum <<< 2) : acc_sum;

    // ---------------- FIN ----------------
    // r - y computed for both saturated cases and the plain case in parallel
    // with the saturation check; overflow of e from its sign bits.
    wire signed [11:0] acc_sh = acc[19:8];                  // already rounded
    wire               y_hi   = !acc_sh[11] && (|acc_sh[10:7]);
    wire               y_lo   =  acc_sh[11] && !(&acc_sh[10:7]);
    wire signed [7:0]  y      = y_hi ? 8'sd127 : y_lo ? -8'sd128 : acc_sh[7:0];
    wire signed [8:0]  r9     = {r_q[7], r_q};
    wire signed [8:0]  e_pl   = r9 - {acc_sh[7], acc_sh[7:0]};
    wire signed [8:0]  e_hi_y = r9 - 9'sd127;
    wire signed [8:0]  e_lo_y = r9 + 9'sd128;
    wire signed [8:0]  e_int  = y_hi ? e_hi_y : y_lo ? e_lo_y : e_pl;   // Q2.7
    wire               e_hi   = !e_int[8] &&  e_int[7];
    wire               e_lo   =  e_int[8] && !e_int[7];
    wire signed [7:0]  e_sat  = e_hi ? 8'sd127 : e_lo ? -8'sd128 : e_int[7:0];
    // sign(e) by comparison, alongside the subtraction (y saturated at 127:
    // e = r - 127 <= 0; at -128: e = r + 128 >= 0)
    wire signed [7:0]  y_pl   = acc_sh[7:0];
    wire               e_pos  = y_hi ? 1'b0 : y_lo ? (r_q != -8'sd128) : (r_q > y_pl);
    wire               e_neg  = y_hi ? (r_q != 8'sd127) : y_lo ? 1'b0 : (r_q < y_pl);

    // ---------------- UPD (weight at position 0) ----------------
    // w0 + mu and w0 - mu in parallel, each saturated from its sign bits;
    // sign(e) * sign(x) only drives the final select.
    wire        [14:0] mu_s  = {7'd0, mu_q};
    wire               up    = (se_pos && xp_q) || (se_neg && xn_q);
    wire               dn    = (se_pos && xn_q) || (se_neg && xp_q);
    wire        [14:0] w_p   = {w0[13], w0} + mu_s;
    wire        [14:0] w_m   = {w0[13], w0} - mu_s;
    wire               sat_p = w_p[14] ^ w_p[13];
    wire               sat_m = w_m[14] ^ w_m[13];
    wire signed [13:0] w_ps  = sat_p ? {w_p[14], {13{~w_p[14]}}} : w_p[13:0];
    wire signed [13:0] w_ms  = sat_m ? {w_m[14], {13{~w_m[14]}}} : w_m[13:0];
    wire signed [13:0] w_new = up ? w_ps : dn ? w_ms : w0;
    wire               w_sat = (up && sat_p) || (dn && sat_m);
    wire               upd_wr = (state == S_UPD) && adapt_q;

    // ---------------- Next-state logic ----------------
    wire go_x, go_a, go_f;            // step start (registered, replicated)
    reg [1:0] state_n;
    reg [4:0] p_n;
    reg       ro_req_n, ro_act_n;
    reg [1:0] ro_idx_n;
    reg [4:0] ro_cnt_n;
    always @(*) begin
        state_n = state;
        p_n     = p;
        case (state)
            S_IDLE: if (go_f) begin
                state_n = S_MAC;
                p_n     = 5'd0;
            end
            S_MAC: begin
                p_n = p + 5'd1;
                if (p == 5'd16)
                    state_n = S_FIN;
            end
            S_FIN: begin
                state_n = S_UPD;
                p_n     = 5'd0;
            end
            S_UPD: begin
                p_n = p + 5'd1;
                if (p == 5'd3)
                    state_n = S_IDLE;
            end
        endcase

        ro_req_n = ro_req;
        ro_act_n = ro_act;
        ro_idx_n = ro_idx;
        ro_cnt_n = ro_cnt;
        if (ro_trig && !ro_trig_d && !adapt_en)
            ro_req_n = 1'b1;
        if (ro_act) begin
            ro_cnt_n = (ro_cnt == 5'd19) ? 5'd0 : ro_cnt + 5'd1;
            if (ro_cnt == 5'd19) begin
                ro_idx_n = ro_idx + 2'd1;
                if (ro_idx == 2'd3) begin
                    ro_act_n = 1'b0;
                    ro_req_n = 1'b0;
                end
            end
        end else if (ro_req && state == S_IDLE && !go_f) begin
            ro_act_n = 1'b1;
            ro_idx_n = 2'd0;
            ro_cnt_n = 5'd0;
        end
    end

    // Next cycle's controls, from the next state only
    wire go_n     = d_pre && (state_n == S_IDLE) && !ro_req_n && !ro_act_n;
    wire rot_n    = ((state_n == S_MAC) && !p_n[4]) || (state_n == S_UPD) ||
                    (ro_act_n && ro_cnt_n == 5'd1);
    wire load_n   = ((state_n == S_UPD) && (p_n == 5'd0)) ||
                    (ro_act_n && ro_cnt_n == 5'd0);

    // Replicated control flops: one rotate copy per x ring half and per
    // weight, one step-start copy per x ring half and one for the rest
    wire [1:0] rot_x, go_xs;
    wire [3:0] rot_w;
    genvar g;
    generate
        for (g = 0; g < 2; g = g + 1) begin : g_x
            ctl_ff u_rot (.clk(clk), .rst_n(rst_n), .d(rot_n), .q(rot_x[g]));
            ctl_ff u_go  (.clk(clk), .rst_n(rst_n), .d(go_n),  .q(go_xs[g]));
        end
        for (g = 0; g < 4; g = g + 1) begin : g_w
            ctl_ff u_rot (.clk(clk), .rst_n(rst_n), .d(rot_n), .q(rot_w[g]));
        end
    endgenerate
    ctl_ff u_go_a (.clk(clk), .rst_n(rst_n), .d(go_n), .q(go_a));   // datapath
    ctl_ff u_go_f (.clk(clk), .rst_n(rst_n), .d(go_n), .q(go_f));   // FSM, flags
    assign go_x = go_xs[0];

    wire _unused = &{1'b0, acc[7:0], go_x};

    // ---------------- Rings ----------------
    // x ring: halves {x[0], x[1]} and {x[2], x[3]}, each with its own controls
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            x[0] <= 8'sd0;
            x[1] <= 8'sd0;
        end else if (go_xs[0]) begin
            x[0] <= d_u;
            x[1] <= x[0];
        end else if (rot_x[0]) begin
            x[0] <= x[1];
            x[1] <= x[2];
        end
    end
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            x[2] <= 8'sd0;
            x[3] <= 8'sd0;
        end else if (go_xs[1]) begin
            x[2] <= x[1];
            x[3] <= x[2];
        end else if (rot_x[1]) begin
            x[2] <= x[3];
            x[3] <= x[0];
        end
    end

    // w ring: one control copy per weight; the updated weight enters at w[3]
    always @(posedge clk or negedge rst_n)
        if (!rst_n)        w[0] <= 14'sd0;
        else if (rot_w[0]) w[0] <= w[1];
    always @(posedge clk or negedge rst_n)
        if (!rst_n)        w[1] <= 14'sd0;
        else if (rot_w[1]) w[1] <= w[2];
    always @(posedge clk or negedge rst_n)
        if (!rst_n)        w[2] <= 14'sd0;
        else if (rot_w[2]) w[2] <= w[3];
    always @(posedge clk or negedge rst_n)
        if (!rst_n)        w[3] <= 14'sd0;
        else if (rot_w[3]) w[3] <= upd_wr ? w_new : w[0];

    // ---------------- FSM and flags ----------------
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            state     <= S_IDLE;
            p         <= 5'd0;
            dig_q     <= 2'd3;
            tx_load_q <= 1'b0;
            adapt_q   <= 1'b0;
            sat_flag  <= 1'b0;
            ovf_flag  <= 1'b0;
            ro_trig_d <= 1'b0;
            ro_req    <= 1'b0;
            ro_act    <= 1'b0;
            ro_idx    <= 2'd0;
            ro_cnt    <= 5'd0;
            mu_q      <= 8'd1;
        end else begin
            state     <= state_n;
            p         <= p_n;
            dig_q     <= ~p_n[3:2];
            tx_load_q <= load_n;
            ro_trig_d <= ro_trig;
            ro_req    <= ro_req_n;
            ro_act    <= ro_act_n;
            ro_idx    <= ro_idx_n;
            ro_cnt    <= ro_cnt_n;
            mu_q      <= 8'd1 << mu_sel;

            if (d_stb && !go_f)
                ovf_flag <= 1'b1;
            if (state == S_FIN) begin
                adapt_q <= adapt_en;
                if (y_hi || y_lo || e_hi || e_lo)
                    sat_flag <= 1'b1;
            end
            if (upd_wr && w_sat)
                sat_flag <= 1'b1;
        end
    end

    // ---------------- Datapath registers without reset ----------------
    // acc runs every cycle: the first MAC cycle adds the cleared pp_q, and
    // whatever it accumulates after the last MAC cycle is never read.
    always @(posedge clk) begin
        if (go_a)
            r_q <= d_r;
        acc    <= go_a ? 20'sd2 : acc_n;    // 2 * 4^3 = 128: round half up
        pp_q   <= go_a ? 12'sd0 : pp;
        gend_q <= mac_issue && (p[1:0] == 2'd3) && (p[3:2] != 2'd3);
        if (state == S_FIN) begin
            y_q    <= y;
            e_q    <= e_sat;
            se_pos <= e_pos;
            se_neg <= e_neg;
        end
        // Sign of the tap that will be at position 0 next cycle: x[0] while
        // the rings stand still (FIN), x[1] while they rotate (UPD)
        xp_q <= (state == S_UPD) ? (!x[1][7] && |x[1][6:0]) : (!x0[7] && |x0[6:0]);
        xn_q <= (state == S_UPD) ? x[1][7] : x0[7];
    end

    // ---------------- Output ----------------
    assign tx_load = tx_load_q;
    assign tx_a    = ro_act ? w0[13:6] : y_q;
    assign tx_b    = ro_act ? x0       : e_q;

endmodule
