`default_nettype none

// Adaptive filter core (clk domain), 4-tap prototype v5: FIR with a serial
// radix-4 Booth multiplier, sign-sign LMS CUE, output sequencing and readout.
//
// Data: taps x[] (Q1.7) and weights w[] (Q2.12) are circular shift registers
// ("rings"). The multiplier, the update and the readout only ever look at
// position 0 (x[0], w[0]); to reach the other taps the rings rotate by one.
// There is no read mux and no write decoder: each register bit only talks to
// its neighbour.
//
// One step per input sample (23 clk). Controls are registered, so each row
// shows what the flops hold during that cycle; the rings move at its end.
//
//   cycle  state  cnt   go rot  what happens
//   0      IDLE   -     1  0    u enters x[0] (oldest tap drops out), r is
//                               latched, acc = 2 (rounding constant)
//   1-16   MAC    0-15  0  1    pp_q = Booth digit * w[0] for the tap at
//                               x[0]; acc += previous pp_q; rotate
//   17     MAC    16    0  0    drain: acc += last pp_q
//   18     FIN    -     0  0    y = sat8(acc >> 8), e = sat8(r - y)
//   19-22  UPD    0-3   0  1    w[0] += mu * sign(e) * sign(x[0]) (if
//                               adapting) as it rotates to w[3]; the y/e
//                               frame is loaded into the serializer at cnt 0
//
// The 16 MAC issues run in Horner order: Booth digit outer (3 down to 0), tap
// inner. The sum that ends digit groups 3, 2 and 1 is shifted left by 2, which
// also scales the initial acc = 2 by 4^3 into the 2^7 rounding constant.
// 20 rotations per step = 5 full turns, so both rings end in their starting
// order. At clk = 40 MHz the input may run up to 1.74 MS/s (spec: 13 SCLK_IN
// periods at 20 MHz per sample = 26 clk, 1.54 MS/s). A sample that arrives
// while a step or readout is in progress is dropped and sets ovf_flag.
//
// Readout: rising edge of debug_i1 while adapt_en = 0. Steps stop; four
// 20-cycle slots, each loading one frame (lane a = w[0] top 8 bits, Q2.6;
// lane b = x[0]) and then rotating the rings once.
//
// mu = 2^mu_sel weight-register LSBs (mu_sel = 0: 2^-12 ... 7: 2^-5).
// Weights and taps are cleared by rst_n only.
//
// Naming: *_n is the value a register takes at the next clock edge, *_q a
// registered copy of a signal of the same name.
//
// Timing (built against 50 MHz, signed off at 40 MHz at all corners, worst
// 125 C, 3.0 V):
//   - every ring / accumulator / serializer control is a flip-flop, decided a
//     cycle early from the next-state logic; the ring controls are replicated
//     (ctl_ff) so each copy drives one ring segment
//   - the Booth partial product is registered (pp_q)
//   - the rounding constant is preloaded into acc, and the Horner shift sits
//     after the adder, so neither adds to a critical path
//   - acc and pp_q have no enable: acc is only read in FIN, and pp_q is
//     cleared at step start, so they simply run every cycle
//   - FIN computes r - y, and sign(r - y) by comparison, in parallel with
//     y's saturation check
//   - the weight update forms w0 + mu and w0 - mu in parallel (registered
//     one-hot mu, saturation from the sign bits) and picks one late
//   - the serializer loads on a registered pulse: no start/ready handshake
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
    // ======================================================================
    // 1. Step FSM and readout sequencer
    // ======================================================================
    localparam S_IDLE = 2'd0, S_MAC = 2'd1, S_FIN = 2'd2, S_UPD = 2'd3;

    reg  [1:0] state;
    reg  [4:0] cnt;          // MAC: {digit group, tap}, 16 = drain; UPD: tap
    reg  [1:0] digit_q;      // Booth digit of the next MAC issue (3 .. 0)

    reg        ro_trig_d;    // ro_trig one cycle late, for edge detection
    reg        ro_req;       // readout requested, waiting for the step to end
    reg        ro_act;       // readout running
    reg  [1:0] ro_slot;      // which of the 4 frames
    reg  [4:0] ro_cyc;       // cycle within a 20-cycle slot

    wire       go_f;         // step start, FSM copy (section 2)

    reg  [1:0] state_n;
    reg  [4:0] cnt_n;
    reg        ro_req_n, ro_act_n;
    reg  [1:0] ro_slot_n;
    reg  [4:0] ro_cyc_n;
    always @(*) begin
        state_n = state;
        cnt_n   = cnt;
        case (state)
            S_IDLE: if (go_f) begin
                state_n = S_MAC;
                cnt_n   = 5'd0;
            end
            S_MAC: begin
                cnt_n = cnt + 5'd1;
                if (cnt == 5'd16)
                    state_n = S_FIN;
            end
            S_FIN: begin
                state_n = S_UPD;
                cnt_n   = 5'd0;
            end
            S_UPD: begin
                cnt_n = cnt + 5'd1;
                if (cnt == 5'd3)
                    state_n = S_IDLE;
            end
        endcase

        // A readout waits for the FSM to be idle, then runs 4 slots
        ro_req_n  = ro_req;
        ro_act_n  = ro_act;
        ro_slot_n = ro_slot;
        ro_cyc_n  = ro_cyc;
        if (ro_trig && !ro_trig_d && !adapt_en)
            ro_req_n = 1'b1;
        if (ro_act) begin
            ro_cyc_n = (ro_cyc == 5'd19) ? 5'd0 : ro_cyc + 5'd1;
            if (ro_cyc == 5'd19) begin
                ro_slot_n = ro_slot + 2'd1;
                if (ro_slot == 2'd3) begin
                    ro_act_n = 1'b0;
                    ro_req_n = 1'b0;
                end
            end
        end else if (ro_req && state == S_IDLE && !go_f) begin
            ro_act_n  = 1'b1;
            ro_slot_n = 2'd0;
            ro_cyc_n  = 5'd0;
        end
    end

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            state     <= S_IDLE;
            cnt       <= 5'd0;
            digit_q   <= 2'd3;
            ro_trig_d <= 1'b0;
            ro_req    <= 1'b0;
            ro_act    <= 1'b0;
            ro_slot   <= 2'd0;
            ro_cyc    <= 5'd0;
        end else begin
            state     <= state_n;
            cnt       <= cnt_n;
            digit_q   <= ~cnt_n[3:2];       // digit group 0 .. 3 = digit 3 .. 0
            ro_trig_d <= ro_trig;
            ro_req    <= ro_req_n;
            ro_act    <= ro_act_n;
            ro_slot   <= ro_slot_n;
            ro_cyc    <= ro_cyc_n;
        end
    end

    // ======================================================================
    // 2. Controls, decided one cycle early from the next state
    // ======================================================================
    //   go   : start a step (shift u in, latch r, clear acc)
    //   rot  : rotate both rings by one
    //   load : load a frame into the serializer
    wire go_n   = d_pre && (state_n == S_IDLE) && !ro_req_n && !ro_act_n;
    wire rot_n  = ((state_n == S_MAC) && !cnt_n[4]) || (state_n == S_UPD) ||
                  (ro_act_n && ro_cyc_n == 5'd1);
    wire load_n = ((state_n == S_UPD) && (cnt_n == 5'd0)) ||
                  (ro_act_n && ro_cyc_n == 5'd0);

    // Replicated control flops: one rotate copy per x ring half and per
    // weight, one step-start copy per x ring half, one for the datapath and
    // one for the FSM. All copies of a control hold the same value.
    wire [1:0] rot_x, go_xs;
    wire [3:0] rot_w;
    wire       go_a;
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

    reg tx_load_q;
    always @(posedge clk or negedge rst_n)
        if (!rst_n) tx_load_q <= 1'b0;
        else        tx_load_q <= load_n;

    // ======================================================================
    // 3. Rings
    // ======================================================================
    reg signed [7:0]  x [0:3];        // Q1.7; x[0] = newest between steps
    reg signed [13:0] w [0:3];        // Q2.12
    wire signed [7:0]  x0 = x[0];
    wire signed [13:0] w0 = w[0];

    wire signed [13:0] w_new;         // updated w[0] (section 6)
    wire               w_write;       // write w_new instead of w[0]

    // x ring: halves {x[0], x[1]} and {x[2], x[3]}, each with its own controls.
    // go shifts u in (x[3] drops out); rot moves every tap one place towards 0.
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
        else if (rot_w[3]) w[3] <= w_write ? w_new : w[0];

    // ======================================================================
    // 4. MAC: y = sum of x[k] * w[k], one radix-4 Booth digit per cycle
    // ======================================================================
    reg signed [19:0] acc;            // Q5.15, plus the rounding constant
    reg signed [11:0] pp_q;           // registered Booth partial product
    reg               horner_shift_q; // pp_q ends digit group 3, 2 or 1

    // Partial product: Booth digit (-2 .. +2) of x[0] times the top 10 bits
    // of w[0]. The digit is read from 3 overlapping bits of x[0].
    wire               mac_issue  = (state == S_MAC) && !cnt[4];
    wire        [8:0]  x0_ext     = {x0, 1'b0};                       // x[-1] = 0
    wire        [2:0]  booth_bits = x0_ext[{1'b0, digit_q, 1'b0} +: 3];  // {x[2d+1], x[2d], x[2d-1]}
    wire signed [9:0]  w0_mul     = w0[13:4];                         // Q2.8
    reg  signed [11:0] pp;
    always @(*) begin
        case (booth_bits)
            3'b001, 3'b010: pp =  {{2{w0_mul[9]}}, w0_mul};           // +1
            3'b011:         pp =  {w0_mul[9], w0_mul, 1'b0};          // +2
            3'b100:         pp = -{w0_mul[9], w0_mul, 1'b0};          // -2
            3'b101, 3'b110: pp = -{{2{w0_mul[9]}}, w0_mul};           // -1
            default:        pp = 12'sd0;                              //  0
        endcase
    end

    // Horner: the sum that ends a digit group (except the last) is shifted
    wire signed [19:0] acc_sum = acc + {{8{pp_q[11]}}, pp_q};
    wire signed [19:0] acc_n   = horner_shift_q ? (acc_sum <<< 2) : acc_sum;

    // No reset, no enable: the first MAC cycle adds the cleared pp_q, and
    // whatever acc accumulates after the last MAC cycle is never read.
    always @(posedge clk) begin
        acc            <= go_a ? 20'sd2 : acc_n;    // 2 * 4^3 = 128: round half up
        pp_q           <= go_a ? 12'sd0 : pp;
        horner_shift_q <= mac_issue && (cnt[1:0] == 2'd3) && (cnt[3:2] != 2'd3);
    end

    // ======================================================================
    // 5. FIN: y = sat8(acc >> 8), e = sat8(r - y), sign(e)
    // ======================================================================
    // r - y is computed for both saturated cases and the plain case in
    // parallel with the saturation check; overflow of e from its sign bits.
    reg signed [7:0] r_q;             // r of this step, latched on go
    reg signed [7:0] y_q, e_q;        // the y/e frame
    reg              e_pos_q, e_neg_q;

    wire signed [11:0] acc_y    = acc[19:8];                    // already rounded
    wire               y_sat_hi = !acc_y[11] && (|acc_y[10:7]);
    wire               y_sat_lo =  acc_y[11] && !(&acc_y[10:7]);
    wire signed [7:0]  y_plain  = acc_y[7:0];
    wire signed [7:0]  y        = y_sat_hi ? 8'sd127 : y_sat_lo ? -8'sd128 : y_plain;

    wire signed [8:0]  r9        = {r_q[7], r_q};
    wire signed [8:0]  e_plain   = r9 - {acc_y[7], acc_y[7:0]};
    wire signed [8:0]  e_if_y_hi = r9 - 9'sd127;
    wire signed [8:0]  e_if_y_lo = r9 + 9'sd128;
    wire signed [8:0]  e_wide    = y_sat_hi ? e_if_y_hi : y_sat_lo ? e_if_y_lo : e_plain;  // Q2.7
    wire               e_sat_hi  = !e_wide[8] &&  e_wide[7];
    wire               e_sat_lo  =  e_wide[8] && !e_wide[7];
    wire signed [7:0]  e         = e_sat_hi ? 8'sd127 : e_sat_lo ? -8'sd128 : e_wide[7:0];

    // sign(e) by comparison, alongside the subtraction (y saturated at 127:
    // e = r - 127 <= 0; at -128: e = r + 128 >= 0)
    wire e_pos = y_sat_hi ? 1'b0 : y_sat_lo ? (r_q != -8'sd128) : (r_q > y_plain);
    wire e_neg = y_sat_hi ? (r_q != 8'sd127) : y_sat_lo ? 1'b0 : (r_q < y_plain);

    always @(posedge clk) begin
        if (go_a)
            r_q <= d_r;
        if (state == S_FIN) begin
            y_q     <= y;
            e_q     <= e;
            e_pos_q <= e_pos;
            e_neg_q <= e_neg;
        end
    end

    // ======================================================================
    // 6. UPD: w[0] += mu * sign(e) * sign(x[0]), saturated
    // ======================================================================
    // w0 + mu and w0 - mu in parallel, each saturated from its sign bits;
    // sign(e) * sign(x) only drives the final select.
    reg [7:0] mu_q;                   // one-hot mu, in weight-register LSBs
    reg       adapt_q;                // adapt_en, sampled once per step in FIN
    reg       x_pos_q, x_neg_q;       // sign of the tap at position 0

    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            mu_q    <= 8'd1;
            adapt_q <= 1'b0;
        end else begin
            mu_q <= 8'd1 << mu_sel;
            if (state == S_FIN)
                adapt_q <= adapt_en;
        end
    end

    // Sign of the tap that will be at position 0 next cycle: x[0] while the
    // rings stand still (FIN), x[1] while they rotate (UPD)
    always @(posedge clk) begin
        x_pos_q <= (state == S_UPD) ? (!x[1][7] && |x[1][6:0]) : (!x0[7] && |x0[6:0]);
        x_neg_q <= (state == S_UPD) ? x[1][7] : x0[7];
    end

    wire        [14:0] mu_ext      = {7'd0, mu_q};
    wire               w_inc       = (e_pos_q && x_pos_q) || (e_neg_q && x_neg_q);
    wire               w_dec       = (e_pos_q && x_neg_q) || (e_neg_q && x_pos_q);
    wire        [14:0] w_plus      = {w0[13], w0} + mu_ext;
    wire        [14:0] w_minus     = {w0[13], w0} - mu_ext;
    wire               w_plus_ovf  = w_plus[14] ^ w_plus[13];
    wire               w_minus_ovf = w_minus[14] ^ w_minus[13];
    wire signed [13:0] w_plus_sat  = w_plus_ovf  ? {w_plus[14],  {13{~w_plus[14]}}}  : w_plus[13:0];
    wire signed [13:0] w_minus_sat = w_minus_ovf ? {w_minus[14], {13{~w_minus[14]}}} : w_minus[13:0];
    wire               w_clipped   = (w_inc && w_plus_ovf) || (w_dec && w_minus_ovf);
    assign w_new   = w_inc ? w_plus_sat : w_dec ? w_minus_sat : w0;
    assign w_write = (state == S_UPD) && adapt_q;

    // ======================================================================
    // 7. Sticky flags
    // ======================================================================
    //   ovf_flag : a sample arrived while a step or readout was running
    //   sat_flag : y, e or a weight saturated
    always @(posedge clk or negedge rst_n) begin
        if (!rst_n) begin
            ovf_flag <= 1'b0;
            sat_flag <= 1'b0;
        end else begin
            if (d_stb && !go_f)
                ovf_flag <= 1'b1;
            if (state == S_FIN && (y_sat_hi || y_sat_lo || e_sat_hi || e_sat_lo))
                sat_flag <= 1'b1;
            if (w_write && w_clipped)
                sat_flag <= 1'b1;
        end
    end

    // ======================================================================
    // 8. Output
    // ======================================================================
    assign tx_load = tx_load_q;
    assign tx_a    = ro_act ? w0[13:6] : y_q;
    assign tx_b    = ro_act ? x0       : e_q;

    wire _unused = &{1'b0, acc[7:0]};

endmodule
