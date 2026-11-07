// afx_shot: the real explorer interface off-screen (SDL's software renderer), for
// screenshots of a few scenes, each run for a while first.
//   afx_shot [out_dir]
// (the last three: the chip's arithmetic, converging and then clipping)

#include "Ui.h"

#include <SDL.h>
#include <imgui.h>
#include <imgui_impl_sdlrenderer2.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

using namespace afx;

int main (int argc, char** argv)
{
    const std::filesystem::path dir = argc > 1 ? argv[1] : "shots";
    std::filesystem::create_directories (dir);
    const int w = 1720, h = 1040;
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat (0, w, h, 32, SDL_PIXELFORMAT_ABGR8888);
    SDL_Renderer* renderer = SDL_CreateSoftwareRenderer (surface);
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
    io.DisplaySize = { (float) w, (float) h };
    applyStyle();
    loadFonts();
    ImGui_ImplSDLRenderer2_Init (renderer);
    int failures = 0;
    {
        App app;
        app.setFixedStep (1.0 / 30.0);
        double uiSeconds = 0.0;
        int frames = 0;
        auto frame = [&] {
            io.DeltaTime = 1.0f / 30.0f;
            ImGui_ImplSDLRenderer2_NewFrame();
            ImGui::NewFrame();
            const auto t0 = std::chrono::steady_clock::now();
            app.frame();
            ImGui::Render();
            uiSeconds += std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
            ++frames;
            SDL_SetRenderDrawColor (renderer, 14, 16, 19, 255);
            SDL_RenderClear (renderer);
            ImGui_ImplSDLRenderer2_RenderDrawData (ImGui::GetDrawData(), renderer);
            SDL_RenderPresent (renderer);
        };
        // Simulated seconds at a speed, then a few frames at the speed to show.
        auto run = [&] (double simSeconds, double fast, double shown) {
            app.setSpeed (fast);
            for (int i = 0; i < (int) (simSeconds * 30.0 / fast); ++i) frame();
            app.setSpeed (shown);
            for (int i = 0; i < 4; ++i) frame();
        };
        auto save = [&] (const char* name) {
            stbi_write_png ((dir / name).string().c_str(), w, h, 4, surface->pixels, surface->pitch);
            std::printf ("     %s  (t = %.1f s, SNR improvement %+.1f dB)\n", (dir / name).string().c_str(), app.simulator().time(),
                         app.simulator().improvement());
        };

        // 1. The default scene, just started: e still has the maternal beats in it.
        run (6.0, 2.0, 2.0);
        save ("start.png");
        // 2. Two minutes on: converged.
        run (114.0, 20.0, 2.0);
        save ("converged.png");
        const bool converged = app.simulator().improvement() > 20.0;
        std::printf ("%s  the default scene converges in the interface (%+.1f dB)\n", converged ? "ok  " : "FAIL", app.simulator().improvement());
        failures += converged ? 0 : 1;

        // 3. A path the 8 taps can't hold: a 13-tap echo.
        app.scene().primaryPath = pathFromPreset ("long echo");
        app.select (Block::primaryPath);
        app.restart();
        run (60.0, 20.0, 2.0);
        save ("long-path.png");

        // 4. 16-tap LMS on the same path, the fetal ECG selected.
        app.scene().filter.algorithm = Algorithm::lms;
        app.scene().filter.taps = 16;
        app.select (Block::signal);
        app.restart();
        run (60.0, 20.0, 2.0);
        save ("lms-16.png");

        // 5. RLS on mains hum under a sine, the error selected.
        Scene sc = Scene::defaults();
        sc.signal = SourceSpec::defaults (SourceKind::sine);
        sc.signal.frequency = 3.0;
        sc.interference = SourceSpec::defaults (SourceKind::hum);
        sc.interference.amplitude = 0.5;
        sc.primaryPath = pathFromPreset ("short echo");
        sc.filter.algorithm = Algorithm::rls;
        app.setScene (sc);
        app.select (Block::output);
        run (10.0, 2.0, 1.0);
        save ("rls-hum.png");

        // 6. The chip: the default scene on tt_um_adaptive_filter's arithmetic, the filter selected.
        app.loadChip();
        run (6.0, 2.0, 2.0);
        save ("chip-start.png");
        run (54.0, 20.0, 2.0);
        save ("chip-converged.png");
        const bool chipOk = app.simulator().improvement() > 20.0 && app.simulator().hasTwin();
        std::printf ("%s  the chip's arithmetic converges in the interface (%+.1f dB, float twin %+.1f dB)\n", chipOk ? "ok  " : "FAIL",
                     app.simulator().improvement(), app.simulator().improvementTwin());
        failures += chipOk ? 0 : 1;
        // 7. The chip with the inputs clipping: a full scale too small, the error selected.
        app.scene().filter.fullScale = 0.5;
        app.select (Block::filter);
        run (10.0, 2.0, 2.0);
        save ("chip-clipping.png");
        // The interface's own work a frame (the simulation, the displays), without the software renderer.
        const double ms = 1000.0 * uiSeconds / std::max (1, frames);
        std::printf ("%s  %.1f ms a frame for the simulation and the displays (%d frames, up to ×20)\n", ms < 12.0 ? "ok  " : "FAIL", ms, frames);
        failures += ms < 12.0 ? 0 : 1;
    }
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer (renderer);
    SDL_FreeSurface (surface);
    return failures;
}
