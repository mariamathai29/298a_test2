"""End-to-end tests: tt_um_adaptive_filter driven only through its pins.

See chip_tb.py for the bench. Expected frames come from filter_golden.py, the
bit-exact reference model. The same tests run on the RTL (test workflow) and
on the hardened netlist (gl_test).
"""
import math
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


async def set_ctl(tb, g, adapt_en, mu_sel):
    """Change the control pins between samples (outputs drained first, as the
    datasheet asks), in the chip and the model together."""
    await tb.drain(tb.sent)
    tb.set_ctl(adapt_en, mu_sel)
    g.set_ctl(adapt_en, mu_sel)
    await ClockCycles(tb.dut.clk, 5)      # control synchronizers


async def stream(tb, g, pairs):
    """Send (u, r) pairs; every frame must match the model bit for bit.
    Returns the frames."""
    n0 = len(tb.frames)
    exp = [g.step(u, r) for u, r in pairs]
    for u, r in pairs:
        await tb.send(u, r)
    await tb.drain(n0 + len(pairs))
    got = tb.frames[n0:]
    assert tb.bad_frames == 0, f"{tb.bad_frames} frames cut short"
    assert len(got) == len(exp), f"{len(got)} frames for {len(exp)} samples"
    for i, (a, b) in enumerate(zip(got, exp)):
        assert a == b, f"frame {n0 + i}: (y, e, w) {a}, expected {b}"
    return got


def random_pairs(rng, n, lo=-128, hi=127):
    return [(rng.randint(lo, hi), rng.randint(lo, hi)) for _ in range(n)]


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


@cocotb.test()
async def test_partial_frames(dut):
    """A frame aborted after 1 .. 7 bits (CS raised early) is discarded: no
    step, no frame, no overrun, and the next frame starts from bit 0."""
    tb = ChipTB(dut)
    await tb.reset()
    g = Golden()
    await set_ctl(tb, g, 1, 6)
    rng = random.Random(2)
    exp = []
    for _ in range(4):
        for bits in range(1, 8):
            await tb.send_partial(rng.randint(-128, 127), rng.randint(-128, 127), bits)
            u, r = rng.randint(-128, 127), rng.randint(-128, 127)
            exp.append(g.step(u, r))
            await tb.send(u, r)
    await tb.drain(len(exp))
    assert tb.bad_frames == 0
    assert len(tb.frames) == len(exp), f"{len(tb.frames)} frames for {len(exp)} complete samples"
    for i, (a, b) in enumerate(zip(tb.frames, exp)):
        assert a == b, f"frame {i}: (y, e, w) {a}, expected {b}"
    assert dut.ovf.value == 0


@cocotb.test()
async def test_clock_drift(dut):
    """SCLK_IN 0.25 % fast (20.05 MHz, not a clk multiple) at 12 periods per
    sample (23.94 clk): the toggle crosses into clk at every phase, and some
    samples arrive at the 23-clk minimum. Nothing is lost or corrupted."""
    tb = ChipTB(dut, tsclk_ps=49875, gap=4)
    await tb.reset()
    g = Golden()
    await set_ctl(tb, g, 1, 4)
    await stream(tb, g, random_pairs(random.Random(3), 600))
    assert dut.ovf.value == 0


@cocotb.test()
async def test_max_rate(dut):
    """12 SCLK periods per sample at 20 MHz (24 clk, 1.67 MS/s), the fastest
    whole-period rate: every sample accepted and bit-exact."""
    tb = ChipTB(dut, gap=4)
    await tb.reset()
    g = Golden()
    await set_ctl(tb, g, 1, 4)
    await stream(tb, g, random_pairs(random.Random(4), 400))
    assert dut.ovf.value == 0


@cocotb.test()
async def test_overrun(dut):
    """10 SCLK periods per sample (20 clk < 23): samples are dropped, so fewer
    frames come back, and debug_o1 (uo_out[6]) sets and stays set."""
    tb = ChipTB(dut, gap=2)
    await tb.reset()
    assert dut.ovf.value == 0
    rng = random.Random(5)
    for u, r in random_pairs(rng, 100):
        await tb.send(u, r)
    await tb.drain(tb.sent, limit=200)
    assert tb.bad_frames == 0
    assert 0 < len(tb.frames) < tb.sent, f"{len(tb.frames)} frames for {tb.sent} samples"
    assert dut.ovf.value == 1, "debug_o1 not set by an overrun"
    await ClockCycles(dut.clk, 200)
    assert dut.ovf.value == 1, "debug_o1 did not stay set"


@cocotb.test()
async def test_control_pins(dut):
    """adapt_en and mu_sel through their synchronizers: every mu_sel, adapting
    and frozen, changed between samples. Every frame bit-exact; frozen blocks
    show the same weight on the monitor lane every 4 frames."""
    tb = ChipTB(dut)
    await tb.reset()
    g = Golden()
    rng = random.Random(6)
    for mu_sel in range(8):
        await set_ctl(tb, g, 1, mu_sel)
        await stream(tb, g, random_pairs(rng, 40))
        await set_ctl(tb, g, 0, mu_sel)
        frozen = await stream(tb, g, random_pairs(rng, 8))
        w = [f[2] for f in frozen]
        assert w[:4] == w[4:], f"mu_sel {mu_sel}: weights moved while frozen: {w}"


@cocotb.test()
async def test_saturation_flag(dut):
    """debug_o2 (uo_out[7]) stays clear for small signals, sets when y, e or
    a weight saturates (full-scale random at mu = 2^-5), and stays set."""
    tb = ChipTB(dut)
    await tb.reset()
    g = Golden()
    rng = random.Random(7)
    await set_ctl(tb, g, 1, 2)
    await stream(tb, g, random_pairs(rng, 100, -20, 20))
    assert not g.f.sat_flag and dut.sat.value == 0, "debug_o2 set without saturation"
    await set_ctl(tb, g, 1, 7)
    await stream(tb, g, random_pairs(rng, 300))
    assert g.f.sat_flag, "the stimulus did not saturate the model"
    assert dut.sat.value == 1, "debug_o2 not set by saturation"
    await set_ctl(tb, g, 1, 2)
    await stream(tb, g, random_pairs(rng, 50, -20, 20))
    assert dut.sat.value == 1, "debug_o2 did not stay set"


def to_q7(v):
    return filter_golden.to_q(v, 7, 8)


@cocotb.test()
async def test_interference_cancelling(dut):
    """The nominal use: r = signal + interference filtered by an unknown path,
    u = the interference. The chip must learn the path and remove it: over the
    second half, the interference left in e must be at least 10 dB below that
    in r. Every frame is bit-exact too."""
    tb = ChipTB(dut)
    await tb.reset()
    g = Golden()
    await set_ctl(tb, g, 1, 4)            # mu = 2^-8
    rng = random.Random(1)
    n, path = 2000, [0.5, -0.3, 0.2, 0.1]
    v = [max(-0.99, min(0.99, rng.gauss(0, 0.3))) for _ in range(n)]
    sig = [0.25 * math.sin(2 * math.pi * 0.013 * k) for k in range(n)]
    pairs = [(to_q7(v[k]), to_q7(sig[k] + sum(path[j] * v[k - j] for j in range(4) if k >= j)))
             for k in range(n)]
    frames = await stream(tb, g, pairs)
    half = range(n // 2, n)
    p_r = sum((pairs[k][1] - 128 * sig[k]) ** 2 for k in half)
    p_e = sum((frames[k][1] - 128 * sig[k]) ** 2 for k in half)
    reduction = 10 * math.log10(p_r / p_e)
    dut._log.info(f"interference reduction (second half): {reduction:.1f} dB")
    assert reduction >= 10, f"interference reduced by only {reduction:.1f} dB"


@cocotb.test()
async def test_system_identification(dut):
    """r = an unknown 4-tap filter applied to white noise u (plus a little
    noise). The weights on the monitor lane must converge to it (within 3 LSB
    of 1/64) and the error power must end at least 20 dB below r."""
    tb = ChipTB(dut)
    await tb.reset()
    g = Golden()
    rng = random.Random(1)
    h = [0.4375, -0.28125, 0.15625, 0.0625]
    n1, n2 = 1000, 500                    # mu = 2^-7 to converge, then 2^-10
    v = [max(-0.99, min(0.99, rng.gauss(0, 0.35))) for _ in range(n1 + n2)]
    pairs = []
    for k in range(n1 + n2):
        d = sum(h[j] * v[k - j] for j in range(4) if k >= j) + rng.gauss(0, 0.004)
        pairs.append((to_q7(v[k]), to_q7(max(-0.99, min(0.99, d)))))
    await set_ctl(tb, g, 1, 5)
    f1 = await stream(tb, g, pairs[:n1])
    await set_ctl(tb, g, 1, 2)
    f2 = await stream(tb, g, pairs[n1:])
    frames = f1 + f2

    # The last 4 frames carry one weight each: frame n has weight n mod 4
    w = {n % 4: frames[n][2] / 64 for n in range(len(frames) - 4, len(frames))}
    w = [w[k] for k in range(4)]
    err = max(abs(a - b) for a, b in zip(w, h))
    last = range(len(frames) - 500, len(frames))
    p_r = sum(pairs[k][1] ** 2 for k in last)
    p_e = sum(frames[k][1] ** 2 for k in last)
    reduction = 10 * math.log10(p_r / max(p_e, 1))
    dut._log.info(f"weights {[f'{x:+.3f}' for x in w]} vs h {h}: max error {err:.3f}; "
                  f"error {reduction:.1f} dB below r")
    assert err <= 3 / 64, f"weights {w} not within 3 LSB of {h}"
    assert reduction >= 20, f"error only {reduction:.1f} dB below r"
