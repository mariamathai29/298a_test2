"""End-to-end tests: tt_um_adaptive_filter driven only through its pins.

See chip_tb.py for the bench. Expected frames come from filter_golden.py, the
bit-exact reference model. The same tests run on the RTL (test workflow) and
on the hardened netlist (gl_test).
"""
import random

import cocotb
from cocotb.triggers import ClockCycles

import filter_golden
from chip_tb import ChipTB

filter_golden.N_TAPS = 4


class Golden:
    """filter_golden plus the monitor lane: frame n carries weight n mod 4,
    top 8 bits, before that sample's update."""
    def __init__(self):
        self.f = filter_golden.AdaptiveFilter()
        self.f.adapt_en = False
        self.n = 0

    def set_ctl(self, adapt_en, mu_sel):
        self.f.adapt_en = bool(adapt_en)
        self.f.mu_q = 1 << mu_sel

    def step(self, u, r):
        w = self.f.w[self.n % 4] >> 6
        self.n += 1
        y, e = self.f.step(u, r)
        return y, e, w


@cocotb.test()
async def test_reset_pins(dut):
    """After reset, with no samples: every output pin low (no SCLK_OUT, no
    frame, flags clear) and every uio pin an input."""
    tb = ChipTB(dut)
    await tb.reset()
    for _ in range(200):
        await ClockCycles(dut.clk, 1)
        assert dut.uo_out.value == 0, f"uo_out {dut.uo_out.value} while idle"
        assert dut.uio_oe.value == 0, f"uio_oe {dut.uio_oe.value}"
        assert dut.uio_out.value == 0, f"uio_out {dut.uio_out.value}"
    assert tb.frames == []


@cocotb.test()
async def test_first_samples(dut):
    """50 random samples at the spec rate, adapting: one frame per sample,
    each (y, e, w) bit-exact against the golden model."""
    tb = ChipTB(dut)
    await tb.reset()
    g = Golden()
    tb.set_ctl(1, 7)
    g.set_ctl(1, 7)
    await ClockCycles(dut.clk, 5)         # control synchronizers

    rng = random.Random(1)
    exp = []
    for _ in range(50):
        u, r = rng.randint(-128, 127), rng.randint(-128, 127)
        exp.append(g.step(u, r))
        await tb.send(u, r)
    await tb.drain(len(exp))

    assert tb.bad_frames == 0
    assert len(tb.frames) == len(exp), f"{len(tb.frames)} frames for {len(exp)} samples"
    for i, (got, want) in enumerate(zip(tb.frames, exp)):
        assert got == want, f"frame {i}: (y, e, w) {got}, expected {want}"
    assert dut.ovf.value == 0
