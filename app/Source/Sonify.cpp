#include "Sonify.h"

#include <algorithm>
#include <cmath>

namespace afx
{
ThumpVoice adultHeart()
{
    ThumpVoice v;
    v.pitch = 52.0;
    v.decay = 0.13;
    v.click = 0.12;
    v.dub = 0.5;
    v.dubAfter = 0.3;
    return v;
}

ThumpVoice fetalHeart()
{
    ThumpVoice v;
    v.pitch = 150.0;
    v.decay = 0.045;
    v.click = 0.3;
    v.dub = 0.0;
    v.level = 2.2;   // its R waves are a fifth of the mother's: it would be lost otherwise
    return v;
}

ThumpVoice heartFor (const SourceSpec& s) { return s.bpm >= 100.0 ? fetalHeart() : adultHeart(); }

// ================================================================ finding the beats
namespace
{
// RBJ's cookbook: second-order high- and low-pass sections at the simulation's rate.
template <typename B>
B section (double f, bool high)
{
    const double w = 2.0 * pi * f / sampleRate, c = std::cos (w), al = std::sin (w) / (2.0 * 0.7071);
    const double a0 = 1.0 + al;
    B b;
    if (high)
    {
        b.b0 = (1.0 + c) / 2.0 / a0;
        b.b1 = -(1.0 + c) / a0;
        b.b2 = b.b0;
    }
    else
    {
        b.b0 = (1.0 - c) / 2.0 / a0;
        b.b1 = (1.0 - c) / a0;
        b.b2 = b.b0;
    }
    b.a1 = -2.0 * c / a0;
    b.a2 = (1.0 - al) / a0;
    return b;
}

constexpr double energyFloor = 2e-5;                // mV²: well above the default leads' noise in the band
constexpr long long refractory = (long long) (0.2 * sampleRate);
constexpr long long longestBeat = (long long) (0.04 * sampleRate);
constexpr size_t heightLookBack = 24;               // samples: the band-pass is late by about this much
} // namespace

void BeatDetector::init()
{
    hp1 = section<Biquad> (8.0, true);
    hp2 = section<Biquad> (8.0, true);
    lp1 = section<Biquad> (30.0, false);
    lp2 = section<Biquad> (30.0, false);
    base = section<Biquad> (3.0, true);
    window.assign ((size_t) (0.03 * sampleRate), 0.0);
    ready = true;
}

bool BeatDetector::push (double v)
{
    if (! ready) init();
    const long long i = n++;
    const double bp = lp2.run (lp1.run (hp2.run (hp1.run (v))));
    const double h = base.run (v);
    sum += bp * bp - window[at];
    window[at] = bp * bp;
    at = (at + 1) % window.size();
    const double energy = std::max (0.0, sum / (double) window.size());
    // the recent beats' level fades with a time constant of 1.5 s
    level *= std::exp (-1.0 / (1.5 * sampleRate));
    const double threshold = std::max (0.3 * level, energyFloor);

    // the heights lately, to find the R wave in (the band-pass is behind it)
    heights[(size_t) (i % heightsSize)] = std::abs (h);
    if (! inBeat)
    {
        if (energy > threshold && i - lastBeat > refractory && i > (long long) (0.3 * sampleRate))
        {
            inBeat = true;
            maxEnergy = energy;
            candidateAt = i;
        }
        return false;
    }
    maxEnergy = std::max (maxEnergy, energy);
    if (energy > 0.5 * maxEnergy && i - candidateAt < longestBeat) return false;
    // The beat: its R wave is the biggest swing from a little before it crossed to now.
    inBeat = false;
    double maxHeight = 0.0;
    peakAt = candidateAt;
    for (long long j = std::max (0LL, candidateAt - (long long) heightLookBack); j <= i; ++j)
        if (heights[(size_t) (j % heightsSize)] > maxHeight)
        {
            maxHeight = heights[(size_t) (j % heightsSize)];
            peakAt = j;
        }
    peakHeight = maxHeight;
    lastBeat = peakAt;
    level = level <= 0.0 ? maxEnergy : 0.7 * level + 0.3 * maxEnergy;
    return true;
}

// ================================================================ the sound
HeartSonifier::HeartSonifier (double audioRate)
    : rate (audioRate), perSample (std::max (1, (int) std::lround (audioRate / sampleRate)))
{
    future.assign ((size_t) (3.0 * rate) * 2, 0.0f);
    delayLine.assign ((size_t) std::lround (latency * sampleRate) + 1, 0.0);
}

void HeartSonifier::setVoices (const ThumpVoice& l, const ThumpVoice& f)
{
    lockedVoice = l;
    freeVoice = f;
}

void HeartSonifier::tone (long long atFrame, const ThumpVoice& v, double pitchScale, double g)
{
    const size_t frames = future.size() / 2;
    const long long len = (long long) (rate * (6.0 * v.decay + 0.02));
    const double panL = std::cos ((v.pan + 1.0) * pi / 4.0) * std::sqrt (2.0), panR = std::sin ((v.pan + 1.0) * pi / 4.0) * std::sqrt (2.0);
    const double drive = 1.4, norm = 1.0 / std::tanh (drive);
    double phase = 0.0, clickLp = 0.0;
    const double clickK = 1.0 - std::exp (-2.0 * pi * 1800.0 / rate);
    const long long now = futureAt;   // frames before this have gone out
    for (long long k = 0; k < len; ++k)
    {
        const double t = (double) k / rate;
        const double f = v.pitch * pitchScale * (1.0 + 1.5 * std::exp (-t / 0.015));
        phase += 2.0 * pi * f / rate;
        const double env = (1.0 - std::exp (-t / 0.0015)) * std::exp (-t / v.decay);
        noise = noise * 1664525u + 1013904223u;
        const double white = (double) (noise >> 8) / 8388608.0 - 1.0;
        clickLp += clickK * (white - clickLp);
        const double click = v.click * 2.0 * clickLp * std::exp (-t / 0.003);
        const double body = (std::sin (phase) + 0.3 * std::sin (2.0 * phase) + 0.12 * std::sin (3.0 * phase)) * env;
        const double s = g * norm * std::tanh (drive * (0.7 * body + click));
        const long long frame = atFrame + k;
        if (frame < now) continue;
        if (frame - now >= (long long) frames) break;
        const size_t idx = (size_t) (frame % (long long) frames) * 2;
        future[idx] += (float) (s * panL);
        future[idx + 1] += (float) (s * panR);
    }
}

void HeartSonifier::thump (long long atSample, const ThumpVoice& v, double g)
{
    const long long frame = (atSample + (long long) std::lround (latency * sampleRate)) * perSample;
    tone (frame, v, 1.0, g);
    if (v.dub > 0.0) tone (frame + (long long) (v.dubAfter * rate), v, 1.3, g * v.dub);
}

void HeartSonifier::classify (const Pending& p)
{
    const long long window = (long long) (0.06 * sampleRate);
    bool nearReference = false;
    if (referenceHeart)
        for (const long long t : referenceTimes)
            if (std::llabs (t - p.at) <= window) nearReference = true;
    const bool fitsFree = freeRr > 0.0 && freeRun >= 3
                          && std::abs ((double) (p.at - lastFree) - freeRr * sampleRate) <= 0.06 * sampleRate;
    // In step with the reference but no taller than the signal's own beats lately: the signal's heart
    // (what is left of the interference that small is not worth hearing as a heart).
    const bool likeFree = freeHeight > 0.0 && p.at - lastFree < (long long) (2.0 * sampleRate) && p.height < 1.5 * freeHeight;
    // Loudness follows the R wave's height, a little compressed (1 mV: full).
    auto loudness = [&] (const ThumpVoice& v) { return gain * v.level * std::min (1.5, std::pow (std::max (0.0, p.height), 0.8)); };
    // Small, in step with the reference and out of the baby's rhythm (once that is known): what is left
    // of the mother's beat, too little to be a heart: not heard.
    if (nearReference && ! fitsFree && likeFree && freeRun >= 3) return;
    if (nearReference && ! fitsFree && ! likeFree)
    {
        ++lockedCount;
        lockedGain = loudness (lockedVoice);
        if (! lockedMuted) thump (p.at, lockedVoice, lockedGain);
        return;
    }
    ++freeCount;
    freeGain = loudness (freeVoice);
    freeHeight = freeHeight <= 0.0 ? p.height : 0.8 * freeHeight + 0.2 * p.height;
    thump (p.at, freeVoice, freeGain);
    const double interval = (double) (p.at - lastFree) / sampleRate;
    if (interval > 0.25 && interval < 1.6)
    {
        if (freeRr > 0.0 && std::abs (interval - freeRr) < 0.2 * freeRr)
        {
            freeRr = 0.8 * freeRr + 0.2 * interval;
            ++freeRun;
        }
        else
        {
            freeRr = interval;
            freeRun = 0;
        }
    }
    lastFree = p.at;
}

void HeartSonifier::resync()
{
    listenedBeats = BeatDetector();
    referenceBeats = BeatDetector();
    pending.clear();
    referenceTimes.clear();
    freeRun = 0;
    freeRr = 0.0;
    texFade = 0.0;
}

void HeartSonifier::push (double listened, double reference, std::vector<float>& out)
{
    const long long i = n++;
    // (the detectors count from their own start: since the last resync)
    if (listenedBeats.push (listened)) pending.push_back ({ i - (listenedBeats.samples() - 1) + listenedBeats.beatSample(), listenedBeats.amplitude() });
    if (referenceBeats.push (reference)) referenceTimes.push_back (i - (referenceBeats.samples() - 1) + referenceBeats.beatSample());
    std::erase_if (referenceTimes, [&] (long long t) { return i - t > (long long) (2.0 * sampleRate); });
    // Decide whose each beat is once the reference has had time to show its own (60 ms).
    const long long wait = (long long) (0.06 * sampleRate);
    for (const auto& p : pending)
        if (i - p.at >= wait) classify (p);
    std::erase_if (pending, [&] (const Pending& p) { return i - p.at >= wait; });

    // The texture: the signal itself, as late as the thumps, above 20 Hz.
    const size_t dl = delayLine.size();
    delayLine[(size_t) (i % (long long) dl)] = listened;
    const double late = delayLine[(size_t) ((i + 1) % (long long) dl)];
    const double hpK = std::exp (-2.0 * pi * 20.0 / rate), lpK = 1.0 - std::exp (-2.0 * pi * 300.0 / rate);
    const size_t frames = future.size() / 2;
    for (int j = 0; j < perSample; ++j)
    {
        const double x = texPrev + (late - texPrev) * (double) (j + 1) / (double) perSample;
        texHp = hpK * (texHp + x - texIn);
        texIn = x;
        texLp1 += lpK * (texHp - texLp1);
        texLp2 += lpK * (texLp1 - texLp2);
        texFade = std::min (1.0, texFade + 1.0 / (0.15 * rate));
        const double t = gain * texture * texLp2 * texFade * texFade;
        const size_t idx = (size_t) (futureAt % (long long) frames) * 2;
        out.push_back (future[idx] + (float) t);
        out.push_back (future[idx + 1] + (float) t);
        future[idx] = future[idx + 1] = 0.0f;
        ++futureAt;
    }
    texPrev = late;
}

// ================================================================ following a simulator
void Listener::voicesFrom (const Scene& s)
{
    const bool twoHearts = isEcg (s.interference.kind);
    ThumpVoice signalHeart = heartFor (s.signal);
    if (! isEcg (s.signal.kind)) signalHeart.level = 0.0;
    sonify.setReferenceIsHeart (twoHearts);
    sonify.setVoices (twoHearts ? heartFor (s.interference) : adultHeart(), signalHeart);
}

void Listener::follow (const Simulator& sim, double seconds, std::vector<float>& out)
{
    const long long now = sim.samples();
    const double expected = seconds * sampleRate;
    const long long advanced = lastSeen < 0 || now < lastSeen ? std::min (now, (long long) std::lround (expected)) : now - lastSeen;
    lastSeen = now;
    if ((double) advanced <= 1.5 * expected + 2.0)
    {
        if (! live) sonify.resync();   // from the copy back to the simulator itself: another stretch of signal
        live = true;
        shadowReady = false;
        sim.history (channel).last ((size_t) advanced, a);
        sim.history (Channel::reference).last ((size_t) advanced, b);
        for (size_t i = 0; i < a.size() && i < b.size(); ++i) sonify.push (a[i], b[i], out);
        return;
    }
    // Faster than real time: a copy at real time, with the weights it has now.
    live = false;
    if (! shadowReady)
    {
        shadow = sim;
        shadowReady = true;
        owed = 0.0;
    }
    Scene sc = sim.scene();
    sc.filter.freeze = true;
    if (! (sc == shadow.scene())) shadow.setScene (sc);
    shadow.filter().setWeights (sim.filter().weights());
    owed += expected;
    const long long k = (long long) owed;
    owed -= (double) k;
    shadow.run (k);
    shadow.history (channel).last ((size_t) k, a);
    shadow.history (Channel::reference).last ((size_t) k, b);
    for (size_t i = 0; i < a.size() && i < b.size(); ++i) sonify.push (a[i], b[i], out);
}
} // namespace afx
