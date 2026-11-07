#include "Views.h"

#include "Fonts.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <map>

namespace afx
{
namespace
{
ImVec4 toVec (ImU32 c) { return ImGui::ColorConvertU32ToFloat4 (c); }
ImU32 fromVec (ImVec4 v) { return ImGui::ColorConvertFloat4ToU32 (v); }
} // namespace

ImU32 withAlpha (ImU32 c, float alpha)
{
    auto v = toVec (c);
    v.w = std::clamp (alpha, 0.0f, 1.0f);
    return fromVec (v);
}

ImU32 brighter (ImU32 c, float amount)
{
    auto v = toVec (c);
    const float k = std::clamp (amount, 0.0f, 1.0f) * 0.5f;
    v.x += (1.0f - v.x) * k;
    v.y += (1.0f - v.y) * k;
    v.z += (1.0f - v.z) * k;
    return fromVec (v);
}

ImU32 darker (ImU32 c, float amount)
{
    auto v = toVec (c);
    const float k = 1.0f / (1.0f + std::max (0.0f, amount));
    v.x *= k;
    v.y *= k;
    v.z *= k;
    return fromVec (v);
}

ImU32 hsv (float h, float s, float v, float a)
{
    float r, g, b;
    ImGui::ColorConvertHSVtoRGB (h - std::floor (h), s, v, r, g, b);
    return fromVec ({ r, g, b, a });
}

// ================================================================ fonts and style
Fonts& fonts()
{
    static Fonts f;
    return f;
}

void loadFonts (float rasterScale)
{
    auto& io = ImGui::GetIO();
    // Latin-1, Greek (σ, μ, λ, Σ ...), modifier letters (ᵀ), punctuation (– … ·), sub- and superscripts (xₙ),
    // arrows and maths (→ ≈ − √) and shapes (▶ ■).
    static const ImWchar ranges[] = { 0x0020, 0x00ff, 0x0370, 0x03ff, 0x1d00, 0x1d7f, 0x2010, 0x2044, 0x2070, 0x209f,
                                      0x2190, 0x21ff, 0x2200, 0x22ff, 0x25a0, 0x25ff, 0 };
    auto add = [&] (Face face, float size) {
        ImFontConfig cfg;
        cfg.FontDataOwnedByAtlas = false;   // the data is compiled in
        cfg.OversampleH = 2;
        const auto& e = font (face);
        ImFont* f = io.Fonts->AddFontFromMemoryTTF (const_cast<unsigned char*> (e.data), (int) e.size, size * rasterScale, &cfg, ranges);
        f->Scale = 1.0f / rasterScale;
        return f;
    };
    auto& f = fonts();
    f.ui = add (Face::sans, 15.0f);   // the first is the default
    f.small = add (Face::sans, 12.5f);
    f.tiny = add (Face::sans, 10.5f);
    f.bold = add (Face::sansBold, 14.0f);
    f.title = add (Face::sansBold, 17.0f);
    f.mono = add (Face::mono, 13.0f);
}

void applyStyle()
{
    auto& s = ImGui::GetStyle();
    ImGui::StyleColorsDark (&s);
    s.WindowRounding = 0.0f;
    s.ChildRounding = 4.0f;
    s.FrameRounding = 4.0f;
    s.PopupRounding = 4.0f;
    s.GrabRounding = 3.0f;
    s.TabRounding = 4.0f;
    s.WindowPadding = { 8, 6 };
    s.FramePadding = { 7, 4 };
    s.ItemSpacing = { 6, 5 };
    s.ScrollbarSize = 12.0f;
    s.HoverDelayNormal = 0.5f;
    s.HoverStationaryDelay = 0.4f;
    auto* c = s.Colors;
    c[ImGuiCol_WindowBg] = toVec (windowBack);
    c[ImGuiCol_ChildBg] = toVec (panelBack);
    c[ImGuiCol_PopupBg] = toVec (argb (0xf01c2128));
    c[ImGuiCol_Border] = toVec (argb (0xff2a3038));
    c[ImGuiCol_FrameBg] = toVec (argb (0xff232a31));
    c[ImGuiCol_FrameBgHovered] = toVec (argb (0xff2d3640));
    c[ImGuiCol_FrameBgActive] = toVec (argb (0xff36414c));
    c[ImGuiCol_Button] = toVec (argb (0xff2a3038));
    c[ImGuiCol_ButtonHovered] = toVec (argb (0xff3a434e));
    c[ImGuiCol_ButtonActive] = toVec (argb (0xff4a5560));
    c[ImGuiCol_Header] = toVec (argb (0xff2f3742));
    c[ImGuiCol_HeaderHovered] = toVec (argb (0xff3a4552));
    c[ImGuiCol_HeaderActive] = toVec (argb (0xff46525f));
    c[ImGuiCol_Tab] = toVec (argb (0xff1f242b));
    c[ImGuiCol_TabHovered] = toVec (argb (0xff4a3a24));
    c[ImGuiCol_TabSelected] = toVec (argb (0xff5c4626));
    c[ImGuiCol_CheckMark] = toVec (accent);
    c[ImGuiCol_SliderGrab] = toVec (accent);
    c[ImGuiCol_TextSelectedBg] = toVec (withAlpha (accent, 0.35f));
    c[ImGuiCol_Text] = toVec (argb (0xffe6e9ec));
    c[ImGuiCol_TextDisabled] = toVec (argb (0xff6a737c));
    c[ImGuiCol_ScrollbarBg] = toVec (argb (0xff111418));
}

void drawText (ImDrawList* dl, ImFont* f, ImVec2 p, ImU32 c, const std::string& s)
{
    dl->AddText (f, f->FontSize * f->Scale, p, ImGui::GetColorU32 (c), s.c_str());
}

ImVec2 textSize (ImFont* f, const std::string& s) { return f->CalcTextSizeA (f->FontSize * f->Scale, 1e9f, 0.0f, s.c_str()); }

void drawTextIn (ImDrawList* dl, ImFont* f, ImVec2 a, ImVec2 b, ImU32 c, const std::string& s, int h)
{
    std::string t = s;
    const float w = b.x - a.x;
    auto size = textSize (f, t);
    if (size.x > w && ! t.empty())
    {
        // Shorten with an ellipsis (UTF-8 aware).
        while (! t.empty() && textSize (f, t + "…").x > w)
        {
            t.pop_back();
            while (! t.empty() && ((unsigned char) t.back() & 0xc0) == 0x80) t.pop_back();
            if (! t.empty() && ((unsigned char) t.back() & 0xc0) == 0xc0) t.pop_back();
        }
        t += "…";
        size = textSize (f, t);
    }
    const float x = h < 0 ? a.x : h > 0 ? b.x - size.x : 0.5f * (a.x + b.x - size.x);
    const float y = 0.5f * (a.y + b.y - size.y);
    dl->PushClipRect (a, b, true);
    drawText (dl, f, { x, y }, c, t);
    dl->PopClipRect();
}

// ================================================================ small widgets
void drawSignal (ImDrawList* dl, ImVec2 a, ImVec2 b, const std::vector<float>& x, int span, ImU32 colour, bool trigger)
{
    const float cy = 0.5f * (a.y + b.y), half = 0.45f * (b.y - a.y);
    dl->AddLine ({ a.x, cy }, { b.x, cy }, IM_COL32 (255, 255, 255, 25));
    if (x.size() < 4) return;
    span = std::clamp (span, 2, (int) x.size());
    // From the newest upward zero crossing that leaves a whole span after it.
    size_t from = x.size() - (size_t) span;
    if (trigger)
        for (size_t i = x.size() - (size_t) span; i > x.size() - std::min (x.size(), (size_t) span * 2) + 1 && i > 1; --i)
            if (x[i - 1] < 0.0f && x[i] >= 0.0f)
            {
                from = i;
                break;
            }
    float peak = 1.0f;
    for (size_t i = from; i < from + (size_t) span; ++i) peak = std::max (peak, std::abs (x[i]));
    const int columns = std::max (2, (int) (b.x - a.x));
    std::vector<ImVec2> top, bottom;
    top.reserve ((size_t) columns);
    bottom.reserve ((size_t) columns);
    for (int c = 0; c < columns; ++c)
    {
        const size_t i0 = from + (size_t) ((long long) c * span / columns), i1 = std::max (i0 + 1, from + (size_t) ((long long) (c + 1) * span / columns));
        float lo = x[i0], hi = x[i0];
        for (size_t i = i0; i < i1 && i < x.size(); ++i)
        {
            lo = std::min (lo, x[i]);
            hi = std::max (hi, x[i]);
        }
        top.push_back ({ a.x + (float) c, cy - half * hi / peak });
        bottom.push_back ({ a.x + (float) c, cy - half * lo / peak });
    }
    for (int c = 0; c < columns; ++c)
        if (bottom[(size_t) c].y - top[(size_t) c].y > 1.5f) dl->AddLine (top[(size_t) c], bottom[(size_t) c], withAlpha (colour, 0.55f));
    dl->AddPolyline (top.data(), (int) top.size(), colour, 0, 1.0f);
    dl->AddPolyline (bottom.data(), (int) bottom.size(), colour, 0, 1.0f);
    if (peak > 1.0f)
    {
        char buf[32];
        std::snprintf (buf, sizeof buf, "peak %.2f", peak);
        drawText (dl, fonts().tiny, { b.x - 4 - textSize (fonts().tiny, buf).x, a.y + 2 }, textGrey, buf);
    }
}

void scope (const char* id, ImVec2 size, const std::vector<float>& values, ImU32 colour, const std::string& title, int span)
{
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    ImGui::Dummy (size);
    (void) id;
    auto* dl = ImGui::GetWindowDrawList();
    const ImVec2 p1 { p0.x + size.x, p0.y + size.y };
    dl->AddRectFilled (p0, p1, argb (0xff0b0b12));
    drawSignal (dl, { p0.x + 1, p0.y + 1 }, { p1.x - 1, p1.y - 1 }, values, span, colour, true);
    if (! title.empty()) drawText (dl, fonts().small, { p0.x + 4, p0.y + 2 }, textGrey, title);
}

void helpText (const std::string& s)
{
    ImGui::PushFont (fonts().small);
    ImGui::PushStyleColor (ImGuiCol_Text, textGrey);
    ImGui::TextWrapped ("%s", s.c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

void header (const char* title)
{
    ImGui::Dummy ({ 0, 2 });
    ImGui::PushFont (fonts().bold);
    ImGui::PushStyleColor (ImGuiCol_Text, accent);
    ImGui::TextUnformatted (title);
    ImGui::PopStyleColor();
    ImGui::PopFont();
    const auto p = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddLine ({ p.x, p.y - 2 }, { p.x + ImGui::GetContentRegionAvail().x, p.y - 2 }, withAlpha (accent, 0.3f));
    ImGui::Dummy ({ 0, 1 });
}

bool segmented (const char* id, int& value, const std::vector<std::string>& labels, ImU32 on, float width,
                const std::vector<const char*>& tips)
{
    ImGui::PushID (id);
    const float total = width > 0 ? width : ImGui::GetContentRegionAvail().x;
    const float w = (total - (float) (labels.size() - 1) * 2.0f) / (float) labels.size();
    bool changed = false;
    ImGui::PushStyleVar (ImGuiStyleVar_ItemSpacing, { 2, 0 });
    for (size_t i = 0; i < labels.size(); ++i)
    {
        if (i > 0) ImGui::SameLine();
        const bool sel = value == (int) i;
        if (sel)
        {
            ImGui::PushStyleColor (ImGuiCol_Button, on);
            ImGui::PushStyleColor (ImGuiCol_ButtonHovered, brighter (on, 0.2f));
        }
        if (ImGui::Button ((labels[i] + "##" + std::to_string (i)).c_str(), { w, 0 }) && ! sel)
        {
            value = (int) i;
            changed = true;
        }
        markItem (std::string (id) + "." + std::to_string (i));
        if (i < tips.size()) tooltip (tips[i]);
        if (sel) ImGui::PopStyleColor (2);
    }
    ImGui::PopStyleVar();
    ImGui::PopID();
    return changed;
}

namespace
{
std::map<std::string, ImVec2>& itemMarks()
{
    static std::map<std::string, ImVec2> m;
    return m;
}
} // namespace

void markItem (const std::string& name)
{
    const auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
    itemMarks()[name] = { 0.5f * (a.x + b.x), 0.5f * (a.y + b.y) };
}

void markPoint (const std::string& name, ImVec2 p) { itemMarks()[name] = p; }

const ImVec2* markedItem (const std::string& name)
{
    const auto it = itemMarks().find (name);
    return it != itemMarks().end() ? &it->second : nullptr;
}

void tooltip (const char* text)
{
    if (text != nullptr && *text != 0) ImGui::SetItemTooltip ("%s", text);
}
} // namespace afx
