#pragma once

// The adaptive FIR filter in interference-cancelling mode: y = wᵀx_n, e = d - y,
// and the weights follow e.  x_n = (x[n], x[n-1], ..., x[n-N+1]).
//   sign-sign LMS   w ← w + μ·sign(e)·sign(x_n)
//   LMS             w ← w + μ·e·x_n
//   NLMS            w ← w + μ·e·x_n / (ε + x_nᵀx_n)
//   sign-error LMS  w ← w + μ·sign(e)·x_n
//   sign-data LMS   w ← w + μ·e·sign(x_n)
//   RLS             exponentially weighted recursive least squares (forgetting factor λ)
// Leakage shrinks the weights a little each step first: w ← (1 - leak)·w (not RLS).
// Arithmetic: all of these in floating point, or the chip's own (Chip.h): 4-tap
// sign-sign LMS in Q1.7 samples and Q2.12 weights, bit for bit as the silicon
// does it. x and d are quantized to Q1.7 at a full scale (mV), and y and e
// come back from their codes.
// Also here: the Wiener-optimal N-tap solution from known signals, and the
// frequency response of a set of taps.

#include "Chip.h"

#include <array>
#include <cmath>
#include <complex>
#include <vector>

namespace afx
{
enum class Algorithm { signSign, lms, nlms, signError, signData, rls };
constexpr int numAlgorithms = 6;
const char* algorithmName (Algorithm);    // "sign-sign LMS"
const char* algorithmShort (Algorithm);   // "SS"
const char* algorithmId (Algorithm);      // for files
const char* algorithmUpdate (Algorithm);  // the update, written out
constexpr int maxTaps = 64;
enum class Arithmetic { floating, chip };
const char* arithmeticName (Arithmetic);   // "floating point", "chip (Q1.7 / Q2.12)"
const char* arithmeticId (Arithmetic);     // for files

struct FilterSpec
{
    Algorithm algorithm = Algorithm::signSign;
    int taps = 8;
    // μ for each algorithm (RLS has none), so switching keeps what suits each.
    std::array<double, numAlgorithms> mu { 2e-4, 0.01, 0.01, 5e-4, 3e-3, 0.0 };
    double leakage = 0.0;   // per step
    double epsilon = 1.0;   // NLMS: added to x_nᵀx_n (mV²), so the quiet between beats doesn't make huge steps
    double lambda = 0.9999; // RLS: forgetting factor (a memory of 1/(1-λ) samples)
    double delta = 0.01;    // RLS: P(0) = I/δ
    bool freeze = false;    // hold the weights (still filtering)
    // The chip's arithmetic (the algorithm and taps above are then the chip's: 4-tap sign-sign).
    Arithmetic arithmetic = Arithmetic::floating;
    double fullScale = 1.5; // chip: mV at full scale (Q1.7 code ±128); 1 LSB = fullScale / 128
    int muSel = 0;          // chip: μ = 2^(mu_sel − 12), 0 .. 7

    bool isChip() const { return arithmetic == Arithmetic::chip; }
    double stepSize() const { return isChip() ? chipMu (muSel) : mu[(size_t) algorithm]; }
    Algorithm effectiveAlgorithm() const { return isChip() ? Algorithm::signSign : algorithm; }
    int effectiveTaps() const { return isChip() ? ChipCore::taps : taps; }
    static double chipMu (int sel) { return std::ldexp (1.0, (sel & 7) - 12); }
    bool operator== (const FilterSpec&) const = default;
    static double defaultMu (Algorithm);
};

class AdaptiveFilter
{
public:
    // Applies the settings; a change of taps keeps the weights there are (new ones 0).
    void configure (const FilterSpec&);
    void reset();   // weights to 0 (and RLS's P to I/δ); the input history stays
    void clearHistory();
    // One sample: x in, d in; returns e and leaves y in output().
    double step (double x, double d);
    double output() const { return y; }
    const std::vector<double>& weights() const { return w; }
    void setWeights (const std::vector<double>&);
    const std::vector<double>& input() const { return xn; }   // x_n, newest first
    const FilterSpec& spec() const { return s; }
    // The chip (when its arithmetic is chosen): the core's state and the codes of
    // the last sample (u, r in; y, e out).
    const ChipCore& chip() const { return core; }
    struct Codes
    {
        int u = 0, r = 0, y = 0, e = 0;
    };
    const Codes& lastCodes() const { return codes; }

private:
    void resetP();
    double stepChip (double x, double d);
    void fromChip();   // w and x_n from the core's codes

    ChipCore core;
    Codes codes;

    FilterSpec s;
    std::vector<double> w, xn, P, k, px;   // P is N×N, row-major (RLS)
    double y = 0.0;
};

// The Wiener-optimal N-tap filter from x to `target` over the samples given (equal
// length): w = R⁻¹p with R the input's autocorrelation (Toeplitz) and p the
// cross-correlation.  `mse`: the mean squared error it leaves, over the same samples.
std::vector<double> wiener (const std::vector<float>& x, const std::vector<float>& target, int taps, double* mse = nullptr);

// H(e^{jω}) of FIR taps at f Hz.
std::complex<double> response (const std::vector<double>& taps, double f, double fs);
double magnitudeDb (std::complex<double>);
} // namespace afx
