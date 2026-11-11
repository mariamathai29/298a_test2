# Sample testbench for a Tiny Tapeout project

This is a sample testbench for a Tiny Tapeout project. It uses [cocotb](https://docs.cocotb.org/en/stable/) to drive the DUT and check the outputs.
See below to get started or for more information, check the [website](https://tinytapeout.com/hdl/testing/).

## Setting up

1. Edit [Makefile](Makefile) and modify `PROJECT_SOURCES` to point to your Verilog files.
2. Edit [tb.v](tb.v) and replace `tt_um_example` with your module name.

## How to run

To run the RTL simulation (end-to-end tests, then the SPI and core unit tests):

```sh
make -B
```

To run one test module only, e.g. the core unit tests: `make -B COCOTB_TEST_MODULES=test_af_core`.

To run gatelevel simulation, first harden your project and copy `../runs/wokwi/results/final/verilog/gl/{your_module_name}.v` to `gate_level_netlist.v`.

Then run:

```sh
make -B GATES=yes
```

If you wish to save the waveform in VCD format instead of FST format, edit tb.v to use `$dumpfile("tb.vcd");` and then run:

```sh
make -B FST=
```

This will generate `tb.vcd` instead of `tb.fst`.

## Core unit tests

`test_af_core.py` (bench: `af_core_tb.py`) tests `filter_core` directly at its ports, without the SPI
interface, against the golden model `filter_golden.py`. It drives a separate core instance at the end
of `tb.v` (`u_core`, signals `core_*`). Like the SPI unit tests it runs in the RTL simulation (results
in `results.xml`), and not at gate level: the netlist has no separate core.

## SPI unit tests

`test_spi_unit.py` tests `spi_transmit` and `spi_receive` on their own and connected back to back
(transmitter into receiver), using separate instances at the end of `tb.v`. They run in the RTL
simulation together with the end-to-end tests (results in `results.xml`), and not at gate level.

## How to view the waveform file

Using GTKWave

```sh
gtkwave tb.fst tb.gtkw
```

Using Surfer

```sh
surfer tb.fst
```
