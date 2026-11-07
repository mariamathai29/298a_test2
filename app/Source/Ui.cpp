#include "Ui.h"

#include "Log.h"
#include "Plots.h"
#include "Tune.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdlib>
#include <filesystem>

namespace afx
{
namespace
{
std::string homeFolder()
{
    const char* h = std::getenv ("HOME");
    return h != nullptr ? h : ".";
}

std::string fileName (const std::string& path) { return std::filesystem::path (path).filename().string(); }

double db (double power) { return 10.0 * std::log10 (std::max (power, 1e-30)); }

std::string fmt (const char* f, double v)
{
    char buf[64];
    std::snprintf (buf, sizeof buf, f, v);
    return buf;
}

// Gridlines for an axis: a step of 1, 2 or 5 times a power of ten giving about `count` lines.
double gridStep (double range, int count)
{
    const double raw = range / std::max (1, count);
    const double p = std::pow (10.0, std::floor (std::log10 (std::max (raw, 1e-12))));
    for (double m : { 1.0, 2.0, 5.0 })
        if (m * p >= raw) return m * p;
    return 10.0 * p;
}

const long long stepSamples[4] = { 1, 10, 100, (long long) sampleRate };
const char* const stepNames[4] = { "1 sample", "10 samples", "100 samples", "1 second" };
} // namespace

// ================================================================ App
App::App()
{
    lastFolder = homeFolder();
    edit = sim.scene();
    status = "Fetal ECG extraction: the maternal heartbeat, picked up on the chest (x), is cancelled from the abdominal lead (d) by an "
             "8-tap sign-sign LMS filter. Watch e come down onto the fetal ECG s, and the learning curve fall.";
}

ImVec2 App::where (const std::string& name) const
{
    const auto* p = markedItem (name);
    return p != nullptr ? *p : ImVec2 {};
}

void App::step (long long samples)
{
    running = false;
    apply();
    sim.run (samples);
}

void App::restart()
{
    apply();
    sim.restart();
    owed = 0.0;
    spectrumAt = -1.0;
}

void App::setSpeed (double s) { view.speed = std::clamp (s, 0.25, 100.0); }

void App::setScene (const Scene& sc)
{
    edit = sc;
    apply();
    restart();
}

void App::loadDefaults()
{
    setScene (Scene::defaults());
    view = View {};
    selection = Block::filter;
    status = "The default scene: fetal extraction with an 8-tap sign-sign LMS filter, through a 4-tap abdominal path it can model.";
}

void App::loadChip()
{
    setScene (Scene::chip());
    view = View {};
    selection = Block::filter;
    status = "The chip: the same fetal extraction on tt_um_adaptive_filter's datapath, bit for bit (4 taps, Q1.7 samples, Q2.12 weights), "
             "with a floating-point twin beside it.";
}

bool App::openProject (const std::string& path)
{
    Project pr;
    std::string error;
    if (! loadProject (path, pr, error))
    {
        status = reportError ("Could not open those settings", path + ": " + error);
        return false;
    }
    tuneRangesFromJson (pr.ranges);
    setScene (pr.scene);
    view = pr.view;
    status = "Opened " + fileName (path);
    lastFolder = std::filesystem::path (path).parent_path().string();
    return true;
}

bool App::saveProjectTo (const std::string& path)
{
    Project pr;
    pr.scene = edit;
    pr.view = view;
    pr.ranges = tuneRangesToJson();
    std::string error;
    if (! saveProject (path, pr, error))
    {
        status = reportError ("Could not save the settings", path + ": " + error);
        return false;
    }
    status = "Saved " + fileName (path);
    lastFolder = std::filesystem::path (path).parent_path().string();
    return true;
}

void App::apply()
{
    if (! (edit == sim.scene()))
    {
        sim.setScene (edit);
        ear.voicesFrom (edit);
    }
}

void App::setListening (bool on)
{
    if (on && ! listen)
    {
        ear = Listener (soundRate);
        ear.voicesFrom (edit);
        ear.sonifier().setTexture (0.12);
        sound.clear();
    }
    listen = on;
}

void App::takeSound (std::vector<float>& out)
{
    out.swap (sound);
    sound.clear();
}

void App::advance (double dt)
{
    if (running)
    {
        owed += std::clamp (dt, 0.0, 0.25) * view.speed * sampleRate;
        long long n = (long long) owed;
        owed -= (double) n;
        sim.run (n);
    }
    // The Wiener solution and the reference's spectrum, now and then (they take a little working out).
    const double now = sim.time();
    if (sim.samples() > 200 && (now - sim.wienerTime() >= 0.5 || now < sim.wienerTime() || (int) sim.wienerTaps().size() != edit.filter.effectiveTaps()))
        sim.solveWiener (10.0);
    const double wall = ImGui::GetTime();
    if (sim.samples() > 512 && (spectrumAt < 0.0 || (std::abs (now - spectrumAt) >= 0.5 && wall - spectrumWall >= 0.25)))
    {
        // Hann-windowed DFT of the last 2048 samples of x, at 1 Hz steps.
        sim.history (Channel::reference).last (2048, scratch);
        const size_t n = scratch.size();
        static std::vector<double> hann;
        if (hann.size() != n)
        {
            hann.resize (n);
            for (size_t i = 0; i < n; ++i) hann[i] = 0.5 - 0.5 * std::cos (2.0 * pi * (double) i / (double) (n - 1));
        }
        constexpr int bins = 251;
        xSpectrum.assign (bins, -200.0f);
        for (int k = 0; k < bins; ++k)
        {
            const std::complex<double> by = std::polar (1.0, -2.0 * pi * (double) k / sampleRate);
            std::complex<double> sum { 0.0, 0.0 }, rot { 1.0, 0.0 };
            for (size_t i = 0; i < n; ++i)
            {
                sum += rot * ((double) scratch[i] * hann[i]);
                rot *= by;
            }
            xSpectrum[(size_t) k] = (float) db (std::norm (sum) / (double) n);
        }
        spectrumAt = now;
        spectrumWall = wall;
    }
}

// ================================================================ frames
void App::frame()
{
    const auto& io = ImGui::GetIO();
    const double dt = fixedStep > 0.0 ? fixedStep : io.DeltaTime;
    advance (dt);
    if (listen)
    {
        // what is heard: d or x when selected, e otherwise
        const Channel c = selection == Block::primarySum ? Channel::primary : selection == Block::referenceSum ? Channel::reference : Channel::error;
        if (c != heard)
        {
            heard = c;
            ear.listenTo (c);
            ear.sonifier().resync();
        }
        ear.follow (sim, std::clamp (dt, 0.0, 0.25), sound);
        const size_t most = (size_t) soundRate;   // a second, if nothing takes it
        if (sound.size() > 2 * most) sound.erase (sound.begin(), sound.end() - (std::ptrdiff_t) (2 * most));
    }
    ImGui::SetNextWindowPos ({ 0, 0 });
    ImGui::SetNextWindowSize (io.DisplaySize);
    ImGui::PushStyleVar (ImGuiStyleVar_WindowPadding, { 6, 6 });
    ImGui::Begin ("AFX", nullptr,
                  ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
                      | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollWithMouse);
    ImGui::PopStyleVar();

    toolbar();
    constexpr float gap = 6.0f, left = 370.0f, statusH = 20.0f;
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float right = std::clamp (avail.x * 0.3f, 440.0f, 540.0f);
    const float middle = std::max (300.0f, avail.x - left - right - 2 * gap);
    const float upper = std::max (300.0f, avail.y - statusH - gap);

    // Where each part is (for the tools: the promo's close-ups).
    const ImVec2 at = ImGui::GetCursorScreenPos();
    markPoint ("view.inspector", at);
    markPoint ("view.inspector.end", { at.x + left, at.y + upper });
    ImGui::BeginChild ("inspector", { left, upper }, ImGuiChildFlags_None);
    inspector();
    ImGui::EndChild();
    ImGui::SameLine (0, gap);
    ImGui::BeginChild ("middle", { middle, upper }, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    {
        const float h = ImGui::GetContentRegionAvail().y;
        diagram ({ middle, std::clamp (h * 0.36f, 250.0f, 330.0f) });
        traces ({ middle, ImGui::GetContentRegionAvail().y });
    }
    ImGui::EndChild();
    ImGui::SameLine (0, gap);
    ImGui::BeginChild ("right", { right, upper }, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
    {
        const float h = ImGui::GetContentRegionAvail().y - 2 * gap;
        learning ({ right, h * 0.31f });
        weightsView ({ right, h * 0.37f });
        responseView ({ right, ImGui::GetContentRegionAvail().y });
    }
    ImGui::EndChild();
    ImGui::PushFont (fonts().small);
    ImGui::PushStyleColor (ImGuiCol_Text, textGrey);
    ImGui::SetCursorPosY (ImGui::GetCursorPosY() + 2);
    ImGui::TextUnformatted (status.c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();

    dialogs();
    shortcuts();
    ImGui::End();
    apply();
}

void App::toolbar()
{
    auto button = [] (const char* label, ImU32 c, bool on, ImVec2 size = { 0, 0 }) {
        ImGui::PushStyleColor (ImGuiCol_Button, on ? c : argb (0xff2a3038));
        ImGui::PushStyleColor (ImGuiCol_ButtonHovered, brighter (on ? c : argb (0xff3a434e), 0.15f));
        const bool pressed = ImGui::Button (label, size);
        ImGui::PopStyleColor (2);
        return pressed;
    };
    const ImU32 green = argb (0xff2e7d32), amber = argb (0xffb07a1e);
    if (button (running ? "■ Pause" : "▶ Run", running ? amber : green, true, { 84, 0 })) running = ! running;
    markItem ("run");
    tooltip ("run or pause the simulation (Space)");
    ImGui::SameLine (0, 3);
    if (ImGui::Button ("Step", { 54, 0 })) step (stepSamples[stepSize]);
    markItem ("step");
    tooltip ("pause and advance by the step to the right (. or the right arrow)");
    ImGui::SameLine (0, 3);
    ImGui::SetNextItemWidth (112);
    if (ImGui::BeginCombo ("##stepsize", stepNames[stepSize]))
    {
        for (int i = 0; i < 4; ++i)
            if (ImGui::Selectable (stepNames[i], i == stepSize)) stepSize = i;
        ImGui::EndCombo();
    }
    tooltip ("how far one Step goes: one sample is one update of the weights");
    ImGui::SameLine (0, 3);
    if (ImGui::Button ("Restart", { 70, 0 }))
    {
        restart();
        status = "Restarted: time 0, the sources from their seeds, the weights at 0.";
    }
    markItem ("restart");
    tooltip ("back to time 0: the sources from their seeds, the weights at 0, the displays empty (R)");
    ImGui::SameLine (0, 3);
    if (ImGui::Button ("Reset weights", { 106, 0 }))
    {
        sim.resetWeights();
        status = "The weights are back at 0; the signals carry on. Watch it learn again.";
    }
    markItem ("resetWeights");
    tooltip ("the weights to 0, nothing else: watch it learn again from where the signals are (W)");
    ImGui::SameLine (0, 3);
    if (button (edit.filter.freeze ? "Frozen" : "Freeze", argb (0xff3d6a9a), edit.filter.freeze, { 66, 0 })) edit.filter.freeze = ! edit.filter.freeze;
    markItem ("freeze");
    tooltip ("hold the weights where they are: it still filters, but stops learning (F)");
    ImGui::SameLine (0, 3);
    if (button ("Listen", argb (0xff2e7d32), listen, { 62, 0 })) setListening (! listen);
    markItem ("listen");
    tooltip ("hear it (L): the heartbeats found in e (or in d or x, when selected) as thumps, the mother's low, the baby's high; "
             "at speeds above ×1 a real-time copy with the weights as they are now");

    ImGui::SameLine (0, 16);
    TuneOptions o;
    o.lo = 0.25;
    o.hi = 100.0;
    o.defaultValue = 2.0;
    o.decimals = 2;
    o.log = true;
    o.suffix = "×";
    o.nameWidth = 46.0f;
    o.rangeWidth = 38.0f;
    o.width = 270.0f;
    o.tip = "simulated seconds per second: ×1 is real time (500 samples a second)";
    tune ("view.speed", "speed", view.speed, o);
    view.speed = std::clamp (view.speed, 0.25, 100.0);
    ImGui::SameLine (0, 12);
    char buf[96];
    std::snprintf (buf, sizeof buf, "t = %.2f s   n = %lld", sim.time(), sim.samples());
    ImGui::PushFont (fonts().mono);
    ImGui::SetCursorPosY (ImGui::GetCursorPosY() + 3);
    ImGui::TextColored (ImGui::ColorConvertU32ToFloat4 (argb (0xffff9a3c)), "%s", buf);
    ImGui::PopFont();

    const float rightWidth = 104 + 84 + 66 + 66 + 3 * 4;
    ImGui::SameLine (std::max (ImGui::GetCursorPosX() + 10, contentRight() - rightWidth));
    ImGui::SetCursorPosY (ImGui::GetCursorPosY() - 3);
    if (ImGui::Button ("Default scene", { 104, 0 })) loadDefaults();
    markItem ("defaults");
    tooltip ("fetal ECG extraction: maternal 75 bpm, fetal 140 bpm, a 4-tap abdominal path, an 8-tap sign-sign LMS filter");
    ImGui::SameLine (0, 4);
    if (button ("Chip scene", argb (0xff7a5a1e), edit.filter.isChip(), { 84, 0 })) loadChip();
    markItem ("chipScene");
    tooltip ("the default scene on the chip's own arithmetic (tt_um_adaptive_filter: 4-tap sign-sign LMS, Q1.7 / Q2.12, bit-exact "
             "with the RTL); C switches any scene between float and chip");
    ImGui::SameLine (0, 4);
    if (ImGui::Button ("Save...", { 66, 0 }))
    {
        fileDialog.show ("Save settings", FileDialog::save, lastFolder, "scene.afx.json", ".afx.json");
        pending = Pending::save;
    }
    ImGui::SameLine (0, 4);
    if (ImGui::Button ("Open...", { 66, 0 }))
    {
        fileDialog.show ("Open settings", FileDialog::open, lastFolder, "", ".json");
        pending = Pending::load;
    }
    ImGui::Dummy ({ 0, 1 });
}

void App::dialogs()
{
    std::string path;
    if (fileDialog.draw (path))
    {
        if (pending == Pending::save) saveProjectTo (path);
        else if (pending == Pending::load) openProject (path);
        pending = Pending::none;
    }
}

void App::shortcuts()
{
    const auto& io = ImGui::GetIO();
    if (io.WantTextInput || fileDialog.isOpen()) return;
    if (io.KeyCtrl)
    {
        if (ImGui::IsKeyPressed (ImGuiKey_S, false))
        {
            fileDialog.show ("Save settings", FileDialog::save, lastFolder, "scene.afx.json", ".afx.json");
            pending = Pending::save;
        }
        if (ImGui::IsKeyPressed (ImGuiKey_O, false))
        {
            fileDialog.show ("Open settings", FileDialog::open, lastFolder, "", ".json");
            pending = Pending::load;
        }
        return;
    }
    if (ImGui::IsKeyPressed (ImGuiKey_Space, false)) running = ! running;
    if (ImGui::IsKeyPressed (ImGuiKey_Period) || (! anyTuneFocused() && ImGui::IsKeyPressed (ImGuiKey_RightArrow))) step (stepSamples[stepSize]);
    if (ImGui::IsKeyPressed (ImGuiKey_R, false)) restart();
    if (ImGui::IsKeyPressed (ImGuiKey_W, false)) sim.resetWeights();
    if (ImGui::IsKeyPressed (ImGuiKey_F, false)) edit.filter.freeze = ! edit.filter.freeze;
    if (ImGui::IsKeyPressed (ImGuiKey_C, false))
        edit.filter.arithmetic = edit.filter.isChip() ? Arithmetic::floating : Arithmetic::chip;
    if (ImGui::IsKeyPressed (ImGuiKey_L, false)) setListening (! listen);
}

// ================================================================ the traces
void App::traces (ImVec2 size)
{
    auto* dl = ImGui::GetWindowDrawList();
    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    markPoint ("view.traces", p0);
    markPoint ("view.traces.end", { p0.x + size.x, p0.y + size.y });
    ImGui::Dummy ({ size.x, 26 });
    drawText (dl, fonts().bold, { p0.x + 4, p0.y + 4 }, textLight, "Signals");
    drawText (dl, fonts().small, { p0.x + 72, p0.y + 6 }, textGrey, "the newest at the right; e is cleaned, and s is what it should be");
    ImGui::SetCursorScreenPos ({ p0.x + size.x - 230, p0.y + 1 });
    int span = view.span <= 2.0 ? 0 : view.span <= 4.0 ? 1 : view.span <= 8.0 ? 2 : 3;
    if (segmented ("span", span, { "2 s", "4 s", "8 s", "16 s" }, darker (accent, 1.2f), 230))
        view.span = span == 0 ? 2.0 : span == 1 ? 4.0 : span == 2 ? 8.0 : 16.0;
    ImGui::SetCursorScreenPos ({ p0.x, p0.y + 28 });

    struct Row
    {
        const char* name;
        const char* what;
        Channel channel;
        ImU32 colour;
    };
    const Row rows[4] = { { "d", "primary: s + Hp·m + nₚ", Channel::primary, colourPrimary },
                          { "x", "reference: Hr·m + nᵣ", Channel::reference, colourReference },
                          { "y", "filter output: wᵀxₙ", Channel::output, colourOutput },
                          { "e", "error d − y, over the true s", Channel::error, colourError } };
    const size_t spanSamples = (size_t) (view.span * sampleRate);
    const float axisH = 16.0f, rowGap = 4.0f;
    const float rowH = std::max (40.0f, (size.y - 28 - axisH - 3 * rowGap) / 4.0f);
    const double now = sim.time(), t0 = now - view.span;
    const ImVec2 base = ImGui::GetCursorScreenPos();
    const float labelW = 34.0f;
    for (int r = 0; r < 4; ++r)
    {
        const ImVec2 a { base.x, base.y + (float) r * (rowH + rowGap) }, b { base.x + size.x, a.y + rowH };
        const ImVec2 ia { a.x + labelW, a.y }, ib { b.x, b.y };
        dl->AddRectFilled (a, b, plotBack, 4.0f);
        markPoint (std::string ("view.trace.") + rows[r].name, a);
        markPoint (std::string ("view.trace.") + rows[r].name + ".end", b);
        // seconds
        for (double t = std::ceil (std::max (0.0, t0)); t <= now; t += view.span > 8.0 ? 2.0 : 1.0)
        {
            const float gx = ib.x - (float) ((now - t) / view.span) * (ib.x - ia.x);
            dl->AddLine ({ gx, ia.y }, { gx, ib.y }, gridLine);
        }
        dl->AddLine ({ ia.x, 0.5f * (ia.y + ib.y) }, { ib.x, 0.5f * (ia.y + ib.y) }, IM_COL32 (255, 255, 255, 30));
        sim.history (rows[r].channel).last (spanSamples, scratch);
        float peak = peakOf (scratch);
        if (r == 3)
        {
            sim.history (Channel::signal).last (spanSamples, scratch2);
            peak = std::max (peak, peakOf (scratch2));
        }
        follow (scales[r], peak);
        const float scale = niceAbove (scales[r]);
        if (r == 3) drawTrace (dl, ia, ib, scratch2, spanSamples, scale, withAlpha (colourSignal, 0.85f), 2.2f);
        drawTrace (dl, ia, ib, scratch, spanSamples, scale, rows[r].colour, 1.2f);
        drawText (dl, fonts().title, { a.x + 10, a.y + 0.5f * rowH - 10 }, rows[r].colour, rows[r].name);
        std::string label = rows[r].what;
        drawText (dl, fonts().small, { ia.x + 6, a.y + 3 }, textGrey, label);
        if (r == 3)
        {
            const float w = textSize (fonts().small, label).x;
            dl->AddLine ({ ia.x + 14 + w, a.y + 10 }, { ia.x + 30 + w, a.y + 10 }, colourSignal, 2.2f);
            drawText (dl, fonts().small, { ia.x + 34 + w, a.y + 3 }, colourSignal, "s (true)");
        }
        const std::string sc = "±" + (scale >= 0.1f ? fmt ("%.2g mV", scale) : fmt ("%.2g µV", scale * 1000.0f));
        drawText (dl, fonts().tiny, { ib.x - 6 - textSize (fonts().tiny, sc).x, a.y + 3 }, textGrey, sc);
    }
    // the time axis
    const float ay = base.y + 4 * rowH + 3 * rowGap + 2;
    for (double t = std::ceil (std::max (0.0, t0)); t <= now; t += view.span > 8.0 ? 2.0 : 1.0)
    {
        const float gx = base.x + size.x - (float) ((now - t) / view.span) * (size.x - labelW);
        const std::string s = fmt ("%.0f s", t);
        drawText (dl, fonts().tiny, { gx - 0.5f * textSize (fonts().tiny, s).x, ay }, textGrey, s);
    }
    ImGui::SetCursorScreenPos ({ base.x, base.y + size.y - 28 });
    ImGui::Dummy ({ size.x, 1 });
}

// ================================================================ the learning curve
void App::learning (ImVec2 size)
{
    const auto f = plotFrame ("learning", size, "Learning curve", 40.0f, 18.0f, 64.0f);
    markPoint ("view.learning", f.a);
    markPoint ("view.learning.end", f.b);
    auto* dl = ImGui::GetWindowDrawList();
    const double gain = sim.improvement();
    const std::string big = "SNR improvement " + dbText (gain);
    drawText (dl, fonts().title, { f.b.x - 10 - textSize (fonts().title, big).x, f.a.y + 2 }, accent, big);
    const double wienerGain = sim.mseIn() > 0 && sim.wienerMse() > 0 ? db (sim.mseIn()) - db (sim.wienerMse()) : 0.0;
    std::string line = "SNR " + dbText (sim.snrIn(), "%.1f dB") + " in d → " + dbText (sim.snrOut(), "%.1f dB") + " in e;   the Wiener limit "
                       + dbText (wienerGain);
    if (sim.hasTwin()) line = "the chip (Q1.7 / Q2.12) " + dbText (sim.improvement()) + ",  its float twin " + dbText (sim.improvementTwin())
                              + ";   the Wiener limit " + dbText (wienerGain);
    drawText (dl, fonts().small, { f.a.x + 8, f.a.y + 23 }, textGrey, line);

    const auto& pts = sim.curve();
    const double wienerDb = db (sim.wienerMse());
    const double tMax = std::max (10.0, pts.empty() ? 0.0 : (double) pts.back().time);
    float lo = 1e9f, hi = -1e9f;
    for (const auto& p : pts)
    {
        lo = std::min (lo, (float) db (p.mseOut));
        if (p.mseTwin > 0.0f) lo = std::min (lo, (float) db (p.mseTwin));
        hi = std::max ({ hi, (float) db (p.mseIn), (float) db (p.mseOut) });
    }
    if (sim.wienerTime() >= 0.0) lo = std::min (lo, (float) wienerDb);
    if (pts.empty())
    {
        lo = -60.0f;
        hi = 0.0f;
    }
    lo = 10.0f * std::floor ((lo - 2.0f) / 10.0f);
    hi = 10.0f * std::ceil ((hi + 2.0f) / 10.0f);
    lo = std::max (lo, hi - 120.0f);
    auto xOf = [&] (double t) { return f.ia.x + (float) (t / tMax) * (f.ib.x - f.ia.x); };
    auto yOf = [&] (double v) { return std::clamp (f.ia.y + (float) ((hi - v) / (hi - lo)) * (f.ib.y - f.ia.y), f.ia.y, f.ib.y); };
    for (float v = lo; v <= hi + 0.1f; v += 10.0f)
    {
        dl->AddLine ({ f.ia.x, yOf (v) }, { f.ib.x, yOf (v) }, gridLine);
        const std::string s = fmt ("%.0f", v);
        drawText (dl, fonts().tiny, { f.ia.x - 6 - textSize (fonts().tiny, s).x, yOf (v) - 6 }, textGrey, s);
    }
    const double tStep = gridStep (tMax, 6);
    for (double t = 0.0; t <= tMax + 1e-9; t += tStep)
    {
        dl->AddLine ({ xOf (t), f.ia.y }, { xOf (t), f.ib.y }, gridLine);
        const std::string s = fmt ("%.0f s", t);
        drawText (dl, fonts().tiny, { xOf (t) - 0.5f * textSize (fonts().tiny, s).x, f.ib.y + 3 }, textGrey, s);
    }
    drawText (dl, fonts().tiny, { f.a.x + 112, f.a.y + 7 }, textGrey, "dB re 1 mV²");
    std::vector<ImVec2> in, out, twin;
    in.reserve (pts.size());
    out.reserve (pts.size());
    for (const auto& p : pts)
    {
        in.push_back ({ xOf (p.time), yOf (db (p.mseIn)) });
        out.push_back ({ xOf (p.time), yOf (db (p.mseOut)) });
        if (p.mseTwin > 0.0f) twin.push_back ({ xOf (p.time), yOf (db (p.mseTwin)) });
    }
    dl->PushClipRect (f.ia, f.ib, true);
    if (sim.wienerTime() >= 0.0) dashedLine (dl, { f.ia.x, yOf (wienerDb) }, { f.ib.x, yOf (wienerDb) }, colourWiener, 1.4f);
    dl->AddPolyline (in.data(), (int) in.size(), withAlpha (colourPrimary, 0.8f), 0, 1.4f);
    if (sim.hasTwin()) dl->AddPolyline (twin.data(), (int) twin.size(), withAlpha (colourTruth, 0.75f), 0, 1.4f);
    dl->AddPolyline (out.data(), (int) out.size(), colourError, 0, 1.8f);
    dl->PopClipRect();
    // the legend, under the numbers
    float lx = f.a.x + 8;
    const float ly = f.a.y + 40;
    auto key = [&] (ImU32 c, const char* text, bool dashed) {
        if (dashed) dashedLine (dl, { lx, ly + 7 }, { lx + 18, ly + 7 }, c, 1.4f, 3.0f);
        else dl->AddLine ({ lx, ly + 7 }, { lx + 18, ly + 7 }, c, 1.8f);
        drawText (dl, fonts().tiny, { lx + 22, ly }, textLight, text);
        lx += 30 + textSize (fonts().tiny, text).x;
    };
    key (colourPrimary, "interference in d: (d − s)²", false);
    key (colourError, sim.hasTwin() ? "left in e, the chip: (e − s)²" : "left in e: (e − s)²", false);
    if (sim.hasTwin()) key (withAlpha (colourTruth, 0.75f), "float twin", false);
    key (colourWiener, "Wiener", true);
}

// ================================================================ the weights
void App::weightsView (ImVec2 size)
{
    const auto f = plotFrame ("weights", size, "Tap weights", 40.0f, 16.0f, 24.0f);
    markPoint ("view.weights", f.a);
    markPoint ("view.weights.end", f.b);
    auto* dl = ImGui::GetWindowDrawList();
    const auto& w = sim.filter().weights();
    const auto& truth = sim.truth();
    const auto& wiener = sim.wienerTaps();
    const int n = (int) w.size();
    {
        // the legend
        float lx = f.a.x + 112;
        const float ly = f.a.y + 6;
        dl->AddRectFilled ({ lx, ly + 2 }, { lx + 10, ly + 12 }, accent);
        drawText (dl, fonts().small, { lx + 14, ly }, textLight, "w");
        lx += 34;
        dl->AddLine ({ lx, ly + 7 }, { lx + 14, ly + 7 }, colourTruth, 2.0f);
        drawText (dl, fonts().small, { lx + 18, ly }, textLight, "true Hp/Hr");
        lx += 96;
        dl->AddCircleFilled ({ lx + 5, ly + 7 }, 3.5f, colourWiener);
        drawText (dl, fonts().small, { lx + 14, ly }, textLight, "Wiener");
        std::string need;
        if (! truth.exists) need = "Hr is zero";
        else if (! truth.causal) need = "Hp/Hr is not causal";
        else if (! truth.stable) need = "Hp/Hr goes on for ever";
        else need = "Hp/Hr needs " + std::to_string (truth.needed) + (truth.needed == 1 ? " tap" : " taps");
        const ImU32 c = (! truth.exists || ! truth.causal || ! truth.stable || truth.needed > n) ? argb (0xffff7060) : textGrey;
        drawText (dl, fonts().small, { f.b.x - 10 - textSize (fonts().small, need).x, ly }, c, need);
    }
    // the bars, above; the trajectories, below
    const float split = f.ia.y + 0.48f * (f.ib.y - f.ia.y);
    const ImVec2 ba = f.ia, bb { f.ib.x, split - 14 };
    const ImVec2 ta { f.ia.x, split + 8 }, tb = f.ib;
    float range = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        range = std::max (range, (float) std::abs (w[(size_t) i]));
        if ((size_t) i < truth.taps.size() && truth.exists && truth.stable) range = std::max (range, (float) std::abs (truth.taps[(size_t) i]));
    }
    // The Wiener solution can be wild where the input is weak (it is clipped at the edge then).
    for (size_t i = 0; i < wiener.size() && i < (size_t) n; ++i)
        if (std::abs (wiener[i]) < 2.0 * std::max (range, 0.25f)) range = std::max (range, (float) std::abs (wiener[i]));
    range = niceAbove (std::max (range * 1.05f, 0.1f));
    auto yBar = [&] (double v) { return std::clamp (0.5f * (ba.y + bb.y) - (float) (v / range) * 0.5f * (bb.y - ba.y), ba.y, bb.y); };
    for (const double v : { -(double) range, -0.5 * range, 0.0, 0.5 * range, (double) range })
    {
        dl->AddLine ({ ba.x, yBar (v) }, { bb.x, yBar (v) }, v == 0.0 ? IM_COL32 (255, 255, 255, 50) : gridLine);
        const std::string s = fmt ("%.2g", v);
        drawText (dl, fonts().tiny, { ba.x - 6 - textSize (fonts().tiny, s).x, yBar (v) - 6 }, textGrey, s);
    }
    const float slot = (bb.x - ba.x) / (float) std::max (1, n);
    for (int i = 0; i < n; ++i)
    {
        const float x0 = ba.x + slot * (float) i, cx = x0 + 0.5f * slot, half = std::max (1.0f, 0.32f * slot);
        const float y0 = yBar (0.0), y1 = yBar (w[(size_t) i]);
        dl->AddRectFilled ({ cx - half, std::min (y0, y1) }, { cx + half, std::max (y0, y1) + 0.5f }, withAlpha (accent, 0.85f), 1.5f);
        if ((size_t) i < truth.taps.size() && truth.exists && truth.stable)
            dl->AddLine ({ cx - half - 2, yBar (truth.taps[(size_t) i]) }, { cx + half + 2, yBar (truth.taps[(size_t) i]) }, colourTruth, 2.0f);
        if ((size_t) i < wiener.size()) dl->AddCircleFilled ({ cx, yBar (wiener[(size_t) i]) }, std::min (3.5f, 0.3f * slot + 1.0f), colourWiener);
        if (n <= 16 || i % 8 == 0)
        {
            const std::string s = std::to_string (i);
            drawText (dl, fonts().tiny, { cx - 0.5f * textSize (fonts().tiny, s).x, bb.y + 1 }, textGrey, s);
        }
    }
    // The trajectories: each tap's weight since the start.
    const auto& pts = sim.curve();
    const double tMax = std::max (10.0, pts.empty() ? 0.0 : (double) pts.back().time);
    float trange = 0.0f;
    for (const auto& p : pts)
        for (int i = 0; i < n; ++i) trange = std::max (trange, std::abs (p.w[(size_t) i]));
    trange = niceAbove (std::max (trange * 1.05f, 0.05f));
    auto xOf = [&] (double t) { return ta.x + (float) (t / tMax) * (tb.x - ta.x); };
    auto yT = [&] (double v) { return std::clamp (0.5f * (ta.y + tb.y) - (float) (v / trange) * 0.5f * (tb.y - ta.y), ta.y, tb.y); };
    dl->AddRectFilled ({ ta.x, ta.y }, tb, argb (0xff0e0e17));
    for (const double v : { -(double) trange, 0.0, (double) trange })
    {
        dl->AddLine ({ ta.x, yT (v) }, { tb.x, yT (v) }, v == 0.0 ? IM_COL32 (255, 255, 255, 40) : gridLine);
        const std::string s = fmt ("%.2g", v);
        drawText (dl, fonts().tiny, { ta.x - 6 - textSize (fonts().tiny, s).x, yT (v) - 6 }, textGrey, s);
    }
    const double tStep = gridStep (tMax, 6);
    for (double t = 0.0; t <= tMax + 1e-9; t += tStep)
    {
        dl->AddLine ({ xOf (t), ta.y }, { xOf (t), tb.y }, gridLine);
        const std::string s = fmt ("%.0f s", t);
        drawText (dl, fonts().tiny, { xOf (t) - 0.5f * textSize (fonts().tiny, s).x, tb.y + 2 }, textGrey, s);
    }
    drawText (dl, fonts().tiny, { ta.x + 4, ta.y + 2 }, textGrey, "each weight since the start");
    dl->PushClipRect (ta, tb, true);
    std::vector<ImVec2> line;
    for (int i = 0; i < n; ++i)
    {
        line.clear();
        for (const auto& p : pts) line.push_back ({ xOf (p.time), yT (p.w[(size_t) i]) });
        dl->AddPolyline (line.data(), (int) line.size(), hsv (0.08f + 0.85f * (float) i / (float) std::max (1, n), 0.55f, 1.0f, 0.9f), 0, 1.2f);
    }
    dl->PopClipRect();
}

// ================================================================ the magnitude response
void App::responseView (ImVec2 size)
{
    const auto f = plotFrame ("response", size, "Magnitude response (dB)", 40.0f, 18.0f, 30.0f);
    markPoint ("view.response", f.a);
    markPoint ("view.response.end", f.b);
    auto* dl = ImGui::GetWindowDrawList();
    const double nyquist = 0.5 * sampleRate;
    const auto truth = truePath (edit.primaryPath, edit.referencePath, 512);
    const auto& w = sim.filter().weights();
    const auto& wiener = sim.wienerTaps();
    const int points = std::max (32, (int) (f.ib.x - f.ia.x));
    std::vector<double> magW (points), magT (points), magO (points);
    double top = -1e9;
    for (int i = 0; i < points; ++i)
    {
        const double fr = nyquist * i / (points - 1);
        magW[(size_t) i] = magnitudeDb (response (w, fr, sampleRate));
        magT[(size_t) i] = magnitudeDb (response (truth.taps, fr, sampleRate));
        magO[(size_t) i] = magnitudeDb (response (wiener, fr, sampleRate));
        top = std::max ({ top, magW[(size_t) i], truth.exists && truth.stable ? magT[(size_t) i] : -1e9, magO[(size_t) i] });
    }
    const float hi = (float) std::clamp (10.0 * std::ceil ((top + 3.0) / 10.0), 0.0, 60.0), lo = hi - 60.0f;
    auto xOf = [&] (double fr) { return f.ia.x + (float) (fr / nyquist) * (f.ib.x - f.ia.x); };
    auto yOf = [&] (double v) { return std::clamp (f.ia.y + (float) ((hi - v) / (hi - lo)) * (f.ib.y - f.ia.y), f.ia.y, f.ib.y); };
    // Behind: where the reference has its power (its spectrum, the top 60 dB of it).
    if (! xSpectrum.empty())
    {
        const float peak = *std::max_element (xSpectrum.begin(), xSpectrum.end());
        for (size_t k = 0; k + 1 < xSpectrum.size(); ++k)
        {
            const float level = std::clamp ((xSpectrum[k] - peak + 60.0f) / 60.0f, 0.0f, 1.0f);
            if (level <= 0.0f) continue;
            const float x0 = xOf ((double) k), x1 = xOf ((double) k + 1.0);
            dl->AddRectFilled ({ x0, f.ib.y - level * (f.ib.y - f.ia.y) }, { x1 + 0.5f, f.ib.y }, withAlpha (colourReference, 0.16f));
        }
    }
    for (float v = lo; v <= hi + 0.1f; v += 10.0f)
    {
        dl->AddLine ({ f.ia.x, yOf (v) }, { f.ib.x, yOf (v) }, gridLine);
        const std::string s = fmt ("%.0f", v);
        drawText (dl, fonts().tiny, { f.ia.x - 6 - textSize (fonts().tiny, s).x, yOf (v) - 6 }, textGrey, s);
    }
    for (double fr = 0.0; fr <= nyquist + 1e-9; fr += 50.0)
    {
        dl->AddLine ({ xOf (fr), f.ia.y }, { xOf (fr), f.ib.y }, gridLine);
        const std::string s = fmt ("%.0f Hz", fr);
        drawText (dl, fonts().tiny, { std::min (xOf (fr) - 0.5f * textSize (fonts().tiny, s).x, f.ib.x - textSize (fonts().tiny, s).x), f.ib.y + 3 }, textGrey,
                  s);
    }
    std::vector<ImVec2> pw, pt, po;
    for (int i = 0; i < points; ++i)
    {
        const double fr = nyquist * i / (points - 1);
        pw.push_back ({ xOf (fr), yOf (magW[(size_t) i]) });
        pt.push_back ({ xOf (fr), yOf (magT[(size_t) i]) });
        po.push_back ({ xOf (fr), yOf (magO[(size_t) i]) });
    }
    dl->PushClipRect (f.ia, f.ib, true);
    if (truth.exists) dashedPolyline (dl, pt, colourTruth, 1.6f, 5.0f);
    dl->AddPolyline (po.data(), (int) po.size(), colourWiener, 0, 1.5f);
    dl->AddPolyline (pw.data(), (int) pw.size(), colourError, 0, 2.0f);
    dl->PopClipRect();
    // the legend
    float lx = f.a.x + 196;
    const float ly = f.a.y + 6;
    auto key = [&] (ImU32 c, const char* text, bool dashed) {
        if (dashed) dashedLine (dl, { lx, ly + 7 }, { lx + 18, ly + 7 }, c, 1.6f, 4.0f);
        else dl->AddLine ({ lx, ly + 7 }, { lx + 18, ly + 7 }, c, 2.0f);
        drawText (dl, fonts().small, { lx + 22, ly }, textLight, text);
        lx += 32 + textSize (fonts().small, text).x;
    };
    key (colourError, "w", false);
    key (colourTruth, "Hp/Hr", true);
    key (colourWiener, "Wiener", false);
    dl->AddRectFilled ({ lx, ly + 2 }, { lx + 12, ly + 12 }, withAlpha (colourReference, 0.35f));
    drawText (dl, fonts().small, { lx + 16, ly }, textLight, "x's spectrum");
}
} // namespace afx
