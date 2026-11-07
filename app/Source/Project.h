#pragma once

// Settings as JSON ("afx 1"): the scene (sources, paths, the filter), the view
// (speed, trace span) and the tuning ranges of the controls.  Nothing recorded.

#include "Scene.h"

#include <nlohmann/json.hpp>

#include <string>

namespace afx
{
struct View
{
    double speed = 2.0;   // simulated seconds per second
    double span = 4.0;    // s of traces shown
    bool operator== (const View&) const = default;
};

struct Project
{
    Scene scene = Scene::defaults();
    View view;
    nlohmann::json ranges = nlohmann::json::object();
};

constexpr const char* formatTag = "afx 1";
nlohmann::json toJson (const Project&);
bool fromJson (const nlohmann::json&, Project&, std::string& error);
bool saveProject (const std::string& path, const Project&, std::string& error);
bool loadProject (const std::string& path, Project&, std::string& error);
} // namespace afx
