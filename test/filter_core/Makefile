# cocotb unit tests for af_core, driven directly at its ports (no SPI, no pins)
#
#   make                                   every test
#   make COCOTB_TEST_FILTER=test_reset     one test
#   make WAVES=1                           also write $(SIM_BUILD)/af_core.fst
#
# ctl_ff uses its behavioural model (AF_BEHAVIORAL_FF): no PDK needed.

SIM ?= icarus
TOPLEVEL_LANG ?= verilog
SRC_DIR = $(CURDIR)/../../src

VERILOG_SOURCES = $(SRC_DIR)/filter_core.v $(SRC_DIR)/ctl_ff.v
COMPILE_ARGS   += -DAF_BEHAVIORAL_FF
SIM_BUILD      ?= sim_build

COCOTB_HDL_TIMEUNIT      = 1ns
COCOTB_HDL_TIMEPRECISION = 1ps

COCOTB_TOPLEVEL     = filter_core
COCOTB_TEST_MODULES = test_af_core

include $(shell cocotb-config --makefiles)/Makefile.sim
