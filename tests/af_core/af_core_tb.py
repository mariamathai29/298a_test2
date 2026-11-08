"""Bench for driving af_core directly at its ports.

Inputs are driven, and outputs sampled, on the falling edge of clk, half a
cycle away from the rising edge the core acts on, so there are no races.

An output is one tx_load pulse at the end of a step: (tx_a, tx_b, tx_c) =
(y, e, monitor weight top 8 bits). Outputs are recorded signed (-128 .. 127),
like every value the tests compare.
"""
import cocotb
from cocotb.clock import Clock
from cocotb.triggers import ClockCycles, FallingEdge

TCLK_NS = 25                              # 40 MHz
S_IDLE, S_MAC, S_FIN, S_UPD = 0, 1, 2, 3


def s8(v):
    """Signed value of an 8-bit field."""
    v = int(v) & 0xFF
    return v - 256 if v & 0x80 else v


def s14(v):
    """Signed value of a 14-bit field."""
    v = int(v) & 0x3FFF
    return v - 0x4000 if v & 0x2000 else v


class AfCoreTB:
    def __init__(self, dut):
        self.dut = dut
        self.cycle = 0                    # falling edges since start()
        self.outputs = []                 # (cycle, y, e, w), signed

    async def start(self):
        """Start clk with every input idle and the core in reset."""
        d = self.dut
        d.rst_n.value = 0
        d.adapt_en.value = 0
        d.mu_sel.value = 0
        d.d_pre.value = 0
        d.d_stb.value = 0
        d.d_u.value = 0
        d.d_r.value = 0
        Clock(d.clk, TCLK_NS, "ns").start(start_high=False)
        cocotb.start_soon(self._monitor())

    async def reset(self, cycles=5):
        await self.start()
        await ClockCycles(self.dut.clk, cycles, rising=False)
        self.dut.rst_n.value = 1
        await FallingEdge(self.dut.clk)

    async def _monitor(self):
        d = self.dut
        while True:
            await FallingEdge(d.clk)
            self.cycle += 1
            if d.tx_load.value == 1:
                self.outputs.append((self.cycle, s8(d.tx_a.value), s8(d.tx_b.value),
                                     s8(d.tx_c.value)))

    async def wait(self, cycles):
        await ClockCycles(self.dut.clk, cycles, rising=False)

    async def send(self, u, r):
        """Present one sample as the SPI receiver does: d_u / d_r held (they
        stay until the next sample), d_pre for one cycle, then d_stb."""
        d = self.dut
        d.d_u.value = u & 0xFF
        d.d_r.value = r & 0xFF
        d.d_pre.value = 1
        await FallingEdge(d.clk)
        d.d_pre.value = 0
        d.d_stb.value = 1
        await FallingEdge(d.clk)
        d.d_stb.value = 0

    async def send_schedule(self, samples):
        """Present samples on a cycle schedule: (t, u, r) puts d_pre on cycle t
        and d_stb on t + 1, counted in falling edges from now. Times must be at
        least 2 apart (as from the SPI receiver, where they are >= 16)."""
        pre = {t: (u, r) for t, u, r in samples}
        d = self.dut
        for c in range(max(pre) + 2):
            if c in pre:
                d.d_u.value = pre[c][0] & 0xFF
                d.d_r.value = pre[c][1] & 0xFF
            d.d_pre.value = int(c in pre)
            d.d_stb.value = int(c - 1 in pre)
            await FallingEdge(d.clk)
        d.d_stb.value = 0

    def set_ctl(self, adapt_en, mu_sel):
        self.dut.adapt_en.value = adapt_en
        self.dut.mu_sel.value = mu_sel

    # ---------------- White-box reads ----------------
    def state(self):
        return int(self.dut.state.value)

    def taps(self):
        return [s8(self.dut.x[k].value) for k in range(4)]

    def weights(self):
        return [s14(self.dut.w[k].value) for k in range(4)]

    def set_weights(self, w):
        """Deposit weights (Q2.12) into the ring; they hold until it rotates."""
        for k in range(4):
            self.dut.w[k].value = w[k] & 0x3FFF

    def set_taps(self, x):
        """Deposit taps (Q1.7) into the delay line; x[0] = newest."""
        for k in range(4):
            self.dut.x[k].value = x[k] & 0xFF

    async def until_state(self, s, limit=100):
        """Wait for a falling edge in a cycle where the FSM is in state s."""
        for _ in range(limit):
            await FallingEdge(self.dut.clk)
            if self.state() == s:
                return
        raise AssertionError(f"state {s} not reached in {limit} cycles")

    async def step(self, u, r):
        """Run one step from idle; return its output (y, e), signed."""
        assert self.state() == S_IDLE, "step() must start from idle"
        n = len(self.outputs)
        await self.send(u, r)
        for _ in range(40):
            if len(self.outputs) > n:
                break
            await FallingEdge(self.dut.clk)
        else:
            raise AssertionError(f"no output for sample u={u} r={r}")
        await self.wait(4)                # rest of UPD
        _, y, e, _ = self.outputs[n]
        return y, e

    def snapshot(self):
        """Every control and both rings, as seen during the current cycle."""
        d = self.dut
        return {
            "cycle":   self.cycle,
            "state":   self.state(),
            "cnt":     int(d.cnt.value),
            "go":      [int(d.go_xs.value[i]) for i in range(2)] + [int(d.go_a.value), int(d.go_f.value)],
            "rot":     [int(d.rot_x.value[i]) for i in range(2)] + [int(d.rot_w.value[i]) for i in range(4)],
            "tx_load": int(d.tx_load.value),
            "x":       self.taps(),
            "w":       self.weights(),
        }

    async def trace(self, cycles):
        """Snapshot on each of the next `cycles` falling edges."""
        rows = []
        for _ in range(cycles):
            await FallingEdge(self.dut.clk)
            rows.append(self.snapshot())
        return rows
