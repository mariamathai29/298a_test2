// The inspector: the selected block's settings and what it is doing.

#include "Ui.h"

#include "Plots.h"
#include "Tune.h"

#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>

namespace afx
{
namespace
{
TuneOptions opts (double lo, double hi, double def, int decimals, ImU32 colour, const char* tip = nullptr, bool log = false,
                  const char* suffix = "")
{
    TuneOptions o;
    o.lo = lo;
    o.hi = hi;
    o.defaultValue = def;
    o.decimals = decimals;
    o.accent = colour;
    o.tip = tip;
    o.log = log;
    o.suffix = suffix;
    o.nameWidth = 96.0f;
    return o;
}

double rms (const std::vector<float>& x)
{
    double s = 0.0;
    for (float v : x) s += (double) v * v;
    return x.empty() ? 0.0 : std::sqrt (s / (double) x.size());
}

std::string level (double v)
{
    char buf[48];
    if (v < 0.1) std::snprintf (buf, sizeof buf, "%.3g µV", 1000.0 * v);
    else std::snprintf (buf, sizeof buf, "%.3g mV", v);
    return buf;
}

double db (double power) { return 10.0 * std::log10 (std::max (power, 1e-30)); }

std::string fmtText (const char* f, ...)
{
    char buf[256];
    va_list a;
    va_start (a, f);
    std::vsnprintf (buf, sizeof buf, f, a);
    va_end (a);
    return buf;
}

void textLine (const std::string& s, ImU32 c = textLight)
{
    ImGui::PushStyleColor (ImGuiCol_Text, c);
    ImGui::TextUnformatted (s.c_str());
    ImGui::PopStyleColor();
}

void equation (const char* s)
{
    ImGui::PushFont (fonts().mono);
    ImGui::PushStyleColor (ImGuiCol_Text, argb (0xffcfe3ff));
    ImGui::TextWrapped ("%s", s);
    ImGui::PopStyleColor();
    ImGui::PopFont();
}

// A little live trace of a channel, under the settings.
void miniScope (const Simulator& sim, Channel c, ImU32 colour, const char* title, float height = 110.0f)
{
    static std::vector<float> data;
    sim.history (c).last ((size_t) (4.0 * sampleRate), data);
    const ImVec2 a = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    ImGui::Dummy ({ w, height });
    auto* dl = ImGui::GetWindowDrawList();
    const ImVec2 b { a.x + w, a.y + height };
    dl->AddRectFilled (a, b, plotBack, 4.0f);
    const float scale = niceAbove (std::max (peakOf (data), 1e-5f));
    dl->AddLine ({ a.x, 0.5f * (a.y + b.y) }, { b.x, 0.5f * (a.y + b.y) }, IM_COL32 (255, 255, 255, 30));
    drawTrace (dl, { a.x + 2, a.y + 16 }, { b.x - 2, b.y - 2 }, data, (size_t) (4.0 * sampleRate), scale, colour, 1.2f);
    drawText (dl, fonts().small, { a.x + 6, a.y + 2 }, textGrey, title);
    const std::string s = "±" + level (scale) + "   RMS " + level (rms (data));
    drawText (dl, fonts().tiny, { b.x - 6 - textSize (fonts().tiny, s).x, a.y + 3 }, textGrey, s);
}

const char* sourceTip (SourceKind k)
{
    switch (k)
    {
        case SourceKind::none: return "nothing: 0";
        case SourceKind::maternalEcg:
            return "a synthetic maternal ECG: P, Q, R, S and T waves as Gaussian bumps on the beat's phase (after McSharry's ECGSYN), "
                   "about 75 bpm, the R wave about 1 mV, the heart rate swaying with breathing and the slower Mayer waves.";
        case SourceKind::fetalEcg:
            return "a synthetic fetal ECG: the same model, faster (about 140 bpm), smaller (0.1 to 0.3 of the maternal), "
                   "with smaller P and T waves and its own variability.";
        case SourceKind::sine: return "a sine wave";
        case SourceKind::square: return "a square wave (rich in odd harmonics)";
        case SourceKind::chirp: return "a sine whose frequency sweeps linearly from one to the other, over and over";
        case SourceKind::white: return "white Gaussian noise: equal power at every frequency (the level is the RMS)";
        case SourceKind::pink: return "pink noise: equal power in every octave, more at low frequencies (the level is the RMS)";
        case SourceKind::hum: return "power-line interference: 50 or 60 Hz and its third harmonic";
        case SourceKind::wander: return "baseline wander: the slow sway of breathing and movement, below 1 Hz";
        case SourceKind::emg: return "EMG-like noise: muscle activity, 20 to 150 Hz, in bursts (the level is the RMS in a burst)";
    }
    return "";
}
} // namespace

void App::inspector()
{
    ImGui::PushItemWidth (-1);
    switch (selection)
    {
        case Block::none:
        {
            header ("Adaptive filter explorer");
            ImGui::PushTextWrapPos (0);
            ImGui::TextUnformatted ("An adaptive filter cancelling interference: the classic case is taking the mother's heartbeat out of an "
                                    "abdominal recording, to find the baby's.");
            ImGui::Spacing();
            ImGui::TextUnformatted ("The primary d (the abdomen) has the signal of interest s and the interference m through a path Hp. The "
                                    "reference x (the chest) has m through its own path Hr, and none of s. The filter w learns to make x "
                                    "look like the interference in d; what is left, e = d − y, is s.");
            ImGui::Spacing();
            ImGui::TextUnformatted ("Click a block on the diagram to edit it. Space runs and pauses; . steps; R restarts; W resets the weights; F "
                                    "freezes them.");
            ImGui::PopTextWrapPos();
            break;
        }
        case Block::signal:
            header ("Signal of interest s");
            helpText ("what the canceller should leave behind: it goes straight into the primary (the abdomen), and not into the reference.");
            sourceEditor (edit.signal, "signal", true);
            miniScope (sim, Channel::signal, colourSignal, "s, the last 4 s");
            break;
        case Block::interference:
            header ("Interference m");
            helpText ("what the canceller should take out: it reaches the primary through Hp and the reference through Hr.");
            sourceEditor (edit.interference, "interference", true);
            miniScope (sim, Channel::interference, colourInterference, "m, the last 4 s");
            break;
        case Block::primaryNoise:
            header ("Primary noise nₚ");
            helpText ("noise of the primary's own (the abdominal electrodes): nothing in the reference matches it, so it stays in e.");
            sourceEditor (edit.primaryNoise, "primaryNoise", false);
            miniScope (sim, Channel::primaryNoise, colourNoise, "nₚ, the last 4 s");
            break;
        case Block::referenceNoise:
            header ("Reference noise nᵣ");
            helpText ("noise of the reference's own (the chest electrodes): the filter passes it on into e, so it limits how well the "
                      "interference can be cancelled. A whisper of it (the default 2 µV) also keeps the problem well posed.");
            sourceEditor (edit.referenceNoise, "referenceNoise", false);
            miniScope (sim, Channel::referenceNoise, colourNoise, "nᵣ, the last 4 s");
            break;
        case Block::primaryPath:
            header ("Primary path Hp");
            helpText ("how the interference reaches the primary (the mother's heart to the abdominal electrodes): an FIR filter. The adaptive "
                      "filter must become Hp/Hr: give it more taps than that needs, or it falls short.");
            pathEditor (edit.primaryPath, "primaryPath", true);
            break;
        case Block::referencePath:
            header ("Reference path Hr");
            helpText ("how the interference reaches the reference (the chest electrodes). Usually straight through. Delay it more than Hp and "
                      "Hp/Hr needs the future: a causal filter can't do it.");
            pathEditor (edit.referencePath, "referencePath", false);
            break;
        case Block::primarySum:
        {
            header ("Primary d");
            equation ("d = s + Hp·m + nₚ");
            helpText ("the abdominal lead: the signal of interest buried in the interference.");
            std::vector<float> d, s, np;
            sim.history (Channel::primary).last (2000, d);
            sim.history (Channel::signal).last (2000, s);
            sim.history (Channel::primaryNoise).last (2000, np);
            std::vector<float> inter (d.size());
            for (size_t i = 0; i < d.size() && i < s.size() && i < np.size(); ++i) inter[i] = d[i] - s[i] - np[i];
            ImGui::Spacing();
            textLine ("RMS over the last 4 s:", textGrey);
            textLine ("   s        " + level (rms (s)), colourSignal);
            textLine ("   Hp·m     " + level (rms (inter)), colourInterference);
            textLine ("   nₚ       " + level (rms (np)), colourNoise);
            textLine ("   d        " + level (rms (d)), colourPrimary);
            textLine ("SNR in d: " + dbText (db (rms (s) * rms (s)) - db (rms (inter) * rms (inter) + rms (np) * rms (np)), "%.1f dB"));
            ImGui::Spacing();
            miniScope (sim, Channel::primary, colourPrimary, "d, the last 4 s");
            break;
        }
        case Block::referenceSum:
        {
            header ("Reference x");
            equation ("x = Hr·m + nᵣ + c·s");
            helpText ("the chest lead: the interference, as like what is in d as can be, and (ideally) none of s.");
            ImGui::Spacing();
            tune ("scene.signalInReference", "s in x (c)", edit.signalInReference,
                  opts (0.0, 0.5, 0.0, 3, colourSignal,
                        "how much of the signal of interest leaks into the reference: the filter then cancels some of s too (it can't tell "
                        "s from m), and e loses it"));
            std::vector<float> x, nr;
            sim.history (Channel::reference).last (2000, x);
            sim.history (Channel::referenceNoise).last (2000, nr);
            ImGui::Spacing();
            textLine ("RMS over the last 4 s:", textGrey);
            textLine ("   nᵣ       " + level (rms (nr)), colourNoise);
            textLine ("   x        " + level (rms (x)), colourReference);
            ImGui::Spacing();
            miniScope (sim, Channel::reference, colourReference, "x, the last 4 s");
            break;
        }
        case Block::filter: filterEditor(); break;
        case Block::output:
        {
            header ("Error e: the cleaned signal");
            equation ("e = d − y = s + (Hp·m − y) + nₚ");
            helpText ("when w has learnt the path, y is the interference in d and e is s (with the primary's own noise). The learning curve "
                      "shows (e − s)², what is left of the interference.");
            ImGui::Spacing();
            textLine ("SNR in d          " + dbText (sim.snrIn(), "%.1f dB"), colourPrimary);
            textLine ("SNR in e          " + dbText (sim.snrOut(), "%.1f dB"), colourError);
            textLine ("improvement       " + dbText (sim.improvement()), accent);
            const double wienerGain = db (sim.mseIn()) - db (sim.wienerMse());
            textLine ("Wiener limit      " + dbText (wienerGain), colourWiener);
            textLine ("(e − s)² now      " + dbText (db (sim.mseOut()), "%.1f dB re 1 mV²"), textGrey);
            ImGui::Spacing();
            miniScope (sim, Channel::error, colourError, "e, the last 4 s");
            break;
        }
    }
    ImGui::PopItemWidth();
}

void App::sourceEditor (SourceSpec& s, const char* id, bool canSwap)
{
    ImGui::PushID (id);
    const std::string key = std::string ("source.") + id + ".";
    ImGui::Spacing();
    ImGui::SetNextItemWidth (canSwap ? -120 : -1);
    if (ImGui::BeginCombo ("##kind", sourceKindName (s.kind)))
    {
        for (int k = 0; k < numSourceKinds; ++k)
        {
            const auto kind = (SourceKind) k;
            if (ImGui::Selectable (sourceKindName (kind), kind == s.kind) && kind != s.kind)
            {
                const int seed = s.seed;
                s = SourceSpec::defaults (kind);
                if (! isEcg (kind)) s.seed = seed;
            }
            markItem (key + "kind." + sourceKindId (kind));
            tooltip (sourceTip (kind));
        }
        ImGui::EndCombo();
    }
    markItem (key + "kind");
    tooltip (sourceTip (s.kind));
    if (canSwap)
    {
        ImGui::SameLine (0, 4);
        if (ImGui::Button ("Swap s and m", { -1, 0 }))
        {
            std::swap (edit.signal, edit.interference);
            status = "Swapped: " + std::string (sourceKindName (edit.signal.kind)) + " is now the signal of interest, "
                     + sourceKindName (edit.interference.kind) + " the interference.";
        }
        tooltip ("make the interference the signal of interest, and the other way round");
    }
    ImGui::Spacing();
    const ImU32 c = canSwap ? (id[0] == 's' ? colourSignal : colourInterference) : colourNoise;
    auto k = [&] (const char* name) { return key + name; };
    const bool noise = s.kind == SourceKind::white || s.kind == SourceKind::pink || s.kind == SourceKind::emg;
    if (s.kind != SourceKind::none)
    {
        if (noise)
            tune (k ("amplitude").c_str(), s.kind == SourceKind::emg ? "RMS in a burst" : "RMS", s.amplitude,
                  opts (1e-4, 1.0, SourceSpec::defaults (s.kind).amplitude, 4, c, "the level (mV RMS)", true, " mV"));
        else
            tune (k ("amplitude").c_str(), isEcg (s.kind) ? "R wave" : "amplitude", s.amplitude,
                  opts (-2.0, 2.0, SourceSpec::defaults (s.kind).amplitude, 3, c,
                        isEcg (s.kind) ? "the R wave's height (mV); negative turns the complex upside down, as some leads see it"
                                       : "the peak (mV)",
                        false, " mV"));
    }
    switch (s.kind)
    {
        case SourceKind::maternalEcg:
        case SourceKind::fetalEcg:
            tune (k ("bpm").c_str(), "heart rate", s.bpm, opts (30.0, 240.0, SourceSpec::defaults (s.kind).bpm, 1, c, "the mean heart rate", false, " bpm"));
            {
                double pc = 100.0 * s.hrv;
                if (tune (k ("hrv").c_str(), "variability", pc,
                          opts (0.0, 20.0, 100.0 * SourceSpec::defaults (s.kind).hrv, 1, c,
                                "heart-rate variability: how far the RR interval swings with breathing, the slower Mayer waves and from beat to beat",
                                false, " %")))
                    s.hrv = pc / 100.0;
            }
            tune (k ("breathing").c_str(), "breathing", s.breathing,
                  opts (0.05, 2.0, SourceSpec::defaults (s.kind).breathing, 2, c, "the breathing rate that sways the heart rate (respiratory sinus arrhythmia)",
                        false, " Hz"));
            tune (k ("highPass").c_str(), "high-pass", s.highPass,
                  opts (0.0, 5.0, 0.5, 2, c,
                        "AC coupling, as an ECG amplifier records: takes out the ECG's mean (0: DC coupled). Without it every sample of "
                        "the reference has the same sign between beats, and the sign-data algorithms learn slowly",
                        false, " Hz"));
            break;
        case SourceKind::sine:
        case SourceKind::square:
            tune (k ("frequency").c_str(), "frequency", s.frequency, opts (0.1, 0.5 * sampleRate, 10.0, 2, c, nullptr, true, " Hz"));
            break;
        case SourceKind::chirp:
            tune (k ("frequency").c_str(), "from", s.frequency, opts (0.1, 0.5 * sampleRate, 1.0, 2, c, "where the sweep starts", true, " Hz"));
            tune (k ("frequency2").c_str(), "to", s.frequency2, opts (0.1, 0.5 * sampleRate, 100.0, 2, c, "where it ends", true, " Hz"));
            tune (k ("sweep").c_str(), "sweep time", s.sweep, opts (0.1, 60.0, 4.0, 2, c, "seconds for one sweep", true, " s"));
            break;
        case SourceKind::hum:
        {
            int mains = s.mains == 50 ? 0 : 1;
            ImGui::TextUnformatted ("mains");
            ImGui::SameLine (96);
            if (segmented ("mains", mains, { "50 Hz", "60 Hz" }, darker (c, 1.0f), -1.0f, { "Europe, Asia, Africa ...", "North America ..." }))
                s.mains = mains == 0 ? 50 : 60;
            tune (k ("harmonics").c_str(), "3rd harmonic", s.harmonics, opts (0.0, 1.0, 0.3, 2, c, "the third harmonic, against the fundamental"));
            break;
        }
        case SourceKind::wander:
            tune (k ("frequency").c_str(), "breathing", s.frequency, opts (0.05, 2.0, 0.25, 2, c, "the main sway (Hz)", false, " Hz"));
            break;
        case SourceKind::emg:
            tune (k ("burstRate").c_str(), "bursts", s.burstRate, opts (0.0, 5.0, 0.5, 2, c, "bursts a second, at random", false, " /s"));
            tune (k ("burstLength").c_str(), "burst length", s.burstLength, opts (0.05, 3.0, 0.4, 2, c, nullptr, false, " s"));
            break;
        default: break;
    }
    const bool seeded = isEcg (s.kind) || noise || s.kind == SourceKind::wander;
    if (seeded)
        tune (k ("seed").c_str(), "seed", s.seed, opts (0, 100, 1, 0, c, "the random numbers' seed: change it for another run of the same thing"));
    ImGui::Spacing();
    ImGui::PopID();
}

void App::pathEditor (PathSpec& p, const char* id, bool primary)
{
    ImGui::PushID (id);
    const std::string key = std::string ("path.") + id + ".";
    const ImU32 c = colourInterference;
    ImGui::Spacing();
    ImGui::TextUnformatted ("preset");
    ImGui::SameLine (96);
    ImGui::SetNextItemWidth (-1);
    if (ImGui::BeginCombo ("##preset", p.preset.c_str()))
    {
        for (const auto& q : pathPresets())
        {
            if (ImGui::Selectable (q.name, p.preset == q.name)) p = pathFromPreset (q.name);
            markItem (key + "preset." + q.name);
            tooltip (q.tip);
        }
        ImGui::EndCombo();
    }
    markItem (key + "preset");
    bool changed = false;
    changed |= tune ((key + "gain").c_str(), "gain", p.gain, opts (-2.0, 2.0, 1.0, 3, c, "scales every tap"));
    changed |= tune ((key + "delay").c_str(), "delay", p.delay, opts (0, maxPathDelay, 0, 0, c, "samples (2 ms each) before the first tap", false, " smp"));
    int count = (int) p.taps.size();
    if (tune ((key + "count").c_str(), "taps", count, opts (1, maxPathTaps, 1, 0, c, "how many taps the path has")))
    {
        p.taps.resize ((size_t) std::clamp (count, 1, maxPathTaps), 0.0);
        changed = true;
    }
    ImGui::Spacing();
    // the taps, two to a row
    const float half = 0.5f * (ImGui::GetContentRegionAvail().x - 6.0f);
    for (size_t i = 0; i < p.taps.size(); ++i)
    {
        if (i % 2 == 1) ImGui::SameLine (0, 6);
        char name[16], tid[64];
        std::snprintf (name, sizeof name, "h[%zu]", i + (size_t) p.delay);
        std::snprintf (tid, sizeof tid, "%stap%zu", key.c_str(), i);
        auto o = opts (-1.5, 1.5, 0.0, 3, c);
        o.nameWidth = 40.0f;
        o.rangeWidth = 0.0f;
        o.width = half;
        changed |= tune (tid, name, p.taps[i], o);
    }
    if (changed) p.preset = "custom";
    ImGui::Spacing();
    // what it does: the response, and what the filter must become
    const auto h = p.effective();
    {
        const ImVec2 a = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x, ht = 110.0f;
        ImGui::Dummy ({ w, ht });
        auto* dl = ImGui::GetWindowDrawList();
        const ImVec2 b { a.x + w, a.y + ht };
        dl->AddRectFilled (a, b, plotBack, 4.0f);
        drawText (dl, fonts().small, { a.x + 6, a.y + 2 }, textGrey, "|H|, 0 to 250 Hz");
        const ImVec2 ia { a.x + 30, a.y + 18 }, ib { b.x - 6, b.y - 14 };
        std::vector<ImVec2> pts;
        auto yOf = [&] (double v) { return std::clamp (ia.y + (float) ((10.0 - v) / 50.0) * (ib.y - ia.y), ia.y, ib.y); };
        for (double v : { 0.0, -20.0, -40.0 })
        {
            dl->AddLine ({ ia.x, yOf (v) }, { ib.x, yOf (v) }, gridLine);
            drawText (dl, fonts().tiny, { a.x + 4, yOf (v) - 6 }, textGrey, std::to_string ((int) v));
        }
        for (int i = 0; i <= (int) (ib.x - ia.x); ++i)
        {
            const double f = 0.5 * sampleRate * i / (ib.x - ia.x);
            pts.push_back ({ ia.x + (float) i, yOf (magnitudeDb (response (h, f, sampleRate))) });
        }
        dl->AddPolyline (pts.data(), (int) pts.size(), c, 0, 1.6f);
        for (double f : { 50.0, 100.0, 150.0, 200.0 })
        {
            const float x = ia.x + (float) (f / (0.5 * sampleRate)) * (ib.x - ia.x);
            dl->AddLine ({ x, ia.y }, { x, ib.y }, gridLine);
            drawText (dl, fonts().tiny, { x - 8, ib.y + 1 }, textGrey, std::to_string ((int) f));
        }
    }
    ImGui::Spacing();
    const auto t = truePath (edit.primaryPath, edit.referencePath);
    std::string what;
    ImU32 colour = textLight;
    if (! t.exists)
    {
        what = "Hr is all zero: the reference has no interference in it, so there is nothing to learn from.";
        colour = argb (0xffff7060);
    }
    else if (! t.causal)
    {
        what = "Hr is later than Hp: Hp/Hr needs samples from the future, which no causal filter has. Cancelling stays poor.";
        colour = argb (0xffff7060);
    }
    else if (! t.stable)
    {
        what = "Hr has zeros outside the unit circle: its inverse grows for ever, so Hp/Hr can only be approximated.";
        colour = argb (0xffff7060);
    }
    else
    {
        char buf[200];
        std::snprintf (buf, sizeof buf, "Hp/Hr needs %d tap%s; the filter has %d.%s", t.needed, t.needed == 1 ? "" : "s", edit.filter.effectiveTaps(),
                       t.needed > edit.filter.effectiveTaps() ? " Too few: it can only approximate the path." : "");
        what = buf;
        if (t.needed > edit.filter.effectiveTaps()) colour = argb (0xffffc060);
    }
    ImGui::PushTextWrapPos (0);
    ImGui::PushStyleColor (ImGuiCol_Text, colour);
    ImGui::TextUnformatted (what.c_str());
    ImGui::PopStyleColor();
    ImGui::PopTextWrapPos();
    if (primary) helpText ("Only Hp/Hr below about 30 Hz matters with an ECG: it has almost no power above that, so the filter neither needs "
                           "nor learns the path's response up there (see the magnitude response).");
    ImGui::PopID();
}

void App::filterEditor()
{
    auto& f = edit.filter;
    const ImU32 c = colourOutput;
    header ("Adaptive filter w");
    equation ("y = wᵀxₙ,   e = d − y");
    helpText ("an FIR filter on the reference; its N weights follow the error. xₙ = (x[n], x[n−1], ..., x[n−N+1]).");
    ImGui::Spacing();
    int arith = f.isChip() ? 1 : 0;
    ImGui::TextUnformatted ("arithmetic");
    ImGui::SameLine (96);
    if (segmented ("arithmetic", arith, { "float", "chip" }, darker (accent, 1.0f), -1.0f,
                   { "double-precision floating point: any of the algorithms, any number of taps",
                     "the chip's own datapath, bit for bit: 4-tap sign-sign LMS, Q1.7 samples, Q2.12 weights (tt_um_adaptive_filter)" }))
    {
        f.arithmetic = arith == 1 ? Arithmetic::chip : Arithmetic::floating;
        status = f.isChip() ? "The filter now runs on the chip's arithmetic: 4 taps, Q1.7 / Q2.12, bit-exact with the RTL."
                            : "The filter now runs in floating point.";
    }
    markItem ("filter.arithmetic");
    ImGui::Spacing();
    if (f.isChip())
    {
        chipEditor();
        return;
    }
    int a = (int) f.algorithm;
    std::vector<std::string> labels;
    for (int i = 0; i < numAlgorithms; ++i) labels.push_back (algorithmShort ((Algorithm) i));
    if (segmented ("algorithm", a, labels, darker (c, 1.0f), -1.0f,
                   { "sign-sign LMS: only the signs of the error and the data (the cheapest; no multiplications)", "LMS: the steepest descent of the error's square, a step at a time",
                     "normalized LMS: the step divided by the input's energy, so it doesn't depend on the level",
                     "sign-error LMS: the sign of the error, the data itself", "sign-data LMS: the error itself, the sign of the data",
                     "recursive least squares: fast, whatever the input's spectrum, at N² work a sample" }))
        f.algorithm = (Algorithm) a;
    textLine (algorithmName (f.algorithm), accent);
    equation (algorithmUpdate (f.algorithm));
    ImGui::Spacing();
    tune ("filter.taps", "taps N", f.taps, opts (1, maxTaps, 8, 0, c, "how many weights: enough to hold Hp/Hr"));
    if (f.algorithm != Algorithm::rls)
    {
        const std::string id = std::string ("filter.mu.") + algorithmId (f.algorithm);
        const double def = FilterSpec::defaultMu (f.algorithm);
        tune (id.c_str(), "step size μ", f.mu[(size_t) f.algorithm],
              opts (def / 100.0, def * 100.0, def, 6, c, "bigger learns faster but settles less exactly (and too big runs away)", true));
        double leak = f.leakage;
        if (tune ("filter.leakage", "leakage", leak,
                  opts (0.0, 1e-3, 0.0, 6, c, "each step the weights shrink by this fraction first: w ← (1 − leak)·w; keeps them bounded when the input doesn't excite every tap")))
            f.leakage = std::max (0.0, leak);
        if (f.algorithm == Algorithm::nlms)
            tune ("filter.epsilon", "ε", f.epsilon,
                  opts (1e-4, 10.0, 1.0, 4, c, "added to xₙᵀxₙ (mV²): without it the quiet between heartbeats would make huge steps", true, " mV²"));
    }
    else
    {
        tune ("filter.lambda", "forgetting λ", f.lambda,
              opts (0.99, 1.0, 0.9999, 5, c, "how much of the past it keeps: a memory of about 1/(1 − λ) samples; smaller tracks changes faster"));
        tune ("filter.delta", "δ", f.delta, opts (1e-4, 10.0, 0.01, 4, c, "the start: P = I/δ (small: confident in nothing)", true));
    }
    ImGui::Spacing();
    ImGui::Checkbox ("Freeze the weights", &f.freeze);
    markItem ("filter.freeze");
    tooltip ("hold the weights: it still filters, but stops learning (F)");
    ImGui::SameLine();
    if (ImGui::Button ("Reset weights", { -1, 0 })) sim.resetWeights();
    tooltip ("the weights to 0 (and RLS starts afresh); the signals carry on (W)");
    weightTable();
}

void App::chipEditor()
{
    auto& f = edit.filter;
    const ImU32 c = colourOutput;
    textLine ("tt_um_adaptive_filter: 4-tap sign-sign LMS", accent);
    equation ("u = q₇(x / FS),  r = q₇(d / FS)\n"
              "y = sat₈((Σ u[n−k]·(w[k] >> 4) + 2⁷) >> 8)\n"
              "e = sat₈(r − y)\n"
              "w[k] ← sat₁₄(w[k] + 2^mu_sel·sign(r − y)·sign(u[n−k]))");
    helpText ("the chip's datapath, bit for bit: samples are 8-bit Q1.7, weights 14-bit Q2.12 registers of which the FIR uses the top 10 "
              "(Q2.8). x and d are quantized at the full scale FS; y and e come back from their codes. A floating-point twin (the same "
              "4-tap sign-sign LMS and μ) runs beside it, so the learning curve shows what the word lengths cost. Checked against the "
              "RTL and the routed netlist by app/tb/chip_tb.py.");
    ImGui::Spacing();
    tune ("filter.fullScale", "full scale", f.fullScale,
          opts (0.1, 10.0, 1.5, 3, c, "the level (mV) that reaches code ±128: the chip's ADC range. Too small clips d and x; too large wastes bits", true,
                " mV"));
    tune ("filter.muSel", "mu_sel", f.muSel,
          opts (0, 7, 0, 0, c, "uio[3:1]: μ = 2^(mu_sel − 12) of full scale per weight step (0: 0.000244 ... 7: 0.03125)"));
    f.muSel = std::clamp (f.muSel, 0, 7);
    textLine (fmtText ("μ = 2^%d = %.6f;  1 LSB = %s", f.muSel - 12, FilterSpec::chipMu (f.muSel), level (f.fullScale / 128.0).c_str()), textGrey);
    ImGui::Spacing();
    bool adapt = ! f.freeze;
    if (ImGui::Checkbox ("adapt_en (uio[0])", &adapt)) f.freeze = ! adapt;
    markItem ("filter.freeze");
    tooltip ("uio[0]: off holds the weights (it still filters), as Freeze does (F); the chip's weight readout only runs with it off");
    ImGui::SameLine();
    if (ImGui::Button ("Reset weights", { -1, 0 })) sim.resetWeights();
    tooltip ("the weights to 0, and the saturation flag cleared (W)");
    ImGui::Spacing();

    const auto& chip = sim.filter().chip();
    const auto& k = sim.filter().lastCodes();
    auto hex = [] (int v) { return fmtText ("%02X", v & 0xff); };
    ImGui::PushFont (fonts().mono);
    textLine ("last sample   u " + hex (k.u) + "  r " + hex (k.r) + "  →  y " + hex (k.y) + "  e " + hex (k.e), textLight);
    textLine (std::string ("debug_o2      ") + (chip.satFlag ? "1  saturated (sticky)" : "0  no saturation"),
              chip.satFlag ? argb (0xffffc060) : textGrey);
    const double pc = sim.samples() > 0 ? 100.0 * (double) sim.clipped() / (double) sim.samples() : 0.0;
    textLine (fmtText ("inputs at the rails  %lld  (%.2f %%)", sim.clipped(), pc), sim.clipped() > 0 ? argb (0xffffc060) : textGrey);
    textLine ("SNR improvement  chip " + dbText (sim.improvement()) + "   float twin " + dbText (sim.improvementTwin()), accent);
    ImGui::PopFont();
    helpText (fmtText ("on silicon, at 40 MHz the chip takes a sample every 13 SCLK_IN periods (1.54 MS/s); here it is fed the scene's "
                       "%.0f samples a second, which changes nothing in its arithmetic.", sampleRate));
    ImGui::Spacing();
    ImGui::PushTextWrapPos (0);
    textLine ("the readout (debug_i1, adapt_en = 0): y lane w[k] >> 6 (Q2.6), e lane x[k]", textGrey);
    ImGui::PopTextWrapPos();
    ImGui::PushFont (fonts().mono);
    if (ImGui::BeginTable ("chipregs", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchSame))
    {
        for (const char* h : { "k", "w[k] reg", "y lane", "e lane" }) ImGui::TableSetupColumn (h);
        ImGui::TableHeadersRow();
        for (int i = 0; i < ChipCore::taps; ++i)
        {
            const auto ro = chip.readout (i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text ("%d", i);
            ImGui::TableNextColumn();
            ImGui::Text ("%04X", chip.w[(size_t) i] & 0x3fff);
            ImGui::TableNextColumn();
            ImGui::Text ("%02X", ro.weight & 0xff);
            ImGui::TableNextColumn();
            ImGui::Text ("%02X", ro.tap & 0xff);
        }
        ImGui::EndTable();
    }
    ImGui::PopFont();
    weightTable();
}

void App::weightTable()
{
    ImGui::Spacing();
    // the weights, in numbers
    const auto& w = sim.filter().weights();
    const auto& wiener = sim.wienerTaps();
    const auto& truth = sim.truth().taps;
    textLine ("the weights now, the Wiener solution, and Hp/Hr:", textGrey);
    ImGui::PushFont (fonts().mono);
    if (ImGui::BeginTable ("weights", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_ScrollY,
                           { 0, std::max (80.0f, ImGui::GetContentRegionAvail().y - 4) }))
    {
        ImGui::TableSetupScrollFreeze (0, 1);
        ImGui::TableSetupColumn ("tap");
        ImGui::TableSetupColumn ("w");
        ImGui::TableSetupColumn ("Wiener");
        ImGui::TableSetupColumn ("Hp/Hr");
        ImGui::TableHeadersRow();
        for (size_t i = 0; i < w.size(); ++i)
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::Text ("%zu", i);
            ImGui::TableNextColumn();
            ImGui::TextColored (ImGui::ColorConvertU32ToFloat4 (accent), "%+.4f", w[i]);
            ImGui::TableNextColumn();
            if (i < wiener.size()) ImGui::TextColored (ImGui::ColorConvertU32ToFloat4 (colourWiener), "%+.4f", wiener[i]);
            ImGui::TableNextColumn();
            if (i < truth.size()) ImGui::Text ("%+.4f", truth[i]);
        }
        ImGui::EndTable();
    }
    ImGui::PopFont();
}
} // namespace afx
