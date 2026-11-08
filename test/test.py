"""Placeholder until the end-to-end tests replace it: always passes.

It still runs the template flow, so the design has to build and simulate:
the RTL in the test workflow, the hardened netlist in gl_test.
"""
import cocotb
from cocotb.triggers import Timer


@cocotb.test()
async def test_placeholder(dut):
    dut._log.info("Placeholder test: the end-to-end tests are not written yet")
    await Timer(1, unit="ns")
