// Headless checks of the adaptive filter explorer's core: the ECG generators'
// heart rates and variability, the sources, the effective path, sign-sign LMS on
// the default scene (convergence and SNR improvement), LMS, NLMS and RLS against
// the Wiener solution, a filter too short for its path, freezing, the
// sonification (whose heartbeat is heard, before and after), the settings JSON and
// the speed.
//   afx_selftest

#include "Project.h"
#include "Simulator.h"
#include "Sonify.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <string>

using namespace afx;
namespace fs = std::filesystem;

static int failures = 0;

static std::string fmt (const char* f, ...)
{
    char buf[512];
    va_list a;
    va_start (a, f);
    std::vsnprintf (buf, sizeof buf, f, a);
    va_end (a);
    return buf;
}

static void check (bool ok, const char* what, const std::string& detail = {})
{
    std::printf ("%s  %s %s\n", ok ? "ok  " : "FAIL", what, detail.c_str());
    if (! ok) ++failures;
}

static double db (double power) { return 10.0 * std::log10 (std::max (power, 1e-30)); }

// R waves in an ECG: upward crossings of half the amplitude, at least 0.15 s apart.
struct Beats
{
    double bpm = 0.0, rrSpread = 0.0, peak = 0.0;
    int count = 0;
};
static Beats beatsOf (const SourceSpec& spec, double seconds)
{
    Source src;
    src.configure (spec);
    std::vector<double> times;
    double prev = 0.0, last = -1.0;
    Beats b;
    const long long n = (long long) (seconds * sampleRate);
    for (long long i = 0; i < n; ++i)
    {
        const double v = src.next(), t = (double) i / sampleRate;
        b.peak = std::max (b.peak, v);
        if (prev < 0.5 * spec.amplitude && v >= 0.5 * spec.amplitude && t - last > 0.15)
        {
            times.push_back (t);
            last = t;
        }
        prev = v;
    }
    b.count = (int) times.size();
    if (times.size() < 3) return b;
    b.bpm = 60.0 * (double) (times.size() - 1) / (times.back() - times.front());
    double mean = 0.0, sq = 0.0;
    for (size_t i = 1; i < times.size(); ++i) mean += times[i] - times[i - 1];
    mean /= (double) (times.size() - 1);
    for (size_t i = 1; i < times.size(); ++i) sq += (times[i] - times[i - 1] - mean) * (times[i] - times[i - 1] - mean);
    b.rrSpread = std::sqrt (sq / (double) (times.size() - 1)) / mean;
    return b;
}

// Over the last `seconds`: the interference in d against what is left of it in e.
struct Result
{
    double inDb, outDb, improvement;
};
static Result measure (const Simulator& sim, double seconds)
{
    std::vector<float> d, e, s;
    const size_t n = (size_t) (seconds * sampleRate);
    sim.history (Channel::primary).last (n, d);
    sim.history (Channel::error).last (n, e);
    sim.history (Channel::signal).last (n, s);
    double in = 0.0, out = 0.0;
    for (size_t i = 0; i < s.size(); ++i)
    {
        in += (double) (d[i] - s[i]) * (d[i] - s[i]);
        out += (double) (e[i] - s[i]) * (e[i] - s[i]);
    }
    in /= (double) s.size();
    out /= (double) s.size();
    return { db (in), db (out), db (in) - db (out) };
}

static double distance (const std::vector<double>& a, const std::vector<double>& b)
{
    double num = 0.0, den = 0.0;
    for (size_t i = 0; i < a.size(); ++i)
    {
        const double bi = i < b.size() ? b[i] : 0.0;
        num += (a[i] - bi) * (a[i] - bi);
        den += bi * bi;
    }
    return std::sqrt (num / std::max (den, 1e-30));
}

// The largest difference between two responses' magnitudes over a band (dB).
static double bandError (const std::vector<double>& a, const std::vector<double>& b, double lo, double hi)
{
    double worst = 0.0;
    for (double f = lo; f <= hi; f += 0.5) worst = std::max (worst, std::abs (magnitudeDb (response (a, f, sampleRate)) - magnitudeDb (response (b, f, sampleRate))));
    return worst;
}

static std::string tapsText (const std::vector<double>& w)
{
    std::string s = "[";
    for (size_t i = 0; i < w.size(); ++i) s += fmt ("%s%.3f", i ? " " : "", w[i]);
    return s + "]";
}

int main()
{
    // ---- the ECGs ----
    {
        const auto m = beatsOf (SourceSpec::defaults (SourceKind::maternalEcg), 120.0);
        check (std::abs (m.bpm - 75.0) < 1.5, "maternal ECG: 75 bpm asked", fmt ("(%.2f bpm from %d beats in 120 s)", m.bpm, m.count));
        check (m.rrSpread > 0.01 && m.rrSpread < 0.08, "maternal ECG: its heart rate varies", fmt ("(RR spread %.1f %%)", 100.0 * m.rrSpread));
        auto dc = SourceSpec::defaults (SourceKind::maternalEcg);
        dc.highPass = 0.0;
        const auto direct = beatsOf (dc, 20.0);
        check (std::abs (direct.peak - 1.0) < 0.02 && m.peak > 0.85, "maternal ECG: the R wave is 1 mV",
               fmt ("(%.3f mV DC-coupled, %.3f mV through the 0.5 Hz high-pass)", direct.peak, m.peak));
        const auto f = beatsOf (SourceSpec::defaults (SourceKind::fetalEcg), 120.0);
        check (std::abs (f.bpm - 140.0) < 2.8, "fetal ECG: 140 bpm asked", fmt ("(%.2f bpm from %d beats, R wave %.3f mV)", f.bpm, f.count, f.peak));
        auto steady = SourceSpec::defaults (SourceKind::maternalEcg);
        steady.bpm = 90.0;
        steady.hrv = 0.0;
        const auto s = beatsOf (steady, 60.0);
        check (std::abs (s.bpm - 90.0) < 0.2 && s.rrSpread < 0.005, "an ECG without variability beats evenly",
               fmt ("(%.3f bpm, RR spread %.2f %%)", s.bpm, 100.0 * s.rrSpread));
    }

    // ---- the other sources: their levels ----
    {
        auto rmsOf = [] (SourceKind k, double seconds = 60.0) {
            Source src;
            src.configure (SourceSpec::defaults (k));
            double sum = 0.0;
            const long long n = (long long) (seconds * sampleRate);
            for (long long i = 0; i < n; ++i)
            {
                const double v = src.next();
                sum += v * v;
            }
            return std::sqrt (sum / (double) n);
        };
        const double white = rmsOf (SourceKind::white), pink = rmsOf (SourceKind::pink), sine = rmsOf (SourceKind::sine);
        check (std::abs (white / 0.005 - 1.0) < 0.05 && std::abs (pink / 0.02 - 1.0) < 0.25 && std::abs (sine / (0.2 / std::sqrt (2.0)) - 1.0) < 0.01,
               "noises have the RMS asked, tones the peak", fmt ("(white %.4f, pink %.4f, sine %.4f mV RMS)", white, pink, sine));
        bool finite = true;
        for (int k = 0; k < numSourceKinds; ++k)
        {
            Source src;
            src.configure (SourceSpec::defaults ((SourceKind) k));
            for (int i = 0; i < 20000; ++i) finite = finite && std::isfinite (src.next());
        }
        check (finite, "every source gives finite samples");
    }

    // ---- the effective path ----
    {
        const Scene sc = Scene::defaults();
        const auto t = truePath (sc.primaryPath, sc.referencePath);
        check (t.needed == 4 && t.causal && t.stable && distance (std::vector<double> (t.taps.begin(), t.taps.begin() + 4), sc.primaryPath.taps) < 1e-12,
               "the default path: Hp/Hr is Hp, 4 taps", fmt ("(needs %d)", t.needed));
        auto late = pathFromPreset ("delay 2");
        const auto nc = truePath (pathFromPreset ("direct"), late);
        check (! nc.causal, "a reference later than the primary: Hp/Hr is not causal");
        PathSpec echo;
        echo.taps = { 1.0, 0.5 };
        const auto inv = truePath (pathFromPreset ("direct"), echo);
        check (inv.stable && inv.needed > 6 && std::abs (inv.taps[3] + 0.125) < 1e-12, "dividing by an echo: an infinite (decaying) response",
               fmt ("(needs %d taps)", inv.needed));
    }

    // ---- 8-tap sign-sign LMS on the default scene ----
    // The ECG has almost nothing above 30 Hz, so only the paths' responses below that
    // matter (and only they can be learnt): the checks are the cancelling and that band.
    {
        Simulator sim;
        const auto& path = sim.scene().primaryPath.effective();
        sim.run ((long long) (10.0 * sampleRate));
        const auto early = measure (sim, 5.0);
        sim.run ((long long) (110.0 * sampleRate));
        const auto r = measure (sim, 30.0);
        sim.solveWiener (30.0);
        const double excess = r.outDb - db (sim.wienerMse());
        const double band = bandError (sim.filter().weights(), path, 1.0, 25.0);
        check (sim.scene().filter.algorithm == Algorithm::signSign && sim.scene().filter.taps == 8, "the default filter is 8-tap sign-sign LMS");
        check (r.improvement > 20.0, "8-tap sign-sign LMS on the fetal scene: a large SNR improvement",
               fmt ("(%.1f dB over the last 30 s of 120: interference %.1f → %.1f dB re 1 mV²; %.1f dB at 5-10 s)", r.improvement, r.inDb,
                    r.outDb, early.improvement));
        check (band < 1.0 && excess < 6.0, "8-tap sign-sign LMS converges on the 4-tap path where the ECG is",
               fmt ("(|W| within %.2f dB of |Hp| over 1-25 Hz; MSE %.1f dB above the Wiener MSE; w %s)", band, excess,
                    tapsText (sim.filter().weights()).c_str()));
        check (std::isfinite (sim.snrOut()) && std::abs (sim.improvement() - r.improvement) < 4.0, "the running SNR improvement agrees",
               fmt ("(running: SNR %.1f → %.1f dB, %+.1f dB)", sim.snrIn(), sim.snrOut(), sim.improvement()));
    }

    // ---- with a broadband interference every tap is learnt: the weights converge on the path ----
    for (const Algorithm a : { Algorithm::signSign, Algorithm::lms, Algorithm::nlms, Algorithm::signError, Algorithm::signData, Algorithm::rls })
    {
        Scene sc = Scene::defaults();
        sc.interference = SourceSpec::defaults (SourceKind::white);
        sc.interference.amplitude = 0.3;
        sc.interference.seed = 11;
        sc.filter.algorithm = a;
        Simulator sim (sc);
        sim.run ((long long) (60.0 * sampleRate));
        sim.solveWiener (20.0);
        const auto r = measure (sim, 20.0);
        const std::vector<double> path (sim.truth().taps.begin(), sim.truth().taps.begin() + 8);
        const double toPath = distance (sim.filter().weights(), path), toWiener = distance (sim.filter().weights(), sim.wienerTaps());
        check (toPath < 0.05 && toWiener < 0.05, fmt ("%s, white-noise interference: the weights converge on the path", algorithmName (a)).c_str(),
               fmt ("(%.1f %% from Hp, %.1f %% from Wiener; %.1f dB improvement)", 100.0 * toPath, 100.0 * toWiener, r.improvement));
    }

    // ---- LMS, NLMS and RLS near the Wiener solution on the fetal scene ----
    for (const Algorithm a : { Algorithm::lms, Algorithm::nlms, Algorithm::rls })
    {
        Scene sc = Scene::defaults();
        sc.filter.algorithm = a;
        Simulator sim (sc);
        sim.run ((long long) (120.0 * sampleRate));
        sim.solveWiener (30.0);
        const auto r = measure (sim, 30.0);
        const double excess = r.outDb - db (sim.wienerMse());
        const double band = bandError (sim.filter().weights(), sim.wienerTaps(), 1.0, 25.0);
        check (excess < 1.5 && band < 0.5, fmt ("%s reaches near the Wiener solution", algorithmName (a)).c_str(),
               fmt ("(MSE %.2f dB above the Wiener MSE, |W| within %.2f dB of it over 1-25 Hz; %.1f dB improvement, Wiener %.1f)", excess, band,
                    r.improvement, r.inDb - db (sim.wienerMse())));
    }

    // ---- the Wiener solution itself ----
    {
        Scene sc = Scene::defaults();
        sc.primaryNoise = SourceSpec::defaults (SourceKind::none);
        sc.referenceNoise = SourceSpec::defaults (SourceKind::none);
        Simulator sim (sc);
        sim.run ((long long) (20.0 * sampleRate));
        sim.solveWiener (10.0);
        const std::vector<double> want (sim.truth().taps.begin(), sim.truth().taps.begin() + 8);
        const double dist = distance (sim.wienerTaps(), want);
        check (dist < 0.01, "without noise, the Wiener solution is the path", fmt ("(%.2g relative error)", dist));
    }

    // ---- a path longer than the filter ----
    {
        auto improvementWith = [] (int taps) {
            Scene sc = Scene::defaults();
            sc.primaryPath = pathFromPreset ("long echo");
            sc.filter.algorithm = Algorithm::lms;
            sc.filter.taps = taps;
            Simulator sim (sc);
            sim.run ((long long) (60.0 * sampleRate));
            return measure (sim, 20.0).improvement;
        };
        const double eight = improvementWith (8), sixteen = improvementWith (16);
        check (sixteen > eight + 6.0, "a 13-tap path: 8 taps fall short, 16 reach it", fmt ("(%.1f dB with 8 taps, %.1f dB with 16)", eight, sixteen));
    }

    // ---- the sonification: the mother's heartbeat before, the baby's after ----
    {
        auto listen = [] (bool learnt, int& locked, int& free, double& peak) {
            Scene sc = Scene::defaults();
            Simulator sim (sc);
            if (learnt) sim.run ((long long) (90.0 * sampleRate));
            sc.filter.freeze = true;
            sim.setScene (sc);
            Listener ear;
            ear.voicesFrom (sc);
            std::vector<float> out;
            for (int f = 0; f < 30 * 20; ++f)   // 20 s at real time, a frame at a time
            {
                sim.run (f % 3 != 2 ? 17 : 16);   // 500 a second
                ear.follow (sim, 1.0 / 30.0, out);
            }
            locked = ear.sonifier().lockedBeats();
            free = ear.sonifier().freeBeats();
            peak = 0.0;
            for (const float v : out) peak = std::max (peak, (double) std::abs (v));
            return out.size();
        };
        int l0 = 0, f0 = 0, l1 = 0, f1 = 0;
        double p0 = 0, p1 = 0;
        const size_t frames = listen (false, l0, f0, p0) / 2;
        listen (true, l1, f1, p1);
        // 20 s: the mother's 75 bpm is 25 beats, the baby's 140 bpm 47 (a few more found while the threshold settles)
        check (l0 >= 22 && l0 <= 27 && f0 <= 5, "listening before it learns: the mother's heartbeat", fmt ("(%d hers, %d others in 20 s)", l0, f0));
        check (f1 >= 40 && f1 <= 54 && l1 <= 3, "listening once it has learnt: the baby's heartbeat", fmt ("(%d the baby's, %d hers in 20 s)", f1, l1));
        check (std::abs ((double) frames - 20.0 * 48000.0) < 2000.0 && p0 < 1.0 && p1 < 1.0 && p1 > 0.05, "the sound: in step with the frames, and within range",
               fmt ("(%zu frames for 20 s, peaks %.2f and %.2f)", frames, p0, p1));
    }

    // ---- freeze ----
    {
        Scene sc = Scene::defaults();
        Simulator sim (sc);
        sim.run ((long long) (20.0 * sampleRate));
        sc.filter.freeze = true;
        sim.setScene (sc);
        const auto held = sim.filter().weights();
        sim.run ((long long) (10.0 * sampleRate));
        const auto r = measure (sim, 5.0);
        check (sim.filter().weights() == held && r.improvement > 10.0, "freeze holds the weights (and still cancels)",
               fmt ("(%.1f dB while frozen)", r.improvement));
        sc.filter.freeze = false;
        sim.setScene (sc);
        sim.run (100);
        check (sim.filter().weights() != held, "unfreezing adapts again");
        sim.resetWeights();
        check (std::all_of (sim.filter().weights().begin(), sim.filter().weights().end(), [] (double v) { return v == 0.0; }), "reset weights: all 0");
    }

    // ---- leakage pulls the weights in ----
    {
        Scene sc = Scene::defaults();
        sc.filter.algorithm = Algorithm::lms;
        Simulator plain (sc);
        sc.filter.leakage = 1e-3;
        Simulator leaky (sc);
        plain.run ((long long) (60.0 * sampleRate));
        leaky.run ((long long) (60.0 * sampleRate));
        double a = 0.0, b = 0.0;
        for (double v : plain.filter().weights()) a += v * v;
        for (double v : leaky.filter().weights()) b += v * v;
        check (b < a, "leakage shrinks the weights", fmt ("(|w|² %.3f without, %.3f with)", a, b));
    }

    // ---- the settings JSON ----
    {
        Project a;
        a.scene.signal = SourceSpec::defaults (SourceKind::chirp);
        a.scene.signal.frequency2 = 77.0;
        a.scene.interference.bpm = 82.0;
        a.scene.primaryNoise = SourceSpec::defaults (SourceKind::hum);
        a.scene.primaryNoise.mains = 50;
        a.scene.referenceNoise = SourceSpec::defaults (SourceKind::emg);
        a.scene.primaryPath = pathFromPreset ("long low-pass");
        a.scene.referencePath.gain = 0.7;
        a.scene.referencePath.delay = 3;
        a.scene.signalInReference = 0.05;
        a.scene.filter.algorithm = Algorithm::rls;
        a.scene.filter.taps = 17;
        a.scene.filter.mu[1] = 0.123;
        a.scene.filter.lambda = 0.995;
        a.scene.filter.leakage = 1e-5;
        a.scene.filter.freeze = true;
        a.view.speed = 10.0;
        a.view.span = 8.0;
        a.ranges = { { "filter.mu", { 1e-5, 0.1, true } } };
        Project b;
        std::string error;
        check (fromJson (toJson (a), b, error) && b.scene == a.scene && b.view == a.view && b.ranges == a.ranges, "settings JSON round trip", error);
        const auto path = fs::temp_directory_path() / "afx_selftest.afx.json";
        Project c;
        check (saveProject (path.string(), a, error) && loadProject (path.string(), c, error) && c.scene == a.scene, "settings through a file", error);
        fs::remove (path);
        check (! fromJson (nlohmann::json { { "format", "reel 3" } }, b, error), "a Reel patch is refused", error);
        check (! loadProject ("/nonexistent/afx.json", b, error), "a missing file is refused", error);
        check (toJson (Project {}).at ("format") == "afx 1", "the format tag");
    }

    // ---- the chip's arithmetic (bit-exact with the RTL: tb/chip_tb.py; here, its rules by hand) ----
    {
        ChipCore c;
        c.reset();
        c.adaptEn = false;
        c.w[0] = 2048;   // 0.5 in Q2.12; the FIR sees 2048 >> 4 = 128 (Q2.8)
        const int y100 = c.step (100, 0).y, y101 = c.step (101, 0).y, yn101 = c.step (-101, 0).y;
        check (y100 == 50 && y101 == 51 && yn101 == -50, "chip FIR: (acc + 2^7) >> 8 rounds half up",
               fmt ("(0.5 × 100, 101, -101 → %d, %d, %d)", y100, y101, yn101));
        const auto o = c.step (-128, 127);   // y = -64, e = 127 + 64 → 127
        check (o.y == -64 && o.e == 127 && c.satFlag, "chip: e saturates to 8 bits and sets the sticky flag", fmt ("(y %d, e %d)", o.y, o.e));
        c.reset();
        c.adaptEn = true;
        c.muSel = 7;
        c.w.fill (8190);
        c.step (1, 127);   // y = (511 + 128) >> 8 = 2, e > 0, x[0] > 0: w[0] + 128 saturates
        check (c.w[0] == 8191 && c.satFlag, "chip: a weight update saturates at 14 bits", fmt ("(w[0] = %d)", c.w[0]));
        c.reset();
        c.muSel = 3;
        c.step (5, 100);   // e > 0, x[0] > 0, the rest 0: only w[0] moves, by 2^3
        check (c.w[0] == 8 && c.w[1] == 0 && c.readout (0).weight == 0 && c.readout (0).tap == 5, "chip: sign-sign update by 2^mu_sel, sign(0) = 0",
               fmt ("(w = %d %d %d %d)", c.w[0], c.w[1], c.w[2], c.w[3]));
        check (toQ17 (0.5) == 64 && toQ17 (1.0) == 127 && toQ17 (-1.0) == -128 && toQ17 (0.5 / 128) == 0 && toQ17 (1.5 / 128) == 2,
               "Q1.7 quantization: round half even, saturated (as lms_model.to_q)");

        Simulator sim (Scene::chip());
        sim.run ((long long) (60.0 * sampleRate));
        const auto r = measure (sim, 20.0);
        check (r.improvement > 20.0 && std::abs (sim.improvement() - sim.improvementTwin()) < 2.0 && sim.clipped() == 0,
               "the chip on the fetal scene: cancels the maternal ECG, near its float twin, nothing clipped",
               fmt ("(%+.1f dB, twin %+.1f dB over the last 20 s of 60)", r.improvement, sim.improvementTwin()));
        check (sim.filter().weights().size() == 4 && sim.wienerTaps().size() == 4, "the chip has 4 taps (and the Wiener solution too)");

        Scene sc = Scene::chip();
        sc.filter.freeze = true;
        const auto held = sim.filter().weights();
        sim.setScene (sc);
        sim.run (5000);
        check (sim.filter().weights() == held, "adapt_en = 0 holds the chip's weights");

        // float → chip → float: the weights carry over, to a register LSB
        Scene fl = Scene::defaults();
        fl.filter.taps = 4;
        Simulator sw (fl);
        sw.run ((long long) (30.0 * sampleRate));
        const auto before = sw.filter().weights();
        fl.filter.arithmetic = Arithmetic::chip;
        sw.setScene (fl);
        double worst = 0.0;
        for (size_t i = 0; i < 4; ++i) worst = std::max (worst, std::abs (sw.filter().weights()[i] - before[i]));
        fl.filter.arithmetic = Arithmetic::floating;
        sw.setScene (fl);
        check (worst <= 0.5 / 4096.0 + 1e-12 && sw.filter().weights().size() == 4, "switching to the chip and back keeps the weights (to an LSB)",
               fmt ("(%.2g)", worst));

        Project a, b;
        a.scene = Scene::chip();
        a.scene.filter.muSel = 5;
        a.scene.filter.fullScale = 0.8;
        std::string error;
        check (fromJson (toJson (a), b, error) && b.scene == a.scene, "the chip's settings in the JSON", error);
    }

    // ---- speed ----
    {
        Scene sc = Scene::defaults();
        sc.filter.algorithm = Algorithm::rls;
        sc.filter.taps = maxTaps;
        Simulator sim (sc);
        auto t0 = std::chrono::steady_clock::now();
        sim.run ((long long) (100.0 * sampleRate));
        const double rls = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        sc.filter.algorithm = Algorithm::signSign;
        sc.filter.taps = 8;
        Simulator fast (sc);
        t0 = std::chrono::steady_clock::now();
        fast.run ((long long) (600.0 * sampleRate));
        const double ss = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        check (rls < 1.0 && ss < 1.0, "×100 is easy: 100 s of 64-tap RLS, 600 s of 8-tap sign-sign, each under a second",
               fmt ("(%.2f s and %.2f s)", rls, ss));
    }
    std::printf ("\n%s (%d failure%s)\n", failures == 0 ? "all passed" : "FAILED", failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
