"""cocotb testbench for the 4-tap prototype (v5).

Plays each case of af_cases.py into the chip as SPI master on the input port,
receives every output frame as the frame-sync receiver on the output port, and
compares them bit-exactly with lms_model.py. The reports go to stdout in the
DV log format (docs/gen_spec.py reads them); captured frames go to
$SIM_BUILD/out_<case>.txt.

Tests: test_main test_sysid test_tight test_async test_overrun test_stress, and
test_vectors (AF_VECTORS=<dir> AF_CASES="<case> ...": replay <case>.ops from
<dir> and expect <case>.exp, as app/tb/chip_tb.py does). See the Makefile.
"""
import os

import cocotb
from cocotb.clock import Clock
from cocotb.triggers import ClockCycles, FallingEdge, RisingEdge, Timer

import af_cases

CASES = af_cases.build_cases()
OUT = os.path.abspath(os.environ.get("SIM_BUILD", "sim_build"))


def bit(sig):
    """'0' or '1', or 'x' for anything unresolved (gate level)."""
    v = str(sig.value).lower()
    return v if v in ("0", "1") else "x"


class Bench:
    def __init__(self, dut, tclk_ps, tsclk_ps, gap):
        self.dut = dut
        self.tclk_ps, self.tsclk_ps, self.gap = tclk_ps, tsclk_ps, gap
        self.got = []
        self.n_sent = self.n_frames = self.n_bad = self.pending = 0
        self.ro_mode, self.ro_left = False, 0
        self.nb = -1                       # bits of the frame being received, -1 = none
        self.tasks = []

    async def start(self):
        d = self.dut
        d.rst_n.value = 0
        d.cs_n.value = 1
        d.mosi_u.value = 0
        d.mosi_r.value = 0
        d.debug_i1.value = 0
        d.uio_ctl.value = 0
        self.clocks = [Clock(d.clk, self.tclk_ps, "ps", impl="gpi")]
        self.clocks[0].start(start_high=False)
        self.tasks.append(cocotb.start_soon(self._sclk()))
        self.tasks.append(cocotb.start_soon(self._receive()))
        await ClockCycles(d.clk, 5)
        d.rst_n.value = 1
        await ClockCycles(d.clk, 10)

    async def _sclk(self):
        # SCLK_IN is not edge-aligned with clk; an odd period gets the extra ps low
        await Timer(3300, "ps")
        half = self.tsclk_ps // 2
        clk = Clock(self.dut.sclk_in, self.tsclk_ps, "ps", impl="gpi", period_high=half)
        self.clocks.append(clk)
        clk.start(start_high=False)

    def stop(self):
        for c in self.clocks:
            c.stop()
        for t in self.tasks:
            t.cancel()

    # ---------------- Output receiver ----------------
    async def _receive(self):
        d = self.dut
        sa = sb = ""
        while True:
            await RisingEdge(d.sclk_out)
            if bit(d.fs_out) == "1":
                if self.nb > 0:
                    self.n_bad += 1        # previous frame cut short
                self.nb = 0
                sa = sb = ""
            if self.nb >= 0:
                sa += bit(d.sdo_y)
                sb += bit(d.sdo_e)
                self.nb += 1
                if self.nb == 8:
                    a = f"{int(sa, 2):02x}" if "x" not in sa else "xx"
                    b = f"{int(sb, 2):02x}" if "x" not in sb else "xx"
                    if self.ro_mode:
                        self.got.append(f"R {a} {b}")
                        self.ro_left -= 1
                        if self.ro_left == 0:
                            self.ro_mode = False
                    else:
                        self.got.append(f"Y {a} {b}")
                        self.pending -= 1
                    self.n_frames += 1
                    self.nb = -1

    # ---------------- Input master ----------------
    async def send_frame(self, u, r):
        d = self.dut
        await FallingEdge(d.sclk_in)
        d.cs_n.value = 0
        for i in range(7, -1, -1):
            d.mosi_u.value = (u >> i) & 1
            d.mosi_r.value = (r >> i) & 1
            if i > 0:
                await FallingEdge(d.sclk_in)
        await FallingEdge(d.sclk_in)
        d.cs_n.value = 1
        if self.gap > 1:
            await ClockCycles(d.sclk_in, self.gap - 1, rising=False)
        self.pending += 1

    # Dropped samples (overrun) never produce a frame, so give up after a
    # while and forget them
    async def drain(self):
        t = 0
        while (self.pending > 0 or self.nb >= 0 or self.ro_mode) and t < 2000:
            await RisingEdge(self.dut.clk)
            t += 1
        self.pending = 0
        await ClockCycles(self.dut.clk, 40)

    async def play(self, ops):
        d = self.dut
        for line in ops:
            a = line.split()
            if not a:
                continue
            op = int(a[0], 16)
            if op == 1:
                await self.send_frame(int(a[1], 16) & 0xFF, int(a[2], 16) & 0xFF)
                self.n_sent += 1
            elif op == 2:
                await self.drain()
                d.uio_ctl.value = int(a[1], 16) & 0x3F
                await ClockCycles(d.clk, 10)
            elif op == 5:
                await self.drain()
                self.ro_mode, self.ro_left = True, 4
                d.debug_i1.value = 1
                await ClockCycles(d.clk, 10)
                d.debug_i1.value = 0
                await self.drain()
            elif op == 8:
                await self.drain()
            elif op == 10:
                await self.drain()
                self.got.append(f"F {bit(d.ovf)} {bit(d.sat)}")
        await self.drain()
        return self.got

    def done_line(self):
        return (f"TB done: {self.n_sent} data frames sent, {self.n_frames} frames captured, "
                f"{self.n_bad} bad frames")


async def run_case(dut, case):
    print(af_cases.header(case), flush=True)
    tb = Bench(dut, case.tclk_ps, case.tsclk_ps, case.gap)
    await tb.start()
    got = await tb.play(case.plan.ops)
    tb.stop()
    os.makedirs(OUT, exist_ok=True)
    with open(os.path.join(OUT, f"out_{case.name}.txt"), "w") as fh:
        fh.write("\n".join(got) + "\n")
    ok = af_cases.check(case, got, tb.done_line(), OUT)
    print(flush=True)
    assert ok, f"{case.name}: see the report above"


# The watchdog: a stuck case ends here rather than running forever
TIMEOUT = dict(timeout_time=400, timeout_unit="ms")


@cocotb.test(**TIMEOUT)
async def test_main(dut):
    await run_case(dut, CASES["main"])


@cocotb.test(**TIMEOUT)
async def test_sysid(dut):
    await run_case(dut, CASES["sysid"])


@cocotb.test(**TIMEOUT)
async def test_tight(dut):
    await run_case(dut, CASES["tight"])


@cocotb.test(**TIMEOUT)
async def test_async(dut):
    await run_case(dut, CASES["async"])


@cocotb.test(**TIMEOUT)
async def test_overrun(dut):
    await run_case(dut, CASES["overrun"])


@cocotb.test(**TIMEOUT)
async def test_stress(dut):
    await run_case(dut, CASES["stress"])


@cocotb.test(skip=not os.environ.get("AF_VECTORS"), **TIMEOUT)
async def test_vectors(dut):
    vec = os.environ["AF_VECTORS"]
    ok = True
    for name in os.environ.get("AF_CASES", "").split():
        with open(os.path.join(vec, name + ".ops")) as fh:
            ops = [o for o in fh.read().split("\n") if o]
        with open(os.path.join(vec, name + ".exp")) as fh:
            exp = [e for e in fh.read().split("\n") if e]
        case = af_cases.Case(name, af_cases.Vectors(ops, exp))
        try:
            await run_case(dut, case)
        except AssertionError:
            ok = False
    assert ok, "vectors: see the reports above"
