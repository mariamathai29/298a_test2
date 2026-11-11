# Makefile
# See https://docs.cocotb.org/en/stable/quickstart.html for more info
#
#   make                                  RTL: end-to-end, SPI unit and core unit tests
#   make COCOTB_TEST_MODULES=test_af_core one test module only
#   GATES=yes make                        end-to-end tests on the gate-level netlist
#                                         (what the Tiny Tapeout gl_test action runs)

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
# top-level tests + unit tests for spi.v and filter_core.v (instances at the
# end of tb.v)
COCOTB_TEST_MODULES = test,test_spi_unit,test_af_core

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

# the netlist has no separate spi_transmit / spi_receive / filter_core, so
# only the top-level tests run
COCOTB_TEST_MODULES = test

endif

# Allow sharing configuration between design and testbench via `include`:
COMPILE_ARGS 		+= -I$(SRC_DIR)

# Include the testbench sources:
VERILOG_SOURCES += $(PWD)/tb.v
TOPLEVEL = tb

# include cocotb's make rules to take care of the simulator setup
include $(shell cocotb-config --makefiles)/Makefile.sim
