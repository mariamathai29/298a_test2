#include "Plots.h"

#include <algorithm>
#include <cmath>

namespace afx
{
void drawTrace (ImDrawList* dl, ImVec2 a, ImVec2 b, const std::vector<float>& x, size_t span, float scale, ImU32 colour, float thickness)
{
    if (x.size() < 2 || span < 2 || scale <= 0.0f) return;
    const size_t n = std::min (x.size(), span), from = x.size() - n;
    const float w = b.x - a.x, cy = 0.5f * (a.y + b.y), half = 0.5f * (b.y - a.y) * 0.92f;
    const float perSample = w / (float) (span - 1);
    const float x0 = b.x - perSample * (float) (n - 1);   // where the oldest shown sample goes
    auto yOf = [&] (float v) { return std::clamp (cy - half * v / scale, a.y, b.y); };
    dl->PushClipRect (a, b, true);
    if (perSample >= 1.0f)
    {
        std::vector<ImVec2> pts (n);
        for (size_t i = 0; i < n; ++i) pts[i] = { x0 + perSample * (float) i, yOf (x[from + i]) };
        dl->AddPolyline (pts.data(), (int) pts.size(), colour, 0, thickness);
    }
    else
    {
        // A column per pixel: a line from its lowest to its highest sample, and the path through their middles.
        const int columns = std::max (1, (int) std::floor (b.x - x0));
        std::vector<ImVec2> mid;
        mid.reserve ((size_t) columns);
        for (int c = 0; c < columns; ++c)
        {
            const size_t i0 = from + (size_t) ((double) c / columns * (double) n);
            const size_t i1 = std::max (i0 + 1, from + (size_t) ((double) (c + 1) / columns * (double) n));
            float lo = x[i0], hi = x[i0];
            for (size_t i = i0; i < i1 && i < x.size(); ++i)
            {
                lo = std::min (lo, x[i]);
                hi = std::max (hi, x[i]);
            }
            const float px = x0 + (float) c + 0.5f;
            if (yOf (lo) - yOf (hi) > 1.0f) dl->AddLine ({ px, yOf (hi) }, { px, yOf (lo) }, colour, thickness);
            mid.push_back ({ px, yOf (0.5f * (lo + hi)) });
        }
        dl->AddPolyline (mid.data(), (int) mid.size(), colour, 0, thickness);
    }
    dl->PopClipRect();
}

float peakOf (const std::vector<float>& x, size_t last)
{
    float p = 0.0f;
    const size_t from = last >= x.size() ? 0 : x.size() - last;
    for (size_t i = from; i < x.size(); ++i) p = std::max (p, std::abs (x[i]));
    return p;
}

void follow (float& scale, float peak)
{
    const float want = std::max (peak * 1.1f, 1e-4f);
    if (want > scale) scale = want;
    else scale += 0.04f * (want - scale);
}

float niceAbove (float v)
{
    if (v <= 0.0f) return 1.0f;
    const float p = std::pow (10.0f, std::floor (std::log10 (v)));
    for (float m : { 1.0f, 2.0f, 5.0f, 10.0f })
        if (m * p >= v * 0.9999f) return m * p;
    return 10.0f * p;
}

void dashedLine (ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 colour, float thickness, float dash)
{
    const float dx = b.x - a.x, dy = b.y - a.y, len = std::sqrt (dx * dx + dy * dy);
    if (len < 1e-3f) return;
    for (float t = 0.0f; t < len; t += 2.0f * dash)
    {
        const float t1 = std::min (len, t + dash);
        dl->AddLine ({ a.x + dx * t / len, a.y + dy * t / len }, { a.x + dx * t1 / len, a.y + dy * t1 / len }, colour, thickness);
    }
}

void dashedPolyline (ImDrawList* dl, const std::vector<ImVec2>& pts, ImU32 colour, float thickness, float dash)
{
    // The dashes carry on from one segment to the next.
    float along = 0.0f;
    for (size_t i = 1; i < pts.size(); ++i)
    {
        const ImVec2 a = pts[i - 1], b = pts[i];
        const float dx = b.x - a.x, dy = b.y - a.y, len = std::sqrt (dx * dx + dy * dy);
        if (len < 1e-4f) continue;
        float t = 0.0f;
        while (t < len)
        {
            const float phase = std::fmod (along, 2.0f * dash);
            const bool on = phase < dash;
            const float step = std::max (1e-3f, std::min (len - t, (on ? dash : 2.0f * dash) - phase));
            if (on)
                dl->AddLine ({ a.x + dx * t / len, a.y + dy * t / len }, { a.x + dx * std::min (len, t + step) / len, a.y + dy * std::min (len, t + step) / len },
                             colour, thickness);
            t += step;
            along += step;
        }
    }
}

Frame plotFrame (const char* id, ImVec2 size, const std::string& title, float left, float bottom, float top)
{
    Frame f;
    f.a = ImGui::GetCursorScreenPos();
    ImGui::Dummy (size);
    (void) id;
    f.b = { f.a.x + size.x, f.a.y + size.y };
    f.ia = { f.a.x + left, f.a.y + top };
    f.ib = { f.b.x - 8.0f, f.b.y - bottom };
    auto* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled (f.a, f.b, plotBack, 4.0f);
    if (! title.empty()) drawText (dl, fonts().bold, { f.a.x + 8, f.a.y + 3 }, textLight, title);
    return f;
}

std::string dbText (double db, const char* format)
{
    if (! std::isfinite (db) || db < -250.0) return "−∞ dB";
    char buf[48];
    std::snprintf (buf, sizeof buf, format, db);
    return buf;
}
} // namespace afx
