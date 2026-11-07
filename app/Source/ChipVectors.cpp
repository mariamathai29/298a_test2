// afx_chipvec: test vectors for the chip's datapath, from the explorer's own
// bit-exact model (Chip.h), for tb/chip_tb.py to replay through the Python golden
// model (../lms_model.py) and the Verilog testbench (test/tb.v) on the RTL or
// the routed netlist.
//   afx_chipvec [out_dir]
//
// Each case is two files in the testbench's own formats:
//   <case>.ops   what to drive (hex): 1 u r (a sample), 2 v (uio[5:0] = adapt_en | mu_sel << 1),
//                5 (weight readout), a (record the debug flags)
//   <case>.exp   the frames the chip must send back: Y y e, R w x, F ovf sat
//
// Cases:
//   app_fetal   the explorer's default scene (fetal ECG under the maternal one) run
//               by the Simulator itself on the chip arithmetic: the codes it quantized
//               and the y, e it got back, with μ steps, freezes and readouts on the way
//   app_hum     a sine under mains hum through an echo, at a full scale that clips
//               the inputs and saturates y and e (the sticky flag), and a fast μ
//   corners     full-range random codes at every mu_sel, adapt_en toggles, the
//               rails, and weights driven into saturation

#include "Chip.h"
#include "Simulator.h"

#include <cstdio>
#include <filesystem>
#include <random>
#include <string>
#include <vector>

using namespace afx;
namespace fs = std::filesystem;

namespace
{
struct Case
{
    std::vector<std::string> ops, exp;
    long long samples = 0;
    int readouts = 0;

    static std::string hex2 (int v)
    {
        char b[8];
        std::snprintf (b, sizeof b, "%02x", v & 0xff);
        return b;
    }
    void uio (bool adapt, int muSel)
    {
        char b[16];
        std::snprintf (b, sizeof b, "2 %x", (adapt ? 1 : 0) | (muSel << 1));
        ops.push_back (b);
    }
    void sample (int u, int r, int y, int e)
    {
        ops.push_back ("1 " + hex2 (u) + " " + hex2 (r));
        exp.push_back ("Y " + hex2 (y) + " " + hex2 (e));
        ++samples;
    }
    void readout (const ChipCore& c)
    {
        ops.push_back ("5");
        for (int k = 0; k < ChipCore::taps; ++k)
        {
            const auto r = c.readout (k);
            exp.push_back ("R " + hex2 (r.weight) + " " + hex2 (r.tap));
        }
        ++readouts;
    }
    void flags (const ChipCore& c)
    {
        ops.push_back ("a");
        exp.push_back (std::string ("F 0 ") + (c.satFlag ? "1" : "0"));
    }
    bool write (const fs::path& dir, const std::string& name) const
    {
        for (const auto& [ext, lines] : { std::pair { ".ops", &ops }, std::pair { ".exp", &exp } })
        {
            FILE* f = std::fopen ((dir / (name + ext)).string().c_str(), "w");
            if (f == nullptr) return false;
            for (const auto& l : *lines) std::fprintf (f, "%s\n", l.c_str());
            std::fclose (f);
        }
        std::printf ("     %-10s %6lld samples, %2d readouts, %6zu frames\n", name.c_str(), samples, readouts, exp.size());
        return true;
    }
};

// The Simulator on a scene with the chip's arithmetic, a sample at a time, recording
// what it fed the chip and what came back.  `plan`: (from sample, mu_sel); every
// `every` samples, freeze, read out and carry on (as a host would).
Case fromSimulator (Scene sc, long long samples, const std::vector<std::pair<long long, int>>& plan, long long every)
{
    sc.filter.arithmetic = Arithmetic::chip;
    sc.filter.freeze = false;
    sc.filter.muSel = plan.front().second;
    Simulator sim (sc);
    Case c;
    c.uio (false, sc.filter.muSel);
    c.readout (sim.filter().chip());   // all zero at the start
    c.uio (true, sc.filter.muSel);
    for (long long n = 0; n < samples; ++n)
    {
        for (const auto& [at, mu] : plan)
            if (n == at && at > 0)
            {
                sc.filter.muSel = mu;
                sim.setScene (sc);
                c.uio (true, mu);
            }
        sim.run (1);
        const auto& k = sim.filter().lastCodes();
        c.sample (k.u, k.r, k.y, k.e);
        if ((n + 1) % every == 0)
        {
            sc.filter.freeze = true;
            sim.setScene (sc);
            c.uio (false, sc.filter.muSel);
            c.readout (sim.filter().chip());
            // a few frozen samples: still filtering, not learning
            for (int i = 0; i < 20 && n + 1 < samples; ++i, ++n)
            {
                sim.run (1);
                const auto& f = sim.filter().lastCodes();
                c.sample (f.u, f.r, f.y, f.e);
            }
            sc.filter.freeze = false;
            sim.setScene (sc);
            c.uio (true, sc.filter.muSel);
        }
    }
    c.uio (false, sc.filter.muSel);
    c.readout (sim.filter().chip());
    c.flags (sim.filter().chip());
    return c;
}

Case corners()
{
    ChipCore core;
    core.reset();
    Case c;
    std::mt19937 rng (2026);
    std::uniform_int_distribution<int> code (-128, 127);
    auto step = [&] (int u, int r) {
        const auto o = core.step (u, r);
        c.sample (u, r, o.y, o.e);
    };
    auto set = [&] (bool adapt, int mu) {
        core.adaptEn = adapt;
        core.muSel = mu;
        c.uio (adapt, mu);
    };
    // every mu_sel on full-range random codes, a readout after each
    for (int mu = 0; mu < 8; ++mu)
    {
        set (true, mu);
        for (int i = 0; i < 300; ++i) step (code (rng), code (rng));
        set (false, mu);
        c.readout (core);
    }
    // frozen: filters, doesn't learn
    for (int i = 0; i < 200; ++i) step (code (rng), code (rng));
    // the rails and zero, every pair, learning fast
    set (true, 7);
    const int edge[] = { -128, -127, -1, 0, 1, 126, 127 };
    for (int u : edge)
        for (int r : edge) step (u, r);
    // weights into saturation: a constant input and a target the filter can't reach
    for (int i = 0; i < 300; ++i) step (127, 127);
    set (false, 7);
    c.readout (core);
    set (true, 7);
    for (int i = 0; i < 300; ++i) step (-128, 127);
    set (false, 7);
    c.readout (core);
    // and back down, slowly, alternating adapt_en every 50 samples
    for (int b = 0; b < 12; ++b)
    {
        set (b % 2 == 0, 7 - b % 8);
        for (int i = 0; i < 50; ++i) step (code (rng), -code (rng) / 2);
    }
    set (false, 0);
    c.readout (core);
    c.flags (core);
    return c;
}
} // namespace

int main (int argc, char** argv)
{
    const fs::path dir = argc > 1 ? argv[1] : "vectors";
    fs::create_directories (dir);
    std::printf ("afx_chipvec: chip test vectors into %s\n", dir.string().c_str());
    bool ok = true;

    // The explorer's default scene: μ 2^-9 to converge, then 2^-12 to settle.
    ok = fromSimulator (Scene::defaults(), 12000, { { 0, 3 }, { 5000, 0 } }, 2000).write (dir, "app_fetal") && ok;

    // A sine under hum through an echo, at a full scale too small: d clips.
    Scene hum = Scene::defaults();
    hum.signal = SourceSpec::defaults (SourceKind::sine);
    hum.signal.frequency = 3.0;
    hum.interference = SourceSpec::defaults (SourceKind::hum);
    hum.interference.amplitude = 0.5;
    hum.primaryPath = pathFromPreset ("short echo");
    hum.filter.fullScale = 0.4;   // d peaks near 0.95 mV: the inputs clip, and y and e saturate (debug_o2 sets)
    ok = fromSimulator (hum, 4000, { { 0, 6 }, { 2000, 2 } }, 1000).write (dir, "app_hum") && ok;

    ok = corners().write (dir, "corners") && ok;
    return ok ? 0 : 1;
}
