#pragma once

// Drawing for the explorer's displays: a colour for each signal, traces against
// time, dashed lines, and plot frames with gridlines.

#include "Views.h"

#include <string>
#include <vector>

namespace afx
{
// ---- a colour for each signal, the same everywhere ----
constexpr ImU32 colourSignal = argb (0xff5fc98f);       // s: the signal of interest
constexpr ImU32 colourInterference = argb (0xffe86d5a); // m
constexpr ImU32 colourNoise = argb (0xffb0b8c0);        // n_p, n_r
constexpr ImU32 colourPrimary = argb (0xffe07aa8);      // d
constexpr ImU32 colourReference = argb (0xff6bb5ff);    // x
constexpr ImU32 colourOutput = argb (0xffa98ae8);       // y
constexpr ImU32 colourError = accent;                   // e
constexpr ImU32 colourWiener = argb (0xff5ad8c8);       // the Wiener solution
constexpr ImU32 colourTruth = argb (0xffe6e9ec);        // the true path
constexpr ImU32 plotBack = argb (0xff0b0b12);
constexpr ImU32 gridLine = IM_COL32 (255, 255, 255, 22);

// The last `span` samples of x, newest at the right edge (fewer: they end there too),
// ±scale filling the height.  Min/max per pixel when there are more samples than pixels.
void drawTrace (ImDrawList*, ImVec2 a, ImVec2 b, const std::vector<float>& x, size_t span, float scale, ImU32 colour, float thickness = 1.0f);
float peakOf (const std::vector<float>&, size_t last = (size_t) -1);
// A scale that follows the peak up at once and down slowly (so traces don't jump about).
void follow (float& scale, float peak);
// A "nice" range end: 1, 2 or 5 times a power of ten, at least v.
float niceAbove (float v);

void dashedLine (ImDrawList*, ImVec2 a, ImVec2 b, ImU32 colour, float thickness = 1.0f, float dash = 5.0f);
void dashedPolyline (ImDrawList*, const std::vector<ImVec2>&, ImU32 colour, float thickness = 1.0f, float dash = 5.0f);

// A plot area: the background and a title; returns the inner rectangle (for the data).
struct Frame
{
    ImVec2 a, b;     // the whole
    ImVec2 ia, ib;   // inside the margins
};
Frame plotFrame (const char* id, ImVec2 size, const std::string& title, float left = 40.0f, float bottom = 16.0f, float top = 20.0f);

// "12.3 dB", "−∞ dB" and the like.
std::string dbText (double db, const char* format = "%+.1f dB");
} // namespace afx
