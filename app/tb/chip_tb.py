"""Testbench: the explorer's fixed-point model (app/Source/Chip.h) against the chip.

The vectors come from the explorer itself (build/afx_chipvec): what its bit-exact
model was fed and what it got back. Each case is checked three ways, frame for
frame (every y/e frame, every weight/tap readout, the debug flags):
  1. C++ model (the app)   == Python golden model (../lms_model.py, N_TAPS = 4)
  2. C++ model (the app)   == the RTL through the cocotb testbench (test/test.py,
                              test_vectors; Icarus), or
                              the routed netlist (GL=1: runs/<RUN>/final, zero delay;
                              SDF=<corner>: with that corner's SDF timing)
All three must be identical. Operating spec clocks: clk 40 MHz, SCLK_IN 20 MHz,
13 SCLK periods per sample.

Usage:  python3 chip_tb.py [case ...]                 (cases: app_fetal app_hum corners)
        GL=1 [RUN=r18] python3 chip_tb.py ...         gate level, zero delay
        SDF=max_ss_125C_3v00 [RUN=r18] python3 chip_tb.py ...
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
APP = os.path.dirname(HERE)
ROOT = os.path.dirname(APP)                       # assess4d
RUN = os.environ.get("RUN", "r18")
CASES = ["app_fetal", "app_hum", "corners"]

# What the cocotb testbench simulates: make arguments for test/Makefile
final = os.path.join(ROOT, "runs", RUN, "final")
corner = os.environ.get("SDF")
level = "RTL"
make_args = []
if corner:
    make_args = ["GATES=yes", "GL=" + os.path.join(final, "nl", "tt_um_adaptive_filter.nl.v"),
                 "SDF=" + os.path.join(final, "sdf", corner, f"tt_um_adaptive_filter__{corner}.sdf")]
    level = f"routed netlist ({RUN}), SDF {corner}"
elif os.environ.get("GL"):
    make_args = ["GATES=yes", "GL=" + os.path.join(final, "pnl", "tt_um_adaptive_filter.pnl.v")]
    level = f"routed netlist ({RUN}), zero delay"
tag = "rtl" if level == "RTL" else ("gl" if not corner else f"sdf_{corner}")
WORK = os.path.join(HERE, "work", tag)
sys.path.insert(0, os.path.join(ROOT, "test"))

import af_cases     # noqa: E402  (stimulus and checks shared with test/test.py)
import lms_model    # noqa: E402  (imported by af_cases, with N_TAPS = 4)


def s8(v):
    return v - 256 if v > 127 else v


def python_model(ops):
    """Replay the ops through lms_model; the frames the golden model expects."""
    f = lms_model.AdaptiveFilter()
    f.adapt_en = False
    exp = []
    for line in ops:
        a = line.split()
        if a[0] == "1":
            y, e = f.step(s8(int(a[1], 16)), s8(int(a[2], 16)))
            exp.append(f"Y {y & 0xFF:02x} {e & 0xFF:02x}")
        elif a[0] == "2":
            v = int(a[1], 16)
            f.adapt_en = bool(v & 1)
            f.mu_q = 1 << ((v >> 1) & 7)
        elif a[0] == "5":
            for k in range(4):
                exp.append(f"R {(f.w[k] >> 6) & 0xFF:02x} {f.x[k] & 0xFF:02x}")
        elif a[0] == "a":
            exp.append(f"F 0 {int(f.sat_flag)}")
    return exp


def compare(a, b):
    bad = sum(1 for x, y in zip(a, b) if x != y) + abs(len(a) - len(b))
    first = next((i for i, (x, y) in enumerate(zip(a, b)) if x != y), None)
    return bad, first


def run_chip(vec, names):
    """Replay the vectors through the chip (cocotb test_vectors); True if all pass."""
    env = {k: v for k, v in os.environ.items() if k not in ("GL", "SDF", "GATES")}
    env.update(AF_VECTORS=vec, AF_CASES=" ".join(names), COCOTB_TEST_FILTER="test_vectors",
               COCOTB_RESULTS_FILE=os.path.join(WORK, "results.xml"))
    env.setdefault("PDK_ROOT", os.path.join(ROOT, "..", "pdk"))
    res = subprocess.run(["make", "-s", "-C", os.path.join(ROOT, "test"), "SIM_BUILD=" + os.path.join(WORK, "cocotb")] + make_args,
                         env=env)
    return res.returncode == 0


def main():
    want = [c for c in sys.argv[1:] if c in CASES] or CASES
    vec = os.path.join(WORK, "vectors")
    # CHIPVEC=<binary>: another build of the vector tool (e.g. a deliberately broken one)
    tool = os.environ.get("CHIPVEC", os.path.join(APP, "build", "afx_chipvec"))
    subprocess.run([tool, vec], check=True)
    print(f"\nchip_tb: the explorer's fixed point against lms_model.py and the {level}\n")
    results = []
    for name in want:
        ops = open(os.path.join(vec, name + ".ops")).read().split("\n")
        ops = [o for o in ops if o]
        cpp = [e for e in open(os.path.join(vec, name + ".exp")).read().split("\n") if e]
        py = python_model(ops)
        bad, first = compare(cpp, py)
        ok_py = bad == 0
        print(f"[{name}] C++ model vs lms_model.py: {len(cpp)} frames, {bad} mismatches -> {'PASS' if ok_py else 'FAIL'}")
        if not ok_py:
            print(f"    first mismatch at frame {first}: app '{cpp[first]}', python '{py[first]}'")
        sat = cpp[-1]
        print(f"    flags at the end: {sat} (F ovf sat)")
        results.append(ok_py)
    print(f"\nchip_tb: the explorer's fixed point against the {level}\n", flush=True)
    results.append(run_chip(vec, want))
    print(f"\nRESULT ({level}):", "PASS" if all(results) else "FAIL")
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
