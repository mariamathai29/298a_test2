# cocotb testbench for the 4-tap prototype (test.py, tb.v)
# See https://docs.cocotb.org/en/stable/quickstart.html for more info
#
#   make                                   RTL, every case
#   make COCOTB_TEST_FILTER=test_sysid     one case
#   make GATES=yes [GL=<pnl.v>]            gate level, zero delay
#                                          (default netlist: gate_level_netlist.v)
#   make GATES=yes GL=<nl.v> SDF=<file.sdf>    gate level, SDF timing
#   WAVES=1                                also write $(SIM_BUILD)/tb.fst
#
# Each mode builds in its own sim_build/<mode> directory. The RTL uses the
# GF180 flop cell (ctl_ff); without the PDK (PDK_ROOT) it falls back to the
# behavioural model, AF_BEHAVIORAL_FF.

SIM ?= icarus
TOPLEVEL_LANG ?= verilog
SRC_DIR = $(CURDIR)/../src
PROJECT_SOURCES = tt_um_adaptive_filter.v af_core.v spi_rx.v spi_tx.v ctl_ff.v

PDK_ROOT ?= $(abspath $(CURDIR)/../../pdk)
CELL_DIR = $(PDK_ROOT)/gf180mcuD/libs.ref/gf180mcu_fd_sc_mcu7t5v0/verilog
CELL_SOURCES = $(CURDIR)/nettype_wire.v $(CELL_DIR)/primitives.v $(CELL_DIR)/gf180mcu_fd_sc_mcu7t5v0.v
HAVE_PDK = $(wildcard $(CELL_DIR)/primitives.v)

VERILOG_SOURCES += $(CURDIR)/tb.v

ifneq ($(GATES),yes)

# RTL
SIM_BUILD ?= sim_build/rtl
VERILOG_SOURCES += $(addprefix $(SRC_DIR)/,$(PROJECT_SOURCES))
ifneq ($(HAVE_PDK),)
COMPILE_ARGS    += -DFUNCTIONAL -DUNIT_DELAY=\#1
VERILOG_SOURCES += $(CELL_SOURCES)
else
COMPILE_ARGS    += -DAF_BEHAVIORAL_FF
endif

else

# Gate level: the routed netlist and the GF180 cell models
ifeq ($(HAVE_PDK),)
$(error GATES=yes needs the GF180 cell models: $(CELL_DIR) not found (set PDK_ROOT))
endif
GL ?= $(CURDIR)/gate_level_netlist.v
COMPILE_ARGS    += -DGL_TEST -DSIM
ifdef SDF
# Timed cell models (specify blocks) and the unpowered netlist that matches the SDF
SIM_BUILD ?= sim_build/sdf_$(basename $(notdir $(SDF)))
COMPILE_ARGS    += -DSDF_FILE=\"$(SDF)\" -gspecify
else
SIM_BUILD ?= sim_build/gl
COMPILE_ARGS    += -DFUNCTIONAL -DUSE_POWER_PINS -DUNIT_DELAY=\#1
endif
VERILOG_SOURCES += $(GL) $(CELL_SOURCES)

endif

COCOTB_TOPLEVEL = tb
COCOTB_TEST_MODULES = test

# include cocotb's make rules to take care of the simulator setup
include $(shell cocotb-config --makefiles)/Makefile.sim
