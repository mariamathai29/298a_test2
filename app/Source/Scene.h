#pragma once

// The scene: what is wired where in the interference canceller (a fixed topology).
//
//   signal s ───────────────────────────────┐
//   interference m ──┬─ primary path Hp ─────┼─ Σ ─ d (primary) ──────────┐
//                    │    primary noise ─────┘                            Σ ─ e ≈ s
//                    └─ reference path Hr ─── Σ ─ x ─ adaptive w ─ y ─────┘ (−)
//                         reference noise ────┘ (and, if asked, a little of s)
//
// d = s + Hp·m + n_p,  x = Hr·m + n_r + c·s,  y = wᵀx_n,  e = d − y.
// The filter has to become Hp/Hr: the effective path, worked out here as taps.

#include "Adaptive.h"
#include "Signals.h"

#include <string>
#include <vector>

namespace afx
{
constexpr int maxPathTaps = 32, maxPathDelay = 32;

struct PathSpec
{
    std::vector<double> taps { 1.0 };
    double gain = 1.0;
    int delay = 0;   // samples before the taps
    std::string preset = "direct";

    std::vector<double> effective() const;   // gain·(delay zeros, then the taps)
    bool operator== (const PathSpec&) const = default;
};

struct PathPreset
{
    const char* name;
    const char* tip;
    std::vector<double> taps;
    int delay;
};
const std::vector<PathPreset>& pathPresets();
PathSpec pathFromPreset (const std::string& name);

// A whisper of white noise (2 µV RMS), as electrodes and amplifiers give.
SourceSpec quietLead();

struct Scene
{
    SourceSpec signal = SourceSpec::defaults (SourceKind::fetalEcg);
    SourceSpec interference = SourceSpec::defaults (SourceKind::maternalEcg);
    SourceSpec primaryNoise = SourceSpec::defaults (SourceKind::white);
    SourceSpec referenceNoise = quietLead();   // the chest lead's own few microvolts
    PathSpec primaryPath = pathFromPreset ("abdominal");
    PathSpec referencePath = pathFromPreset ("direct");
    double signalInReference = 0.0;   // c: how much of s reaches the reference too
    FilterSpec filter;

    bool operator== (const Scene&) const = default;
    static Scene defaults();   // fetal extraction, 8-tap sign-sign LMS, a path 8 taps can model
    static Scene chip();       // the same, on the chip's arithmetic: 4 taps, Q1.7 / Q2.12, FS 1.5 mV, mu_sel 1
};

// The effective path Hp/Hr as an impulse response (up to `length` taps), by long division.
struct TruePath
{
    std::vector<double> taps;
    int needed = 0;        // taps it takes to hold all but 1 % of it (by amplitude)
    bool causal = true;    // false: Hr is later than Hp, so part of Hp/Hr is before time 0
    bool stable = true;    // false: Hr has zeros outside the unit circle; its inverse grows
    bool exists = true;    // false: Hr is all zero
};
TruePath truePath (const PathSpec& primary, const PathSpec& reference, int length = maxTaps);
} // namespace afx
