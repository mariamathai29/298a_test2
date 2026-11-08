`default_nettype none
`timescale 1ns / 1ps

/* This testbench just instantiates the module and makes some convenient wires
   that can be driven / tested by the cocotb test.py.

   Each pin the tests use has its own name:
     inputs  sclk_in, cs_n, mosi_u, mosi_r   SPI input port   -> ui_in[0], [1], [2], [4]
             adapt_en, mu_sel[2:0]           control          -> uio_in[0], [3:1]
     outputs sclk_out, fs_out                output port      <- uo_out[0], [1]
             sdo_y, sdo_e, sdo_w             y, e, weight     <- uo_out[2], [3], [4]
             ovf, sat                        sticky flags     <- uo_out[6], [7]
*/
module tb ();

  // Dump the signals to a FST file. You can view it with gtkwave or surfer.
  initial begin
    $dumpfile("tb.fst");
    $dumpvars(0, tb);
    #1;
  end

  // Wire up the inputs and outputs:
  reg clk;
  reg rst_n;
  reg ena;
  reg sclk_in, cs_n, mosi_u, mosi_r;
  reg adapt_en;
  reg [2:0] mu_sel;
  wire [7:0] ui_in  = {3'b000, mosi_r, 1'b0, mosi_u, cs_n, sclk_in};
  wire [7:0] uio_in = {4'b0000, mu_sel, adapt_en};
  wire [7:0] uo_out;
  wire [7:0] uio_out;
  wire [7:0] uio_oe;
`ifdef GL_TEST
  wire VPWR = 1'b1;
  wire VGND = 1'b0;
`endif

  wire sclk_out = uo_out[0];
  wire fs_out   = uo_out[1];
  wire sdo_y    = uo_out[2];
  wire sdo_e    = uo_out[3];
  wire sdo_w    = uo_out[4];
  wire ovf      = uo_out[6];
  wire sat      = uo_out[7];

  tt_um_adaptive_filter user_project (

      // Include power ports for the Gate Level test:
`ifdef GL_TEST
      .VPWR(VPWR),
      .VGND(VGND),
`endif

      .ui_in  (ui_in),    // Dedicated inputs
      .uo_out (uo_out),   // Dedicated outputs
      .uio_in (uio_in),   // IOs: Input path
      .uio_out(uio_out),  // IOs: Output path
      .uio_oe (uio_oe),   // IOs: Enable path (active high: 0=input, 1=output)
      .ena    (ena),      // enable - goes high when design is selected
      .clk    (clk),      // clock
      .rst_n  (rst_n)     // not reset
  );

endmodule
