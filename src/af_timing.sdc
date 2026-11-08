# Common constraints: two asynchronous clocks plus a Tiny Tapeout GF180 I/O
# path model. Sourced by pnr.sdc and signoff.sdc, which set clk_period and
# sclk_period (ns) first. Based on librelane/scripts/base.sdc.
#
#   clk      : system clock on the clk pin
#   sclk_in  : SPI input clock on ui_in[0]
#   sclk_out : SCLK_OUT on uo_out[0], made by a flop from clk (clk / 2)
#
# ---------------------------------------------------------------------------
# I/O path model (all ns). A pin's external path is
#   board -> GF180 pad -> TT mux / routing -> tile port   (inputs)
#   tile port -> TT mux / routing -> GF180 pad -> board   (outputs)
#
#   GF180 pad, input  (bi_24t, PAD->Y)        0.9 .. 3.1   PDK liberty ff..ss
#   GF180 pad, output (bi_24t, A->PAD, 10-20 pF) 2.0 .. 6.7 PDK liberty ff..ss
#   TT mux and routing, each direction        3 .. 12      estimate (1)
#   Clock insertion, pad to tile clk port     4 .. 15      pad + mux (2)
#
# (1) TT publishes no GF180 numbers. Sky130 silicon (TT3.5, 22 C) measured
#     20 ns round trip through the input mux, output mux and both pads; with
#     the Sky130 pads at ~2-4 ns each that leaves ~6-8 ns per mux direction
#     at typical, scaled x1.6 for the slow corner.
# (2) TT's Sky130 spec allows up to 10 ns.
#
# Every interface of this design is either source-synchronous (SPI in, SPI
# out) or synchronised (control pins), so the absolute delays above cancel.
# What remains is the mismatch between pins of the same direction:
#   io_skew  : pad + mux + board mismatch between two pins, TT measured
#              < 2 ns (Sky130); 3.5 ns allowed here
#   host_skew: SPI master's SCLK-to-data launch skew
#   rx_tsu / rx_th : setup / hold of the receiver of SCLK_OUT frames
# ---------------------------------------------------------------------------
set io_skew   3.5
set host_skew 2.0
set rx_tsu    5.0
set rx_th     2.0

create_clock [get_ports clk]        -name clk     -period $clk_period
create_clock [get_ports {ui_in[0]}] -name sclk_in -period $sclk_period
set_clock_groups -asynchronous -group [get_clocks clk] -group [get_clocks sclk_in]

# SCLK_OUT, as it leaves the tile: clk / 2 through the spi_tx flop
create_generated_clock -name sclk_out -source [get_ports clk] -divide_by 2 \
    [get_ports {uo_out[0]}]

# --- SPI input (mode 0): the master changes CS/MOSI on falling SCLK_IN, the
# tile samples on rising, half a period later. Only skew between ui_in[0]
# and the data pins counts against that half period.
set spi_in [get_ports {ui_in[1] ui_in[2] ui_in[4]}]
set_input_delay -max [expr $host_skew + $io_skew]    -clock sclk_in -clock_fall $spi_in
set_input_delay -min [expr -($host_skew + $io_skew)] -clock sclk_in -clock_fall $spi_in

# --- SPI output: FS_OUT and the three data lanes change on falling SCLK_OUT and
# the receiver samples on rising SCLK_OUT, one clk period later. Checked
# against the generated clock at the port, so the tile's own clk-to-out skew
# between SCLK_OUT and data is analysed exactly; io_skew adds the outside.
set spi_out [get_ports {uo_out[1] uo_out[2] uo_out[3] uo_out[4]}]
set_output_delay -max [expr $rx_tsu + $io_skew]    -clock sclk_out $spi_out
set_output_delay -min [expr -($rx_th + $io_skew)]  -clock sclk_out $spi_out
# The data never changes on a rising SCLK_OUT edge (spi_tx shifts only when
# SCLK_OUT falls), so hold is against the change one clk later
set_multicycle_path -hold 1 -start -from [get_clocks clk] -to [get_clocks sclk_out]

# --- Control and status pins are asynchronous to everything: inputs go
# through 2-flop synchronisers (reset: async assert, sync release), outputs
# are static flags. No external timing relationship exists.
set async_in [get_ports {ui_in[3] ui_in[5] ui_in[6] ui_in[7] uio_in[*] rst_n ena}]
set_input_delay 0 -clock clk $async_in
set_false_path -from $async_in
set static_out [get_ports {uo_out[5] uo_out[6] uo_out[7] uio_out[*] uio_oe[*]}]
set_output_delay 0 -clock clk $static_out
set_false_path -to $static_out

# --- Electrical, from base.sdc
set_max_fanout $::env(MAX_FANOUT_CONSTRAINT) [current_design]
if { [info exists ::env(MAX_TRANSITION_CONSTRAINT)] } {
    set_max_transition $::env(MAX_TRANSITION_CONSTRAINT) [current_design]
}
if { [info exists ::env(MAX_CAPACITANCE_CONSTRAINT)] } {
    set_max_capacitance $::env(MAX_CAPACITANCE_CONSTRAINT) [current_design]
}

if { ![info exists ::env(SYNTH_CLK_DRIVING_CELL)] } {
    set ::env(SYNTH_CLK_DRIVING_CELL) $::env(SYNTH_DRIVING_CELL)
}
set clk_ports   [get_ports {clk ui_in[0]}]
set data_inputs [get_ports {ui_in[1] ui_in[2] ui_in[3] ui_in[4] ui_in[5] ui_in[6] ui_in[7] uio_in[*] rst_n ena}]
set_driving_cell \
    -lib_cell [lindex [split $::env(SYNTH_DRIVING_CELL) "/"] 0] \
    -pin [lindex [split $::env(SYNTH_DRIVING_CELL) "/"] 1] \
    $data_inputs
set_driving_cell \
    -lib_cell [lindex [split $::env(SYNTH_CLK_DRIVING_CELL) "/"] 0] \
    -pin [lindex [split $::env(SYNTH_CLK_DRIVING_CELL) "/"] 1] \
    $clk_ports

# The tile port drives the TT mux, not the board
set_load [expr $::env(OUTPUT_CAP_LOAD) / 1000.0] [all_outputs]

set_clock_uncertainty $::env(CLOCK_UNCERTAINTY_CONSTRAINT) [all_clocks]
set_clock_transition  $::env(CLOCK_TRANSITION_CONSTRAINT)  [get_clocks {clk sclk_in}]

set_timing_derate -early [expr 1 - $::env(TIME_DERATING_CONSTRAINT) / 100.0]
set_timing_derate -late  [expr 1 + $::env(TIME_DERATING_CONSTRAINT) / 100.0]

if { [info exists ::env(OPENLANE_SDC_IDEAL_CLOCKS)] && $::env(OPENLANE_SDC_IDEAL_CLOCKS) } {
    unset_propagated_clock [all_clocks]
} else {
    set_propagated_clock [all_clocks]
}
