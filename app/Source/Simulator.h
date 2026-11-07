#pragma once

// The canceller, running: a sample at a time from the sources through the paths
// and the adaptive filter, with what it did kept for the displays (the last half
// minute of every signal; the learning curve and the weights all along).
// No GUI: the app, the checks and the screenshots share it.

#include "Scene.h"

#include <algorithm>
#include <array>
#include <vector>

namespace afx
{
enum class Channel { signal, interference, primaryNoise, referenceNoise, primary, reference, output, error };
constexpr int numChannels = 8;

// The last `capacity` samples of a signal.
class Ring
{
public:
    explicit Ring (size_t capacity = 1) : data (capacity, 0.0f) {}
    void push (float v)
    {
        data[head] = v;
        head = (head + 1) % data.size();
        count = std::min (count + 1, data.size());
    }
    size_t size() const { return count; }
    size_t capacity() const { return data.size(); }
    void clear() { head = count = 0; }
    // The last n (or fewer, if there aren't n), oldest first.
    void last (size_t n, std::vector<float>& out) const;

private:
    std::vector<float> data;
    size_t head = 0, count = 0;
};

// A point on the learning curve: the smoothed errors, and the weights then.
struct CurvePoint
{
    float time;      // s
    float mseOut;    // mV²: (e − s)², what is left of the interference in e
    float mseIn;     // mV²: (d − s)², the interference before cancelling
    float mseTwin;   // mV²: (e − s)² of the float twin (chip arithmetic only; else 0)
    std::array<float, maxTaps> w;
};

class Simulator
{
public:
    static constexpr double historySeconds = 30.0;
    static constexpr double smoothing = 3.0;   // s: the time constant of the running measures (a few heartbeats)
    static constexpr size_t maxPoints = 4096;  // the curve halves its detail when it fills

    explicit Simulator (const Scene& = Scene::defaults());

    // Applied at once, while running: sources keep going (unless their kind or seed
    // changed), the paths and the filter change under it (its weights stay).
    void setScene (const Scene&);
    const Scene& scene() const { return s; }
    void restart();        // time 0: the sources from their seeds, the weights 0, the displays empty
    void resetWeights();   // the weights to 0, nothing else
    void run (long long samples);

    long long samples() const { return n; }
    double time() const { return (double) n / sampleRate; }
    const Ring& history (Channel c) const { return rings[(size_t) c]; }
    const AdaptiveFilter& filter() const { return adaptive; }
    AdaptiveFilter& filter() { return adaptive; }
    const TruePath& truth() const { return trueH; }

    // The running measures (time constant `smoothing`).
    double mseOut() const { return outAvg; }
    double mseIn() const { return inAvg; }
    double signalPower() const { return sigAvg; }
    double snrIn() const;            // dB: s against the interference in d
    double snrOut() const;           // dB: s against what is left of it in e
    double improvement() const { return snrOut() - snrIn(); }

    // With the chip's arithmetic, a twin runs beside it: the same 4-tap sign-sign
    // LMS and μ in floating point on the unquantized x and d, from the same start.
    // What separates them is what the chip's word lengths cost.
    bool hasTwin() const { return s.filter.isChip(); }
    const AdaptiveFilter& twin() const { return twinFilter; }
    double mseTwin() const { return twinAvg; }
    double improvementTwin() const;
    // The input codes that hit the rails (u or r at -128 or 127) since the start.
    long long clipped() const { return clips; }

    const std::vector<CurvePoint>& curve() const { return points; }

    // The Wiener-optimal filter, as many taps as the adaptive one, from x to the
    // interference in d (d − s, known here) over the last `seconds`; and the MSE it leaves.
    void solveWiener (double seconds = 10.0);
    const std::vector<double>& wienerTaps() const { return wienerW; }
    double wienerMse() const { return wienerE; }
    double wienerTime() const { return wienerAt; }

private:
    void record (double sig, double err, double inErr, double twinErr);

    Scene s;
    Source sources[4];   // signal, interference, primary noise, reference noise
    AdaptiveFilter adaptive, twinFilter;
    static FilterSpec twinSpec (const FilterSpec&);
    std::vector<double> hp, hr;   // the paths' taps
    std::vector<double> line;     // the interference lately, newest first (for the paths)
    TruePath trueH;
    std::array<Ring, numChannels> rings;
    long long n = 0;
    double outAvg = 0, inAvg = 0, sigAvg = 0, twinAvg = 0;
    long long clips = 0;
    std::vector<CurvePoint> points;
    long long pointEvery = 10, pointCount = 0;
    double pointOut = 0, pointIn = 0;
    bool wasChip = false;
    std::vector<double> wienerW;
    double wienerE = 0.0, wienerAt = -1.0;
};
} // namespace afx
