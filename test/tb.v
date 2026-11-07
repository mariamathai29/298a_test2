`timescale 1ns/1ps
`default_nettype none

// cocotb wrapper for the 4-tap prototype: names the pins that test.py drives
// and watches. Clocks, stimulus and checking are all in test.py.
//
// Inputs (driven by test.py):
//   clk, rst_n
//   sclk_in, cs_n, mosi_u, mosi_r   SPI input port   -> ui_in[0], [1], [2], [4]
//   debug_i1                        readout trigger  -> ui_in[7]
//   uio_ctl[5:0]                    adapt_en, mu_sel -> uio_in[5:0]
// Outputs (watched by test.py):
//   sclk_out, fs_out, sdo_y, sdo_e  output port      <- uo_out[0], [1], [2], [3]
//   ovf, sat                        debug flags      <- uo_out[6], [7]
module tb;
    reg        clk      = 1'b0;
    reg        rst_n    = 1'b0;
    reg        sclk_in  = 1'b0;
    reg        cs_n     = 1'b1;
    reg        mosi_u   = 1'b0;
    reg        mosi_r   = 1'b0;
    reg        debug_i1 = 1'b0;
    reg  [5:0] uio_ctl  = 6'd0;

    wire [7:0] ui_in  = {debug_i1, 2'b00, mosi_r, 1'b0, mosi_u, cs_n, sclk_in};
    wire [7:0] uio_in = {2'b00, uio_ctl};
    wire [7:0] uo_out, uio_out, uio_oe;

    wire sclk_out = uo_out[0];
    wire fs_out   = uo_out[1];
    wire sdo_y    = uo_out[2];
    wire sdo_e    = uo_out[3];
    wire ovf      = uo_out[6];
    wire sat      = uo_out[7];

`ifdef USE_POWER_PINS
    wire VPWR = 1'b1;
    wire VGND = 1'b0;
`endif

    tt_um_adaptive_filter dut (
`ifdef USE_POWER_PINS
        .VPWR(VPWR), .VGND(VGND),
`endif
        .ui_in(ui_in), .uo_out(uo_out), .uio_in(uio_in), .uio_out(uio_out),
        .uio_oe(uio_oe), .ena(1'b1), .clk(clk), .rst_n(rst_n)
    );

    // Timing simulation: back-annotate the routed netlist with a signoff SDF
`ifdef SDF_FILE
    initial $sdf_annotate(`SDF_FILE, dut);
`endif
endmodule
