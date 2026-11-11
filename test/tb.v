`default_nettype none
`timescale 1ns / 1ps

/* This testbench just instantiates the module and makes some convenient wires
   that can be driven / tested by the cocotb test.py (and, RTL only, separate
   instances for the unit tests test_af_core.py and test_spi_unit.py: see the
   end of the file).

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

`ifndef GL_TEST
  // ===========================================================================
  // Unit-test instance for src/filter_core.v (used by test_af_core.py).
  // RTL only: the gate-level netlist no longer contains this module.
  // It shares `clk` with the top-level design and is otherwise independent.
  // ===========================================================================
  reg        core_rst;
  reg        core_adapt_en;
  reg  [2:0] core_mu_sel;
  reg        core_d_pre;
  reg        core_d_stb;
  reg  [7:0] core_d_u;
  reg  [7:0] core_d_r;
  wire       core_t_load;
  wire [7:0] core_t_y;
  wire [7:0] core_t_e;
  wire [7:0] core_t_w;
  wire       core_sat_flag;
  wire       core_ovf_flag;

  filter_core u_core (
      .clk     (clk),
      .rst     (core_rst),
      .adapt_en(core_adapt_en),
      .mu_sel  (core_mu_sel),
      .d_pre   (core_d_pre),
      .d_stb   (core_d_stb),
      .d_u     (core_d_u),
      .d_r     (core_d_r),
      .t_load  (core_t_load),
      .t_y     (core_t_y),
      .t_e     (core_t_e),
      .t_w     (core_t_w),
      .sat_flag(core_sat_flag),
      .ovf_flag(core_ovf_flag)
  );

  // ===========================================================================
  // Unit-test instances for src/spi.v (used by test_spi_unit.py).
  // RTL only: the gate-level netlist no longer contains these modules.
  // They share `clk` with the top-level design and are otherwise independent.
  // ===========================================================================

  // ------------------------------------------------ spi_transmit on its own
  reg        tx_rst_n;
  reg        tx_load;
  reg  [7:0] tx_d_a;
  reg  [7:0] tx_d_b;
  reg  [7:0] tx_d_c;
  wire       tx_sclk;
  wire       tx_fs;
  wire [2:0] tx_sdo;

  spi_transmit u_tx (
      .clk   (clk),
      .rst_n (tx_rst_n),
      .load  (tx_load),
      .d_a   (tx_d_a),
      .d_b   (tx_d_b),
      .d_c   (tx_d_c),
      .sclk_o(tx_sclk),
      .fs_o  (tx_fs),
      .sdo   (tx_sdo)
  );

  // -------------------------------- spi_receive on its own (sclk/cs_n by test)
  reg        rx_sclk;
  reg        rx_cs_n;
  reg        rx_rst_n;
  reg        rx_mosi_u;
  reg        rx_mosi_r;
  wire       rx_d_tog;
  wire [7:0] rx_d_hold_u;
  wire [7:0] rx_d_hold_r;

  spi_receive u_rx (
      .sclk    (rx_sclk),
      .cs_n    (rx_cs_n),
      .rst_n   (rx_rst_n),
      .mosi_u  (rx_mosi_u),
      .mosi_r  (rx_mosi_r),
      .d_tog   (rx_d_tog),
      .d_hold_u(rx_d_hold_u),
      .d_hold_r(rx_d_hold_r)
  );

  // ------------------------------------------------ spi_transmit -> spi_receive
  // sdo[0] (d_a) -> mosi_u, sdo[1] (d_b) -> mosi_r, sclk_o -> sclk, fs_o -> cs_n
  // (sdo[2], d_c, has no receiver lane)
  reg        lb_rst_n;
  reg        lb_load;
  reg  [7:0] lb_d_a;
  reg  [7:0] lb_d_b;
  reg  [7:0] lb_d_c;
  wire       lb_sclk;
  wire       lb_fs;
  wire [2:0] lb_sdo;
  wire       lb_d_tog;
  wire [7:0] lb_d_hold_u;
  wire [7:0] lb_d_hold_r;

  spi_transmit u_lb_tx (
      .clk   (clk),
      .rst_n (lb_rst_n),
      .load  (lb_load),
      .d_a   (lb_d_a),
      .d_b   (lb_d_b),
      .d_c   (lb_d_c),
      .sclk_o(lb_sclk),
      .fs_o  (lb_fs),
      .sdo   (lb_sdo)
  );

  spi_receive u_lb_rx (
      .sclk    (lb_sclk),
      .cs_n    (lb_fs),
      .rst_n   (lb_rst_n),
      .mosi_u  (lb_sdo[0]),
      .mosi_r  (lb_sdo[1]),
      .d_tog   (lb_d_tog),
      .d_hold_u(lb_d_hold_u),
      .d_hold_r(lb_d_hold_r)
  );
`endif

endmodule
