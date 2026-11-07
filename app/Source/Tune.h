#pragma once

// A tunable value with its own range: [name] [min] [====value====] [max].
//   - min and max are editable (click them and type); a reversed range is swapped;
//   - drag across the bar to slide the value (Shift: fine);
//   - click the bar to give it focus, then Up/Down (or Left/Right, or the wheel)
//     step by 1 % of the range, Shift 10 %, Ctrl 0.1 %; PageUp/PageDown 10 %;
//     Home/End jump to min/max; Return types a value; Escape lets go;
//   - double-click to type a value; right-click for log scale and reset.
// Each control is named by an id; its range (and log scale) is kept under that id
// and saved with the project.

#include <imgui.h>
#include <nlohmann/json.hpp>

#include <string>

namespace afx
{
struct TuneOptions
{
    double lo = 0.0, hi = 1.0;   // the starting range
    double defaultValue = 0.0;   // for "Reset"
    int decimals = 3;
    bool log = false;            // log scale to start with (while min > 0)
    bool integer = false;
    const char* suffix = "";
    const char* tip = nullptr;
    ImU32 accent = 0;            // 0: the default accent
    float nameWidth = 84.0f;
    float rangeWidth = 44.0f;
    float width = -1.0f;         // -1: the rest of the row
};

// Draws the control; true if the value changed (by the user).
bool tune (const char* id, const char* name, double& value, const TuneOptions&);
bool tune (const char* id, const char* name, int& value, const TuneOptions&);

// The ranges, by id.
void setTuneRange (const std::string& id, double lo, double hi, bool log = false);
void forgetTuneRange (const std::string& id);   // back to its starting range on next use
nlohmann::json tuneRangesToJson();
void tuneRangesFromJson (const nlohmann::json&);
bool anyTuneFocused();   // a control has the arrow keys
void releaseTuneFocus();
std::string focusedTune();
// Where a control was drawn last frame (for scripted mouse): the point on its bar showing this value.
bool tunePoint (const std::string& id, double value, ImVec2& at);
bool tuneNow (const std::string& id, ImVec2& at, double* value = nullptr);   // its value now
} // namespace afx
