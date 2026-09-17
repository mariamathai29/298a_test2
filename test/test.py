# SPDX-FileCopyrightText: © 2024 Tiny Tapeout
# SPDX-License-Identifier: Apache-2.0

import cocotb
from cocotb.clock import Clock
from cocotb.triggers import RisingEdge, Timer


@cocotb.test()
async def test_project(dut):

    dut._log.info("Start 8-bit counter test")

    # Start clock: 10 us period
    clock = Clock(dut.clk, 10, unit="us")
    cocotb.start_soon(clock.start())

    # Initial values
    dut.ena.value = 1
    dut.ui_in.value = 0
    dut.uio_in.value = 0

    # -------------------------------------------------
    # RESET
    # -------------------------------------------------

    dut._log.info("Testing reset")

    dut.rst_n.value = 0

    await RisingEdge(dut.clk)
    await Timer(1, unit="ns")

    assert dut.uo_out.value == 0

    dut.rst_n.value = 1


    # -------------------------------------------------
    # NORMAL COUNTING
    # -------------------------------------------------

    dut._log.info("Testing counting")

    await RisingEdge(dut.clk)
    await Timer(1, unit="ns")

    assert dut.uo_out.value == 1

    await RisingEdge(dut.clk)
    await Timer(1, unit="ns")

    assert dut.uo_out.value == 2


    # -------------------------------------------------
    # SYNCHRONOUS LOAD
    # -------------------------------------------------

    dut._log.info("Testing synchronous load")

    # Put 42 on bidirectional input bus.
    dut.uio_in.value = 42

    # ui_in[0] = load
    # ui_in[1] = oe
    #
    # load = 1
    # oe   = 0
    dut.ui_in.value = 0b00000001

    await RisingEdge(dut.clk)
    await Timer(1, unit="ns")

    assert dut.uo_out.value == 42


    # -------------------------------------------------
    # RESUME COUNTING
    # -------------------------------------------------

    dut._log.info("Testing count after load")

    # load = 0
    # oe   = 0
    dut.ui_in.value = 0

    await RisingEdge(dut.clk)
    await Timer(1, unit="ns")

    assert dut.uo_out.value == 43


    # -------------------------------------------------
    # OUTPUT ENABLE / TRI-STATE
    # -------------------------------------------------

    dut._log.info("Testing output enable")

    # oe = 0 -> uio pins should not be driven
    assert dut.uio_oe.value == 0x00

    # Turn OE on:
    # load = 0
    # oe   = 1
    dut.ui_in.value = 0b00000010

    await Timer(1, unit="ns")

    assert dut.uio_oe.value == 0xFF
    assert dut.uio_out.value == 43


    # Turn OE back off
    dut.ui_in.value = 0

    await Timer(1, unit="ns")

    assert dut.uio_oe.value == 0x00

    dut._log.info("All counter tests passed!")
