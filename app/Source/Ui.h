#pragma once

// The adaptive filter explorer's window, drawn with Dear ImGui:
//   the toolbar: run, pause, step, restart, the speed, the default scene, save and open;
//   left, the inspector: the selected block's settings;
//   middle, the canceller's block diagram (click a block to edit it) and, under it,
//   the traces: d, x, y, and e over the true s;
//   right, how it is learning: the learning curve and SNR improvement, the tap
//   weights (bars and trajectories), and the magnitude response of w against the
//   true path and the Wiener solution.
// Main.cpp opens the window; the shot tool drives it off-screen.

#include "FileDialog.h"
#include "Project.h"
#include "Simulator.h"
#include "Sonify.h"
#include "Views.h"

#include <string>
#include <vector>

namespace afx
{
enum class Block { none, signal, interference, primaryNoise, referenceNoise, primaryPath, referencePath, primarySum, referenceSum, filter, output };

class App
{
public:
    App();

    void frame();
    bool quitRequested() const { return quit; }

    // ---- actions (also used by the tools) ----
    void setRunning (bool r) { running = r; }
    bool isRunning() const { return running; }
    void step (long long samples);
    void restart();
    void setSpeed (double s);
    double speed() const { return view.speed; }
    void setSpan (double seconds) { view.span = seconds; }
    Scene& scene() { return edit; }   // changes apply at the end of the frame
    void setScene (const Scene&);
    void loadDefaults();
    void loadChip();   // the default scene on the chip's arithmetic
    void select (Block b) { selection = b; }
    Block selected() const { return selection; }
    bool openProject (const std::string& path);
    bool saveProjectTo (const std::string& path);
    const std::string& statusText() const { return status; }
    void setStatus (const std::string& s) { status = s; }
    Simulator& simulator() { return sim; }
    ImVec2 where (const std::string& name) const;
    void setFixedStep (double seconds) { fixedStep = seconds; }   // for the tools: frames this long, whatever the clock says
    // Listening (Sonify.h): the heartbeats in e as thumps, or in d or x when one is selected.
    // The window takes the sound each frame (48 kHz stereo, interleaved) and plays it.
    void setListening (bool on);
    bool listening() const { return listen; }
    void takeSound (std::vector<float>& out);
    static constexpr double soundRate = 48000.0;

private:
    void toolbar();
    void diagram (ImVec2 size);
    void traces (ImVec2 size);
    void learning (ImVec2 size);
    void weightsView (ImVec2 size);
    void responseView (ImVec2 size);
    void inspector();
    void sourceEditor (SourceSpec&, const char* id, bool canSwap);
    void pathEditor (PathSpec&, const char* id, bool primary);
    void filterEditor();
    void chipEditor();
    void weightTable();
    void dialogs();
    void shortcuts();
    void advance (double dt);
    void apply();

    Simulator sim;
    Scene edit;
    View view;
    bool running = true, quit = false;
    double owed = 0.0, fixedStep = 0.0;
    int stepSize = 0;   // 0: 1 sample, 1: 10, 2: 100, 3: one second
    Block selection = Block::filter;
    std::string status;
    bool listen = false;
    Listener ear { soundRate };
    Channel heard = Channel::error;
    std::vector<float> sound;

    // what the displays keep between frames
    std::vector<float> scratch, scratch2;
    float scales[5] = { 1, 1, 1, 1, 1 };
    std::vector<float> xSpectrum;   // dB, the reference's spectrum, for the response view
    double spectrumAt = -1.0, spectrumWall = -1.0;

    FileDialog fileDialog;
    enum class Pending { none, save, load } pending = Pending::none;
    std::string lastFolder;
};
} // namespace afx
