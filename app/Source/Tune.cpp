#include "Tune.h"

#include "Views.h"

#include <imgui_internal.h>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>

namespace afx
{
namespace
{
struct Range
{
    double lo = 0.0, hi = 1.0;
    bool log = false;
};
std::map<std::string, Range>& ranges()
{
    static std::map<std::string, Range> r;
    return r;
}
std::map<std::string, Range>& saved()   // ranges loaded from a project, applied on first use
{
    static std::map<std::string, Range> r;
    return r;
}

struct Bar
{
    ImVec2 a, b;
    double value;
};
std::map<std::string, Bar>& bars()
{
    static std::map<std::string, Bar> m;
    return m;
}

std::string focused;                  // the control with the arrow keys
std::string editing, editBuffer;      // a typed value: "<id>" (value), "<id>#min", "<id>#max"
bool editStart = false;
double dragStart = 0.0;

const ImU32 barBack = argb (0xff1b2026), rangeBack = argb (0xff232a31);

bool isLog (const Range& r) { return r.log && r.lo > 0.0; }

double toP (const Range& r, double v)
{
    if (isLog (r)) return std::log (std::max (r.lo, v) / r.lo) / std::log (r.hi / r.lo);
    return (v - r.lo) / (r.hi - r.lo);
}

double fromP (const Range& r, double p)
{
    p = std::clamp (p, 0.0, 1.0);
    if (isLog (r)) return r.lo * std::pow (r.hi / r.lo, p);
    return r.lo + p * (r.hi - r.lo);
}

// Range ends: short (up to six significant digits).
std::string formatLimit (double v, const TuneOptions& o)
{
    char buf[32];
    if (o.integer) std::snprintf (buf, sizeof buf, "%lld", (long long) std::llround (v));
    else std::snprintf (buf, sizeof buf, "%.4g", v);
    return buf;
}

std::string format (double v, const TuneOptions& o)
{
    char buf[64];
    if (o.integer) std::snprintf (buf, sizeof buf, "%lld", (long long) std::llround (v));
    else
    {
        const double a = std::abs (v);
        if (a > 0.0 && (a < std::pow (10.0, -o.decimals) || a >= 1e6)) std::snprintf (buf, sizeof buf, "%.3e", v);
        else std::snprintf (buf, sizeof buf, "%.*f", o.decimals, v);
    }
    return buf;
}

void setRange (Range& r, double lo, double hi, bool integer)
{
    if (! std::isfinite (lo) || ! std::isfinite (hi)) return;
    if (lo > hi) std::swap (lo, hi);
    if (integer)
    {
        lo = std::round (lo);
        hi = std::round (hi);
    }
    if (hi - lo < 1e-12) hi = lo + (integer ? 1.0 : 1e-6);
    r.lo = lo;
    r.hi = hi;
}

bool parse (const std::string& s, double& v)
{
    char* end = nullptr;
    const double x = std::strtod (s.c_str(), &end);
    if (end == s.c_str() || ! std::isfinite (x)) return false;
    v = x;
    return true;
}

// An in-place text entry over a rectangle; returns true when committed.
bool entry (const std::string& key, ImVec2 a, ImVec2 b, std::string& result)
{
    ImGui::SetCursorScreenPos (a);
    ImGui::SetNextItemWidth (b.x - a.x);
    ImGui::PushStyleVar (ImGuiStyleVar_FramePadding, { 3, std::max (0.0f, (b.y - a.y - ImGui::GetFontSize()) * 0.5f) });
    if (editStart)
    {
        ImGui::SetKeyboardFocusHere();
        editStart = false;
    }
    const bool enter = ImGui::InputText (("##e" + key).c_str(), &editBuffer, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
    ImGui::PopStyleVar();
    bool commit = false;
    if (ImGui::IsKeyPressed (ImGuiKey_Escape)) editing.clear();
    else if (enter || ImGui::IsItemDeactivated())
    {
        result = editBuffer;
        commit = true;
        editing.clear();
    }
    return commit;
}
} // namespace

bool tune (const char* id, const char* name, double& value, const TuneOptions& o)
{
    const std::string key = id;
    auto& rs = ranges();
    auto found = rs.find (key);
    if (found == rs.end())
    {
        Range r;
        setRange (r, o.lo, o.hi, o.integer);
        r.log = o.log;
        if (auto s = saved().find (key); s != saved().end()) r = s->second;
        found = rs.emplace (key, r).first;
    }
    Range& r = found->second;
    // A value outside the range widens it (a loaded patch, or a typed value).
    if (value < r.lo || value > r.hi) setRange (r, std::min (r.lo, value), std::max (r.hi, value), o.integer);

    const double before = value;
    auto snap = [&o] (double v) { return o.integer ? std::round (v) : v; };
    auto change = [&] (double v) { value = snap (std::clamp (v, r.lo, r.hi)); };
    auto nudge = [&] (double dp) {
        double target = fromP (r, toP (r, value) + dp);
        if (o.integer && std::round (target) == value) target = value + (dp > 0 ? 1.0 : -1.0);   // integers always move a step
        change (target);
    };

    ImGui::PushID (id);
    auto* dl = ImGui::GetWindowDrawList();
    const auto& io = ImGui::GetIO();
    const float rowH = 22.0f;
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const float width = o.width > 0 ? o.width : ImGui::GetContentRegionAvail().x;
    const float nameW = name != nullptr && *name != 0 ? o.nameWidth : 0.0f, rangeW = o.rangeWidth;
    const ImVec2 minA { p0.x + nameW, p0.y + 1 }, minB { minA.x + rangeW, p0.y + rowH - 1 };
    const ImVec2 maxB { p0.x + width, p0.y + rowH - 1 }, maxA { maxB.x - rangeW, p0.y + 1 };
    const ImVec2 barA { minB.x + 2, p0.y + 1 }, barB { rangeW > 0 ? maxA.x - 2 : maxB.x, p0.y + rowH - 1 };
    const ImU32 acc = o.accent != 0 ? o.accent : accent;
    const bool isFocused = focused == key;
    const bool disabled = (ImGui::GetCurrentContext()->CurrentItemFlags & ImGuiItemFlags_Disabled) != 0;
    bars()[key] = { barA, barB, value };

    // name
    if (nameW > 0)
        drawTextIn (dl, fonts().small, { p0.x, p0.y }, { p0.x + nameW - 4, p0.y + rowH }, isFocused ? acc : textLight, name);

    // min and max: click to type a new limit
    for (int end = 0; end < 2 && rangeW > 0; ++end)
    {
        const ImVec2 a = end == 0 ? minA : maxA, b = end == 0 ? minB : maxB;
        const std::string ekey = key + (end == 0 ? "#min" : "#max");
        if (editing == ekey)
        {
            std::string typed;
            if (entry (ekey, a, b, typed))
            {
                double v;
                if (parse (typed, v)) setRange (r, end == 0 ? v : r.lo, end == 0 ? r.hi : v, o.integer);
                change (value);
            }
            continue;
        }
        ImGui::SetCursorScreenPos (a);
        if (ImGui::InvisibleButton (end == 0 ? "min" : "max", { b.x - a.x, b.y - a.y }))
        {
            editing = ekey;
            editBuffer = formatLimit (end == 0 ? r.lo : r.hi, o);
            editStart = true;
        }
        tooltip ("click to type a new limit");
        dl->AddRectFilled (a, b, ImGui::GetColorU32 (rangeBack), 3.0f);
        drawTextIn (dl, fonts().small, a, b, textGrey, formatLimit (end == 0 ? r.lo : r.hi, o), 0);
    }

    // the bar
    if (editing == key)
    {
        std::string typed;
        if (entry (key, barA, barB, typed))
        {
            double v;
            if (parse (typed, v))
            {
                if (v < r.lo || v > r.hi) setRange (r, std::min (r.lo, v), std::max (r.hi, v), o.integer);
                change (v);
            }
        }
    }
    else
    {
        ImGui::SetCursorScreenPos (barA);
        ImGui::InvisibleButton ("bar", { std::max (4.0f, barB.x - barA.x), barB.y - barA.y },
                                ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        const bool hovered = ImGui::IsItemHovered();
        if (ImGui::IsItemActivated() && ImGui::IsMouseClicked (ImGuiMouseButton_Left))
        {
            focused = key;
            dragStart = toP (r, value);
        }
        if (ImGui::IsItemActive() && ImGui::IsMouseDown (ImGuiMouseButton_Left) && ImGui::IsMouseDragging (ImGuiMouseButton_Left, 1.0f))
        {
            const double fine = io.KeyShift ? 0.1 : 1.0;
            const double dx = io.MousePos.x - io.MouseClickedPos[0].x;
            change (fromP (r, dragStart + fine * dx / std::max (20.0f, barB.x - barA.x)));
        }
        if (hovered && ImGui::IsMouseDoubleClicked (ImGuiMouseButton_Left))
        {
            editing = key;
            editBuffer = format (value, o);
            editStart = true;
        }
        if (hovered && ImGui::IsMouseClicked (ImGuiMouseButton_Right))
        {
            focused = key;
            ImGui::OpenPopup ("tunemenu");
        }
        if (hovered && io.MouseWheel != 0.0f && ! disabled)
        {
            nudge ((io.MouseWheel > 0 ? 1.0 : -1.0) * (io.KeyShift ? 0.1 : 0.01));
            ImGui::SetKeyOwner (ImGuiKey_MouseWheelY, ImGui::GetItemID());
        }
        tooltip (o.tip);
        // A click anywhere else lets go of the arrow keys.
        if (isFocused && ! hovered && (ImGui::IsMouseClicked (ImGuiMouseButton_Left) || ImGui::IsMouseClicked (ImGuiMouseButton_Right))
            && ! ImGui::IsPopupOpen ("tunemenu"))
            focused.clear();
        if (isFocused && ! io.WantTextInput && ! disabled)
        {
            const double step = io.KeyShift ? 0.1 : io.KeyCtrl ? 0.001 : 0.01;
            if (ImGui::IsKeyPressed (ImGuiKey_UpArrow) || ImGui::IsKeyPressed (ImGuiKey_RightArrow)) nudge (step);
            if (ImGui::IsKeyPressed (ImGuiKey_DownArrow) || ImGui::IsKeyPressed (ImGuiKey_LeftArrow)) nudge (-step);
            if (ImGui::IsKeyPressed (ImGuiKey_PageUp)) nudge (0.1);
            if (ImGui::IsKeyPressed (ImGuiKey_PageDown)) nudge (-0.1);
            if (ImGui::IsKeyPressed (ImGuiKey_Home, false)) change (r.lo);
            if (ImGui::IsKeyPressed (ImGuiKey_End, false)) change (r.hi);
            if (ImGui::IsKeyPressed (ImGuiKey_Enter, false) || ImGui::IsKeyPressed (ImGuiKey_KeypadEnter, false))
            {
                editing = key;
                editBuffer = format (value, o);
                editStart = true;
            }
            if (ImGui::IsKeyPressed (ImGuiKey_Escape, false)) focused.clear();
        }
        if (ImGui::BeginPopup ("tunemenu"))
        {
            bool lg = r.log;
            if (ImGui::MenuItem ("Logarithmic scale", nullptr, &lg, r.lo > 0.0)) r.log = lg;
            if (ImGui::MenuItem (("Reset to " + format (o.defaultValue, o)).c_str())) change (o.defaultValue);
            if (ImGui::MenuItem ("Type a value..."))
            {
                editing = key;
                editBuffer = format (value, o);
                editStart = true;
            }
            ImGui::EndPopup();
        }

        const bool focusNow = focused == key;
        dl->AddRectFilled (barA, barB, ImGui::GetColorU32 (barBack), 3.0f);
        const float w = barB.x - barA.x;
        const float p = (float) std::clamp (toP (r, value), 0.0, 1.0);
        // From zero when the range spans it (signed depths), else from the left.
        const float zero = r.lo < 0.0 && r.hi > 0.0 && ! isLog (r) ? (float) toP (r, 0.0) : 0.0f;
        const float x0 = barA.x + w * std::min (p, zero), x1 = barA.x + w * std::max (p, zero);
        dl->AddRectFilled ({ x0, barA.y }, { std::max (x0 + 1.0f, x1), barB.y }, ImGui::GetColorU32 (withAlpha (acc, focusNow ? 0.55f : 0.35f)), 3.0f);
        dl->AddRectFilled ({ barA.x + w * p - 1.0f, barA.y }, { barA.x + w * p + 1.0f, barB.y }, ImGui::GetColorU32 (acc));
        drawTextIn (dl, fonts().ui, { barA.x + 3, barA.y }, { barB.x - 3, barB.y }, IM_COL32_WHITE, format (value, o) + o.suffix, 0);
        if (isLog (r)) drawText (dl, fonts().tiny, { barB.x - 18, barB.y - 12 }, textGrey, "log");
        if (focusNow)
        {
            dl->AddRect (barA, barB, ImGui::GetColorU32 (acc), 3.0f, 0, 1.5f);
            drawText (dl, fonts().tiny, { barA.x + 3, barA.y + 5 }, acc, "↑↓");
        }
    }

    ImGui::SetCursorScreenPos (p0);
    ImGui::Dummy ({ width, rowH });
    ImGui::PopID();
    return value != before;
}

bool tune (const char* id, const char* name, int& value, const TuneOptions& o)
{
    double v = value;
    auto opts = o;
    opts.integer = true;
    const bool changed = tune (id, name, v, opts);
    value = (int) std::llround (v);
    return changed;
}

void setTuneRange (const std::string& id, double lo, double hi, bool log)
{
    Range r;
    setRange (r, lo, hi, false);
    r.log = log;
    ranges()[id] = r;
}

void forgetTuneRange (const std::string& id) { ranges().erase (id); }

nlohmann::json tuneRangesToJson()
{
    nlohmann::json out = nlohmann::json::object();
    for (const auto& [id, r] : ranges()) out[id] = { r.lo, r.hi, r.log };
    return out;
}

void tuneRangesFromJson (const nlohmann::json& j)
{
    if (! j.is_object()) return;
    for (auto it = j.begin(); it != j.end(); ++it)
    {
        const auto& a = it.value();
        if (! a.is_array() || a.size() < 2 || ! a[0].is_number() || ! a[1].is_number()) continue;
        Range r;
        setRange (r, a[0].get<double>(), a[1].get<double>(), false);
        r.log = a.size() >= 3 && a[2].is_boolean() && a[2].get<bool>();
        ranges()[it.key()] = r;
        saved()[it.key()] = r;
    }
}

bool anyTuneFocused() { return ! focused.empty(); }
void releaseTuneFocus() { focused.clear(); }
std::string focusedTune() { return focused; }

bool tuneNow (const std::string& id, ImVec2& at, double* value)
{
    const auto b = bars().find (id);
    if (b == bars().end()) return false;
    if (value != nullptr) *value = b->second.value;
    return tunePoint (id, b->second.value, at);
}

bool tunePoint (const std::string& id, double value, ImVec2& at)
{
    const auto b = bars().find (id);
    const auto r = ranges().find (id);
    if (b == bars().end() || r == ranges().end()) return false;
    const float p = (float) std::clamp (toP (r->second, value), 0.0, 1.0);
    at = { b->second.a.x + p * (b->second.b.x - b->second.a.x), 0.5f * (b->second.a.y + b->second.b.y) };
    return true;
}
} // namespace afx
