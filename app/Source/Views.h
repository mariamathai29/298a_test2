#pragma once

// Drawing helpers for the interface (Reel's): colours, fonts, scopes and small
// widgets.

#include <imgui.h>

#include <array>
#include <functional>
#include <string>
#include <vector>

namespace afx
{
// ---- colours (0xAARRGGBB, as in VSyn) and fonts ---------------------------------
constexpr ImU32 argb (unsigned c)
{
    return IM_COL32 ((c >> 16) & 0xff, (c >> 8) & 0xff, c & 0xff, (c >> 24) & 0xff);
}
ImU32 withAlpha (ImU32, float alpha);
ImU32 brighter (ImU32, float amount);   // towards white
ImU32 darker (ImU32, float amount);     // towards black
ImU32 hsv (float h, float s, float v, float a = 1.0f);

constexpr ImU32 accent = argb (0xffffb454);
constexpr ImU32 panelBack = argb (0xff15181d);
constexpr ImU32 windowBack = argb (0xff0e1013);
constexpr ImU32 textGrey = argb (0xff8a949e);
constexpr ImU32 textLight = argb (0xffd3d3d3);

struct Fonts
{
    ImFont* ui = nullptr;      // 15 px
    ImFont* small = nullptr;   // 12 px
    ImFont* tiny = nullptr;    // 10 px
    ImFont* bold = nullptr;    // 14 px bold
    ImFont* title = nullptr;   // 17 px bold
    ImFont* mono = nullptr;    // 13 px monospaced
};
Fonts& fonts();
// Into the current ImGui context (before the first frame).  rasterScale: glyphs drawn
// that many times larger, and the fonts scaled back by as much (the layout is the
// same), for drawing the interface magnified and still sharp (the promo's close-ups).
void loadFonts (float rasterScale = 1.0f);
void applyStyle();

// Text at a point with a font, clipped to nothing in particular.
void drawText (ImDrawList*, ImFont*, ImVec2, ImU32, const std::string&);
ImVec2 textSize (ImFont*, const std::string&);
// Text fitted into a rectangle: h -1 left, 0 centred, 1 right; vertically centred.
void drawTextIn (ImDrawList*, ImFont*, ImVec2 min, ImVec2 max, ImU32, const std::string&, int h = -1);

// ---- signals --------------------------------------------------------------------
// The last `span` samples of x into the rectangle (min/max per pixel), from an
// upward zero crossing if `trigger` (so a steady tone stands still).
void drawSignal (ImDrawList*, ImVec2 a, ImVec2 b, const std::vector<float>& x, int span, ImU32 colour, bool trigger);
// A signal over the last moments, with a title.
void scope (const char* id, ImVec2 size, const std::vector<float>& values, ImU32 colour, const std::string& title, int span = 1024);
// ---- small widgets ----------------------------------------------------------------
// Small boxed help text, wrapped.
void helpText (const std::string&);
// A section header with a rule under it.
void header (const char*);
// Segmented buttons: returns true if the choice changed.
bool segmented (const char* id, int& value, const std::vector<std::string>& labels, ImU32 on, float width = -1.0f,
                const std::vector<const char*>& tips = {});
void tooltip (const char*);   // on the last item, when hovered a moment

// Where named items were drawn last frame, for scripted mouse (the tools and tests).
void markItem (const std::string& name);   // the last item's centre
void markPoint (const std::string& name, ImVec2);
const ImVec2* markedItem (const std::string& name);
// The right edge of the content region, in window coordinates (for SameLine).
inline float contentRight() { return ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x; }
} // namespace afx
