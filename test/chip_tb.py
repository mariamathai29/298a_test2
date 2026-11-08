"""Bench for driving tt_um_adaptive_filter through its pins (see tb.v).

The bench is the host: an SPI master on the input port (mode 0, MSB first,
CS active low, u and r on two lanes) and a frame-sync receiver on the output
port (data captured on rising SCLK_OUT, FS_OUT high for the first bit). A
frame is (y, e, w): w is the monitor weight, top 8 bits (Q2.6). Values are
signed (-128 .. 127).

Operating spec: clk 40 MHz, SCLK_IN 20 MHz, 13 SCLK_IN periods per sample
(8 bits + 5 with CS high).
"""
import cocotb
from cocotb.clock import Clock
from cocotb.triggers import ClockCycles, FallingEdge, RisingEdge, Timer

TCLK_PS = 25000                           # clk 40 MHz
TSCLK_PS = 50000                          # SCLK_IN 20 MHz
GAP = 5                                   # SCLK_IN periods with CS high per sample


def s8(v):
    """Signed value of an 8-bit field."""
    v = int(v) & 0xFF
    return v - 256 if v & 0x80 else v


def bit(sig):
    """'0' or '1', or 'x' for anything unresolved (gate level)."""
    v = str(sig.value).lower()
    return v if v in ("0", "1") else "x"


class ChipTB:
    def __init__(self, dut, tclk_ps=TCLK_PS, tsclk_ps=TSCLK_PS, gap=GAP):
        self.dut = dut
        self.tclk_ps, self.tsclk_ps, self.gap = tclk_ps, tsclk_ps, gap
        self.frames = []                  # (y, e, w), signed; None for a lane with X
        self.bad_frames = 0               # frames cut short by a new FS_OUT
        self.sent = 0

    async def start(self):
        """Clocks running, every input idle, chip in reset."""
        d = self.dut
        d.ena.value = 1
        d.rst_n.value = 0
        d.sclk_in.value = 0
        d.cs_n.value = 1
        d.mosi_u.value = 0
        d.mosi_r.value = 0
        d.adapt_en.value = 0
        d.mu_sel.value = 0
        Clock(d.clk, self.tclk_ps, "ps").start(start_high=False)
        cocotb.start_soon(self._sclk_in())
        cocotb.start_soon(self._receive())

    async def reset(self):
        await self.start()
        await ClockCycles(self.dut.clk, 10)
        self.dut.rst_n.value = 1
        await ClockCycles(self.dut.clk, 10)  # reset synchronizer

    async def _sclk_in(self):
        # SCLK_IN is not aligned with clk; an odd period gets the extra ps low
        await Timer(3300, "ps")
        Clock(self.dut.sclk_in, self.tsclk_ps, "ps",
              period_high=self.tsclk_ps // 2).start(start_high=False)

    async def _receive(self):
        d = self.dut
        n, ys, es, ws = -1, "", "", ""
        while True:
            await RisingEdge(d.sclk_out)
            if bit(d.fs_out) == "1":
                if n > 0:
                    self.bad_frames += 1
                n, ys, es, ws = 0, "", "", ""
            if n >= 0:
                ys += bit(d.sdo_y)
                es += bit(d.sdo_e)
                ws += bit(d.sdo_w)
                n += 1
                if n == 8:
                    self.frames.append(tuple(s8(int(b, 2)) if "x" not in b else None
                                             for b in (ys, es, ws)))
                    n = -1

    def set_ctl(self, adapt_en, mu_sel):
        """Control pins; they reach the core through 2-flop synchronizers."""
        self.dut.adapt_en.value = adapt_en
        self.dut.mu_sel.value = mu_sel

    async def send(self, u, r):
        """One sample: CS low, 8 bits MSB first (data changes on falling
        SCLK_IN, the chip samples on rising), then CS high for `gap` periods."""
        d = self.dut
        await FallingEdge(d.sclk_in)
        d.cs_n.value = 0
        for i in range(7, -1, -1):
            d.mosi_u.value = (u >> i) & 1
            d.mosi_r.value = (r >> i) & 1
            await FallingEdge(d.sclk_in)
        d.cs_n.value = 1
        await ClockCycles(d.sclk_in, self.gap - 1, rising=False)
        self.sent += 1

    async def send_partial(self, u, r, bits):
        """An aborted frame: CS low for only `bits` (1 .. 7) bits, then high."""
        d = self.dut
        await FallingEdge(d.sclk_in)
        d.cs_n.value = 0
        for i in range(7, 7 - bits, -1):
            d.mosi_u.value = (u >> i) & 1
            d.mosi_r.value = (r >> i) & 1
            await FallingEdge(d.sclk_in)
        d.cs_n.value = 1
        await ClockCycles(d.sclk_in, self.gap - 1, rising=False)

    async def drain(self, expect, limit=2000):
        """Wait until `expect` frames have arrived (or `limit` clk cycles)."""
        for _ in range(limit):
            if len(self.frames) >= expect:
                break
            await RisingEdge(self.dut.clk)
        await ClockCycles(self.dut.clk, 40)   # let any frame in flight finish
