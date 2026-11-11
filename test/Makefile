# Makefile
# See https://docs.cocotb.org/en/stable/quickstart.html for more info
#
#   make             core unit tests (results_core.xml), then the end-to-end and
#                    SPI unit tests (results.xml)
#   make SUITE=core  core unit tests only
#   make SUITE=e2e   end-to-end and SPI unit tests only
#   GATES=yes make   end-to-end tests on the gate-level netlist (what the Tiny
#                    Tapeout gl_test action runs)

# defaults
SIM ?= icarus
FST ?= -fst # Use more efficient FST format
TOPLEVEL_LANG ?= verilog
SRC_DIR = $(PWD)/../src
PROJECT_SOURCES = tt_um_adaptive_filter.v filter_core.v spi.v ctl_ff.v

ifneq ($(GATES),yes)

# RTL simulation:
SIM_BUILD				= sim_build/rtl
VERILOG_SOURCES += $(addprefix $(SRC_DIR)/,$(PROJECT_SOURCES))
COMPILE_ARGS    += -DAF_BEHAVIORAL_FF
# top-level tests + per-module SPI unit tests
COCOTB_TEST_MODULES = test,test_spi_unit

else

# Gate level simulation:
SIM_BUILD				= sim_build/gl
COMPILE_ARGS    += -DGL_TEST
COMPILE_ARGS    += -DFUNCTIONAL
COMPILE_ARGS    += -DUSE_POWER_PINS
COMPILE_ARGS    += -DSIM
COMPILE_ARGS    += -DUNIT_DELAY=\#1
VERILOG_SOURCES += $(PDK_ROOT)/gf180mcuD/libs.ref/gf180mcu_fd_sc_mcu7t5v0/verilog/primitives.v
VERILOG_SOURCES += $(PDK_ROOT)/gf180mcuD/libs.ref/gf180mcu_fd_sc_mcu7t5v0/verilog/gf180mcu_fd_sc_mcu7t5v0.v

# this gets copied in by the GDS action workflow
VERILOG_SOURCES += $(PWD)/gate_level_netlist.v

# the netlist has no separate spi_transmit / spi_receive, so only the
# top-level tests run
COCOTB_TEST_MODULES = test

endif

# Allow sharing configuration between design and testbench via `include`:
COMPILE_ARGS 		+= -I$(SRC_DIR)

# Include the testbench sources:
VERILOG_SOURCES += $(PWD)/tb.v
TOPLEVEL = tb

# Core unit tests (test_af_core.py): a separate simulation with filter_core as
# the top level, so tb.v is not used. Replaces the settings above.
ifeq ($(SUITE),core)
SIM_BUILD                = sim_build/core
COCOTB_RESULTS_FILE      = results_core.xml
VERILOG_SOURCES          = $(SRC_DIR)/filter_core.v $(SRC_DIR)/ctl_ff.v
COMPILE_ARGS             = -DAF_BEHAVIORAL_FF
COCOTB_HDL_TIMEUNIT      = 1ns
COCOTB_HDL_TIMEPRECISION = 1ps
TOPLEVEL                 = filter_core
COCOTB_TEST_MODULES      = test_af_core
endif

# plain RTL make: the core unit tests run first
ifeq ($(GATES)$(SUITE),)
CUSTOM_SIM_DEPS += core_tests
endif

# include cocotb's make rules to take care of the simulator setup
include $(shell cocotb-config --makefiles)/Makefile.sim

.PHONY: core_tests
core_tests:
	$(MAKE) SUITE=core

clean::
	rm -rf sim_build/core results_core.xml
