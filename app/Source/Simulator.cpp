#include "Simulator.h"

#include <algorithm>
#include <cmath>

namespace afx
{
void Ring::last (size_t want, std::vector<float>& out) const
{
    const size_t k = std::min (want, count);
    out.resize (k);
    const size_t cap = data.size();
    size_t from = (head + cap - k) % cap;
    for (size_t i = 0; i < k; ++i)
    {
        out[i] = data[from];
        from = from + 1 == cap ? 0 : from + 1;
    }
}

namespace
{
double db (double power) { return 10.0 * std::log10 (std::max (power, 1e-30)); }
} // namespace

Simulator::Simulator (const Scene& scene)
{
    for (auto& r : rings) r = Ring ((size_t) (historySeconds * sampleRate));
    line.assign ((size_t) (maxPathDelay + maxPathTaps + 1), 0.0);
    setScene (scene);
    restart();
}

void Simulator::setScene (const Scene& scene)
{
    s = scene;
    sources[0].configure (s.signal);
    sources[1].configure (s.interference);
    sources[2].configure (s.primaryNoise);
    sources[3].configure (s.referenceNoise);
    hp = s.primaryPath.effective();
    hr = s.referencePath.effective();
    adaptive.configure (s.filter);
    twinFilter.configure (twinSpec (s.filter));
    if (s.filter.isChip() && ! wasChip)
    {
        // The twin starts where the chip does.
        twinFilter.setWeights (adaptive.weights());
        twinAvg = outAvg;
    }
    wasChip = s.filter.isChip();
    trueH = truePath (s.primaryPath, s.referencePath);
}

FilterSpec Simulator::twinSpec (const FilterSpec& f)
{
    FilterSpec t = f;
    t.arithmetic = Arithmetic::floating;
    t.algorithm = Algorithm::signSign;
    t.taps = ChipCore::taps;
    t.mu[(size_t) Algorithm::signSign] = FilterSpec::chipMu (f.muSel);
    t.leakage = 0.0;
    return t;
}

void Simulator::restart()
{
    for (auto& src : sources) src.restart();
    adaptive.reset();
    adaptive.clearHistory();
    twinFilter.reset();
    twinFilter.clearHistory();
    twinAvg = 0.0;
    clips = 0;
    std::fill (line.begin(), line.end(), 0.0);
    for (auto& r : rings) r.clear();
    n = 0;
    outAvg = inAvg = sigAvg = 0.0;
    points.clear();
    pointEvery = 10;
    pointCount = 0;
    pointOut = pointIn = 0.0;
    wienerW.assign ((size_t) s.filter.effectiveTaps(), 0.0);
    wienerE = 0.0;
    wienerAt = -1.0;
}

void Simulator::resetWeights()
{
    adaptive.reset();
    twinFilter.reset();
}

double Simulator::improvementTwin() const { return db (inAvg) - db (twinAvg); }

double Simulator::snrIn() const { return db (sigAvg) - db (inAvg); }
double Simulator::snrOut() const { return db (sigAvg) - db (outAvg); }

void Simulator::run (long long count)
{
    for (long long i = 0; i < count; ++i)
    {
        const double sig = sources[0].next(), m = sources[1].next(), np = sources[2].next(), nr = sources[3].next();
        for (size_t k = line.size() - 1; k > 0; --k) line[k] = line[k - 1];
        line[0] = m;
        double ip = 0.0, ir = 0.0;
        for (size_t k = 0; k < hp.size(); ++k) ip += hp[k] * line[k];
        for (size_t k = 0; k < hr.size(); ++k) ir += hr[k] * line[k];
        const double d = sig + ip + np;
        const double x = ir + nr + s.signalInReference * sig;
        const double e = adaptive.step (x, d);
        const double y = adaptive.output();
        double twinErr = 0.0;
        if (s.filter.isChip())
        {
            twinErr = twinFilter.step (x, d) - sig;
            const auto& c = adaptive.lastCodes();
            if (c.u <= -128 || c.u >= 127 || c.r <= -128 || c.r >= 127) ++clips;
        }
        const float values[numChannels] = { (float) sig, (float) m, (float) np, (float) nr, (float) d, (float) x, (float) y, (float) e };
        for (int c = 0; c < numChannels; ++c) rings[(size_t) c].push (values[c]);
        ++n;
        record (sig, e - sig, d - sig, twinErr);
    }
}

void Simulator::record (double sig, double err, double inErr, double twinErr)
{
    // An exponential average, a plain running mean until it has seen a time constant.
    const double a = std::max (1.0 / (smoothing * sampleRate), 1.0 / (double) n);
    outAvg += a * (err * err - outAvg);
    inAvg += a * (inErr * inErr - inAvg);
    sigAvg += a * (sig * sig - sigAvg);
    if (s.filter.isChip()) twinAvg += a * (twinErr * twinErr - twinAvg);
    if (++pointCount < pointEvery) return;
    pointCount = 0;
    CurvePoint p {};
    p.time = (float) time();
    p.mseOut = (float) outAvg;
    p.mseIn = (float) inAvg;
    p.mseTwin = s.filter.isChip() ? (float) twinAvg : 0.0f;
    const auto& w = adaptive.weights();
    for (size_t i = 0; i < w.size() && i < p.w.size(); ++i) p.w[i] = (float) w[i];
    points.push_back (p);
    if (points.size() >= maxPoints)
    {
        // Every other point, and half as often from now on.
        size_t j = 0;
        for (size_t i = 0; i < points.size(); i += 2) points[j++] = points[i];
        points.resize (j);
        pointEvery *= 2;
    }
}

void Simulator::solveWiener (double seconds)
{
    const size_t want = (size_t) std::max (1.0, seconds * sampleRate);
    std::vector<float> x, d, sig;
    rings[(size_t) Channel::reference].last (want, x);
    rings[(size_t) Channel::primary].last (want, d);
    rings[(size_t) Channel::signal].last (want, sig);
    for (size_t i = 0; i < d.size() && i < sig.size(); ++i) d[i] -= sig[i];   // the interference in d, and its noise
    wienerW = wiener (x, d, s.filter.effectiveTaps(), &wienerE);
    wienerAt = time();
}
} // namespace afx
