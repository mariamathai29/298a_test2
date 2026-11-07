#include "Scene.h"

#include <algorithm>
#include <cmath>

namespace afx
{
namespace
{
// A 16-tap windowed-sinc low-pass at 30 Hz (Hann window), unity gain at DC.
std::vector<double> longLowPass()
{
    constexpr int n = 16;
    const double fc = 30.0 / sampleRate;
    std::vector<double> h (n);
    double sum = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double m = i - 0.5 * (n - 1);
        const double sinc = std::abs (m) < 1e-12 ? 2.0 * fc : std::sin (2.0 * pi * fc * m) / (pi * m);
        h[(size_t) i] = sinc * (0.5 - 0.5 * std::cos (2.0 * pi * (i + 0.5) / n));
        sum += h[(size_t) i];
    }
    for (auto& v : h) v /= sum;
    return h;
}
} // namespace

std::vector<double> PathSpec::effective() const
{
    std::vector<double> h ((size_t) std::clamp (delay, 0, maxPathDelay), 0.0);
    for (size_t i = 0; i < taps.size() && i < (size_t) maxPathTaps; ++i) h.push_back (gain * taps[i]);
    if (h.empty()) h.push_back (0.0);
    return h;
}

const std::vector<PathPreset>& pathPresets()
{
    static const std::vector<PathPreset> list {
        { "direct", "straight through: H = 1", { 1.0 }, 0 },
        { "attenuated", "a smaller copy: H = 0.6", { 0.6 }, 0 },
        { "inverted", "upside down and smaller: H = −0.8", { -0.8 }, 0 },
        { "delay 2", "two samples (4 ms) late", { 1.0 }, 2 },
        { "abdominal", "the default interference path: 4 taps, the heartbeat a little later and smeared on its way to the abdomen",
          { 0.15, 0.5, 0.3, 0.1 }, 0 },
        { "rough", "5 taps with sharp alternations: its high frequencies are ones an ECG barely has", { 0.25, 0.9, -0.45, 0.2, -0.08 }, 0 },
        { "short echo", "the signal and an echo 3 samples later: 4 taps", { 1.0, 0.0, 0.0, 0.5 }, 0 },
        { "low-pass", "a 5-tap binomial smoother (about 100 Hz)", { 1.0 / 16, 4.0 / 16, 6.0 / 16, 4.0 / 16, 1.0 / 16 }, 0 },
        { "delay 10", "10 samples (20 ms) late: 11 taps, more than 8 can reach", { 1.0 }, 10 },
        { "long echo", "an echo 12 samples (24 ms) later: 13 taps", { 1.0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.5 }, 0 },
        { "long low-pass", "a 16-tap windowed-sinc low-pass at 30 Hz", longLowPass(), 0 } };
    return list;
}

PathSpec pathFromPreset (const std::string& name)
{
    PathSpec p;
    for (const auto& q : pathPresets())
        if (name == q.name)
        {
            p.taps = q.taps;
            p.delay = q.delay;
            p.gain = 1.0;
            p.preset = q.name;
            return p;
        }
    return p;
}

SourceSpec quietLead()
{
    auto s = SourceSpec::defaults (SourceKind::white);
    s.amplitude = 0.002;
    s.seed = 7;
    return s;
}

Scene Scene::defaults() { return Scene {}; }

Scene Scene::chip()
{
    Scene sc;
    sc.filter.arithmetic = Arithmetic::chip;
    sc.filter.fullScale = 1.5;   // d peaks near 1.25 mV: nothing clips
    sc.filter.muSel = 1;         // μ = 2^-11: settles in about 30 s here
    return sc;
}

// h = Hp/Hr by long division: h[n] = (p[n] - Σ_{j≥1} r[j]·h[n-j]) / r[0], after the
// common delay is taken out of both.
TruePath truePath (const PathSpec& primary, const PathSpec& reference, int length)
{
    TruePath out;
    const auto p = primary.effective(), r = reference.effective();
    size_t k0 = 0;
    while (k0 < r.size() && std::abs (r[k0]) < 1e-12) ++k0;
    if (k0 == r.size())
    {
        out.exists = false;
        out.taps.assign ((size_t) length, 0.0);
        return out;
    }
    for (size_t i = 0; i < k0 && i < p.size(); ++i)
        if (std::abs (p[i]) > 1e-12) out.causal = false;
    constexpr size_t longer = 512;   // long enough to see whether it dies away
    std::vector<double> h (longer, 0.0);
    double peak = 0.0;
    for (size_t n = 0; n < longer; ++n)
    {
        double v = n + k0 < p.size() ? p[n + k0] : 0.0;
        for (size_t j = 1; k0 + j < r.size() && j <= n; ++j) v -= r[k0 + j] * h[n - j];
        h[n] = v / r[k0];
        if (! std::isfinite (h[n]) || std::abs (h[n]) > 1e6)
        {
            out.stable = false;
            h.resize (n);
            break;
        }
        peak = std::max (peak, std::abs (h[n]));
    }
    if (out.stable && h.size() == longer)
    {
        double tail = 0.0;
        for (size_t n = longer / 2; n < longer; ++n) tail = std::max (tail, std::abs (h[n]));
        if (tail > 0.05 * peak) out.stable = false;
    }
    out.needed = 0;
    for (size_t n = 0; n < h.size(); ++n)
        if (std::abs (h[n]) > 0.01 * peak) out.needed = (int) n + 1;
    if (! out.stable) out.needed = (int) longer;
    out.taps.assign ((size_t) length, 0.0);
    for (size_t n = 0; n < (size_t) length && n < h.size(); ++n) out.taps[n] = h[n];
    return out;
}
} // namespace afx
