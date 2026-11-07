"""Stimulus, expected frames and checks for the 4-tap prototype (v5).

Every output frame (y, e, weight and tap readouts) is predicted bit-exactly by
lms_model.py (N_TAPS = 4); test.py drives each case through the chip and checks
what it captures here. Operating spec: clk 40 MHz, SCLK_IN 20 MHz, SCLK_OUT
20 MHz.

Cases (spec clocks unless noted):
  main     13 SCLK periods per sample (1.54 MS/s): interference cancelling,
           readouts, mu_sel sweep, saturation, freeze
  sysid    13 periods: identify an unknown 4-tap FIR; the weights read back
           from the chip must converge to it and the error must fall
  tight    12 periods per sample (1.67 MS/s, 1 spare clk)
  async    SCLK_IN 0.25 % fast (not a clk multiple), 12 periods per sample
  overrun  10 periods per sample (2.0 MS/s, 20 clk < 23): overrun flag must set
  stress   clk 50 MHz, SCLK_IN 25 MHz, 13 periods (old 50 MHz target)

A case is a list of ops (the format of app/tb/vectors/*.ops, hex):
  1 u r      send one data frame
  2 v        set uio[5:0] = v (drains outputs first)
  5          readout (debug_i1 pulse), capture 4 frames
  8          drain (wait for all outputs)
  a          record debug flags
and the frames it must produce, as captured: "Y y e", "R w x", "F ovf sat".
"""
import json
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
# lms_model.py sits at the top of the tree (repo) or one level up (298_test)
for p in (os.path.join(ROOT, ".."), ROOT):
    sys.path.insert(0, p)

import lms_model  # noqa: E402

lms_model.N_TAPS = 4

TCLK_PS = 25000                           # 40 MHz
TSCLK_PS = 50000                          # 20 MHz
H = [0.5, -0.3, 0.2, 0.1]


class Plan:
    def __init__(self):
        self.f = lms_model.AdaptiveFilter()
        self.f.adapt_en = False
        self.uio = 0
        self.ops, self.exp = [], []
        self.ic = []  # (r, e) for NR measurement

    def set_uio(self, adapt, mu_sel):
        self.uio = (adapt & 1) | (mu_sel << 1)
        self.f.adapt_en = bool(adapt)
        self.f.mu_q = 1 << mu_sel
        self.ops.append(f"2 {self.uio:x}")

    def sample(self, u, r, track=False, s=0.0):
        self.ops.append(f"1 {u & 0xFF:x} {r & 0xFF:x}")
        y, e = self.f.step(u, r)
        self.exp.append(f"Y {y & 0xFF:02x} {e & 0xFF:02x}")
        if track:
            self.ic.append((r - s, e - s))

    def readout(self):
        self.ops.append("5")
        for k in range(4):
            w = (self.f.w[k] >> 6) & 0xFF
            t = self.f.x[k] & 0xFF
            self.exp.append(f"R {w:02x} {t:02x}")

    def flags(self, ovf):
        self.ops.append("a")
        self.exp.append(f"F {int(ovf)} {int(self.f.sat_flag)}")


class Vectors:
    """A case from files (app/tb/vectors): the ops and the frames to expect."""
    def __init__(self, ops, exp):
        self.ops, self.exp, self.ic = ops, exp, []


class Case:
    def __init__(self, name, plan, tsclk_ps=TSCLK_PS, gap=5, tclk_ps=TCLK_PS,
                 check_nr=False, ovf=False, sysid=None):
        self.name, self.plan = name, plan
        self.tsclk_ps, self.gap, self.tclk_ps = tsclk_ps, gap, tclk_ps
        self.check_nr, self.ovf, self.sysid = check_nr, ovf, sysid


def ic_signals(n, rng, tone=False):
    k = np.arange(n)
    s = 0.25 * np.sin(2 * np.pi * 0.013 * k)
    v = 0.5 * np.sin(2 * np.pi * 0.071 * k + 0.3) if tone else np.clip(rng.normal(0, 0.3, n), -0.99, 0.99)
    d = s + np.convolve(v, H)[:n]
    u = [lms_model.to_q(a, 7, 8) for a in v]
    r = [lms_model.to_q(a, 7, 8) for a in d]
    ic_signals.s = [a * 128 for a in s]
    return u, r


def sysid_plan(rng):
    """Unknown plant H2, white input; r = H2 * u + small noise."""
    h = [0.4375, -0.28125, 0.15625, 0.0625]
    n1, n2, every = 1500, 2500, 100
    v = np.clip(rng.normal(0, 0.35, n1 + n2), -0.99, 0.99)
    d = np.convolve(v, h)[:n1 + n2] + rng.normal(0, 0.004, n1 + n2)
    u = [lms_model.to_q(a, 7, 8) for a in v]
    r = [lms_model.to_q(a, 7, 8) for a in np.clip(d, -0.99, 0.99)]
    p = Plan()
    p.set_uio(0, 5)
    p.readout()                           # weights start at zero
    steps = [(0, 5), (n1, 2)]             # mu = 2^-7 to converge, then 2^-10
    ms = 5
    for k in range(n1 + n2):
        for at, m in steps:
            if k == at:
                ms = m
                p.set_uio(1, ms)
        p.sample(u[k], r[k])
        if (k + 1) % every == 0:
            # readout runs only while adaptation is frozen
            p.set_uio(0, ms)
            p.readout()
            p.set_uio(1, ms)
    p.flags(ovf=False)
    return p, {"h": h, "r": r, "mu_steps": steps}


def build_cases():
    """All cases, always built in this order: they share one random stream."""
    rng = np.random.default_rng(1)
    cases = {}

    p = Plan()
    u, r = ic_signals(6000, rng)
    p.set_uio(1, 4)                       # adapt, mu = 2^-8
    for a, b, c in zip(u, r, ic_signals.s):
        p.sample(a, b, track=True, s=c)
    p.set_uio(0, 4)
    p.readout()
    p.set_uio(1, 0)                       # mu = 2^-12, tone reference
    u, r = ic_signals(1500, rng, tone=True)
    for a, b in zip(u, r):
        p.sample(a, b)
    p.set_uio(1, 7)                       # mu = 2^-5, full-scale random: saturates
    for _ in range(400):
        p.sample(int(rng.integers(-128, 128)), int(rng.integers(-128, 128)))
    p.set_uio(0, 7)                       # frozen
    for _ in range(200):
        p.sample(int(rng.integers(-128, 128)), int(rng.integers(-128, 128)))
    p.readout()
    for v in (0, -128, 127, 1, -1):
        p.sample(v, max(-128, min(127, -v)))
    p.flags(ovf=False)
    cases["main"] = Case("main", p, check_nr=True)

    p, info = sysid_plan(np.random.default_rng(7))
    cases["sysid"] = Case("sysid", p, sysid=info)

    for name, tsclk, gap in (("tight", TSCLK_PS, 4), ("async", 49875, 4)):
        p = Plan()
        p.set_uio(1, 4)
        u, r = ic_signals(2000, rng)
        for a, b in zip(u, r):
            p.sample(a, b)
        p.set_uio(0, 4)
        p.readout()
        p.flags(ovf=False)
        cases[name] = Case(name, p, tsclk, gap)

    p = Plan()
    p.set_uio(1, 4)
    u, r = ic_signals(300, rng)
    for a, b in zip(u, r):
        p.sample(a, b)
    p.flags(ovf=True)
    cases["overrun"] = Case("overrun", p, gap=2, ovf=True)

    p = Plan()
    p.set_uio(1, 4)
    u, r = ic_signals(1500, rng)
    for a, b in zip(u, r):
        p.sample(a, b)
    p.set_uio(0, 4)
    p.readout()
    p.flags(ovf=False)
    cases["stress"] = Case("stress", p, tsclk_ps=40000, tclk_ps=20000)

    return cases


def header(case):
    per = (8 + case.gap) * case.tsclk_ps
    return (f"[{case.name}] clk {1e6 / case.tclk_ps:.2f} MHz, SCLK_IN {1e6 / case.tsclk_ps:.3f} MHz, "
            f"{8 + case.gap} SCLK periods per sample = {per / case.tclk_ps:.2f} clk, {1e6 / per:.3f} MS/s")


def check(case, got, tb_line, out_dir):
    """Report a case's result in the DV log format; True if it passed."""
    print("    " + tb_line)
    plan = case.plan
    if case.ovf:
        flags = [g for g in got if g.startswith("F")]
        ok = flags == ["F 1 0"] or bool(flags and flags[-1].startswith("F 1"))
        print(f"    flags {flags} (overrun bit must be 1)  -> {'PASS' if ok else 'FAIL'}")
        return ok
    bad = sum(1 for a, b in zip(plan.exp, got) if a != b) + abs(len(plan.exp) - len(got))
    ok = bad == 0 and "0 bad frames" in tb_line
    print(f"    expected {len(plan.exp)} frames, got {len(got)}, mismatches {bad}  -> {'PASS' if ok else 'FAIL'}")
    if bad:
        for i, (a, b) in enumerate(zip(plan.exp, got)):
            if a != b:
                print(f"    first mismatch at frame {i}: expected '{a}', got '{b}'")
                break
    if case.check_nr and plan.ic:
        half = plan.ic[len(plan.ic) // 2:]
        pr = np.mean([v ** 2 for v, _ in half])
        pe = np.mean([v ** 2 for _, v in half])
        print(f"    interference reduction, mu = 2^-8, white (second half): {10 * np.log10(pr / pe):.1f} dB")
    if case.sysid is not None:
        ok = sysid_check(got, case.sysid, out_dir) and ok
    return ok


def sysid_check(got, info, out_dir):
    """Weights from the chip's own readout frames must converge to H."""
    w_trace, i, n = [], 0, 0
    e = []
    for g in got:
        f = g.split()
        if f[0] == "Y":
            v = int(f[2], 16)
            e.append((v - 256 if v > 127 else v) / 128)
            n += 1
        elif f[0] == "R":
            v = int(f[1], 16)
            w_trace.append((n, (v - 256 if v > 127 else v) / 64))
    rows = [w_trace[k:k + 4] for k in range(0, len(w_trace), 4)]
    final = [w for _, w in rows[-1]]
    err = max(abs(a - b) for a, b in zip(final, info["h"]))
    e = np.array(e)
    r = np.array(info["r"][:len(e)]) / 128
    first, last = slice(0, 200), slice(len(e) - 1000, len(e))
    red = 10 * np.log10(np.mean(r[last] ** 2) / np.mean(e[last] ** 2))
    print(f"    true H        {' '.join(f'{h:+.3f}' for h in info['h'])}")
    print(f"    chip readout  {' '.join(f'{w:+.3f}' for w in final)}   max |w - h| = {err:.3f} (limit 0.047 = 3 LSB)")
    print(f"    error power: first 200 samples {10 * np.log10(np.mean(e[first] ** 2)):.1f} dB, "
          f"last 1000 {10 * np.log10(np.mean(e[last] ** 2)):.1f} dB; "
          f"r/e reduction {red:.1f} dB (limit 20 dB)")
    with open(os.path.join(out_dir, "sysid_trace.json"), "w") as fh:
        json.dump({"h": info["h"], "mu_steps": info["mu_steps"],
                   "w": [[rw[0][0]] + [w for _, w in rw] for rw in rows],
                   "e": e.tolist(),
                   "r_db": float(10 * np.log10(np.mean(r[last] ** 2)))}, fh)
    ok = err <= 0.047 and red >= 20
    print(f"    adaptive-filter check -> {'PASS' if ok else 'FAIL'}")
    return ok
