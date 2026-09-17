# SPDX-FileCopyrightText: © 2024 Tiny Tapeout
# SPDX-License-Identifier: Apache-2.0

import cocotb
from cocotb.clock import Clock
from cocotb.triggers import ClockCycles, RisingEdge, ReadOnly


async def clock_and_settle(dut):
    """Wait for one active clock edge, then let combinational outputs settle."""
    await RisingEdge(dut.clk)
    await ReadOnly()


@cocotb.test()
async def test_project(dut):
    dut._log.info("Start 8-bit counter test")

    # 100 kHz clock, matching the original Tiny Tapeout example test setup.
    clock = Clock(dut.clk, 10, unit="us")
    cocotb.start_soon(clock.start())

    # Initial inputs: load=0, oe=0, bidirectional bus used as an input.
    dut.ena.value = 1
    dut.ui_in.value = 0
    dut.uio_in.value = 0

    # Active-low asynchronous reset.
    dut.rst_n.value = 0
    await ClockCycles(dut.clk, 2)
    assert dut.uo_out.value == 0
    dut.rst_n.value = 1

    # Counter increments when load is low.
    await clock_and_settle(dut)
    assert dut.uo_out.value == 1
    await clock_and_settle(dut)
    assert dut.uo_out.value == 2

    # Synchronously load an 8-bit value from the bidirectional bus.
    dut.uio_in.value = 42
    dut.ui_in.value = 0b00000001  # load=1, oe=0
    await clock_and_settle(dut)
    assert dut.uo_out.value == 42

    # Disable load and verify counting resumes.
    dut.ui_in.value = 0
    await clock_and_settle(dut)
    assert dut.uo_out.value == 43

    # oe=0: all bidirectional pins are inputs (high-Z from this design).
    assert dut.uio_oe.value == 0x00

    # oe=1: all bidirectional pins drive the current counter value.
    dut.ui_in.value = 0b00000010
    await ReadOnly()
    assert dut.uio_oe.value == 0xFF
    assert dut.uio_out.value == 43

    # Turn output enable back off and verify the bus returns to input mode.
    dut.ui_in.value = 0
    await ReadOnly()
    assert dut.uio_oe.value == 0x00

    dut._log.info("Counter reset/count/load/output-enable tests passed")
