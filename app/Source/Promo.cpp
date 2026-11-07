// afx_promo: the explorer's 60-second promotional video, rendered off-screen and
// deterministic.  The real interface (Dear ImGui through SDL's software renderer,
// as afx_shot draws it) is driven by a script: scenes, a camera on each (close-ups
// are drawn magnified with the fonts rasterized to match: never upscaled), the
// captions and cards, and the sonification (Sonify.h), rendered in step.
//   afx_promo <out_dir>                 raw RGBA 1920×1080 frames at 30 fps on stdout (for ffmpeg)
//   afx_promo <out_dir> --still t,...   only PNGs of the frames at those times (for checking)
//   afx_promo <out_dir> --audio         only the sound
// Into out_dir: sonification.wav (48 kHz stereo, 24-bit), afx_promo.srt, and one
// frame a scene (frames/scene_1.png ... scene_9.png) for the contact sheet.

#include "Fonts.h"
#include "Plots.h"
#include "Sonify.h"
#include "Tune.h"
#include "Ui.h"
#include "Views.h"

#include <SDL.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

using namespace afx;
namespace fs = std::filesystem;

namespace
{
constexpr int W = 1920, H = 1080, fps = 30;
constexpr double duration = 60.0;
constexpr int totalFrames = (int) (duration * fps);
constexpr float LW = 1600.0f, LH = 900.0f;   // the interface's own size (a wide shot draws it at 1.2×)
constexpr double audioRate = 48000.0, fade = 0.4;
constexpr double dt = 1.0 / fps;

double clamp01 (double v) { return std::clamp (v, 0.0, 1.0); }
double smooth (double u) { u = clamp01 (u); return u * u * (3.0 - 2.0 * u); }
double ramp (double t, double a, double b) { return smooth ((t - a) / (b - a)); }
double logLerp (double a, double b, double u) { return std::exp (std::log (a) + (std::log (b) - std::log (a)) * u); }

// ================================================================ the camera
// A 16:9 window onto the interface (its logical coordinates).
struct Cam
{
    float x = 0, y = 0, w = LW;
    float k() const { return (float) W / w; }
    float h() const { return w * 9.0f / 16.0f; }
    ImVec2 toScreen (ImVec2 p, const SDL_Rect& vp) const
    {
        const float s = (float) vp.w / w;
        return { vp.x + (p.x - x) * s, vp.y + (p.y - y) * s };
    }
};

Cam clampCam (Cam c)
{
    c.w = std::min (c.w, LW);
    c.x = std::clamp (c.x, 0.0f, LW - c.w);
    c.y = std::clamp (c.y, 0.0f, LH - c.h());
    return c;
}

// The camera around a rectangle, with a margin, its centre moved by (dx, dy) of its size.
Cam fit (ImVec2 a, ImVec2 b, float margin, float dx = 0, float dy = 0)
{
    const float bw = b.x - a.x + 2 * margin, bh = b.y - a.y + 2 * margin;
    Cam c;
    c.w = std::max (bw, bh * 16.0f / 9.0f);
    const float cx = 0.5f * (a.x + b.x) + dx * c.w, cy = 0.5f * (a.y + b.y) + dy * c.h();
    c.x = cx - 0.5f * c.w;
    c.y = cy - 0.5f * c.h();
    return clampCam (c);
}

Cam mix (const Cam& a, const Cam& b, double u)
{
    u = clamp01 (u);
    const float w = (float) logLerp (a.w, b.w, u);
    const float ax = a.x + 0.5f * a.w, ay = a.y + 0.5f * a.h(), bx = b.x + 0.5f * b.w, by = b.y + 0.5f * b.h();
    // the centre moves in step with the zoom (so a push-in looks straight)
    const double v = std::abs (b.w - a.w) > 1.0f ? (std::log (w) - std::log (a.w)) / (std::log (b.w) - std::log (a.w)) : u;
    Cam c;
    c.w = w;
    c.x = (float) (ax + (bx - ax) * v) - 0.5f * w;
    c.y = (float) (ay + (by - ay) * v) - 0.5f * c.h();
    return clampCam (c);
}

ImVec2 mark (const std::string& name)
{
    const auto* p = markedItem (name);
    return p != nullptr ? *p : ImVec2 { 0, 0 };
}
Cam fitMark (const std::string& name, float margin, float dx = 0, float dy = 0) { return fit (mark (name), mark (name + ".end"), margin, dx, dy); }

// ================================================================ drawing ImGui's output, transformed
std::vector<float> xyScratch;

void drawData (SDL_Renderer* r, ImDrawData* dd, const Cam& cam, const SDL_Rect& vp)
{
    if (dd == nullptr) return;
    const float s = (float) vp.w / cam.w;
    const float ox = vp.x - cam.x * s, oy = vp.y - cam.y * s;
    for (const ImDrawList* list : dd->CmdLists)
    {
        const ImDrawVert* vtx = list->VtxBuffer.Data;
        const ImDrawIdx* idx = list->IdxBuffer.Data;
        xyScratch.resize ((size_t) list->VtxBuffer.Size * 2);
        for (int i = 0; i < list->VtxBuffer.Size; ++i)
        {
            xyScratch[(size_t) i * 2] = vtx[i].pos.x * s + ox;
            xyScratch[(size_t) i * 2 + 1] = vtx[i].pos.y * s + oy;
        }
        for (const ImDrawCmd& cmd : list->CmdBuffer)
        {
            if (cmd.UserCallback != nullptr) continue;
            const float x0 = std::max ((float) vp.x, cmd.ClipRect.x * s + ox), y0 = std::max ((float) vp.y, cmd.ClipRect.y * s + oy);
            const float x1 = std::min ((float) (vp.x + vp.w), cmd.ClipRect.z * s + ox), y1 = std::min ((float) (vp.y + vp.h), cmd.ClipRect.w * s + oy);
            if (x1 <= x0 || y1 <= y0) continue;
            const SDL_Rect clip { (int) std::floor (x0), (int) std::floor (y0), (int) std::ceil (x1) - (int) std::floor (x0),
                                  (int) std::ceil (y1) - (int) std::floor (y0) };
            SDL_RenderSetClipRect (r, &clip);
            const ImDrawVert* base = vtx + cmd.VtxOffset;
            SDL_RenderGeometryRaw (r, (SDL_Texture*) (intptr_t) cmd.GetTexID(), xyScratch.data() + (size_t) cmd.VtxOffset * 2, (int) sizeof (float) * 2,
                                   (const SDL_Color*) (const void*) ((const char*) base + offsetof (ImDrawVert, col)), (int) sizeof (ImDrawVert),
                                   (const float*) (const void*) ((const char*) base + offsetof (ImDrawVert, uv)), (int) sizeof (ImDrawVert),
                                   list->VtxBuffer.Size - (int) cmd.VtxOffset, idx + cmd.IdxOffset, (int) cmd.ElemCount, (int) sizeof (ImDrawIdx));
        }
    }
    SDL_RenderSetClipRect (r, nullptr);
}

SDL_Texture* atlasTexture (SDL_Renderer* r, ImFontAtlas* atlas)
{
    unsigned char* px = nullptr;
    int tw = 0, th = 0;
    atlas->GetTexDataAsRGBA32 (&px, &tw, &th);
    SDL_Texture* t = SDL_CreateTexture (r, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, tw, th);
    SDL_UpdateTexture (t, nullptr, px, 4 * tw);
    SDL_SetTextureBlendMode (t, SDL_BLENDMODE_BLEND);
    atlas->SetTexID ((ImTextureID) (intptr_t) t);
    return t;
}

// ================================================================ one interface
// Its own ImGui context and fonts (rasterized for the scale it is drawn at), and an App.
class Pane
{
public:
    explicit Pane (SDL_Renderer* r) : renderer (r)
    {
        ctx = ImGui::CreateContext();
        ImGui::SetCurrentContext (ctx);
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;
        io.DisplaySize = { LW, LH };
        io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;   // big meshes (the trajectories) in pieces
        applyStyle();
        ImGui::GetStyle().AntiAliasedLinesUseTex = false;   // the baked lines don't magnify
        app = std::make_unique<App>();
        app->setFixedStep (dt);
    }
    ~Pane()
    {
        ImGui::SetCurrentContext (ctx);
        app.reset();
        if (texture != nullptr) SDL_DestroyTexture (texture);
        ImGui::DestroyContext (ctx);
    }
    Pane (const Pane&) = delete;
    Pane& operator= (const Pane&) = delete;

    App& a() { return *app; }
    Simulator& sim() { return app->simulator(); }
    void mouse (ImVec2 p, bool down)
    {
        mousePos = p;
        mouseDown = down;
    }
    ImVec2 mouseAt() const { return mousePos; }
    bool mouseIsDown() const { return mouseDown; }

    // One frame of the interface, laid out for drawing at k screen pixels to its pixel.
    void step (float k)
    {
        ImGui::SetCurrentContext (ctx);
        k = std::clamp (k, 1.0f, 4.0f);
        if (atlasK <= 0.0f || std::abs (k / atlasK - 1.0f) > 0.012f)
        {
            auto& io = ImGui::GetIO();
            io.Fonts->Clear();
            loadFonts (k);
            io.Fonts->Build();
            if (texture != nullptr) SDL_DestroyTexture (texture);
            texture = atlasTexture (renderer, io.Fonts);
            io.Fonts->ClearTexData();
            mine = fonts();
            atlasK = k;
        }
        fonts() = mine;
        auto& io = ImGui::GetIO();
        io.DisplaySize = { LW, LH };
        io.DeltaTime = (float) dt;
        io.AddMousePosEvent (mousePos.x, mousePos.y);
        if (mouseDown != sentDown)
        {
            io.AddMouseButtonEvent (0, mouseDown);
            sentDown = mouseDown;
        }
        auto& st = ImGui::GetStyle();
        st.CircleTessellationMaxError = 0.3f / atlasK;
        st.CurveTessellationTol = 1.25f / atlasK;
        ImGui::NewFrame();
        ctx->DrawListSharedData.InitialFringeScale = 1.0f / atlasK;
        app->frame();
        ImGui::Render();
        data = ImGui::GetDrawData();
    }
    void draw (const Cam& c, const SDL_Rect& vp) { drawData (renderer, data, c, vp); }

private:
    SDL_Renderer* renderer;
    ImGuiContext* ctx = nullptr;
    std::unique_ptr<App> app;
    SDL_Texture* texture = nullptr;
    float atlasK = 0.0f;
    Fonts mine;
    ImDrawData* data = nullptr;
    ImVec2 mousePos { -10000, -10000 };
    bool mouseDown = false, sentDown = false;
};

// ================================================================ the overlay: captions, cards, labels
struct OverlayFonts
{
    ImFont *caption, *title, *tagline, *endTitle, *endLine, *endSmall, *label, *labelSmall;
};

class Overlay
{
public:
    explicit Overlay (SDL_Renderer* r) : renderer (r)
    {
        ctx = ImGui::CreateContext();
        ImGui::SetCurrentContext (ctx);
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = { (float) W, (float) H };
        io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
        ImGui::GetStyle().AntiAliasedLinesUseTex = false;
        static const ImWchar ranges[] = { 0x0020, 0x00ff, 0x2010, 0x2044, 0x2190, 0x21ff, 0x2212, 0x2212, 0x2713, 0x2713, 0 };
        auto add = [&] (Face face, float size) {
            ImFontConfig cfg;
            cfg.FontDataOwnedByAtlas = false;
            cfg.OversampleH = 2;
            const auto& e = font (face);
            return io.Fonts->AddFontFromMemoryTTF (const_cast<unsigned char*> (e.data), (int) e.size, size, &cfg, ranges);
        };
        f.caption = add (Face::sansBold, 40.0f);
        f.title = add (Face::sansBold, 104.0f);
        f.tagline = add (Face::sans, 42.0f);
        f.endTitle = add (Face::sansBold, 84.0f);
        f.endLine = add (Face::sans, 40.0f);
        f.endSmall = add (Face::sans, 31.0f);
        f.label = add (Face::sansBold, 34.0f);
        f.labelSmall = add (Face::sans, 24.0f);
        io.Fonts->Build();
        texture = atlasTexture (renderer, io.Fonts);
    }
    ~Overlay()
    {
        SDL_DestroyTexture (texture);
        ImGui::DestroyContext (ctx);
    }
    const OverlayFonts& fontsOf() const { return f; }
    // Draws whatever `paint` puts on the foreground, at screen scale.
    void pass (const std::function<void (ImDrawList*)>& paint)
    {
        ImGui::SetCurrentContext (ctx);
        auto& io = ImGui::GetIO();
        io.DeltaTime = (float) dt;
        io.DisplaySize = { (float) W, (float) H };
        ImGui::NewFrame();
        paint (ImGui::GetForegroundDrawList());
        ImGui::Render();
        drawData (renderer, ImGui::GetDrawData(), Cam { 0, 0, (float) W }, SDL_Rect { 0, 0, W, H });
    }

private:
    SDL_Renderer* renderer;
    ImGuiContext* ctx;
    SDL_Texture* texture;
    OverlayFonts f;
};

ImU32 rgba (int r, int g, int b, double a) { return IM_COL32 (r, g, b, (int) std::lround (255.0 * clamp01 (a))); }
ImU32 fadeCol (ImU32 c, double a)
{
    const int alpha = (int) ((c >> IM_COL32_A_SHIFT) & 0xff);
    return (c & ~IM_COL32_A_MASK) | ((ImU32) std::lround (alpha * clamp01 (a)) << IM_COL32_A_SHIFT);
}
ImVec2 textSizeOf (ImFont* f, const std::string& s) { return f->CalcTextSizeA (f->FontSize, 1e9f, 0.0f, s.c_str()); }
void text (ImDrawList* dl, ImFont* f, ImVec2 p, ImU32 c, const std::string& s) { dl->AddText (f, f->FontSize, p, c, s.c_str()); }
void textCentred (ImDrawList* dl, ImFont* f, float cx, float y, ImU32 c, const std::string& s)
{
    text (dl, f, { std::round (cx - 0.5f * textSizeOf (f, s).x), y }, c, s);
}

// The lower third: a rounded translucent box, an accent bar, the line in white.
void caption (ImDrawList* dl, const OverlayFonts& f, const std::string& line, double a, double rise)
{
    if (a <= 0.0) return;
    const ImVec2 ts = textSizeOf (f.caption, line);
    const float padX = 26, padY = 17, bar = 6, gap = 20;
    const float x0 = 80, y1 = (float) H - 76 + (float) (10.0 * (1.0 - rise)), y0 = y1 - ts.y - 2 * padY;
    const float x1 = x0 + padX + bar + gap + ts.x + padX;
    dl->AddRectFilled ({ x0, y0 }, { x1, y1 }, rgba (10, 12, 16, 0.74 * a), 12.0f);
    dl->AddRectFilled ({ x0 + padX, y0 + padY - 2 }, { x0 + padX + bar, y1 - padY + 2 }, fadeCol (accent, a), 3.0f);
    text (dl, f.caption, { x0 + padX + bar + gap, y0 + padY - 1 }, rgba (255, 255, 255, a), line);
}

// A ring round a control being used.
void ring (ImDrawList* dl, ImVec2 c, ImVec2 half, double a, double pulse)
{
    if (a <= 0.0) return;
    const float g = (float) (4.0 + 6.0 * pulse);
    dl->AddRect ({ c.x - half.x - g, c.y - half.y - g }, { c.x + half.x + g, c.y + half.y + g }, fadeCol (accent, a * (1.0 - 0.6 * pulse)), 10.0f, 0, 3.5f);
}

// The pointer: an arrow, white with a dark edge.
void pointer (ImDrawList* dl, ImVec2 p, float s, bool down, double a)
{
    if (a <= 0.0) return;
    const ImVec2 pts[] = { { 0, 0 }, { 0, 17 }, { 4.2f, 13.2f }, { 7.0f, 19.5f }, { 9.6f, 18.4f }, { 6.9f, 12.3f }, { 12.2f, 12.3f } };
    ImVec2 q[7];
    for (int i = 0; i < 7; ++i) q[i] = { p.x + pts[i].x * s, p.y + pts[i].y * s };
    if (down) dl->AddCircleFilled (p, 15.0f * s, rgba (255, 180, 84, 0.35 * a));
    dl->AddConcavePolyFilled (q, 7, rgba (250, 250, 250, a));
    dl->AddPolyline (q, 7, rgba (0, 0, 0, 0.9 * a), ImDrawFlags_Closed, 1.4f * s);
}

// ================================================================ sound
struct Track
{
    std::unique_ptr<Listener> listener;
    std::vector<float> stream;
    long long startFrame = 0;   // the audio frame of stream[0] (before the latency)
    double inA = 0, inB = 0, outA = 1e9, outB = 1e9, gain = 1.0;
};

double envelope (const Track& tr, double t)
{
    double g = tr.gain;
    if (t < tr.inA) return 0.0;
    if (t < tr.inB) g *= std::sin (0.5 * pi * (t - tr.inA) / (tr.inB - tr.inA));
    if (t >= tr.outB) return 0.0;
    if (t > tr.outA) g *= std::cos (0.5 * pi * (t - tr.outA) / (tr.outB - tr.outA));
    return g;
}

// A soft bell (the race's finish) and a tick (a click).
void bell (std::vector<float>& out, double at, double f, double pan, double g)
{
    const long long s0 = (long long) (at * audioRate), len = (long long) (1.6 * audioRate);
    const double pl = std::cos ((pan + 1) * pi / 4) * std::sqrt (2.0), pr = std::sin ((pan + 1) * pi / 4) * std::sqrt (2.0);
    for (long long i = 0; i < len && (size_t) (s0 + i) * 2 + 1 < out.size(); ++i)
    {
        const double t = (double) i / audioRate;
        const double env = (1.0 - std::exp (-t / 0.004)) * std::exp (-t / 0.45);
        const double v = g * env * (std::sin (2 * pi * f * t) + 0.25 * std::sin (2 * pi * 2.0 * f * t) * std::exp (-t / 0.15) + 0.12 * std::sin (2 * pi * 2.76 * f * t) * std::exp (-t / 0.08));
        out[(size_t) (s0 + i) * 2] += (float) (v * pl);
        out[(size_t) (s0 + i) * 2 + 1] += (float) (v * pr);
    }
}

void tick (std::vector<float>& out, double at, double g)
{
    const long long s0 = (long long) (at * audioRate), len = (long long) (0.03 * audioRate);
    unsigned seed = (unsigned) (at * 1000.0) * 2654435761u + 1u;
    double lp = 0.0, hp = 0.0, prev = 0.0;
    for (long long i = 0; i < len && (size_t) (s0 + i) * 2 + 1 < out.size(); ++i)
    {
        seed = seed * 1664525u + 1013904223u;
        const double w = (double) (seed >> 8) / 8388608.0 - 1.0;
        lp += 0.35 * (w - lp);
        hp = 0.9 * (hp + lp - prev);
        prev = lp;
        const double t = (double) i / audioRate;
        const double v = g * hp * std::exp (-t / 0.0025) + g * 0.5 * std::sin (2 * pi * 2200.0 * t) * std::exp (-t / 0.004);
        out[(size_t) (s0 + i) * 2] += (float) v;
        out[(size_t) (s0 + i) * 2 + 1] += (float) v;
    }
}

bool writeWav (const fs::path& path, const std::vector<float>& stereo)
{
    FILE* f = std::fopen (path.string().c_str(), "wb");
    if (f == nullptr) return false;
    const uint32_t frames = (uint32_t) (stereo.size() / 2), bytes = frames * 2 * 3;
    auto u32 = [&] (uint32_t v) { std::fwrite (&v, 4, 1, f); };
    auto u16 = [&] (uint16_t v) { std::fwrite (&v, 2, 1, f); };
    std::fwrite ("RIFF", 1, 4, f);
    u32 (36 + bytes);
    std::fwrite ("WAVEfmt ", 1, 8, f);
    u32 (16);
    u16 (1);
    u16 (2);
    u32 ((uint32_t) audioRate);
    u32 ((uint32_t) audioRate * 6);
    u16 (6);
    u16 (24);
    std::fwrite ("data", 1, 4, f);
    u32 (bytes);
    std::vector<unsigned char> buf (bytes);
    for (size_t i = 0; i < stereo.size(); ++i)
    {
        const int v = (int) std::lround (std::clamp ((double) stereo[i], -1.0, 1.0) * 8388607.0);
        buf[i * 3] = (unsigned char) (v & 0xff);
        buf[i * 3 + 1] = (unsigned char) ((v >> 8) & 0xff);
        buf[i * 3 + 2] = (unsigned char) ((v >> 16) & 0xff);
    }
    std::fwrite (buf.data(), 1, buf.size(), f);
    std::fclose (f);
    return true;
}

std::string srtTime (double t)
{
    const int ms = (int) std::lround (t * 1000.0);
    char b[32];
    std::snprintf (b, sizeof b, "%02d:%02d:%02d,%03d", ms / 3600000, (ms / 60000) % 60, (ms / 1000) % 60, ms % 1000);
    return b;
}

// ================================================================ the script
// When each scene is on screen (crossfades of `fade` centred on the boundaries).
constexpr double tTitle = 0.0, tS1 = 3.0, tS2 = 8.8, tS3 = 15.6, tS4 = 21.4, tS5 = 27.4, tS6 = 33.2, tS7 = 39.6, tS8 = 45.6, tS9 = 51.0, tEnd = 56.0;
constexpr double tS7b = tS7 + 2.9;   // 16 taps
constexpr double h = 0.5 * fade;

struct Caption
{
    double a, b;
    const char* line;
};
const Caption captions[] = {
    { tS1 + 0.3, tS2 - 0.25, "Somewhere in this signal is a second heart" },
    { tS2 + 0.35, tS3 - 0.25, "8-tap sign-sign LMS cancels the mother’s heartbeat" },
    { tS3 + 0.35, tS4 - 0.25, "What’s left is the baby’s heartbeat" },
    { tS4 + 0.35, tS5 - 0.25, "The weights settle near the Wiener solution" },
    { tS5 + 0.35, tS6 - 0.25, "Matching the path where the ECG has energy" },
    { tS6 + 0.35, tS7 - 0.25, "Race them: SS, LMS, NLMS and RLS" },
    { tS7 + 0.35, tS7b - 0.1, "A 13-tap echo: 8 taps fall short" },
    { tS7b + 0.1, tS8 - 0.25, "16 taps nail it" },
    { tS8 + 0.35, tS9 - 0.25, "Mains hum, cancelled to silence" },
    { tS9 + 0.35, tEnd - 0.25, "Swap sources and paths: build your own experiment" },
};

struct Placement
{
    Pane* view;
    Cam cam;
    SDL_Rect vp { 0, 0, W, H };
};

struct Shot
{
    double start, end;
    std::function<std::vector<Placement> (double t)> layout;   // the interfaces it shows
    std::function<void (ImDrawList*, double t, double alpha)> paint;   // on top (at screen scale)
    double mid() const { return 0.5 * (start + end); }
};

struct ViewSlot
{
    double on, off;   // stepped while on <= t < off
    std::function<void (Pane&, std::vector<Track>&)> init;
    std::function<void (Pane&, double t)> script;
    std::unique_ptr<Pane> view;
    std::vector<Track> tracks;
    float k = 1.2f;
    bool done = false;
};

Scene humScene()
{
    Scene sc = Scene::defaults();
    sc.signal = SourceSpec::defaults (SourceKind::maternalEcg);
    sc.signal.seed = 11;
    sc.signal.bpm = 68.0;
    sc.interference = SourceSpec::defaults (SourceKind::hum);
    sc.interference.amplitude = 0.5;
    sc.primaryPath = pathFromPreset ("short echo");
    sc.filter.algorithm = Algorithm::lms;
    sc.filter.mu[(size_t) Algorithm::lms] = 0.05;
    return sc;
}

Track listenerTrack (const Scene& sc, double inA, double inB, double outA, double outB, double gain, double texture, long long startFrame)
{
    Track tr;
    tr.listener = std::make_unique<Listener> (audioRate);
    tr.listener->voicesFrom (sc);
    tr.listener->sonifier().setTexture (texture);
    tr.inA = inA;
    tr.inB = inB;
    tr.outA = outA;
    tr.outB = outB;
    tr.gain = gain;
    tr.startFrame = startFrame;
    return tr;
}
} // namespace

int main (int argc, char** argv)
{
    const fs::path dir = argc > 1 ? argv[1] : "promo";
    std::vector<double> stills;
    if (argc > 3 && std::string (argv[2]) == "--still")
    {
        std::string s = argv[3];
        size_t p = 0;
        while (p < s.size())
        {
            const size_t q = s.find (',', p);
            stills.push_back (std::stod (s.substr (p, q - p)));
            if (q == std::string::npos) break;
            p = q + 1;
        }
    }
    const bool audioOnly = argc > 2 && std::string (argv[2]) == "--audio";
    const bool video = stills.empty() && ! audioOnly;
    fs::create_directories (dir / "frames");
    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat (0, W, H, 32, SDL_PIXELFORMAT_ABGR8888);
    SDL_Renderer* renderer = SDL_CreateSoftwareRenderer (surface);
    if (surface == nullptr || renderer == nullptr)
    {
        std::fprintf (stderr, "afx_promo: cannot draw: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetRenderDrawBlendMode (renderer, SDL_BLENDMODE_BLEND);
    auto overlayOwner = std::make_unique<Overlay> (renderer);
    Overlay& overlay = *overlayOwner;
    const auto& of = overlay.fontsOf();

    std::vector<float> sound ((size_t) (duration * audioRate) * 2, 0.0f);   // the sonification (beyond the listeners: bells, ticks)
    const long long latencyFrames = (long long) std::lround (HeartSonifier::latency * audioRate);

    // ---- the interfaces ----
    std::vector<ViewSlot> slots;
    auto fr = [] (double t) { return (long long) std::lround (t * audioRate); };

    // V1: the default scene, the filter frozen at 0 (e = d), then learning: S1 to S3.
    slots.push_back ({ 0.0, tS4 + h, [&] (Pane& v, std::vector<Track>& tracks) {
                          Scene sc = Scene::defaults();
                          sc.filter.freeze = true;
                          v.a().setScene (sc);
                          v.a().select (Block::filter);
                          v.sim().run ((long long) (6.0 * sampleRate));
                          tracks.push_back (listenerTrack (sc, 0.9, 2.6, tS4 - h, tS4 + h, 1.0, 0.12, 0));
                      },
                       [&] (Pane& v, double t) {
                           auto& sc = v.a().scene();
                           if (t >= tS2 + 0.55 && sc.filter.freeze) sc.filter.freeze = false;
                           double speed = 1.0;
                           if (t >= tS2 + 0.8 && t < tS3 - 0.2) speed = logLerp (1.0, 10.0, ramp (t, tS2 + 0.8, tS2 + 3.3));
                           v.a().setSpeed (speed);
                       } });
    // V4: the default scene from the start, a close look at the weights and the response: S4, S5.
    slots.push_back ({ tS4 - h, tS6 + h, [&] (Pane& v, std::vector<Track>& tracks) {
                          v.a().loadDefaults();
                          v.a().select (Block::filter);
                          v.a().setSpeed (1.0);
                          tracks.push_back (listenerTrack (Scene::defaults(), tS4 - h, tS4 + h, tS6 - h, tS6 + h, 1.0, 0.12, fr (tS4 - h)));
                      },
                       [&] (Pane& v, double t) {
                           double speed = 1.0;
                           if (t >= tS4 + 0.3) speed = logLerp (1.0, 6.0, ramp (t, tS4 + 0.3, tS4 + 1.0));
                           if (t >= tS5) speed = logLerp (6.0, 2.0, ramp (t, tS5, tS5 + 0.6));
                           v.a().setSpeed (speed);
                       } });
    // V6: the race, four of them.
    const Algorithm racers[4] = { Algorithm::signSign, Algorithm::lms, Algorithm::nlms, Algorithm::rls };
    const double racePan[4] = { -0.75, -0.25, 0.25, 0.75 }, racePitch[4] = { 0.85, 1.0, 1.15, 1.35 };
    for (int i = 0; i < 4; ++i)
        slots.push_back ({ tS6 - h, tS7 + h, [&, i] (Pane& v, std::vector<Track>& tracks) {
                              Scene sc = Scene::defaults();
                              sc.filter.algorithm = racers[i];
                              v.a().setScene (sc);
                              v.a().select (Block::filter);
                              v.a().setSpeed (10.0);
                              v.a().setRunning (false);
                              auto tr = listenerTrack (sc, tS6 - h, tS6 + h, tS7 - h, tS7 + h, 0.6, 0.0, fr (tS6 - h));
                              ThumpVoice mother = adultHeart(), baby = fetalHeart();
                              mother.pan = baby.pan = racePan[i];
                              mother.pitch *= racePitch[i];
                              baby.level *= 0.6;
                              tr.listener->sonifier().setVoices (mother, baby);
                              tracks.push_back (std::move (tr));
                          },
                           [&] (Pane& v, double t) { v.a().setRunning (t >= tS6 + 0.25); } });
    // V7: a 13-tap echo; 8 taps of LMS, then 16.
    slots.push_back ({ tS7 - h, tS8 + h, [&] (Pane& v, std::vector<Track>& tracks) {
                          Scene sc = Scene::defaults();
                          sc.primaryPath = pathFromPreset ("long echo");
                          sc.filter.algorithm = Algorithm::lms;
                          sc.filter.mu[(size_t) Algorithm::lms] = 0.02;
                          v.a().setScene (sc);
                          v.a().select (Block::filter);
                          v.sim().run ((long long) (30.0 * sampleRate));
                          v.a().setStatus ("A 13-tap echo on the way to the abdomen: an 8-tap filter can't hold it; 16 taps can.");
                          tracks.push_back (listenerTrack (sc, tS7 - h, tS7 + h, tS8 - h, tS8 + h, 1.0, 0.12, fr (tS7 - h)));
                      },
                       [&] (Pane& v, double t) {
                           if (t >= tS7b && v.a().scene().filter.taps != 16) v.a().scene().filter.taps = 16;
                           v.a().setSpeed (t < tS7b ? 3.0 : logLerp (3.0, 10.0, ramp (t, tS7b, tS7b + 0.5)));
                       } });
    // V8: mains hum on an adult ECG, LMS from a reference of the hum.
    slots.push_back ({ tS8 - h, tS9 + h, [&] (Pane& v, std::vector<Track>& tracks) {
                          Scene sc = humScene();
                          sc.filter.freeze = true;
                          v.a().setScene (sc);
                          v.a().select (Block::interference);
                          v.a().setSpeed (1.0);
                          v.sim().run ((long long) (5.0 * sampleRate));
                          v.a().setStatus ("Mains hum: 60 Hz and its third harmonic on an ECG, cancelled by LMS from a reference of the hum itself.");
                          tracks.push_back (listenerTrack (sc, tS8 - h, tS8 + h, tS9 - h, tS9 + h, 0.9, 0.45, fr (tS8 - h)));
                      },
                       [&] (Pane& v, double t) {
                           if (t >= tS8 + 1.1 && v.a().scene().filter.freeze) v.a().scene().filter.freeze = false;
                       } });
    // V9: build your own: a path preset from the list, ×100, freeze.
    slots.push_back ({ tS9 - h, duration, [&] (Pane& v, std::vector<Track>& tracks) {
                          v.a().loadDefaults();
                          v.a().select (Block::filter);
                          v.a().setSpeed (2.0);
                          v.sim().run ((long long) (50.0 * sampleRate));
                          tracks.push_back (listenerTrack (Scene::defaults(), tS9 - h, tS9 + h, tEnd + 1.0, duration - 0.3, 1.0, 0.12, fr (tS9 - h)));
                      },
                       [&] (Pane& v, double t) {
                           const double s = t - tS9;
                           auto lerpP = [] (ImVec2 a, ImVec2 b, double u) {
                               u = smooth (u);
                               return ImVec2 { a.x + (b.x - a.x) * (float) u, a.y + (b.y - a.y) * (float) u };
                           };
                           const ImVec2 start { 860, 640 }, hp = mark ("block.primaryPath"), combo = mark ("path.primaryPath.preset"),
                                        item = mark ("path.primaryPath.preset.rough"), freeze = mark ("freeze");
                           ImVec2 speedFrom {}, speedTo {};
                           tunePoint ("view.speed", 2.0, speedFrom);
                           tunePoint ("view.speed", 100.0, speedTo);
                           ImVec2 p = start;
                           bool down = false;
                           if (s < 0.75) p = lerpP (start, hp, (s - 0.05) / 0.6);
                           else if (s < 1.05) p = hp;
                           else if (s < 1.45) p = lerpP (hp, combo, (s - 1.05) / 0.4);
                           else if (s < 1.75) p = combo;
                           else if (s < 2.15) p = lerpP (combo, item, (s - 1.75) / 0.4);
                           else if (s < 2.4) p = item;
                           else if (s < 2.8) p = lerpP (item, speedFrom, (s - 2.4) / 0.4);
                           else if (s < 3.5) p = lerpP (speedFrom, speedTo, (s - 2.85) / 0.6);
                           else if (s < 3.95) p = lerpP (speedTo, freeze, (s - 3.5) / 0.45);
                           else p = freeze;
                           down = (s >= 0.8 && s < 0.9) || (s >= 1.5 && s < 1.6) || (s >= 2.2 && s < 2.3) || (s >= 4.15 && s < 4.25) || (s >= 2.82 && s < 3.47);
                           // the speed follows the pointer along its bar (drawn as a drag)
                           if (s >= 2.82 && s < 3.5) v.a().setSpeed (logLerp (2.0, 100.0, smooth ((s - 2.85) / 0.6)));
                           else if (s >= 3.5) v.a().setSpeed (100.0);
                           v.mouse (p, down && (s < 2.82 || s >= 3.5));
                       } });
    enum { V1, V4, V6, V7 = V6 + 4, V8, V9 };
    auto viewOf = [&] (int i) { return slots[(size_t) i].view.get(); };

    // clicks heard as ticks
    for (const double s : { 0.8, 1.5, 2.2, 4.15 }) tick (sound, tS9 + s + 0.09, 0.06);

    // ---- the shots ----
    std::vector<Shot> shots;
    // the title: over the abdominal trace, slowly
    shots.push_back ({ tTitle, tS1, nullptr, [&] (ImDrawList* dl, double t, double) {
                          const double a = ramp (t, 0.15, 1.6);
                          dl->AddRectFilled ({ 0, 0 }, { (float) W, (float) H }, windowBack);
                          if (Pane* v = viewOf (V1))
                          {
                              std::vector<float> d;
                              v->sim().history (Channel::primary).last ((size_t) (4.0 * sampleRate), d);
                              std::vector<ImVec2> pts;
                              const float y0 = 820, scale = 150;
                              for (size_t i = 0; i < d.size(); i += 2) pts.push_back ({ (float) i / (float) (4.0 * sampleRate) * W, y0 - d[i] * scale });
                              dl->AddPolyline (pts.data(), (int) pts.size(), fadeCol (colourPrimary, 0.5 * ramp (t, 0.4, 2.0)), 0, 2.4f);
                          }
                          const std::string name = "Adaptive Filter Explorer", tag = "Hear one heartbeat fade, and another emerge";
                          textCentred (dl, of.title, 0.5f * W, 380, rgba (255, 255, 255, a), name);
                          const float rw = 140.0f * (float) ramp (t, 0.5, 1.8);
                          dl->AddRectFilled ({ 0.5f * W - rw, 520 }, { 0.5f * W + rw, 524 }, fadeCol (accent, a));
                          textCentred (dl, of.tagline, 0.5f * W, 552, rgba (211, 216, 222, ramp (t, 0.6, 2.0)), tag);
                      } });
    shots.push_back ({ tS1, tS2, [&] (double t) {
                          const Cam a = fitMark ("view.traces", 6, 0.0f, 0.0f), b = fit (mark ("view.trace.d"), mark ("view.trace.x.end"), 4, 0.04f, 0.0f);
                          return std::vector<Placement> { { viewOf (V1), mix (a, b, smooth ((t - tS1 + h) / (tS2 - tS1 + fade))) } };
                      },
                       nullptr });
    // wide, then slowly towards the learning curve (the right column kept whole)
    auto s2cam = [&] (double t) { return mix (Cam { 0, 0, LW }, Cam { LW * 0.12f, 0.0f, LW * 0.88f }, smooth ((t - tS2 - 1.2) / (tS3 - tS2 - 0.8))); };
    shots.push_back ({ tS2, tS3, [&] (double t) {
                          return std::vector<Placement> { { viewOf (V1), s2cam (t) } };
                      },
                       [&] (ImDrawList* dl, double t, double alpha) {
                           const double s = t - tS2;
                           if (s > 0.1 && s < 1.6)
                           {
                               const Cam c = s2cam (t);
                               const ImVec2 at = c.toScreen (mark ("freeze"), { 0, 0, W, H });
                               ring (dl, at, { 33 * c.k(), 11 * c.k() }, alpha * ramp (s, 0.1, 0.35) * (1.0 - ramp (s, 1.2, 1.6)), ramp (s, 0.5, 0.7) * (1.0 - ramp (s, 0.7, 1.1)));
                           }
                       } });
    shots.push_back ({ tS3, tS4, [&] (double t) {
                          const ImVec2 e = mark ("view.trace.e"), ee = mark ("view.trace.e.end");
                          const Cam a = fit ({ ee.x - 760, e.y }, ee, 8), b = fit ({ ee.x - 540, e.y + 20 }, { ee.x, ee.y - 10 }, 4);
                          return std::vector<Placement> { { viewOf (V1), mix (a, b, smooth ((t - tS3 + h) / (tS4 - tS3 + fade))) } };
                      },
                       nullptr });
    shots.push_back ({ tS4, tS5, [&] (double t) {
                          const Cam a = fitMark ("view.weights", 14), b = fitMark ("view.weights", -10, 0.0f, -0.08f);
                          return std::vector<Placement> { { viewOf (V4), mix (a, b, smooth ((t - tS4 + h) / (tS5 - tS4 + fade))) } };
                      },
                       nullptr });
    shots.push_back ({ tS5, tS6, [&] (double t) {
                          const Cam a = fitMark ("view.response", 16), b = fitMark ("view.response", -8, -0.1f, 0.0f);
                          return std::vector<Placement> { { viewOf (V4), mix (a, b, smooth ((t - tS5 + h) / (tS6 - tS5 + fade))) } };
                      },
                       nullptr });
    // the race's tiles: 2 × 2, clear of the caption
    auto raceTile = [] (int i) {
        constexpr int tw = 800, th = 450, gap = 14, x0 = (W - 2 * tw - gap) / 2, y0 = 14;
        return SDL_Rect { x0 + (i % 2) * (tw + gap), y0 + (i / 2) * (th + gap), tw, th };
    };
    double finished[4] = { -1, -1, -1, -1 };
    double finishedSim[4] = { -1, -1, -1, -1 };
    shots.push_back ({ tS6, tS7, [&] (double) {
                          std::vector<Placement> p;
                          const Cam c = fitMark ("view.learning", 4);
                          for (int i = 0; i < 4; ++i) p.push_back ({ viewOf (V6 + i), c, raceTile (i) });
                          return p;
                      },
                       [&] (ImDrawList* dl, double t, double alpha) {
                           for (int i = 0; i < 4; ++i)
                           {
                               const SDL_Rect r = raceTile (i);
                               dl->AddRect ({ (float) r.x - 1, (float) r.y - 1 }, { (float) (r.x + r.w) + 1, (float) (r.y + r.h) + 1 }, rgba (60, 68, 78, alpha), 4.0f, 0, 2.0f);
                               const float x1 = (float) (r.x + r.w) - 22, y1 = (float) (r.y + r.h) - 46;
                               const std::string name = algorithmShort (racers[i]), full = algorithmName (racers[i]);
                               const bool done = finished[i] >= 0.0 && t >= finished[i];
                               std::string sub = full;
                               if (done)
                               {
                                   char b[64];
                                   std::snprintf (b, sizeof b, "+20 dB in %.0f s", std::max (1.0, finishedSim[i]));
                                   if (finishedSim[i] < 1.0) std::snprintf (b, sizeof b, "+20 dB in %.1f s", finishedSim[i]);
                                   sub = b;
                               }
                               const ImVec2 ns = textSizeOf (of.label, name), ss = textSizeOf (of.labelSmall, sub);
                               const float bw = ns.x + ss.x + 54, bh = 50;
                               const ImU32 back = done ? fadeCol (accent, 0.92 * alpha) : rgba (10, 12, 16, 0.78 * alpha);
                               const ImU32 fore = done ? rgba (20, 16, 10, alpha) : rgba (255, 255, 255, alpha);
                               dl->AddRectFilled ({ x1 - bw, y1 - bh }, { x1, y1 }, back, 10.0f);
                               text (dl, of.label, { x1 - bw + 16, y1 - bh + 6 }, fore, name);
                               text (dl, of.labelSmall, { x1 - bw + 38 + ns.x, y1 - bh + 14 }, done ? rgba (40, 30, 15, alpha) : rgba (200, 205, 210, alpha), sub);
                           }
                       } });
    shots.push_back ({ tS7, tS8, [&] (double t) {
                          const Cam a = fit (mark ("view.learning"), mark ("view.weights.end"), 34, -0.02f, 0.0f);
                          const Cam b = fit (mark ("view.learning"), mark ("view.weights.end"), 3);
                          return std::vector<Placement> { { viewOf (V7), mix (a, b, smooth ((t - tS7 + h) / (tS8 - tS7 + fade))) } };
                      },
                       nullptr });
    shots.push_back ({ tS8, tS9, [&] (double t) {
                          const Cam a = fit (mark ("view.diagram"), mark ("view.traces.end"), 4), b = fit (mark ("view.trace.d"), mark ("view.traces.end"), 0, 0.0f, 0.02f);
                          return std::vector<Placement> { { viewOf (V8), mix (a, b, smooth ((t - tS8 + h) / (tS9 - tS8 + fade))) } };
                      },
                       nullptr });
    auto s9cam = [&] (double t) { return mix (Cam { 0, 0, LW }, Cam { 20, 10, LW - 80 }, smooth ((t - tS9 + h) / (tEnd - tS9 + fade))); };
    shots.push_back ({ tS9, tEnd, [&] (double t) { return std::vector<Placement> { { viewOf (V9), s9cam (t) } }; },
                       [&] (ImDrawList* dl, double t, double alpha) {
                           if (Pane* v = viewOf (V9))
                           {
                               const Cam c = s9cam (t);
                               pointer (dl, c.toScreen (v->mouseAt(), { 0, 0, W, H }), 1.5f, v->mouseIsDown() || (t - tS9 >= 2.82 && t - tS9 < 3.47), alpha);
                           }
                       } });
    shots.push_back ({ tEnd, duration + 1.0, nullptr, [&] (ImDrawList* dl, double t, double) {
                          dl->AddRectFilled ({ 0, 0 }, { (float) W, (float) H }, windowBack);
                          const double a = ramp (t, tEnd + 0.1, tEnd + 0.8);
                          textCentred (dl, of.endTitle, 0.5f * W, 352, rgba (255, 255, 255, a), "Adaptive Filter Explorer v0.1");
                          dl->AddRectFilled ({ 0.5f * W - 120, 476 }, { 0.5f * W + 120, 480 }, fadeCol (accent, a));
                          textCentred (dl, of.endLine, 0.5f * W, 512, rgba (220, 224, 228, ramp (t, tEnd + 0.4, tEnd + 1.1)),
                                       "An interactive lab for adaptive interference cancelling");
                          textCentred (dl, of.endSmall, 0.5f * W, 600, rgba (150, 158, 166, ramp (t, tEnd + 0.7, tEnd + 1.4)), "Built in C++ · Dear ImGui on SDL2");
                          textCentred (dl, of.endSmall, 0.5f * W, 650, rgba (110, 118, 126, ramp (t, tEnd + 0.9, tEnd + 1.6)), "All signals synthetic");
                          const double black = ramp (t, duration - 1.1, duration - 0.15);
                          if (black > 0) dl->AddRectFilled ({ 0, 0 }, { (float) W, (float) H }, rgba (0, 0, 0, black));
                      } });

    // ---- the SRT ----
    {
        FILE* f = std::fopen ((dir / "afx_promo.srt").string().c_str(), "w");
        int n = 1;
        for (const auto& c : captions) std::fprintf (f, "%d\n%s --> %s\n%s\n\n", n++, srtTime (c.a).c_str(), srtTime (c.b).c_str(), c.line);
        std::fclose (f);
    }

    // A probe, so the marks (where the parts are) are known before the first shot.
    {
        Pane probe (renderer);
        probe.step (1.2f);
        probe.step (1.2f);
        if (std::getenv ("AFX_PROMO_MARKS") != nullptr)
            for (const char* m : { "view.inspector", "view.diagram", "view.traces", "view.trace.d", "view.trace.e", "view.learning", "view.weights", "view.response" })
                std::fprintf (stderr, "%-16s %7.1f %7.1f  %7.1f %7.1f\n", m, mark (m).x, mark (m).y, mark (std::string (m) + ".end").x, mark (std::string (m) + ".end").y);
    }

    std::vector<uint32_t> bufA ((size_t) W * H), bufB ((size_t) W * H);
    std::vector<unsigned char> row ((size_t) W * H * 4);
    std::FILE* out = video ? stdout : nullptr;
    const std::vector<double> sceneMids = { shots[1].mid(), shots[2].mid(), shots[3].mid(), shots[4].mid(), shots[5].mid(), shots[6].mid() + 0.6, shots[7].mid() + 1.6, shots[8].mid(), shots[9].mid() - 0.4 };
    std::vector<int> stillFrames, sceneFrames;
    for (double s : stills) stillFrames.push_back ((int) std::lround (s * fps));
    for (double s : sceneMids) sceneFrames.push_back ((int) std::lround (s * fps));
    const int lastFrame = video || audioOnly ? totalFrames : std::min (totalFrames, *std::max_element (stillFrames.begin(), stillFrames.end()) + 1);

    for (int frame = 0; frame < lastFrame; ++frame)
    {
        const double t = (double) frame / fps;
        // 1. the interfaces: made, scripted, finished
        for (auto& sl : slots)
        {
            if (sl.done || t < sl.on) continue;
            if (t >= sl.off)
            {
                for (auto& tr : sl.tracks)
                    for (size_t i = 0; i < tr.stream.size(); ++i)
                    {
                        const long long g = tr.startFrame + (long long) (i / 2) - latencyFrames;
                        if (g < 0 || (size_t) g * 2 + 1 >= sound.size()) continue;
                        sound[(size_t) g * 2 + (i % 2)] += (float) (envelope (tr, (double) g / audioRate) * tr.stream[i]);
                    }
                sl.tracks.clear();
                sl.view.reset();
                sl.done = true;
                continue;
            }
            if (! sl.view)
            {
                sl.view = std::make_unique<Pane> (renderer);
                sl.init (*sl.view, sl.tracks);
                sl.view->a().setListening (true);   // as the sound is the app's own (its Listener, here rendered offline)
            }
            sl.script (*sl.view, t);
        }
        // 2. what is on screen, and how big
        struct OnScreen
        {
            const Shot* shot;
            double alpha;
            std::vector<Placement> placements;
        };
        std::vector<OnScreen> on;
        for (const auto& s : shots)
            if (t >= s.start - h && t < s.end + h)
                on.push_back ({ &s, s.start <= 0.0 ? 1.0 : clamp01 ((t - (s.start - h)) / fade), s.layout ? s.layout (t) : std::vector<Placement> {} });
        for (auto& sl : slots)
            if (sl.view)
            {
                float k = 0.0f;
                for (const auto& o : on)
                    for (const auto& p : o.placements)
                        if (p.view == sl.view.get()) k = std::max (k, (float) p.vp.w / p.cam.w);
                if (k > 0.0f) sl.k = k;
            }
        // 3. a frame of each, and what it sounds like
        for (auto& sl : slots)
            if (sl.view)
            {
                sl.view->step (sl.k);
                for (auto& tr : sl.tracks) tr.listener->follow (sl.view->sim(), dt, tr.stream);
            }
        // the race: the bells at +20 dB
        for (int i = 0; i < 4; ++i)
            if (Pane* v = viewOf (V6 + i); v != nullptr && finished[i] < 0.0 && t > tS6 + 0.3 && v->sim().improvement() > 20.0)
            {
                finished[i] = t;
                finishedSim[i] = v->sim().time();
                const double notes[4] = { 659.26, 523.25, 783.99, 440.0 };
                bell (sound, t, notes[i], racePan[i], 0.07);
            }

        const bool wanted = video || std::find (stillFrames.begin(), stillFrames.end(), frame) != stillFrames.end();
        if (! wanted) continue;
        // 4. the picture: each shot, crossfaded
        auto render = [&] (const OnScreen& o) {
            SDL_SetRenderDrawColor (renderer, 14, 16, 19, 255);
            SDL_RenderClear (renderer);
            for (const auto& p : o.placements)
                if (p.view != nullptr) p.view->draw (p.cam, p.vp);
            if (! o.shot->layout && o.shot->paint) overlay.pass ([&] (ImDrawList* dl) { o.shot->paint (dl, t, 1.0); });
            SDL_RenderFlush (renderer);
        };
        auto pixels = [&] () { return (uint32_t*) surface->pixels; };
        if (on.size() == 1) render (on[0]);
        else if (on.size() >= 2)
        {
            render (on[0]);
            std::memcpy (bufA.data(), pixels(), bufA.size() * 4);
            render (on[1]);
            std::memcpy (bufB.data(), pixels(), bufB.size() * 4);
            const int a = (int) std::lround (on[1].alpha * 256.0);
            uint32_t* px = pixels();
            for (size_t i = 0; i < bufA.size(); ++i)
            {
                const uint32_t p = bufA[i], q = bufB[i];
                uint32_t r = 0xff000000u;
                for (int c = 0; c < 24; c += 8)
                {
                    const int x = (int) ((p >> c) & 0xff), y = (int) ((q >> c) & 0xff);
                    r |= (uint32_t) ((x * (256 - a) + y * a) >> 8) << c;
                }
                px[i] = r;
            }
        }
        // 5. on top: the shots' marks and the captions
        overlay.pass ([&] (ImDrawList* dl) {
            for (size_t i = 0; i < on.size(); ++i)
                if (on[i].shot->layout && on[i].shot->paint)
                    on[i].shot->paint (dl, t, on.size() == 2 ? (i == 0 ? 1.0 - on[1].alpha : on[1].alpha) : 1.0);
            for (const auto& c : captions)
            {
                const double a = ramp (t, c.a, c.a + 0.3) * (1.0 - ramp (t, c.b - 0.25, c.b));
                caption (dl, of, c.line, a, ramp (t, c.a, c.a + 0.4));
            }
        });
        SDL_RenderFlush (renderer);
        const unsigned char* src = (const unsigned char*) surface->pixels;
        for (int y = 0; y < H; ++y) std::memcpy (row.data() + (size_t) y * W * 4, src + (size_t) y * surface->pitch, (size_t) W * 4);
        for (size_t i = 3; i < row.size(); i += 4) row[i] = 255;
        if (video) std::fwrite (row.data(), 1, row.size(), out);
        auto save = [&] (const std::string& name) { stbi_write_png ((dir / name).string().c_str(), W, H, 4, row.data(), W * 4); };
        for (size_t i = 0; i < sceneFrames.size(); ++i)
            if (frame == sceneFrames[i]) save ("frames/scene_" + std::to_string (i + 1) + ".png");
        if (! video)
        {
            char b[64];
            std::snprintf (b, sizeof b, "still_%05.2f.png", t);
            save (b);
        }
        if (frame % 30 == 0) std::fprintf (stderr, "afx_promo: %4.1f s\n", t);
    }
    if (video || audioOnly)
    {
        if (out != nullptr) std::fflush (out);
        // the listeners still going at the end
        for (auto& sl : slots)
            for (auto& tr : sl.tracks)
                for (size_t i = 0; i < tr.stream.size(); ++i)
                {
                    const long long g = tr.startFrame + (long long) (i / 2) - latencyFrames;
                    if (g < 0 || (size_t) g * 2 + 1 >= sound.size()) continue;
                    sound[(size_t) g * 2 + (i % 2)] += (float) (envelope (tr, (double) g / audioRate) * tr.stream[i]);
                }
        // Peak at −3 dBFS: the mix sets the loudness.
        float peak = 1e-9f;
        for (float v : sound) peak = std::max (peak, std::abs (v));
        const float g = 0.7079f / peak;
        for (float& v : sound) v *= g;
        writeWav (dir / "sonification.wav", sound);
        std::fprintf (stderr, "afx_promo: sonification.wav (peak was %.2f)\n", peak);
    }
    slots.clear();
    overlayOwner.reset();
    SDL_DestroyRenderer (renderer);
    SDL_FreeSurface (surface);
    return 0;
}
