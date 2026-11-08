`default_nettype none

// Adaptive filter (sign-sign LMS, 4 taps) for Tiny Tapeout, lean 1-tile
// prototype (v5, rotating rings). Operating spec: clk 40 MHz, SCLK_IN and
// SCLK_OUT 20 MHz, 13 SCLK_IN periods per sample (1.54 MS/s), one step per
// sample. Built against 50 MHz for margin.
//
// ui_in : [0] SCLK_IN  [1] CS_IN  [2] MOSI_u_in  [4] MOSI_r_in
//         [3], [5], [6], [7] unused
// uo_out: [0] SCLK_OUT [1] FS_OUT [2] MOSI_y_out [3] MOSI_e_out
//         [4] MOSI_w_out (monitor weight, one per frame)
//         [6] debug_o1 (sticky overrun) [7] debug_o2 (sticky saturation)
//         [5] tied low
// uio   : all inputs. [0] adapt_en [3:1] mu_sel  [7:4] unused
module tt_um_adaptive_filter (
    input  wire [7:0] ui_in,
    output wire [7:0] uo_out,
    input  wire [7:0] uio_in,
    output wire [7:0] uio_out,
    output wire [7:0] uio_oe,
    input  wire       ena,
    input  wire       clk,
    input  wire       rst_n
);
    assign uio_out = 8'h00;
    assign uio_oe  = 8'h00;

    // ---------------- clk-domain reset (async assert, sync deassert) -------
    reg [1:0] rst_sync;
    always @(posedge clk or negedge rst_n)
        if (!rst_n) rst_sync <= 2'b00;
        else        rst_sync <= {rst_sync[0], 1'b1};
    wire rst_n_s = rst_sync[1];

    // ---------------- Input port (SCLK_IN domain) ----------------
    wire       d_tog;
    wire [7:0] d_u, d_r;

    spi_receive u_rx (
        .sclk     (ui_in[0]),
        .cs_n     (ui_in[1]),
        .rst_n    (rst_n),
        .mosi_u   (ui_in[2]),
        .mosi_r   (ui_in[4]),
        .d_tog    (d_tog),
        .d_hold_u (d_u),
        .d_hold_r (d_r)
    );

    // ---------------- Synchronizers into clk ----------------
    // ctl = {mu_sel[2:0], adapt_en}
    wire [3:0] ctl_in = uio_in[3:0];
    reg  [3:0] ctl_s1, ctl_s2;
    reg  [2:0] d_s;
    always @(posedge clk or negedge rst_n_s) begin
        if (!rst_n_s) begin
            ctl_s1 <= 4'd0;
            ctl_s2 <= 4'd0;
            d_s    <= 3'd0;
        end else begin
            ctl_s1 <= ctl_in;
            ctl_s2 <= ctl_s1;
            d_s    <= {d_s[1:0], d_tog};
        end
    end
    wire d_stb = d_s[2] ^ d_s[1];
    wire d_pre = d_s[1] ^ d_s[0];        // d_stb one cycle early

    // ---------------- Core ----------------
    wire       tx_load, sat_flag, ovf_flag;
    wire [7:0] tx_a, tx_b, tx_c;

    af_core u_core (
        .clk      (clk),
        .rst_n    (rst_n_s),
        .adapt_en (ctl_s2[0]),
        .mu_sel   (ctl_s2[3:1]),
        .d_pre    (d_pre),
        .d_stb    (d_stb),
        .d_u      (d_u),
        .d_r      (d_r),
        .tx_load  (tx_load),
        .tx_a     (tx_a),
        .tx_b     (tx_b),
        .tx_c     (tx_c),
        .sat_flag (sat_flag),
        .ovf_flag (ovf_flag)
    );

    // ---------------- Output port (master, frame sync) ----------------
    wire       sclk_o, fs_o;
    wire [2:0] sdo;

    spi_transmit u_tx (
        .clk    (clk),
        .rst_n  (rst_n_s),
        .load   (tx_load),
        .d_a    (tx_a),
        .d_b    (tx_b),
        .d_c    (tx_c),
        .sclk_o (sclk_o),
        .fs_o   (fs_o),
        .sdo    (sdo)
    );

    assign uo_out = {sat_flag, ovf_flag, 1'b0, sdo, fs_o, sclk_o};

    wire _unused = &{ena, ui_in[7:5], ui_in[3], uio_in[7:4], 1'b0};

endmodule
