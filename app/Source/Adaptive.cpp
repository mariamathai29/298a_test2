#include "Adaptive.h"

#include "Signals.h"

#include <algorithm>
#include <cmath>

namespace afx
{
namespace
{
const char* const names[numAlgorithms] = { "sign-sign LMS", "LMS", "NLMS", "sign-error LMS", "sign-data LMS", "RLS" };
const char* const shorts[numAlgorithms] = { "SS", "LMS", "NLMS", "SE", "SD", "RLS" };
const char* const ids[numAlgorithms] = { "signSign", "lms", "nlms", "signError", "signData", "rls" };
const char* const updates[numAlgorithms] = { "w ← w + μ·sign(e)·sign(xₙ)", "w ← w + μ·e·xₙ", "w ← w + μ·e·xₙ / (ε + xₙᵀxₙ)",
                                              "w ← w + μ·sign(e)·xₙ", "w ← w + μ·e·sign(xₙ)",
                                              "k = Pxₙ / (λ + xₙᵀPxₙ);  w ← w + k·e;  P ← (P - k·xₙᵀP) / λ" };

const char* const arithNames[2] = { "floating point", "chip (Q1.7 / Q2.12)" };
const char* const arithIds[2] = { "float", "chip" };

double sign (double v) { return v > 0.0 ? 1.0 : v < 0.0 ? -1.0 : 0.0; }
} // namespace

const char* algorithmName (Algorithm a) { return names[std::clamp ((int) a, 0, numAlgorithms - 1)]; }
const char* algorithmShort (Algorithm a) { return shorts[std::clamp ((int) a, 0, numAlgorithms - 1)]; }
const char* algorithmId (Algorithm a) { return ids[std::clamp ((int) a, 0, numAlgorithms - 1)]; }
const char* algorithmUpdate (Algorithm a) { return updates[std::clamp ((int) a, 0, numAlgorithms - 1)]; }

const char* arithmeticName (Arithmetic a) { return arithNames[a == Arithmetic::chip ? 1 : 0]; }
const char* arithmeticId (Arithmetic a) { return arithIds[a == Arithmetic::chip ? 1 : 0]; }

double FilterSpec::defaultMu (Algorithm a) { return FilterSpec {}.mu[(size_t) a]; }

// ================================================================ AdaptiveFilter
void AdaptiveFilter::configure (const FilterSpec& spec)
{
    const int before = (int) w.size();
    const bool rlsBefore = s.algorithm == Algorithm::rls;
    const bool chipBefore = s.isChip();
    const std::vector<double> held = w;
    s = spec;
    s.taps = std::clamp (s.effectiveTaps(), 1, maxTaps);
    s.fullScale = std::clamp (s.fullScale, 1e-3, 100.0);
    s.muSel = std::clamp (s.muSel, 0, 7);
    if (s.isChip())
    {
        s.algorithm = Algorithm::signSign;
        core.muSel = s.muSel;
        if (! chipBefore)
        {
            // Into the chip: the weights as near as its registers hold them, the taps afresh.
            core.reset();
            w.assign ((size_t) s.taps, 0.0);
            xn.assign ((size_t) s.taps, 0.0);
            setWeights (held);
        }
        k.resize ((size_t) s.taps, 0.0);
        px.resize ((size_t) s.taps, 0.0);
        return;
    }
    s.lambda = std::clamp (s.lambda, 0.5, 1.0);
    s.delta = std::max (s.delta, 1e-9);
    const size_t n = (size_t) s.taps;
    w.resize (n, 0.0);
    xn.resize (n, 0.0);
    k.resize (n, 0.0);
    px.resize (n, 0.0);
    // RLS starts its P afresh when it starts, or when its size changes.
    if (s.algorithm == Algorithm::rls && (! rlsBefore || before != s.taps || P.size() != n * n)) resetP();
}

void AdaptiveFilter::reset()
{
    std::fill (w.begin(), w.end(), 0.0);
    core.w.fill (0);
    core.satFlag = false;
    resetP();
    y = 0.0;
}

void AdaptiveFilter::clearHistory()
{
    std::fill (xn.begin(), xn.end(), 0.0);
    core.x.fill (0);
}

void AdaptiveFilter::setWeights (const std::vector<double>& v)
{
    for (size_t i = 0; i < w.size(); ++i) w[i] = i < v.size() ? v[i] : 0.0;
    if (s.isChip())
    {
        constexpr double lsb = 1 << ChipCore::weightFrac;
        constexpr int top = (1 << (ChipCore::weightBits - 1)) - 1;
        for (size_t i = 0; i < core.w.size(); ++i)
            core.w[i] = i < w.size() && std::isfinite (w[i]) ? (int) std::clamp (std::lround (w[i] * lsb), (long) -top - 1, (long) top) : 0;
        fromChip();
    }
}

void AdaptiveFilter::fromChip()
{
    const double lsb = 1.0 / (1 << ChipCore::weightFrac), unit = s.fullScale / 128.0;
    for (size_t i = 0; i < w.size() && i < core.w.size(); ++i)
    {
        w[i] = core.w[i] * lsb;
        xn[i] = core.x[i] * unit;
    }
}

double AdaptiveFilter::stepChip (double x, double d)
{
    codes.u = toQ17 (x / s.fullScale);
    codes.r = toQ17 (d / s.fullScale);
    core.adaptEn = ! s.freeze;
    core.muSel = s.muSel;
    const auto o = core.step (codes.u, codes.r);
    codes.y = o.y;
    codes.e = o.e;
    fromChip();
    const double unit = s.fullScale / 128.0;
    y = o.y * unit;
    return o.e * unit;
}

void AdaptiveFilter::resetP()
{
    const size_t n = (size_t) s.taps;
    P.assign (n * n, 0.0);
    for (size_t i = 0; i < n; ++i) P[i * n + i] = 1.0 / s.delta;
}

double AdaptiveFilter::step (double x, double d)
{
    const size_t n = w.size();
    if (n == 0) return d;
    if (s.isChip()) return stepChip (x, d);
    // The newest sample in front.
    for (size_t i = n - 1; i > 0; --i) xn[i] = xn[i - 1];
    xn[0] = x;
    y = 0.0;
    for (size_t i = 0; i < n; ++i) y += w[i] * xn[i];
    const double e = d - y;
    if (s.freeze) return e;

    const double mu = s.stepSize();
    if (s.algorithm != Algorithm::rls && s.leakage > 0.0)
    {
        const double keep = 1.0 - std::min (s.leakage, 1.0);
        for (auto& v : w) v *= keep;
    }
    switch (s.algorithm)
    {
        case Algorithm::signSign:
        {
            const double g = mu * sign (e);
            if (g != 0.0)
                for (size_t i = 0; i < n; ++i) w[i] += g * sign (xn[i]);
            break;
        }
        case Algorithm::lms:
            for (size_t i = 0; i < n; ++i) w[i] += mu * e * xn[i];
            break;
        case Algorithm::nlms:
        {
            double energy = 0.0;
            for (const double v : xn) energy += v * v;
            const double g = mu * e / (std::max (s.epsilon, 1e-12) + energy);
            for (size_t i = 0; i < n; ++i) w[i] += g * xn[i];
            break;
        }
        case Algorithm::signError:
        {
            const double g = mu * sign (e);
            for (size_t i = 0; i < n; ++i) w[i] += g * xn[i];
            break;
        }
        case Algorithm::signData:
            for (size_t i = 0; i < n; ++i) w[i] += mu * e * sign (xn[i]);
            break;
        case Algorithm::rls:
        {
            if (P.size() != n * n) resetP();
            // px = P x;  k = px / (λ + xᵀ px);  w += k e;  P = (P - k pxᵀ) / λ  (P symmetric).
            double denominator = s.lambda;
            for (size_t i = 0; i < n; ++i)
            {
                double v = 0.0;
                for (size_t j = 0; j < n; ++j) v += P[i * n + j] * xn[j];
                px[i] = v;
                denominator += xn[i] * v;
            }
            for (size_t i = 0; i < n; ++i)
            {
                k[i] = px[i] / denominator;
                w[i] += k[i] * e;
            }
            const double inv = 1.0 / s.lambda;
            for (size_t i = 0; i < n; ++i)
                for (size_t j = i; j < n; ++j)
                {
                    const double v = (P[i * n + j] - k[i] * px[j]) * inv;
                    P[i * n + j] = v;
                    P[j * n + i] = v;   // kept symmetric against rounding
                }
            break;
        }
    }
    return e;
}

// ================================================================ the Wiener solution
std::vector<double> wiener (const std::vector<float>& x, const std::vector<float>& target, int taps, double* mse)
{
    const size_t n = (size_t) std::clamp (taps, 1, maxTaps), count = std::min (x.size(), target.size());
    std::vector<double> w (n, 0.0);
    if (mse != nullptr) *mse = 0.0;
    if (count < n * 4) return w;
    // The covariance method: over the samples m with a whole x_m behind them,
    //   R[i][j] = Σ x[m-i]·x[m-j],  p[i] = Σ t[m]·x[m-i]
    // (exactly consistent, so a target that is an N-tap filtering of x gives that filter back).
    // Each diagonal of R follows from the one before: R[i+1][j+1] = R[i][j] + x[m0-1-i]·x[m0-1-j] - x[m1-i]·x[m1-j].
    const size_t m0 = n - 1, m1 = count - 1;
    std::vector<double> R (n * n, 0.0), p (n, 0.0);
    for (size_t j = 0; j < n; ++j)
    {
        double a = 0.0, b = 0.0;
        for (size_t m = m0; m <= m1; ++m)
        {
            a += (double) x[m] * x[m - j];
            b += (double) target[m] * x[m - j];
        }
        R[j] = R[j * n] = a;
        p[j] = b;
    }
    for (size_t i = 0; i + 1 < n; ++i)
        for (size_t j = i; j + 1 < n; ++j)
        {
            const double v = R[i * n + j] + (double) x[m0 - 1 - i] * x[m0 - 1 - j] - (double) x[m1 - i] * x[m1 - j];
            R[(i + 1) * n + j + 1] = R[(j + 1) * n + i + 1] = v;
        }
    // R w = p by Cholesky (R = LLᵀ), with a whisker of ridge for inputs that don't excite every tap.
    std::vector<double> L (n * n, 0.0);
    const double ridge = 1e-10 * R[0] + 1e-300;
    for (size_t i = 0; i < n; ++i)
        for (size_t j = 0; j <= i; ++j)
        {
            double v = R[i * n + j] + (i == j ? ridge : 0.0);
            for (size_t q = 0; q < j; ++q) v -= L[i * n + q] * L[j * n + q];
            if (i == j) L[i * n + i] = std::sqrt (std::max (v, 1e-300));
            else L[i * n + j] = v / L[j * n + j];
        }
    std::vector<double> z (n);
    for (size_t i = 0; i < n; ++i)
    {
        double v = p[i];
        for (size_t q = 0; q < i; ++q) v -= L[i * n + q] * z[q];
        z[i] = v / L[i * n + i];
    }
    for (size_t i = n; i-- > 0;)
    {
        double v = z[i];
        for (size_t q = i + 1; q < n; ++q) v -= L[q * n + i] * w[q];
        w[i] = v / L[i * n + i];
    }
    if (mse != nullptr)
    {
        double sum = 0.0;
        for (size_t m = n - 1; m < count; ++m)
        {
            double y = 0.0;
            for (size_t i = 0; i < n; ++i) y += w[i] * x[m - i];
            const double e = target[m] - y;
            sum += e * e;
        }
        *mse = sum / (double) (count - n + 1);
    }
    return w;
}

std::complex<double> response (const std::vector<double>& taps, double f, double fs)
{
    std::complex<double> h { 0.0, 0.0 };
    const double w = 2.0 * pi * f / fs;
    for (size_t i = 0; i < taps.size(); ++i) h += taps[i] * std::polar (1.0, -w * (double) i);
    return h;
}

double magnitudeDb (std::complex<double> h) { return 20.0 * std::log10 (std::max (std::abs (h), 1e-6)); }
} // namespace afx
