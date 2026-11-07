"""Build docs/design_spec_v5.html from the signoff run's data.

Usage: python3 gen_spec.py <run>        e.g. python3 gen_spec.py r19

Reads runs/<run>/final/metrics.json, runs/io_model/ifc_<run>.txt (interface
STA), runs/sta_sweep/sweep2_<run>.txt (margin sweep), runs/io_model/
pad_delays.txt, test/dv/<run>/*.log and the sysid trace, and fills the
template parts in docs/spec_parts/.
"""
import collections
import glob
import html
import json
import math
import os
import re
import sys

DOCS = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(DOCS)
RUN = sys.argv[1]
PARTS = os.path.join(DOCS, "spec_parts")
NOTES = {
    "r17": "Same layout as r16, signed off at the spec with the TT I/O model",
    "r18": "Post-global-route design repair: slow-corner slew violations 416 → 76",
    "r19": "Design repair with 40% slew margin, before and after global routing",
}
IOSKEW = 3.5


def rd(*p):
    with open(os.path.join(*p)) as fh:
        return fh.read()


m = json.load(open(os.path.join(ROOT, "runs", RUN, "final", "metrics.json")))

# ---- corners
CORNERS = [f"{rc}_{pvt}" for pvt in ("ss_125C_3v00", "tt_025C_3v30", "ff_n40C_3v60")
           for rc in ("max", "nom", "min")]
rows = []
for c in CORNERS:
    s, h = m[f"timing__setup__ws__corner:{c}"], m[f"timing__hold__ws__corner:{c}"]
    sv = m[f"timing__setup_vio__count__corner:{c}"]
    hv = m[f"timing__hold_vio__count__corner:{c}"]
    rows.append(f'<tr><td><code>{c}</code></td><td class="n">{s:+.3f}</td><td class="n">{h:+.3f}</td>'
                f'<td class="n">{sv}</td><td class="n">{hv}</td></tr>')
ws = min(m[f"timing__setup__ws__corner:{c}"] for c in CORNERS)

# ---- interface margins
ifc = collections.defaultdict(dict)
for l in rd(ROOT, "runs", "io_model", f"ifc_{RUN}.txt").splitlines():
    f = l.split()
    if len(f) == 4 and f[0] == "IFC":
        ifc[f[2]][f[1]] = float(f[3])


def worst(tag):
    c, v = min(ifc[tag].items(), key=lambda kv: kv[1])
    return v, c


IF = [
    ("SPI in setup: CS_IN / MOSI before rising SCLK_IN", "half period 25 ns − 5.5 ns ext.", "spi_in_setup"),
    ("SPI in hold: CS_IN / MOSI after rising SCLK_IN", "changes ≥ 25 ns − 5.5 ns later", "spi_in_hold"),
    ("SPI out setup at the receiver (vs rising SCLK_OUT)", "t<sub>su</sub> 5 ns + skew 3.5 ns", "spi_out_setup"),
    ("SPI out hold at the receiver", "t<sub>h</sub> 2 ns + skew 3.5 ns", "spi_out_hold"),
    ("SCLK_IN-domain register paths, hold", "—", "sclk_reg_hold"),
    ("CDC: resolution time after first sync stage", "positive slack", "cdc_resolve"),
]
ifrows = []
for name, req, tag in IF:
    v, c = worst(tag)
    ifrows.append(f'<tr><td>{name}</td><td class="n">{req}</td><td class="n">{v:+.2f} ns</td><td><code>{c}</code></td></tr>')
cdc_ss = min(v for c, v in ifc["cdc_resolve"].items() if "ss" in c)
cdc_tt = min(v for c, v in ifc["cdc_resolve"].items() if "tt" in c)

# ---- MTBF: T_w = 0.1 ns, f_clk = 40 MHz, f_data = 1.54 MS/s
den = 0.1e-9 * 40e6 * 1.538e6
mtbf_yr = math.exp(cdc_ss / 0.3) / den / 3.156e7
tau_1k = cdc_ss / math.log(1000 * 3.156e7 * den)

# ---- pads
pad = rd(ROOT, "runs", "io_model", "pad_delays.txt")
vin = [float(x) for x in re.findall(r"core_y \(output\)\s+\S+\s+(\S+)", pad)]
vout = [float(x) for x in re.findall(r"pin_o \(output\)\s+\S+\s+(\S+)", pad)]

# ---- sweep
sw = collections.defaultdict(dict)
for l in rd(ROOT, "runs", "sta_sweep", f"sweep2_{RUN}.txt").splitlines():
    r = re.match(r"RESULT corner=(\S+) period=(\S+) setup_ws=(\S+) hold_ws=(\S+)", l)
    if r:
        sw[float(r[2])][r[1]] = float(r[3])
fam = {"slow (ss 125 °C 3.0 V)": "ss", "typical (tt 25 °C 3.3 V)": "tt", "fast (ff −40 °C 3.6 V)": "ff"}
margin = {"periods": sorted(sw), "series": [
    {"name": n, "v": [round(min(v for c, v in sw[p].items() if f"_{k}_" in c), 3) for p in sorted(sw)]}
    for n, k in fam.items()]}

# ---- area
CLS = [("sequential_cell", "Flip-flops"), ("multi_input_combinational_cell", "Logic gates"),
       ("inverter", "Inverters"), ("clock_buffer", "Clock-tree buffers"), ("clock_inverter", "Clock-tree inverters"),
       ("timing_repair_buffer", "Timing / slew repair buffers"), ("buffer", "Other buffers"),
       ("tap_cell", "Tap cells"), ("endcap_cell", "Endcaps"), ("tie_cell", "Tie cells")]
core = 51967.0
arows, tot, ntot = [], 0, 0
for k, n in CLS:
    a, c = m[f"design__instance__area__class:{k}"], m[f"design__instance__count__class:{k}"]
    tot += a
    ntot += c
    arows.append(f'<tr><td>{n}</td><td class="n">{c:,}</td><td class="n">{a:,.0f}</td><td class="n">{100 * a / core:.1f}%</td></tr>')
arows.append(f'<tr><td><strong>All standard cells</strong></td><td class="n"><strong>{ntot:,}</strong></td>'
             f'<td class="n"><strong>{tot:,.0f}</strong></td><td class="n"><strong>{100 * tot / core:.1f}%</strong></td></tr>')
fa, fc = m["design__instance__area__class:fill_cell"], m["design__instance__count__class:fill_cell"]
arows.append(f'<tr><td>Fill and decap</td><td class="n">{fc:,}</td><td class="n">{fa:,.0f}</td><td class="n">{100 * fa / core:.1f}%</td></tr>')

slew = {c: m.get(f"design__max_slew_violation__count__corner:{c}", 0) for c in CORNERS}
slew_ss = max(slew[c] for c in CORNERS if "ss" in c)
slew_tt = max(slew[c] for c in CORNERS if "tt" in c)
slew_ff = max(slew[c] for c in CORNERS if "ff" in c)
if slew_ss:
    slew_txt = (f"{slew_ss} pins over 3 ns at the slow corner, {slew_tt} at typical, {slew_ff} at fast. "
                "Setup still passes there, but those delays come from extrapolated liberty tables")
    slew_short = (f"{slew_ss} pins exceed the 3 ns transition limit at max-RC slow. Timing passes, "
                  "but those cell delays are extrapolated, so the slow-corner margin is a little less certain than the number suggests.")
else:
    slew_txt = "none at any corner"
    slew_short = "none remaining."

# ---- DV
DV = [
    ("main", "8,105 samples: white-noise IC (μ = 2<sup>−8</sup>), readouts, tone at 2<sup>−12</sup>, full-scale random at 2<sup>−5</sup> (saturation), frozen weights, edge values"),
    ("sysid", "4,000 samples identifying an unknown 4-tap plant, 41 weight readouts; convergence criteria"),
    ("tight", "12 SCLK periods per sample (24 clk, 1 spare), 2,000 samples + readout"),
    ("async", "SCLK_IN 20.05 MHz (not a clk multiple), 12 periods per sample = 23.94 clk"),
    ("overrun", "10 periods per sample (20 clk &lt; 23): debug_o1 must set"),
    ("stress", "clk 50 MHz, SCLK_IN 25 MHz, 13 periods: the original target, functional only"),
]
dvd = os.path.join(ROOT, "test", "dv", RUN)


def res(log, case):
    try:
        t = rd(log)
    except OSError:
        return None
    blk = t.split(f"[{case}]")
    if len(blk) < 2:
        return None
    # a block ends at the next case, or at a cocotb log line (its summary table)
    b = re.split(r"\n\[|\n\s*[\d.]+ns ", blk[1])[0]
    return "FAIL" not in b and ("PASS" in b)


def badge(ok):
    if ok is None:
        return '<td class="n tm">—</td>'
    return ('<td><span class="badge"><span class="ic ok">✓</span></span></td>' if ok
            else '<td><span class="badge"><span class="ic bad">✕</span>FAIL</span></td>')


dvrows = []
allpass = True
for case, desc in DV:
    cells = [res(os.path.join(dvd, "rtl.log"), case), res(os.path.join(dvd, "gl.log"), case)]
    for c in ("max_ss_125C_3v00", "min_ff_n40C_3v60"):
        cells.append(res(os.path.join(dvd, f"sdf_{c}_{case}.log"), case))
    allpass &= all(x is not False for x in cells)
    dvrows.append(f"<tr><td>{case}</td><td>{desc}</td>" + "".join(badge(x) for x in cells) + "</tr>")
if not allpass:
    print("WARNING: some DV results are failing or missing")

# ---- sysid charts
tr = json.load(open(os.path.join(dvd, "rtl", "sysid_trace.json")))
e = tr["e"]
B = 50
eblk = [round(10 * math.log10(sum(v * v for v in e[i:i + B]) / len(e[i:i + B]) + 1e-12), 2)
        for i in range(0, len(e), B)]
DATA = {
    "margin": margin,
    "sysid": {"h": tr["h"], "w": tr["w"], "eblk": eblk, "B": B,
              "rpow": round(tr["r_db"], 1)},
}

# ---- assemble
body = rd(PARTS, "body.html")
ring = rd(PARTS, "ring.svg")
sub = {
    "RUN": RUN, "UTIL": f"{100 * m['design__instance__utilization']:.1f}%",
    "WS": f"+{ws:.2f} ns", "PWR": f"{m['power__total'] * 1e3:.1f}",
    "PINT": f"{m['power__internal__total'] * 1e3:.1f}", "PSW": f"{m['power__switching__total'] * 1e3:.1f}",
    "CELLS": f"{m['design__instance__count']:,} instances including {fc:,} fill; "
             f"{m['design__instance__count__class:sequential_cell']} flip-flops",
    "SLEW": slew_txt, "SLEWSHORT": slew_short,
    "IFROWS": "\n".join(ifrows), "CORNERROWS": "\n".join(rows), "AREAROWS": "\n".join(arows),
    "DVROWS": "\n".join(dvrows), "RINGSVG": ring, "RUNNOTE": NOTES.get(RUN, ""),
    "IOSKEW": f"{IOSKEW}", "CDCSS": f"{cdc_ss:.1f}", "CDCTT": f"{cdc_tt:.1f}",
    "MTBF": "{:.0f} × 10<sup>{}</sup>".format(mtbf_yr / 10 ** math.floor(math.log10(mtbf_yr)), math.floor(math.log10(mtbf_yr))), "TAU1K": f"{tau_1k:.2f}",
    "PADIN": f"{min(vin):.1f} … {max(vin):.1f}", "PADOUT": f"{min(vout):.1f} … {max(vout):.1f}",
}
for k, v in sub.items():
    body = body.replace(f"@@{k}@@", v)
left = re.findall(r"@@\w+@@", body)
if left:
    sys.exit(f"unfilled placeholders: {sorted(set(left))}")

out = (rd(PARTS, "head.html") + body +
       "<script>\nconst DATA = " + json.dumps(DATA) + ";\n" + rd(PARTS, "charts.js") +
       "</script>\n</body>\n</html>\n")
with open(os.path.join(DOCS, "design_spec_v5.html"), "w") as fh:
    fh.write(out)
print(f"wrote design_spec_v5.html for {RUN}: WS {ws:+.3f} ns, util {sub['UTIL']}, "
      f"slew ss/tt/ff {slew_ss}/{slew_tt}/{slew_ff}, CDC {cdc_ss:.2f} ns, MTBF(τ=0.3) {mtbf_yr:.1e} yr, "
      f"τ for 1000 yr {tau_1k:.2f} ns, DV {'all pass' if allpass else 'NOT all pass'}")
