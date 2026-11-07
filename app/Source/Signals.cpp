#include "Signals.h"

#include <algorithm>
#include <cmath>

namespace afx
{
namespace
{
// The ECG's waves: P, Q, R, S and T, each a Gaussian bump on the beat phase θ
// (radians; the R wave at 0), after McSharry et al. (2003), "A dynamical model for
// generating synthetic electrocardiogram signals" (ECGSYN).  ECGSYN integrates
// dz/dt = -Σ a_i Δθ_i ω exp(-Δθ_i² / 2b_i²) - (z - z0); its solution, without the
// baseline term, is the sum of the bumps themselves, which is what is drawn here.
// The heights are relative to the R wave.
struct Wave
{
    double theta, height, width;
};
constexpr Wave maternalWaves[5] = { { -pi / 3.0, 0.15, 0.25 }, { -pi / 12.0, -0.12, 0.1 }, { 0.0, 1.0, 0.1 },
                                    { pi / 12.0, -0.25, 0.1 }, { pi / 2.0, 0.30, 0.4 } };
// The fetal complex: the P and T waves smaller against the QRS.
constexpr Wave fetalWaves[5] = { { -pi / 3.0, 0.08, 0.25 }, { -pi / 12.0, -0.10, 0.1 }, { 0.0, 1.0, 0.1 },
                                 { pi / 12.0, -0.22, 0.1 }, { pi / 2.0, 0.15, 0.4 } };

const char* const kindNames[numSourceKinds] = { "none", "maternal ECG", "fetal ECG", "sine", "square", "chirp", "white noise",
                                                "pink noise", "power-line hum", "baseline wander", "EMG-like noise" };
const char* const kindIds[numSourceKinds] = { "none", "maternalEcg", "fetalEcg", "sine", "square", "chirp", "white",
                                              "pink", "hum", "wander", "emg" };

// The EMG band, 20 .. 150 Hz: one band-pass biquad (RBJ, constant peak gain).
struct Biquad
{
    double b0, b2, a1, a2;   // b1 is 0
};
Biquad emgBand()
{
    const double f0 = std::sqrt (20.0 * 150.0), q = f0 / (150.0 - 20.0);
    const double w = 2.0 * pi * f0 / sampleRate, alpha = std::sin (w) / (2.0 * q), a0 = 1.0 + alpha;
    return { alpha / a0, -alpha / a0, -2.0 * std::cos (w) / a0, (1.0 - alpha) / a0 };
}

// The sum of the bumps at the R wave (the others barely touch it): divided out, the R wave peaks at 1.
double ecgPeak()
{
    double z = 0.0;
    for (const auto& w : maternalWaves) z += w.height * std::exp (-w.theta * w.theta / (2.0 * w.width * w.width));
    return z;
}
} // namespace

const char* sourceKindName (SourceKind k) { return kindNames[std::clamp ((int) k, 0, numSourceKinds - 1)]; }
const char* sourceKindId (SourceKind k) { return kindIds[std::clamp ((int) k, 0, numSourceKinds - 1)]; }
bool isEcg (SourceKind k) { return k == SourceKind::maternalEcg || k == SourceKind::fetalEcg; }

SourceSpec SourceSpec::defaults (SourceKind k)
{
    SourceSpec s;
    s.kind = k;
    switch (k)
    {
        case SourceKind::none: s.amplitude = 0.0; break;
        case SourceKind::maternalEcg: s.amplitude = 1.0; s.bpm = 75.0; s.hrv = 0.04; s.breathing = 0.25; s.seed = 1; break;
        case SourceKind::fetalEcg: s.amplitude = 0.2; s.bpm = 140.0; s.hrv = 0.03; s.breathing = 0.7; s.seed = 2; break;
        case SourceKind::sine: s.amplitude = 0.2; s.frequency = 10.0; break;
        case SourceKind::square: s.amplitude = 0.2; s.frequency = 2.0; break;
        case SourceKind::chirp: s.amplitude = 0.2; s.frequency = 1.0; s.frequency2 = 100.0; s.sweep = 4.0; break;
        case SourceKind::white: s.amplitude = 0.005; s.seed = 3; break;
        case SourceKind::pink: s.amplitude = 0.02; s.seed = 4; break;
        case SourceKind::hum: s.amplitude = 0.1; s.mains = 60; s.harmonics = 0.3; break;
        case SourceKind::wander: s.amplitude = 0.3; s.frequency = 0.25; s.seed = 5; break;
        case SourceKind::emg: s.amplitude = 0.05; s.burstRate = 0.5; s.burstLength = 0.4; s.seed = 6; break;
    }
    return s;
}

// ================================================================ Source
void Source::configure (const SourceSpec& spec)
{
    const bool fresh = ! started || spec.kind != s.kind || spec.seed != s.seed;
    s = spec;
    s.bpm = std::clamp (s.bpm, 10.0, 300.0);
    s.hrv = std::clamp (s.hrv, 0.0, 0.3);
    s.frequency = std::clamp (s.frequency, 0.0, 0.5 * sampleRate);
    s.frequency2 = std::clamp (s.frequency2, 0.0, 0.5 * sampleRate);
    s.sweep = std::max (s.sweep, 0.05);
    s.burstLength = std::max (s.burstLength, 0.01);
    s.highPass = std::clamp (s.highPass, 0.0, 20.0);
    if (fresh) restart();
}

void Source::restart()
{
    started = true;
    rng.seed ((std::uint32_t) s.seed * 2654435761u + 12345u);
    normal.reset();
    n = 0;
    beats = 0.0;
    std::fill (std::begin (pink), std::end (pink), 0.0);
    std::fill (std::begin (bp), std::end (bp), 0.0);
    wanderLow = 0.0;
    burstLeft = 0.0;
    pinkGain = 1.0;
    acIn = acOut = 0.0;
    std::uniform_real_distribution<double> u (0.0, 1.0);
    // Each seed starts its heart at its own point in the beat (so two hearts don't beat together).
    phase = isEcg (s.kind) ? -pi + 2.0 * pi * u (rng) : 0.0;
    hrvPhase = 2.0 * pi * u (rng);
    wanderPhase2 = 2.0 * pi * u (rng);
    if (isEcg (s.kind))
    {
        newBeat();
        beats = 0.0;
        acIn = ecgNow();   // the high-pass starts settled on the first sample's level
    }
    // The coloured noises: measure the filter's gain once, so `amplitude` is the RMS.
    if (s.kind == SourceKind::pink || s.kind == SourceKind::emg)
    {
        Source probe;
        probe.s = s;
        probe.s.amplitude = 1.0;
        probe.s.burstRate = 0.0;   // EMG: measured at the tonic level, then scaled to a burst's
        probe.started = true;
        probe.rng.seed (12345u);
        double sum = 0.0;
        constexpr int count = 40000;
        for (int i = 0; i < count; ++i)
        {
            const double v = probe.next();
            sum += v * v;
        }
        const double rms = std::sqrt (sum / count);
        pinkGain = rms > 0.0 ? 1.0 / rms : 1.0;
        if (s.kind == SourceKind::emg) pinkGain *= 0.15;   // the probe ran at the tonic level: a burst's peak is the RMS
    }
}

double Source::gauss() { return normal (rng); }

double Source::ecgNow() const
{
    static const double norm = 1.0 / ecgPeak();
    return s.amplitude * norm * ecgAt (phase);
}

// The next RR interval: the mean, modulated by breathing (respiratory sinus arrhythmia)
// and by the slow ~0.1 Hz Mayer wave, and a little beat-to-beat scatter.  ECGSYN draws
// its RR tachogram from a bimodal spectrum with these two peaks.
void Source::newBeat()
{
    const double t = (double) n / sampleRate, mean = 60.0 / s.bpm;
    const double modulation = 0.6 * std::sin (2.0 * pi * s.breathing * t) + 0.5 * std::sin (2.0 * pi * 0.1 * t + hrvPhase) + 0.3 * gauss();
    rr = mean * std::clamp (1.0 + s.hrv * modulation, 0.5, 1.5);
    beats += 1.0;
}

double Source::ecgAt (double theta) const
{
    const auto& waves = s.kind == SourceKind::fetalEcg ? fetalWaves : maternalWaves;
    double z = 0.0;
    for (const auto& w : waves)
    {
        double d = theta - w.theta;
        d -= 2.0 * pi * std::round (d / (2.0 * pi));   // the bumps wrap around the beat
        z += w.height * std::exp (-d * d / (2.0 * w.width * w.width));
    }
    return z;
}

double Source::next()
{
    if (! started) restart();
    const double t = (double) n / sampleRate;
    ++n;
    const double a = s.amplitude;
    switch (s.kind)
    {
        case SourceKind::none: return 0.0;
        case SourceKind::maternalEcg:
        case SourceKind::fetalEcg:
        {
            static const double norm = 1.0 / ecgPeak();
            double v = a * norm * ecgAt (phase);
            if (s.highPass > 0.0)
            {
                // One-pole high-pass: y[n] = k·(y[n-1] + v[n] - v[n-1]).
                const double k = 1.0 / (1.0 + 2.0 * pi * s.highPass / sampleRate);
                acOut = k * (acOut + v - acIn);
                acIn = v;
                v = acOut;
            }
            const double before = phase;
            phase += 2.0 * pi / (rr * sampleRate);
            if (phase >= pi) phase -= 2.0 * pi;
            if (before < 0.0 && phase >= 0.0) newBeat();   // the next RR from this R wave on
            return v;
        }
        case SourceKind::sine:
        {
            const double v = a * std::sin (2.0 * pi * phase);
            phase = std::fmod (phase + s.frequency / sampleRate, 1.0);
            return v;
        }
        case SourceKind::square:
        {
            const double v = phase < 0.5 ? a : -a;
            phase = std::fmod (phase + s.frequency / sampleRate, 1.0);
            return v;
        }
        case SourceKind::chirp:
        {
            // A linear sweep, start to end over `sweep` seconds, then again.
            const double within = std::fmod (t, s.sweep) / s.sweep;
            const double f = s.frequency + (s.frequency2 - s.frequency) * within;
            const double v = a * std::sin (2.0 * pi * phase);
            phase = std::fmod (phase + f / sampleRate, 1.0);
            return v;
        }
        case SourceKind::white: return a * gauss();
        case SourceKind::pink:
        {
            const double w = gauss();
            auto& b = pink;
            b[0] = 0.99886 * b[0] + w * 0.0555179;
            b[1] = 0.99332 * b[1] + w * 0.0750759;
            b[2] = 0.96900 * b[2] + w * 0.1538520;
            b[3] = 0.86650 * b[3] + w * 0.3104856;
            b[4] = 0.55000 * b[4] + w * 0.5329522;
            b[5] = -0.7616 * b[5] - w * 0.0168980;
            const double v = b[0] + b[1] + b[2] + b[3] + b[4] + b[5] + b[6] + w * 0.5362;
            b[6] = w * 0.115926;
            return a * pinkGain * v;
        }
        case SourceKind::hum:
        {
            const double f = s.mains == 50 ? 50.0 : 60.0;
            return a * (std::sin (2.0 * pi * f * t) + s.harmonics * std::sin (2.0 * pi * 3.0 * f * t + 0.7)) / (1.0 + s.harmonics);
        }
        case SourceKind::wander:
        {
            // Breathing, a slower sway, and a drift (white noise through a 0.05 Hz low-pass).
            const double k = 1.0 - std::exp (-2.0 * pi * 0.05 / sampleRate);
            wanderLow += k * (gauss() * 25.0 - wanderLow);
            return a * (0.6 * std::sin (2.0 * pi * s.frequency * t) + 0.25 * std::sin (2.0 * pi * 0.37 * s.frequency * t + wanderPhase2)
                        + 0.15 * wanderLow);
        }
        case SourceKind::emg:
        {
            // Band-limited noise, loud in bursts (muscle contractions) over a quiet tonic level.
            static const Biquad f = emgBand();
            const double w = gauss();
            const double y = f.b0 * w + f.b2 * bp[1] - f.a1 * bp[2] - f.a2 * bp[3];
            bp[1] = bp[0];
            bp[0] = w;
            bp[3] = bp[2];
            bp[2] = y;
            double envelope = 0.15;
            if (burstLeft > 0.0)
            {
                const double x = 1.0 - burstLeft / burstSamples;   // 0 .. 1 through the burst
                envelope += 0.85 * std::sin (pi * x);
                burstLeft -= 1.0;
            }
            else if (s.burstRate > 0.0 && std::uniform_real_distribution<double> (0.0, 1.0) (rng) < s.burstRate / sampleRate)
            {
                burstSamples = s.burstLength * sampleRate;
                burstLeft = burstSamples;
            }
            return a * pinkGain * envelope * y;
        }
    }
    return 0.0;
}
} // namespace afx
