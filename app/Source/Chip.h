#pragma once

// The chip's datapath, bit for bit: the 4-tap sign-sign LMS core of
// tt_um_adaptive_filter (assess4d v5, src/af_core.v), one call per input sample.
// Everything is an integer holding the raw fixed-point code, as in the RTL and in
// its golden model (../lms_model.py with N_TAPS = 4):
//   u, r, y, e      signed 8-bit  Q1.7
//   w[k] register   signed 14-bit Q2.12;  the FIR uses its top 10 bits (Q2.8, w >> 4)
//   product         x[k]·(w[k] >> 4), Q3.15;  accumulator Q5.15 (20 bits in the RTL)
//   y               (acc + 2^7) >> 8, saturated to 8 bits (round half up)
//   e               r − y (9 bits), saturated to 8 bits for the output
//   update          w[k] ← sat14 (w[k] + 2^mu_sel · sign(r − y) · sign(x[k]))
//                   (mu = 2^(mu_sel − 12); sign(0) = 0; the unsaturated r − y)
//   sat flag        sticky: y or e saturated, or a weight update saturated
// Readout (debug_i1 with adapt_en = 0) gives, for each k, the top 8 bits of w[k]
// (Q2.6) on the y lane and x[k] on the e lane; it leaves the state as it was.
//
// tb/chip_tb.py checks this against lms_model.py and the RTL (and the routed netlist).

#include <array>
#include <cstdint>

namespace afx
{
struct ChipCore
{
    static constexpr int taps = 4;
    static constexpr int weightBits = 14, weightFrac = 12;   // Q2.12
    static constexpr int firShift = 4;                       // the FIR's Q2.8 = w >> 4
    static constexpr int sampleFrac = 7;                     // Q1.7

    struct Out
    {
        int y = 0, e = 0;   // Q1.7 codes, -128 .. 127
    };
    struct Readout
    {
        int weight = 0, tap = 0;   // w[k] >> 6 (Q2.6) and x[k], as signed 8-bit codes
    };

    std::array<int, taps> w {};   // Q2.12 codes
    std::array<int, taps> x {};   // Q1.7 codes, x[0] newest
    bool adaptEn = true;          // uio[0]
    int muSel = 0;                // uio[3:1], 0 .. 7
    bool satFlag = false;         // debug_o2

    void reset();   // rst_n: weights, taps and the flag
    // One sample (u and r Q1.7 codes, -128 .. 127).
    Out step (int u, int r);
    Readout readout (int k) const;
    int muCode() const { return 1 << (muSel & 7); }   // in weight LSBs
};

// Float to a saturated Q1.7 code, round to nearest (lms_model.to_q (v, 7, 8)):
// v in units of full scale (±1).
int toQ17 (double v);
} // namespace afx
