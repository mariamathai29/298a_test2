#include "Project.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace afx
{
using json = nlohmann::json;

namespace
{
double num (const json& o, const char* key, double fallback, double lo, double hi)
{
    if (! o.is_object()) return fallback;
    const auto it = o.find (key);
    if (it == o.end() || ! it->is_number()) return fallback;
    const double v = it->get<double>();
    return std::isfinite (v) ? std::clamp (v, lo, hi) : fallback;
}
int integer (const json& o, const char* key, int fallback, int lo, int hi) { return (int) std::lround (num (o, key, fallback, lo, hi)); }
bool flag (const json& o, const char* key, bool fallback)
{
    if (! o.is_object()) return fallback;
    const auto it = o.find (key);
    return it != o.end() && it->is_boolean() ? it->get<bool>() : fallback;
}
std::string str (const json& o, const char* key)
{
    if (! o.is_object()) return {};
    const auto it = o.find (key);
    return it != o.end() && it->is_string() ? it->get<std::string>() : std::string();
}
const json& sub (const json& o, const char* key)
{
    static const json none;
    if (! o.is_object()) return none;
    const auto it = o.find (key);
    return it != o.end() ? *it : none;
}

json sourceJson (const SourceSpec& s)
{
    return { { "kind", sourceKindId (s.kind) }, { "amplitude", s.amplitude }, { "bpm", s.bpm },   { "hrv", s.hrv },
             { "breathing", s.breathing },      { "highPass", s.highPass },      { "frequency", s.frequency }, { "frequency2", s.frequency2 },
             { "sweep", s.sweep },              { "mains", s.mains },         { "harmonics", s.harmonics },
             { "burstRate", s.burstRate },      { "burstLength", s.burstLength }, { "seed", s.seed } };
}

SourceSpec sourceFrom (const json& o, const SourceSpec& fallback)
{
    if (! o.is_object()) return fallback;
    int kind = -1;
    const auto id = str (o, "kind");
    for (int k = 0; k < numSourceKinds; ++k)
        if (id == sourceKindId ((SourceKind) k)) kind = k;
    if (kind < 0) return fallback;
    SourceSpec s = SourceSpec::defaults ((SourceKind) kind);
    s.amplitude = num (o, "amplitude", s.amplitude, -10.0, 10.0);
    s.bpm = num (o, "bpm", s.bpm, 10.0, 300.0);
    s.hrv = num (o, "hrv", s.hrv, 0.0, 0.3);
    s.breathing = num (o, "breathing", s.breathing, 0.0, 5.0);
    s.highPass = num (o, "highPass", s.highPass, 0.0, 20.0);
    s.frequency = num (o, "frequency", s.frequency, 0.0, 0.5 * sampleRate);
    s.frequency2 = num (o, "frequency2", s.frequency2, 0.0, 0.5 * sampleRate);
    s.sweep = num (o, "sweep", s.sweep, 0.05, 600.0);
    s.mains = integer (o, "mains", s.mains, 50, 60) < 55 ? 50 : 60;
    s.harmonics = num (o, "harmonics", s.harmonics, 0.0, 2.0);
    s.burstRate = num (o, "burstRate", s.burstRate, 0.0, 20.0);
    s.burstLength = num (o, "burstLength", s.burstLength, 0.01, 10.0);
    s.seed = integer (o, "seed", s.seed, 0, 1000000);
    return s;
}

json pathJson (const PathSpec& p) { return { { "preset", p.preset }, { "gain", p.gain }, { "delay", p.delay }, { "taps", p.taps } }; }

PathSpec pathFrom (const json& o, const PathSpec& fallback)
{
    if (! o.is_object()) return fallback;
    PathSpec p;
    p.preset = str (o, "preset").substr (0, 64);
    p.gain = num (o, "gain", 1.0, -10.0, 10.0);
    p.delay = integer (o, "delay", 0, 0, maxPathDelay);
    p.taps.clear();
    if (const auto& t = sub (o, "taps"); t.is_array())
        for (const auto& v : t)
            if (v.is_number() && std::isfinite (v.get<double>()) && (int) p.taps.size() < maxPathTaps)
                p.taps.push_back (std::clamp (v.get<double>(), -10.0, 10.0));
    if (p.taps.empty()) p.taps.push_back (1.0);
    return p;
}
} // namespace

json toJson (const Project& pr)
{
    const auto& sc = pr.scene;
    const auto& f = sc.filter;
    json mu = json::object();
    for (int a = 0; a < numAlgorithms; ++a)
        if ((Algorithm) a != Algorithm::rls) mu[algorithmId ((Algorithm) a)] = f.mu[(size_t) a];
    return { { "format", formatTag },
             { "sampleRate", sampleRate },
             { "sources",
               { { "signal", sourceJson (sc.signal) },
                 { "interference", sourceJson (sc.interference) },
                 { "primaryNoise", sourceJson (sc.primaryNoise) },
                 { "referenceNoise", sourceJson (sc.referenceNoise) } } },
             { "paths", { { "primary", pathJson (sc.primaryPath) }, { "reference", pathJson (sc.referencePath) } } },
             { "signalInReference", sc.signalInReference },
             { "filter",
               { { "algorithm", algorithmId (f.algorithm) },
                 { "taps", f.taps },
                 { "mu", mu },
                 { "leakage", f.leakage },
                 { "lambda", f.lambda },
                 { "delta", f.delta },
                 { "freeze", f.freeze },
                 { "arithmetic", arithmeticId (f.arithmetic) },
                 { "fullScale", f.fullScale },
                 { "muSel", f.muSel } } },
             { "view", { { "speed", pr.view.speed }, { "span", pr.view.span } } },
             { "ranges", pr.ranges.is_object() ? pr.ranges : json::object() } };
}

bool fromJson (const json& v, Project& pr, std::string& error)
{
    if (str (v, "format") != formatTag)
    {
        error = str (v, "format").rfind ("reel", 0) == 0 ? "a Reel patch, not adaptive filter settings" : "not adaptive filter settings";
        return false;
    }
    Project out;
    const Scene d = Scene::defaults();
    auto& sc = out.scene;
    const auto& src = sub (v, "sources");
    sc.signal = sourceFrom (sub (src, "signal"), d.signal);
    sc.interference = sourceFrom (sub (src, "interference"), d.interference);
    sc.primaryNoise = sourceFrom (sub (src, "primaryNoise"), d.primaryNoise);
    sc.referenceNoise = sourceFrom (sub (src, "referenceNoise"), d.referenceNoise);
    const auto& paths = sub (v, "paths");
    sc.primaryPath = pathFrom (sub (paths, "primary"), d.primaryPath);
    sc.referencePath = pathFrom (sub (paths, "reference"), d.referencePath);
    sc.signalInReference = num (v, "signalInReference", 0.0, -2.0, 2.0);
    const auto& f = sub (v, "filter");
    auto& fs = sc.filter;
    const auto id = str (f, "algorithm");
    for (int a = 0; a < numAlgorithms; ++a)
        if (id == algorithmId ((Algorithm) a)) fs.algorithm = (Algorithm) a;
    fs.taps = integer (f, "taps", 8, 1, maxTaps);
    const auto& mu = sub (f, "mu");
    for (int a = 0; a < numAlgorithms; ++a)
        if ((Algorithm) a != Algorithm::rls) fs.mu[(size_t) a] = num (mu, algorithmId ((Algorithm) a), fs.mu[(size_t) a], 1e-9, 10.0);
    fs.leakage = num (f, "leakage", 0.0, 0.0, 0.1);
    fs.lambda = num (f, "lambda", 0.999, 0.5, 1.0);
    fs.delta = num (f, "delta", 0.01, 1e-9, 1e6);
    fs.freeze = flag (f, "freeze", false);
    fs.arithmetic = str (f, "arithmetic") == arithmeticId (Arithmetic::chip) ? Arithmetic::chip : Arithmetic::floating;
    fs.fullScale = num (f, "fullScale", fs.fullScale, 0.01, 10.0);
    fs.muSel = integer (f, "muSel", fs.muSel, 0, 7);
    out.view.speed = num (sub (v, "view"), "speed", 2.0, 0.25, 100.0);
    out.view.span = num (sub (v, "view"), "span", 4.0, 0.5, 30.0);
    if (const auto& r = sub (v, "ranges"); r.is_object()) out.ranges = r;
    pr = std::move (out);
    return true;
}

bool saveProject (const std::string& path, const Project& pr, std::string& error)
{
    std::ofstream f (path, std::ios::binary | std::ios::trunc);
    if (! f)
    {
        error = "cannot open the file for writing";
        return false;
    }
    f << toJson (pr).dump (2) << '\n';
    if (! f)
    {
        error = "writing failed";
        return false;
    }
    return true;
}

bool loadProject (const std::string& path, Project& pr, std::string& error)
{
    std::ifstream f (path, std::ios::binary);
    if (! f)
    {
        error = "cannot open the file";
        return false;
    }
    std::stringstream ss;
    ss << f.rdbuf();
    const auto j = json::parse (ss.str(), nullptr, false);
    if (j.is_discarded())
    {
        error = "not valid JSON";
        return false;
    }
    try
    {
        return fromJson (j, pr, error);
    }
    catch (const std::exception& e)
    {
        error = std::string ("malformed settings: ") + e.what();
        return false;
    }
}
} // namespace afx
