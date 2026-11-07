// The canceller's block diagram: the sources, the paths, the sums, the adaptive
// filter and the error, wired in the one topology there is.  Each block shows a
// little of what it is doing; click one to edit it in the inspector.

#include "Ui.h"

#include "Plots.h"

#include <algorithm>
#include <cmath>

namespace afx
{
namespace
{
void arrowHead (ImDrawList* dl, ImVec2 tip, ImVec2 from, ImU32 c)
{
    const float dx = tip.x - from.x, dy = tip.y - from.y, len = std::max (1e-3f, std::sqrt (dx * dx + dy * dy));
    const float ux = dx / len, uy = dy / len, s = 7.0f;
    dl->AddTriangleFilled (tip, { tip.x - ux * s - uy * s * 0.5f, tip.y - uy * s + ux * s * 0.5f },
                           { tip.x - ux * s + uy * s * 0.5f, tip.y - uy * s - ux * s * 0.5f }, c);
}

// A wire through the points, with an arrow at the end.
void wire (ImDrawList* dl, std::initializer_list<ImVec2> pts, ImU32 c, float thickness = 1.6f)
{
    std::vector<ImVec2> v (pts);
    dl->AddPolyline (v.data(), (int) v.size(), c, 0, thickness);
    if (v.size() >= 2) arrowHead (dl, v.back(), v[v.size() - 2], c);
}

std::string shortSource (const SourceSpec& s)
{
    char buf[96];
    switch (s.kind)
    {
        case SourceKind::none: return "off";
        case SourceKind::maternalEcg:
        case SourceKind::fetalEcg: std::snprintf (buf, sizeof buf, "%.0f bpm, %.2g mV", s.bpm, s.amplitude); break;
        case SourceKind::sine:
        case SourceKind::square: std::snprintf (buf, sizeof buf, "%.3g Hz, %.2g mV", s.frequency, s.amplitude); break;
        case SourceKind::chirp: std::snprintf (buf, sizeof buf, "%.3g → %.3g Hz", s.frequency, s.frequency2); break;
        case SourceKind::hum: std::snprintf (buf, sizeof buf, "%d Hz, %.2g mV", s.mains, s.amplitude); break;
        case SourceKind::wander: std::snprintf (buf, sizeof buf, "%.2g Hz, %.2g mV", s.frequency, s.amplitude); break;
        default:
            if (s.amplitude < 0.1) std::snprintf (buf, sizeof buf, "%.3g µV RMS", 1000.0 * s.amplitude);
            else std::snprintf (buf, sizeof buf, "%.3g mV RMS", s.amplitude);
    }
    return buf;
}
} // namespace

void App::diagram (ImVec2 size)
{
    auto* dl = ImGui::GetWindowDrawList();
    const ImVec2 p0 = ImGui::GetCursorScreenPos(), p1 { p0.x + size.x, p0.y + size.y - 6 };
    const float W = size.x, H = p1.y - p0.y;
    dl->AddRectFilled (p0, p1, argb (0xff12151a), 6.0f);
    markPoint ("view.diagram", p0);
    markPoint ("view.diagram.end", p1);
    drawText (dl, fonts().bold, { p0.x + 8, p0.y + 5 }, textLight, "Interference canceller");
    drawText (dl, fonts().small, { p0.x + 186, p0.y + 7 }, textGrey, "click a block to edit it");
    auto X = [&] (float fx) { return p0.x + fx * W; };
    auto Y = [&] (float fy) { return p0.y + fy * H; };
    const float bh = std::clamp (H * 0.17f, 40.0f, 54.0f), sourceH = std::clamp (H * 0.2f, 46.0f, 62.0f), radius = 12.0f;
    const float rS = 0.19f, rD = 0.39f, rNp = 0.585f, rM = 0.585f, rX = 0.77f, rNr = 0.93f;

    struct Box
    {
        Block block;
        const char* id;
        ImVec2 a, b;
        ImU32 colour;
    };
    auto box = [&] (Block bl, const char* id, float x0, float x1, float row, ImU32 colour, float height) {
        return Box { bl, id, { X (x0), Y (row) - 0.5f * height }, { X (x1), Y (row) + 0.5f * height }, colour };
    };
    const Box boxes[] = {
        box (Block::signal, "block.signal", 0.015f, 0.205f, rS, colourSignal, sourceH),
        box (Block::interference, "block.interference", 0.015f, 0.205f, rM, colourInterference, sourceH),
        box (Block::primaryPath, "block.primaryPath", 0.27f, 0.43f, rD, colourInterference, bh),
        box (Block::referencePath, "block.referencePath", 0.27f, 0.43f, rX, colourInterference, bh),
        box (Block::primaryNoise, "block.primaryNoise", 0.30f, 0.475f, rNp, colourNoise, bh * 0.72f),
        box (Block::referenceNoise, "block.referenceNoise", 0.30f, 0.475f, rNr, colourNoise, bh * 0.72f),
        box (Block::filter, "block.filter", 0.585f, 0.79f, rX, colourOutput, bh * 1.15f),
        box (Block::output, "block.output", 0.905f, 0.99f, rD, colourError, bh * 0.8f),
    };
    const ImVec2 sumP { X (0.52f), Y (rD) }, sumR { X (0.52f), Y (rX) }, sumE { X (0.855f), Y (rD) };

    // ---- the wires ----
    const auto& sb = boxes[0];
    wire (dl, { { sb.b.x, Y (rS) }, { sumP.x, Y (rS) }, { sumP.x, sumP.y - radius } }, colourSignal);
    drawText (dl, fonts().small, { X (0.43f), Y (rS) - 16 }, colourSignal, "s");
    const float branch = X (0.235f);
    dl->AddLine ({ boxes[1].b.x, Y (rM) }, { branch, Y (rM) }, colourInterference, 1.6f);
    dl->AddLine ({ branch, Y (rD) }, { branch, Y (rX) }, colourInterference, 1.6f);
    dl->AddCircleFilled ({ branch, Y (rM) }, 3.0f, colourInterference);
    wire (dl, { { branch, Y (rD) }, { boxes[2].a.x, Y (rD) } }, colourInterference);
    wire (dl, { { branch, Y (rX) }, { boxes[3].a.x, Y (rX) } }, colourInterference);
    drawText (dl, fonts().small, { branch + 5, Y (rM) - 16 }, colourInterference, "m");
    wire (dl, { { boxes[2].b.x, Y (rD) }, { sumP.x - radius, sumP.y } }, colourInterference);
    wire (dl, { { boxes[3].b.x, Y (rX) }, { sumR.x - radius, sumR.y } }, colourInterference);
    wire (dl, { { boxes[4].b.x, Y (rNp) }, { sumP.x, Y (rNp) }, { sumP.x, sumP.y + radius } }, colourNoise);
    wire (dl, { { boxes[5].b.x, Y (rNr) }, { sumR.x, Y (rNr) }, { sumR.x, sumR.y + radius } }, colourNoise);
    wire (dl, { { sumP.x + radius, sumP.y }, { sumE.x - radius, sumE.y } }, colourPrimary, 2.0f);
    drawText (dl, fonts().bold, { X (0.70f), sumP.y - 19 }, colourPrimary, "d");
    drawText (dl, fonts().tiny, { X (0.70f) + 12, sumP.y - 15 }, textGrey, "primary (abdomen)");
    wire (dl, { { sumR.x + radius, sumR.y }, { boxes[6].a.x, sumR.y } }, colourReference, 2.0f);
    drawText (dl, fonts().bold, { sumR.x + radius + 6, sumR.y - 19 }, colourReference, "x");
    wire (dl, { { boxes[6].b.x, Y (rX) }, { sumE.x, Y (rX) }, { sumE.x, sumE.y + radius } }, colourOutput, 2.0f);
    drawText (dl, fonts().bold, { sumE.x + 6, Y (rX) - 20 }, colourOutput, "y");
    drawText (dl, fonts().bold, { sumE.x + 6, sumE.y + radius + 1 }, textLight, "−");
    wire (dl, { { sumE.x + radius, sumE.y }, { boxes[7].a.x, sumE.y } }, colourError, 2.0f);
    // e back to the filter: the error drives the weights.
    const float fbX = X (0.88f), fbY = boxes[6].b.y + 10.0f;
    dl->AddLine ({ fbX, sumE.y }, { fbX, fbY }, withAlpha (colourError, 0.75f), 1.4f);
    dl->AddCircleFilled ({ fbX, sumE.y }, 3.0f, colourError);
    wire (dl, { { fbX, fbY }, { X (0.70f), fbY }, { X (0.70f), boxes[6].b.y } }, withAlpha (colourError, 0.75f), 1.4f);
    drawText (dl, fonts().small, { X (0.705f) + 6, fbY - 3 }, withAlpha (colourError, 0.9f), "e");
    drawText (dl, fonts().tiny, { boxes[7].a.x, sumE.y - bh * 0.4f - 15 }, colourError, "e = d − y");
    if (edit.signalInReference != 0.0) drawText (dl, fonts().small, { sumR.x + radius + 4, sumR.y + 5 }, colourSignal, "+ c·s");

    // ---- the sums ----
    const std::tuple<Block, const char*, ImVec2, ImU32> sums[] = { { Block::primarySum, "block.primarySum", sumP, colourPrimary },
                                                                   { Block::referenceSum, "block.referenceSum", sumR, colourReference },
                                                                   { Block::output, "block.errorSum", sumE, colourError } };
    for (const auto& [bl, id, c, colour] : sums)
    {
        ImGui::SetCursorScreenPos ({ c.x - radius, c.y - radius });
        if (ImGui::InvisibleButton (id, { 2 * radius, 2 * radius })) selection = bl;
        markItem (id);
        const bool hot = ImGui::IsItemHovered(), on = selection == bl;
        dl->AddCircleFilled (c, radius, argb (0xff1d232b));
        dl->AddCircle (c, radius, on ? accent : hot ? brighter (colour, 0.4f) : colour, 24, on ? 2.4f : 1.6f);
        drawTextIn (dl, fonts().bold, { c.x - radius, c.y - radius }, { c.x + radius, c.y + radius }, textLight, "Σ", 0);
    }

    // ---- the blocks ----
    std::vector<float> trace;
    for (const auto& bx : boxes)
    {
        ImGui::SetCursorScreenPos (bx.a);
        if (ImGui::InvisibleButton (bx.id, { bx.b.x - bx.a.x, bx.b.y - bx.a.y })) selection = bx.block;
        markItem (bx.id);
        const bool hot = ImGui::IsItemHovered(), on = selection == bx.block;
        dl->AddRectFilled (bx.a, bx.b, darker (bx.colour, hot ? 3.0f : 4.0f), 5.0f);
        dl->AddRect (bx.a, bx.b, on ? accent : withAlpha (bx.colour, hot ? 1.0f : 0.7f), 5.0f, 0, on ? 2.4f : 1.3f);
        const ImVec2 inA { bx.a.x + 6, bx.a.y + 3 }, inB { bx.b.x - 6, bx.b.y - 3 };
        std::string title, sub;
        const SourceSpec* src = nullptr;
        Channel ch = Channel::signal;
        const PathSpec* path = nullptr;
        switch (bx.block)
        {
            case Block::signal: title = "s  " + std::string (sourceKindName (edit.signal.kind)); src = &edit.signal; ch = Channel::signal; break;
            case Block::interference:
                title = "m  " + std::string (sourceKindName (edit.interference.kind));
                src = &edit.interference;
                ch = Channel::interference;
                break;
            case Block::primaryNoise: title = "nₚ  " + std::string (sourceKindName (edit.primaryNoise.kind)); src = &edit.primaryNoise; ch = Channel::primaryNoise; break;
            case Block::referenceNoise:
                title = "nᵣ  " + std::string (sourceKindName (edit.referenceNoise.kind));
                src = &edit.referenceNoise;
                ch = Channel::referenceNoise;
                break;
            case Block::primaryPath: title = "Hp  " + edit.primaryPath.preset; path = &edit.primaryPath; break;
            case Block::referencePath: title = "Hr  " + edit.referencePath.preset; path = &edit.referencePath; break;
            case Block::filter:
                title = "adaptive FIR w";
                sub = edit.filter.isChip() ? std::string ("chip: 4 taps · Q1.7 / Q2.12")
                                          : std::to_string (edit.filter.taps) + " taps · " + algorithmName (edit.filter.algorithm);
                break;
            case Block::output: title = "e ≈ s"; sub = "cleaned"; break;
            default: break;
        }
        drawTextIn (dl, fonts().bold, inA, { inB.x, inA.y + 15 }, textLight, title);
        const ImVec2 plotA { inA.x, inA.y + 17 }, plotB = inB;
        if (src != nullptr)
        {
            const bool small = bx.block == Block::primaryNoise || bx.block == Block::referenceNoise;
            if (small) drawTextIn (dl, fonts().tiny, { plotA.x, plotA.y - 1 }, { plotB.x, plotA.y + 11 }, textGrey, shortSource (*src));
            else
            {
                // what it is, and the last two seconds of it
                drawTextIn (dl, fonts().tiny, { plotA.x, plotA.y - 1 }, { plotB.x, plotA.y + 11 }, textGrey, shortSource (*src));
                sim.history (ch).last ((size_t) (2.0 * sampleRate), trace);
                drawTrace (dl, { plotA.x, plotA.y + 13 }, plotB, trace, (size_t) (2.0 * sampleRate), niceAbove (std::max (peakOf (trace), 1e-4f)),
                           bx.colour, 1.0f);
            }
        }
        else if (path != nullptr)
        {
            // the impulse response, as stems
            const auto h = path->effective();
            float peak = 0.0f;
            for (double v : h) peak = std::max (peak, (float) std::abs (v));
            peak = std::max (peak, 1e-6f);
            const float cy = 0.5f * (plotA.y + plotB.y), half = 0.5f * (plotB.y - plotA.y) - 1;
            const float step = std::min (10.0f, (plotB.x - plotA.x - 48.0f) / (float) std::max<size_t> (h.size(), 1));
            dl->AddLine ({ plotA.x, cy }, { plotB.x, cy }, IM_COL32 (255, 255, 255, 40));
            for (size_t i = 0; i < h.size(); ++i)
            {
                const float x = plotA.x + 3 + step * (float) i, y = cy - half * (float) h[i] / peak;
                dl->AddLine ({ x, cy }, { x, y }, bx.colour, 1.4f);
                dl->AddCircleFilled ({ x, y }, 2.0f, bx.colour);
            }
            const std::string n = std::to_string (h.size()) + (h.size() == 1 ? " tap" : " taps");
            drawTextIn (dl, fonts().tiny, { plotB.x - 44, cy - 6 }, { plotB.x, cy + 6 }, textGrey, n, 1);
        }
        else if (bx.block == Block::filter)
        {
            drawTextIn (dl, fonts().small, plotA, { plotB.x, plotA.y + 13 }, textGrey, sub);
            const auto& w = sim.filter().weights();
            float peak = 1e-6f;
            for (double v : w) peak = std::max (peak, (float) std::abs (v));
            const ImVec2 ga { plotA.x, plotA.y + 15 }, gb = plotB;
            const float cy = 0.5f * (ga.y + gb.y), half = 0.5f * (gb.y - ga.y);
            const float slot = (gb.x - ga.x) / (float) std::max<size_t> (w.size(), 1);
            dl->AddLine ({ ga.x, cy }, { gb.x, cy }, IM_COL32 (255, 255, 255, 40));
            for (size_t i = 0; i < w.size(); ++i)
            {
                const float x = ga.x + slot * ((float) i + 0.5f), y = cy - half * (float) w[i] / peak;
                dl->AddRectFilled ({ x - std::max (0.5f, 0.3f * slot), std::min (cy, y) }, { x + std::max (0.5f, 0.3f * slot), std::max (cy, y) + 0.5f },
                                   accent);
            }
            if (edit.filter.freeze)
                drawTextIn (dl, fonts().bold, { bx.b.x - 70, bx.a.y + 2 }, { bx.b.x - 6, bx.a.y + 16 }, argb (0xff8fc4ff), "frozen", 1);
            // The adaptive symbol: an arrow across the block.
            dl->AddLine ({ bx.a.x + 10, bx.b.y + 6 }, { bx.b.x - 14, bx.a.y - 4 }, withAlpha (colourError, 0.35f), 1.2f);
            arrowHead (dl, { bx.b.x - 14, bx.a.y - 4 }, { bx.a.x + 10, bx.b.y + 6 }, withAlpha (colourError, 0.7f));
        }
        else if (bx.block == Block::output)
            drawTextIn (dl, fonts().small, plotA, plotB, textGrey, dbText (sim.improvement()), -1);
    }
    ImGui::SetCursorScreenPos ({ p0.x, p1.y + 6 });
    ImGui::Dummy ({ size.x, 0 });
}
} // namespace afx
