#pragma once

// The signal sources, all synthetic: two ECGs (maternal and fetal, ECGSYN-style
// Gaussian waves on a beat phase, with heart-rate variability) and the generic
// ones (tones, a chirp, noises, power-line hum, baseline wander, EMG-like bursts).
// Any of them can be the signal of interest, the interference or a noise.
// Units are millivolts; the sample rate is fixed (see sampleRate).

#include <cstdint>
#include <random>
#include <string>

namespace afx
{
constexpr double sampleRate = 500.0;   // Hz: a usual rate for diagnostic ECG
constexpr double pi = 3.141592653589793;

enum class SourceKind { none, maternalEcg, fetalEcg, sine, square, chirp, white, pink, hum, wander, emg };
constexpr int numSourceKinds = 11;
const char* sourceKindName (SourceKind);   // for people ("maternal ECG")
const char* sourceKindId (SourceKind);     // for files ("maternalEcg")
bool isEcg (SourceKind k);

struct SourceSpec
{
    SourceKind kind = SourceKind::none;
    double amplitude = 1.0;     // mV: the R wave or the peak for waves; the RMS for noises
    // ECG
    double bpm = 75.0;          // mean heart rate
    double hrv = 0.04;          // heart-rate variability: depth of the RR modulation (0.04 = 4 %)
    double breathing = 0.25;    // Hz: the respiratory sinus arrhythmia in the HRV
    double highPass = 0.5;      // Hz: AC coupling, as an ECG amplifier records (0: DC coupled)
    // tones and wander
    double frequency = 10.0;    // Hz: sine, square, chirp start, wander
    double frequency2 = 100.0;  // Hz: chirp end
    double sweep = 4.0;         // s: one chirp sweep
    // hum
    int mains = 60;             // Hz: 50 or 60
    double harmonics = 0.3;     // the 3rd harmonic, relative to the fundamental
    // EMG
    double burstRate = 0.5;     // bursts per second
    double burstLength = 0.4;   // s
    int seed = 1;

    bool operator== (const SourceSpec&) const = default;
    static SourceSpec defaults (SourceKind);   // sensible settings for each kind
};

// One source, running: a sample at a time.  Reconfiguring keeps the phase and the
// noise going, unless the kind or the seed changed.
class Source
{
public:
    void configure (const SourceSpec&);
    void restart();   // back to time 0, from the seed
    double next();
    const SourceSpec& spec() const { return s; }
    double beatsSoFar() const { return beats; }   // ECG: R waves begun (for the checks)

private:
    double gauss();
    void newBeat();
    double ecgAt (double theta) const;
    double ecgNow() const;   // the ECG now, before the high-pass

    SourceSpec s;
    bool started = false;
    std::mt19937 rng { 1 };
    std::normal_distribution<double> normal { 0.0, 1.0 };
    long long n = 0;               // samples since the start
    double phase = 0.0;            // tones: cycles; ECG: the beat phase in radians (-π .. π, R at 0)
    double rr = 0.8;               // ECG: this beat's RR interval (s)
    double beats = 0.0;
    double hrvPhase = 0.0;         // ECG: the slow (Mayer wave) part of the HRV starts here
    double pink[7] {};             // Paul Kellet's pink filter
    double pinkGain = 1.0;
    double wanderLow = 0.0, wanderPhase2 = 0.0;
    double bp[4] {};               // EMG: the band-pass filter's state
    double burstLeft = 0.0;        // EMG: samples left in the burst
    double burstSamples = 1.0;
    double acIn = 0.0, acOut = 0.0;   // ECG: the high-pass filter's state
};
} // namespace afx
