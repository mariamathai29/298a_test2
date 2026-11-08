"""af_core unit tests (cocotb). See af_core_tb.py for the bench.

Expected values come from lms_model.py, the bit-exact reference model.
"""
import os
import random
import sys

import cocotb

from af_core_tb import S_FIN, S_IDLE, S_MAC, S_UPD, AfCoreTB

# lms_model.py sits at the top of the tree (repo) or one level up (298_test)
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
for p in (os.path.join(ROOT, ".."), ROOT):
    sys.path.insert(0, p)
import lms_model  # noqa: E402

lms_model.N_TAPS = 4

STEP_CLK = 23                             # go cycle + 17 MAC + FIN + 4 UPD


def model_from(taps, weights, adapt=False, mu_sel=0):
    """A model holding these taps and weights; frozen unless `adapt`."""
    m = lms_model.AdaptiveFilter(mu_q=1 << mu_sel)
    m.adapt_en = adapt
    m.x = list(taps)
    m.w = list(weights)
    return m


async def check_step(tb, taps, weights, r, adapt=False, mu_sel=0):
    """Run one step that leaves `taps` in the delay line (taps[0] is the new
    sample) with `weights`, and compare (y, e) and the weights after the step
    with the model."""
    tb.set_ctl(int(adapt), mu_sel)
    tb.set_taps(taps[1:] + [0])
    tb.set_weights(weights)
    m = model_from(taps[1:] + [0], weights, adapt, mu_sel)
    exp = m.step(taps[0], r)
    got = await tb.step(taps[0], r)
    ctx = f"taps {taps} weights {weights} r {r} adapt {int(adapt)} mu_sel {mu_sel}"
    assert got == exp, f"{ctx}: got (y, e) {got}, expected {exp}"
    assert tb.weights() == m.w, f"{ctx}: weights after {tb.weights()}, expected {m.w}"
    return m


@cocotb.test()
async def test_reset(dut):
    """After reset: FSM idle, rings and flags clear, and no output while idle.

    y_q / e_q have no reset, so tx_a / tx_b are undefined until the first step;
    they only matter when tx_load pulses.
    """
    tb = AfCoreTB(dut)
    await tb.reset()

    assert tb.state() == S_IDLE
    assert tb.taps() == [0, 0, 0, 0]
    assert tb.weights() == [0, 0, 0, 0]
    assert dut.tx_load.value == 0
    assert dut.sat_flag.value == 0
    assert dut.ovf_flag.value == 0

    await tb.wait(100)
    assert tb.state() == S_IDLE
    assert tb.outputs == []
    assert dut.sat_flag.value == 0
    assert dut.ovf_flag.value == 0


@cocotb.test()
async def test_step_timing(dut):
    """One step, cycle by cycle: go, MAC x 17, FIN, UPD x 4, then idle.

    The rings rotate 20 times (MAC 0-15, UPD 0-3) and end in their starting
    order; tx_load pulses once, on the first UPD cycle. Every replicated copy
    of go and rot agrees in every cycle. The tap and weight at position 0 are
    the ones each MAC issue and UPD cycle should use.
    """
    tb = AfCoreTB(dut)
    await tb.reset()

    # Fill the delay line with distinct taps, adaptation off
    for s in (11, 22, 33, 44):
        await tb.send(s, 0)
        await tb.wait(30)
    tb.outputs.clear()
    w = [100, -200, 300, -400]
    tb.set_weights(w)
    await tb.wait(1)

    u = 55
    taps = [u] + tb.taps()[:3]            # after the step: newest first
    tracer = cocotb.start_soon(tb.trace(40))
    await tb.send(u, 0)
    rows = await tracer

    # Replicated controls agree
    for row in rows:
        assert len(set(row["go"])) == 1, f"go copies disagree: {row}"
        assert len(set(row["rot"])) == 1, f"rot copies disagree: {row}"

    # The step starts on the go cycle
    first = next(i for i, row in enumerate(rows) if row["go"][0])
    step = rows[first:first + STEP_CLK]
    assert sum(row["go"][0] for row in rows) == 1, "go must pulse once"

    expect = ([(S_IDLE, None)] + [(S_MAC, k) for k in range(17)] +
              [(S_FIN, None)] + [(S_UPD, k) for k in range(4)])
    got = [(row["state"], row["cnt"] if exp[1] is not None else None)
           for row, exp in zip(step, expect)]
    assert got == expect, f"state sequence {got}"
    assert rows[first + STEP_CLK]["state"] == S_IDLE, "not idle after 23 clk"

    # 20 rotations, in MAC 0-15 and UPD 0-3 only
    rot = [row["rot"][0] for row in step]
    assert rot == [0] + [1] * 16 + [0, 0] + [1] * 4, f"rot pattern {rot}"
    assert sum(row["rot"][0] for row in rows) == 20

    # tx_load once, on UPD 0
    loads = [i for i, row in enumerate(rows) if row["tx_load"]]
    assert loads == [first + 19], f"tx_load at {loads}, expected {first + 19}"

    # Position 0 during MAC issue k and UPD k: tap k % 4 and weight k % 4
    for k in range(16):
        row = step[1 + k]
        assert row["x"][0] == taps[k % 4] and row["w"][0] == w[k % 4], \
            f"MAC {k}: x0 {row['x'][0]}, w0 {row['w'][0]}"
    for k in range(4):
        row = step[19 + k]
        assert row["x"][0] == taps[k] and row["w"][0] == w[k], \
            f"UPD {k}: x0 {row['x'][0]}, w0 {row['w'][0]}"

    # Rings back in order: the new sample at x[0], weights unchanged
    assert tb.taps() == taps
    assert tb.weights() == w
    assert len(tb.outputs) == 1
    assert dut.ovf_flag.value == 0


# Weight register values (Q2.12) for the single-tap sweep. The multiplier uses
# w[13:4] (Q2.8): 15 has only the ignored low bits set, and -1 truncates to -1.
W_SWEEP = [0, 16, -16, 15, -1, 8191, -8192, 1234, -3001, 4096, -4096]


@cocotb.test()
async def test_mac_single_tap(dut):
    """Each tap position alone: every 8-bit x (so every Booth digit pattern)
    times a set of weights, other weights 0. Covers y saturation too."""
    tb = AfCoreTB(dut)
    await tb.reset()
    rng = random.Random(1)
    for pos in range(4):
        for wv in W_SWEEP:
            weights = [0, 0, 0, 0]
            weights[pos] = wv
            for xv in range(-128, 128):
                taps = [rng.randint(-128, 127) for _ in range(4)]
                taps[pos] = xv
                await check_step(tb, taps, weights, rng.randint(-128, 127))


@cocotb.test()
async def test_rounding(dut):
    """y = round half up of acc / 2^8: x = +-1 against every multiplier weight
    (w[13:4] = -512 .. 511) puts acc on and around every rounding boundary."""
    tb = AfCoreTB(dut)
    await tb.reset()
    for xv in (1, -1):
        for wm in range(-512, 512):
            await check_step(tb, [xv, 0, 0, 0], [wm << 4, 0, 0, 0], 0)


@cocotb.test()
async def test_saturation(dut):
    """y and e saturate to 8 bits, and each saturation sets sat_flag.

    sat_flag is sticky, so each case starts from reset."""
    full = [-128, -128, -128, -128]
    w_max, w_min = [8191] * 4, [-8192] * 4
    cases = [
        # taps, weights, r, sat_flag
        ([0, 0, 0, 0],   [0] * 4, 0,    False),   # nothing saturates
        ([64, 0, 0, 0],  [4096, 0, 0, 0], 127, False),  # y = 64, e = 63
        (full,           w_min,   0,    True),    # y = +4 * 128 * 512 / 256 -> 127
        (full,           w_max,   0,    True),    # y -> -128
        ([-128, 0, 0, 0], [-8192, 0, 0, 0], 0, True),  # one tap: 128 * 512 / 256 = 256 -> 127
        ([127, 0, 0, 0], [8191, 0, 0, 0], -128, True), # y = 127 (sat), e = -255 -> -128
        ([0, 0, 0, 0],   [0] * 4, 127,  False),   # e = 127, no saturation
        ([-128, 0, 0, 0], [4096, 0, 0, 0], 127, True), # y = -128, e = 255 -> 127
        ([127, 0, 0, 0], [4096, 0, 0, 0], -128, True), # y = 127 (exact), e = -255 -> -128
    ]
    for taps, weights, r, sat in cases:
        tb = AfCoreTB(dut)
        await tb.reset()
        m = await check_step(tb, taps, weights, r)
        assert m.sat_flag == sat, f"model sat_flag {m.sat_flag}, case expects {sat}"
        assert int(dut.sat_flag.value) == sat, \
            f"taps {taps} weights {weights} r {r}: sat_flag {int(dut.sat_flag.value)}, expected {int(sat)}"
        assert dut.ovf_flag.value == 0


@cocotb.test()
async def test_mac_random(dut):
    """Random taps, weights (all 14 bits) and r."""
    tb = AfCoreTB(dut)
    await tb.reset()
    rng = random.Random(2)
    for _ in range(2000):
        taps = [rng.randint(-128, 127) for _ in range(4)]
        weights = [rng.randint(-8192, 8191) for _ in range(4)]
        await check_step(tb, taps, weights, rng.randint(-128, 127))


# ---------------- Weight update ----------------
# Taps with every sign (+, -, 0, + full scale) and small weights: y = 0,
# so e = r and r sets sign(e).
SIGN_TAPS = [3, -3, 0, 127]
SIGN_W = [160, -160, 32, 0]


def sgn(v):
    return (v > 0) - (v < 0)


@cocotb.test()
async def test_update_sign_table(dut):
    """w[k] += mu * sign(e) * sign(x[k]) for every sign of e and x, including
    zero (no change), with each tap sign at every position: position 0 takes
    its sign from x[0] (FIN), the others from x[1] while rotating (UPD).

    Weights start at 0, so y = 0 and e = r."""
    tb = AfCoreTB(dut)
    await tb.reset()
    for base in ([3, -3, 0, 127], [-1, 0, 5, -128]):
        for rot in range(4):
            taps = base[rot:] + base[:rot]
            for r in (5, 0, -5):
                m = await check_step(tb, taps, [0, 0, 0, 0], r, adapt=True, mu_sel=2)
                # Independent of the model: the update table itself
                expect = [4 * sgn(r) * sgn(x) for x in taps]
                assert m.w == expect and tb.weights() == expect, \
                    f"taps {taps} r {r}: weights {tb.weights()}, expected {expect}"


@cocotb.test()
async def test_update_mu(dut):
    """mu_sel = 0 .. 7 moves each weight by 2^mu_sel register LSBs."""
    tb = AfCoreTB(dut)
    await tb.reset()
    for mu_sel in range(8):
        for r in (10, -10):
            await check_step(tb, SIGN_TAPS, SIGN_W, r, adapt=True, mu_sel=mu_sel)
            mu = 1 << mu_sel
            expect = [w + mu * sgn(r) * sgn(x) for w, x in zip(SIGN_W, SIGN_TAPS)]
            assert tb.weights() == expect, f"mu_sel {mu_sel} r {r}: {tb.weights()}, expected {expect}"


@cocotb.test()
async def test_update_saturation(dut):
    """Weights clamp at +8191 / -8192 and set sat_flag; landing exactly on a
    limit does not. Each case starts from reset (sat_flag is sticky)."""
    cases = [
        # weights, r, sat_flag      (taps all +1, mu = 128)
        ([8091, -8092, 8063, 8191], 127, True),    # 8091 + 128 and 8191 + 128 clamp
        ([8063, 0, 0, 0], 127, False),             # 8063 + 128 = 8191 exactly
        ([-8100, -8192, -8064, 0], -128, True),    # -8100 - 128 and -8192 - 128 clamp
        ([-8064, 0, 0, 0], -128, False),           # -8064 - 128 = -8192 exactly
    ]
    for weights, r, sat in cases:
        tb = AfCoreTB(dut)
        await tb.reset()
        m = await check_step(tb, [1, 1, 1, 1], weights, r, adapt=True, mu_sel=7)
        assert m.sat_flag == sat, f"model sat_flag {m.sat_flag}, case expects {sat}"
        assert int(dut.sat_flag.value) == sat, \
            f"weights {weights} r {r}: sat_flag {int(dut.sat_flag.value)}, expected {int(sat)}"
        assert all(-8192 <= w <= 8191 for w in tb.weights())


@cocotb.test()
async def test_adapt_en_sampling(dut):
    """adapt_en is sampled once per step, in FIN: its value there decides
    whether all four weights update, whatever it does before or after."""
    tb = AfCoreTB(dut)
    await tb.reset()
    updated = [w + 8 * sgn(5) * sgn(x) for w, x in zip(SIGN_W, SIGN_TAPS)]
    cases = [
        # switch adapt_en in this state, from -> to, update expected
        (S_MAC, 0, 1, True),
        (S_FIN, 1, 0, False),
        (S_FIN, 0, 1, True),
        (S_UPD, 1, 0, True),
        (S_UPD, 0, 1, False),
    ]
    for at, before, after, upd in cases:
        tb.set_ctl(before, 3)
        tb.set_taps(SIGN_TAPS[1:] + [0])
        tb.set_weights(SIGN_W)
        task = cocotb.start_soon(tb.step(SIGN_TAPS[0], 5))
        await tb.until_state(at)
        tb.set_ctl(after, 3)
        await task
        expect = updated if upd else SIGN_W
        assert tb.weights() == expect, \
            f"adapt_en {before} -> {after} in state {at}: weights {tb.weights()}, expected {expect}"


@cocotb.test()
async def test_update_random(dut):
    """A long adapting run with nothing deposited after the start: random
    samples, mu_sel and adapt_en, checked against the model every step."""
    tb = AfCoreTB(dut)
    await tb.reset()
    rng = random.Random(3)
    weights = [rng.randint(-8192, 8191) for _ in range(4)]
    tb.set_weights(weights)
    m = model_from([0, 0, 0, 0], weights)
    for i in range(3000):
        u, r = rng.randint(-128, 127), rng.randint(-128, 127)
        adapt, mu_sel = rng.random() < 0.9, rng.randint(0, 7)
        tb.set_ctl(int(adapt), mu_sel)
        m.adapt_en, m.mu_q = adapt, 1 << mu_sel
        exp = m.step(u, r)
        got = await tb.step(u, r)
        assert got == exp, f"step {i}: got (y, e) {got}, expected {exp}"
        assert tb.weights() == m.w, f"step {i}: weights {tb.weights()}, expected {m.w}"
        assert int(dut.sat_flag.value) == m.sat_flag, f"step {i}: sat_flag differs"


# ---------------- Sample handling ----------------
# A sample is accepted only if its d_stb comes at least STEP_CLK cycles after
# the d_stb (= go cycle) of the last accepted sample; otherwise it is dropped,
# not queued, and sets ovf_flag.

def accepted(times):
    """Which d_pre times the core should accept (d_stb = t + 1)."""
    last, out = None, []
    for t in times:
        ok = last is None or (t + 1) - last >= STEP_CLK
        out.append(ok)
        if ok:
            last = t + 1
    return out


async def run_schedule(tb, samples, weights, mu_sel=4):
    """Adapting run of scheduled samples; the model steps only the accepted
    ones. Returns how many were dropped."""
    tb.set_ctl(1, mu_sel)
    tb.set_weights(weights)
    m = model_from([0, 0, 0, 0], weights, adapt=True, mu_sel=mu_sel)
    n = len(tb.outputs)
    await tb.send_schedule(samples)
    await tb.wait(STEP_CLK + 5)
    acc = accepted([t for t, _, _ in samples])
    exp = [m.step(u, r) for (_, u, r), ok in zip(samples, acc) if ok]
    got = [(y, e) for _, y, e, _ in tb.outputs[n:]]
    assert got == exp, f"outputs {got}, expected {exp} (accepted {acc})"
    assert tb.weights() == m.w, f"weights {tb.weights()}, expected {m.w}"
    return acc.count(False)


@cocotb.test()
async def test_sample_spacing(dut):
    """Two samples s cycles apart, s = 2 .. 30: the second is accepted from
    s = 23 on, and dropped below that without disturbing the first step."""
    w = [1234, -2345, 3456, -4567]
    for s in range(2, 31):
        tb = AfCoreTB(dut)
        await tb.reset()
        dropped = await run_schedule(tb, [(0, 50, -30), (s, -70, 90)], w)
        assert dropped == (1 if s < STEP_CLK else 0)
        assert int(dut.ovf_flag.value) == dropped, \
            f"spacing {s}: ovf_flag {int(dut.ovf_flag.value)}, expected {dropped}"


@cocotb.test()
async def test_max_rate(dut):
    """500 samples exactly 23 cycles apart: none dropped, all match the model."""
    tb = AfCoreTB(dut)
    await tb.reset()
    rng = random.Random(4)
    samples = [(STEP_CLK * i, rng.randint(-128, 127), rng.randint(-128, 127)) for i in range(500)]
    dropped = await run_schedule(tb, samples, [0, 0, 0, 0])
    assert dropped == 0
    assert dut.ovf_flag.value == 0


@cocotb.test()
async def test_overrun_random(dut):
    """Random spacing (10 .. 40 cycles): acceptance depends on the last
    accepted sample, not the last one sent. ovf_flag sets on the first drop."""
    tb = AfCoreTB(dut)
    await tb.reset()
    rng = random.Random(5)
    t, samples = 0, []
    for _ in range(1000):
        samples.append((t, rng.randint(-128, 127), rng.randint(-128, 127)))
        t += rng.randint(10, 40)
    dropped = await run_schedule(tb, samples, [500, -500, 250, -250])
    assert 0 < dropped < len(samples), f"{dropped} dropped: the run does not exercise both cases"
    assert dut.ovf_flag.value == 1


# ---------------- Reset and flags ----------------

async def check_clean_after_reset(tb, where):
    """After a reset: idle, rings and flags clear, no output, and the next
    step matches a fresh model."""
    d = tb.dut
    assert tb.state() == S_IDLE, f"reset {where}: state {tb.state()}"
    assert tb.taps() == [0] * 4 and tb.weights() == [0] * 4, f"reset {where}: rings not cleared"
    assert d.ovf_flag.value == 0 and d.sat_flag.value == 0, f"reset {where}: flags not cleared"
    tb.outputs.clear()
    await tb.wait(100)
    assert tb.outputs == [], f"reset {where}: output after reset {tb.outputs}"
    m = model_from([0] * 4, [0] * 4, adapt=True, mu_sel=3)
    for u, r in ((60, -60), (-25, 90)):
        tb.set_ctl(1, 3)
        assert await tb.step(u, r) == m.step(u, r), f"reset {where}: step after reset wrong"
    assert tb.weights() == m.w
    tb.set_ctl(0, 0)


async def pulse_reset(tb):
    tb.dut.rst_n.value = 0
    await tb.wait(3)
    tb.dut.rst_n.value = 1
    await tb.wait(1)


@cocotb.test()
async def test_reset_mid_operation(dut):
    """rst_n asserted during each phase of a step, with both flags set
    beforehand: the core comes back clean."""
    tb = AfCoreTB(dut)
    await tb.reset()

    async def dirty():
        """Non-zero rings and both flags set (a saturating step, then a
        sample dropped mid-step)."""
        tb.set_taps([-128] * 4)
        tb.set_weights([-8192] * 4)
        await tb.step(-128, 0)            # y saturates: sat_flag
        await tb.send(1, 1)
        await tb.wait(3)
        await tb.send(2, 2)               # inside the step: dropped, ovf_flag
        await tb.wait(STEP_CLK)
        assert dut.sat_flag.value == 1 and dut.ovf_flag.value == 1
        tb.set_weights(MON_W)

    for at in (S_MAC, S_FIN, S_UPD):
        await dirty()
        task = cocotb.start_soon(tb.send(10, 20))
        await tb.until_state(at)
        await pulse_reset(tb)
        task.cancel()
        dut.d_pre.value = 0
        dut.d_stb.value = 0
        await check_clean_after_reset(tb, f"in state {at}")


@cocotb.test()
async def test_flags_sticky(dut):
    """sat_flag and ovf_flag stay set through clean steps until reset, and
    one flag never sets the other."""
    tb = AfCoreTB(dut)
    await tb.reset()
    rng = random.Random(7)

    async def clean_activity():
        tb.set_taps([0] * 4)
        tb.set_weights([0] * 4)
        for _ in range(20):
            await tb.step(rng.randint(-60, 60), rng.randint(-60, 60))

    # sat_flag alone
    tb.set_taps([-128] * 4)
    tb.set_weights([-8192] * 4)
    await tb.step(-128, 0)
    assert (int(dut.sat_flag.value), int(dut.ovf_flag.value)) == (1, 0)
    await clean_activity()
    assert (int(dut.sat_flag.value), int(dut.ovf_flag.value)) == (1, 0), "sat_flag did not stay set"

    # then ovf_flag
    await tb.send(1, 1)
    await tb.wait(5)
    await tb.send(2, 2)
    await tb.wait(STEP_CLK)
    assert (int(dut.sat_flag.value), int(dut.ovf_flag.value)) == (1, 1)
    await clean_activity()
    assert (int(dut.sat_flag.value), int(dut.ovf_flag.value)) == (1, 1), "a flag did not stay set"

    # ovf_flag alone, from reset
    await pulse_reset(tb)
    await tb.send(1, 1)
    await tb.wait(5)
    await tb.send(2, 2)
    await tb.wait(STEP_CLK)
    assert (int(dut.sat_flag.value), int(dut.ovf_flag.value)) == (0, 1)
    await clean_activity()
    assert (int(dut.sat_flag.value), int(dut.ovf_flag.value)) == (0, 1), "ovf_flag did not stay set"

    await pulse_reset(tb)
    assert (int(dut.sat_flag.value), int(dut.ovf_flag.value)) == (0, 0)


# ---------------- Weight monitor lane ----------------
# Every output also carries one weight (top 8 bits, Q2.6) on tx_c: output n,
# counted from reset over accepted samples only, carries weight n mod 4 as it
# was before that step's update.
MON_W = [3072, -8192, 8191, -65]          # 0.75, -2, max; -65 checks truncation


def mon(w):
    """A weight register as the monitor lane shows it: top 8 bits."""
    return w >> 6


@cocotb.test()
async def test_monitor_frozen(dut):
    """Frozen weights: outputs carry weights 0, 1, 2, 3, 0, ... from reset, so
    any 4 consecutive outputs are the full set; y and e are unaffected."""
    tb = AfCoreTB(dut)
    await tb.reset()
    tb.set_weights(MON_W)
    m = model_from([0] * 4, MON_W)
    rng = random.Random(8)
    for n in range(12):
        u, r = rng.randint(-128, 127), rng.randint(-128, 127)
        assert await tb.step(u, r) == m.step(u, r)
        w = tb.outputs[-1][3]
        assert w == mon(MON_W[n % 4]), f"output {n}: weight {w}, expected weight {n % 4} = {mon(MON_W[n % 4])}"


@cocotb.test()
async def test_monitor_adapting(dut):
    """While adapting, output n carries weight n mod 4 before that step's
    update. Random samples, mu_sel and adapt_en, checked against the model."""
    tb = AfCoreTB(dut)
    await tb.reset()
    rng = random.Random(9)
    m = model_from([0] * 4, [0] * 4)
    for n in range(3000):
        u, r = rng.randint(-128, 127), rng.randint(-128, 127)
        adapt, mu_sel = rng.random() < 0.9, rng.randint(0, 7)
        tb.set_ctl(int(adapt), mu_sel)
        m.adapt_en, m.mu_q = adapt, 1 << mu_sel
        before = m.w[n % 4]
        assert await tb.step(u, r) == m.step(u, r), f"output {n}: (y, e) differs"
        w = tb.outputs[-1][3]
        assert w == mon(before), f"output {n}: weight {w}, expected weight {n % 4} before update = {mon(before)}"


@cocotb.test()
async def test_monitor_snapshot(dut):
    """The documented use: adapt, freeze, and the next 4 outputs are an exact
    snapshot of all 4 weights."""
    tb = AfCoreTB(dut)
    await tb.reset()
    rng = random.Random(10)
    m = model_from([0] * 4, [0] * 4, adapt=True, mu_sel=7)
    n = 0
    for _ in range(3):
        tb.set_ctl(1, 7)
        m.adapt_en = True
        for _ in range(301):
            u, r = rng.randint(-128, 127), rng.randint(-128, 127)
            assert await tb.step(u, r) == m.step(u, r)
            n += 1
        tb.set_ctl(0, 7)
        m.adapt_en = False
        snap = {}
        for _ in range(4):
            u, r = rng.randint(-128, 127), rng.randint(-128, 127)
            assert await tb.step(u, r) == m.step(u, r)
            snap[n % 4] = tb.outputs[-1][3]
            n += 1
        got = [snap[k] for k in range(4)]
        assert got == [mon(w) for w in m.w], f"snapshot {got}, expected {[mon(w) for w in m.w]}"


@cocotb.test()
async def test_monitor_dropped_samples(dut):
    """A dropped sample produces no output and does not advance the weight
    index, so counting outputs from reset stays in step."""
    tb = AfCoreTB(dut)
    await tb.reset()
    tb.set_weights(MON_W)
    rng = random.Random(11)
    t, samples = 0, []
    for _ in range(400):
        samples.append((t, rng.randint(-128, 127), rng.randint(-128, 127)))
        t += rng.randint(10, 40)
    await tb.send_schedule(samples)
    await tb.wait(STEP_CLK + 5)
    n_acc = accepted([t for t, _, _ in samples]).count(True)
    assert 0 < n_acc < len(samples), "the run does not exercise dropped samples"
    got = [w for _, _, _, w in tb.outputs]
    assert len(got) == n_acc
    assert got == [mon(MON_W[n % 4]) for n in range(n_acc)], "weight index out of step after a drop"
    assert dut.ovf_flag.value == 1


@cocotb.test()
async def test_monitor_reset(dut):
    """Reset, from any point in the 4-output cycle and in any phase of a step,
    starts the weight index again at 0."""
    tb = AfCoreTB(dut)
    await tb.reset()
    for done in range(1, 4):              # outputs since reset before the next reset
        for at in (S_MAC, S_UPD):
            tb.set_weights(MON_W)
            for _ in range(done):
                await tb.step(1, 1)
            task = cocotb.start_soon(tb.send(1, 1))
            await tb.until_state(at)
            await pulse_reset(tb)
            task.cancel()
            dut.d_pre.value = 0
            dut.d_stb.value = 0
            await tb.wait(2)
            tb.set_weights(MON_W)
            tb.outputs.clear()
            for n in range(4):
                await tb.step(2, 2)
            got = [w for _, _, _, w in tb.outputs]
            assert got == [mon(w) for w in MON_W], \
                f"reset after {done} outputs, in state {at}: weights {got}, expected {[mon(w) for w in MON_W]}"
