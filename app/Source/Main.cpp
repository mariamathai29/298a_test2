// The adaptive filter explorer v0.2 (float, and the chip's fixed point): the window.  SDL2 opens it; the simulation
// runs in the frames, and Listen plays its sonification (SDL audio, if there is a
// device).  A settings file named on the command line is opened.
//   AdaptiveFilterExplorer [scene.afx.json]

#include "Ui.h"

#include <SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_sdlrenderer2.h>

#include <cstdio>
#include <vector>

int main (int argc, char** argv)
{
    if (SDL_Init (SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_AUDIO) != 0 && SDL_Init (SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0)
    {
        std::fprintf (stderr, "AFX: cannot start SDL: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetHint (SDL_HINT_IME_SHOW_UI, "1");
    SDL_Window* window = SDL_CreateWindow ("Adaptive filter explorer v0.1", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1720, 1040,
                                           SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (window == nullptr)
    {
        std::fprintf (stderr, "AFX: cannot open a window: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetWindowMinimumSize (window, 1560, 900);
    SDL_Renderer* renderer = SDL_CreateRenderer (window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (renderer == nullptr) renderer = SDL_CreateRenderer (window, -1, SDL_RENDERER_SOFTWARE);
    if (renderer == nullptr)
    {
        std::fprintf (stderr, "AFX: cannot draw: %s\n", SDL_GetError());
        return 1;
    }
    SDL_RendererInfo info {};
    SDL_GetRendererInfo (renderer, &info);
    const bool vsync = (info.flags & SDL_RENDERER_PRESENTVSYNC) != 0;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
    afx::applyStyle();
    afx::loadFonts();
    ImGui_ImplSDL2_InitForSDLRenderer (window, renderer);
    ImGui_ImplSDLRenderer2_Init (renderer);

    // Sound for Listen: 48 kHz stereo floats, queued a frame at a time.
    SDL_AudioSpec want {}, have {};
    want.freq = (int) afx::App::soundRate;
    want.format = AUDIO_F32SYS;
    want.channels = 2;
    want.samples = 1024;
    const SDL_AudioDeviceID audio = SDL_WasInit (SDL_INIT_AUDIO) != 0 ? SDL_OpenAudioDevice (nullptr, 0, &want, &have, 0) : 0;
    std::vector<float> sound;

    {
        afx::App app;
        if (argc > 1) app.openProject (argv[1]);
        bool open = true;
        while (open && ! app.quitRequested())
        {
            const Uint64 start = SDL_GetTicks64();
            SDL_Event e;
            while (SDL_PollEvent (&e))
            {
                ImGui_ImplSDL2_ProcessEvent (&e);
                if (e.type == SDL_QUIT) open = false;
                if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_CLOSE && e.window.windowID == SDL_GetWindowID (window))
                    open = false;
                if (e.type == SDL_DROPFILE && e.drop.file != nullptr)   // a settings file dropped on the window is opened
                {
                    app.openProject (e.drop.file);
                    SDL_free (e.drop.file);
                }
            }
            ImGui_ImplSDLRenderer2_NewFrame();
            ImGui_ImplSDL2_NewFrame();
            ImGui::NewFrame();
            app.frame();
            ImGui::Render();
            SDL_RenderSetScale (renderer, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y);
            SDL_SetRenderDrawColor (renderer, 14, 16, 19, 255);
            SDL_RenderClear (renderer);
            ImGui_ImplSDLRenderer2_RenderDrawData (ImGui::GetDrawData(), renderer);
            SDL_RenderPresent (renderer);
            app.takeSound (sound);
            if (audio != 0)
            {
                // no more than a fifth of a second queued: what is heard stays with what is seen
                const Uint32 queued = SDL_GetQueuedAudioSize (audio), most = (Uint32) (0.2 * have.freq) * 2 * sizeof (float);
                if (queued > most) SDL_ClearQueuedAudio (audio);
                if (! sound.empty()) SDL_QueueAudio (audio, sound.data(), (Uint32) (sound.size() * sizeof (float)));
                SDL_PauseAudioDevice (audio, app.listening() ? 0 : 1);
            }
            if (! vsync)
                if (const Uint64 spent = SDL_GetTicks64() - start; spent < 16) SDL_Delay ((Uint32) (16 - spent));
        }
    }

    if (audio != 0) SDL_CloseAudioDevice (audio);
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer (renderer);
    SDL_DestroyWindow (window);
    SDL_Quit();
    return 0;
}
