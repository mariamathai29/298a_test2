#pragma once

// Listening to the canceller.  Heartbeats become thumps: the R waves are found in
// the signal listened to as a heart monitor finds them (a QRS band-pass, its
// energy, and a threshold that follows the recent beats), and each beat found is
// played: a beat in step with the reference's (the chest lead's) heartbeat is the
// interference's heart, the rest are the signal's.  The mother's is low and slow
// (lub-dub), the baby's high and quick; loudness follows the R wave's height, so
// as the filter learns the mother's thump fades and the baby's comes through.
// A little of the signal itself can be mixed in as texture (hum is heard as hum).
// No GUI: the promo renders it offline; an app could play it live.

#include "Simulator.h"

#include <vector>

namespace afx
{
// A thump: a short pitched-down sine with a click, and (for an adult heart) a softer second sound.
struct ThumpVoice
{
    double pitch = 55.0;   // Hz, where the thump settles
    double decay = 0.12;   // s
    double click = 0.15;   // the transient's level
    double dub = 0.0;      // the second sound's level (0: none)
    double dubAfter = 0.3; // s after the first
    double level = 1.0;
    double pan = 0.0;      // -1 left .. 1 right
};
ThumpVoice adultHeart();   // low, slow, lub-dub
ThumpVoice fetalHeart();   // high, quick, single
ThumpVoice heartFor (const SourceSpec&);   // by its heart rate (an ECG source)

// R waves in a signal at sampleRate: band-passed to the QRS band (8 to 30 Hz),
// squared and averaged over 30 ms; a beat when that peaks above 30 % of the
// recent beats' level (which decays, so quieter beats are found once loud ones go).
class BeatDetector
{
public:
    // One sample; true when a beat has just been found (a few samples after its R wave).
    bool push (double v);
    long long beatSample() const { return peakAt; }   // where its R wave was (samples pushed so far count from 0)
    double amplitude() const { return peakHeight; }   // mV: the R wave's height (above the 3 Hz high-passed baseline)
    long long samples() const { return n; }

private:
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        double run (double x)
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };
    void init();

    bool ready = false;
    Biquad hp1, hp2, lp1, lp2, base;
    static constexpr size_t heightsSize = 64;
    double heights[heightsSize] {};   // |the R wave's swing| lately
    std::vector<double> window;   // the energy over the last 30 ms
    size_t at = 0;
    double sum = 0.0;
    double level = 0.0;           // the recent beats' peak energy
    bool inBeat = false;
    double maxEnergy = 0.0, peakHeight = 0.0;
    long long n = 0, peakAt = -1, candidateAt = 0, lastBeat = -1000000;
};

class HeartSonifier
{
public:
    explicit HeartSonifier (double audioRate = 48000.0);

    void setVoices (const ThumpVoice& locked, const ThumpVoice& free);
    void setReferenceIsHeart (bool r) { referenceHeart = r; }   // false: every beat is the signal's
    void setTexture (double gainPerMv) { texture = gainPerMv; } // the listened signal itself (above 20 Hz), at this gain
    void setGain (double g) { gain = g; }
    void silenceLocked (bool s) { lockedMuted = s; }

    // One sample of the simulation: the signal listened to and the reference.
    // Appends audioRate / sampleRate stereo frames (interleaved L, R) to `out`,
    // `latency` seconds behind the input (so a thump starts on its R wave there).
    void push (double listened, double reference, std::vector<float>& out);
    static constexpr double latency = 0.1;
    // The signal jumped (another stretch of it from here on): the beat finders start
    // afresh, and the texture fades back in, so the jump is neither a beat nor a click.
    void resync();

    int lockedBeats() const { return lockedCount; }
    int freeBeats() const { return freeCount; }
    double lastLockedGain() const { return lockedGain; }
    double lastFreeGain() const { return freeGain; }

private:
    struct Pending
    {
        long long at;   // sample of the R wave
        double height;
    };
    void classify (const Pending&);
    void thump (long long atSample, const ThumpVoice&, double g);
    void tone (long long atFrame, const ThumpVoice&, double pitchScale, double g);

    double rate;
    int perSample;              // audio frames per simulation sample
    BeatDetector listenedBeats, referenceBeats;
    ThumpVoice lockedVoice = adultHeart(), freeVoice = fetalHeart();
    bool referenceHeart = true, lockedMuted = false;
    double texture = 0.0, gain = 1.0;
    std::vector<Pending> pending;
    std::vector<long long> referenceTimes;   // recent reference R waves
    long long lastFree = -1000000;
    double freeRr = 0.0;        // s: the signal's heart's RR lately (0: not yet known)
    double freeHeight = 0.0;    // mV: its R waves' height lately (0: not yet known)
    int freeRun = 0;
    // the output: a little of the future, thumps added into it
    std::vector<float> future;  // stereo frames, from `futureAt`
    long long futureAt = 0;     // audio frame of future[0]
    long long n = 0;            // simulation samples pushed
    std::vector<double> delayLine;
    double texPrev = 0.0, texHp = 0.0, texLp1 = 0.0, texLp2 = 0.0, texIn = 0.0;
    double texFade = 1.0;
    unsigned noise = 12345u;
    int lockedCount = 0, freeCount = 0;
    double lockedGain = 0.0, freeGain = 0.0;
};

// Following a Simulator by ear.  When it runs at real time its own new samples
// are heard; when it runs faster, a copy of it runs at real time with the weights
// it has now (frozen), so the heartbeats keep their natural pace while it learns.
class Listener
{
public:
    explicit Listener (double audioRate = 48000.0) : sonify (audioRate) {}
    HeartSonifier& sonifier() { return sonify; }
    void listenTo (Channel c) { channel = c; }
    // Configures the voices from a scene (who is a heart, and what kind).
    void voicesFrom (const Scene&);
    // After the simulator has advanced by a frame of `seconds`: the audio for it.
    void follow (const Simulator&, double seconds, std::vector<float>& out);
    bool realTime() const { return live; }

private:
    HeartSonifier sonify;
    Channel channel = Channel::error;
    Simulator shadow;
    bool shadowReady = false, live = true;
    long long lastSeen = -1;
    double owed = 0.0;
    std::vector<float> a, b;
};
} // namespace afx
