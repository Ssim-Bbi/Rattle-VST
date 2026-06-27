#include "RattleGraph.h"
#include "../core/Params.h"
#include <cmath>
#include <vector>

RattleGraph::RattleGraph()
{
    startTimerHz (30);
}

void RattleGraph::setParams (std::atomic<float>* rattle,
                              std::atomic<float>* decay,
                              std::atomic<float>* pace,
                              std::atomic<float>* paceCurve,
                              std::atomic<float>* pitch,
                              std::atomic<float>* pitchCurveAmt,
                              std::atomic<float>* pitchCurveShape,
                              std::atomic<float>* tempoSync,
                              std::atomic<float>* syncDivision,
                              std::atomic<float>* syncGrid)
{
    pRattle          = rattle;
    pDecay           = decay;
    pPace            = pace;
    pPaceCurve       = paceCurve;
    pPitch           = pitch;
    pPitchCurveAmt   = pitchCurveAmt;
    pPitchCurveShape = pitchCurveShape;
    pTempoSync       = tempoSync;
    pSyncDivision    = syncDivision;
    pSyncGrid        = syncGrid;
}

//==============================================================================
void RattleGraph::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds();

    // Background
    g.setColour (juce::Colour (0xff1e1e26));
    g.fillRect (bounds);
    g.setColour (juce::Colour (0xff2e2e3a));
    g.drawRect (bounds, 1);

    if (pRattle == nullptr)
        return;

    // -- Read params --------------------------------------------------------
    const int   rattle          = juce::jmax (1, (int) pRattle->load());
    const float decay           = pDecay->load();
    const float paceMs          = pPace->load();
    const float drift           = pPaceCurve->load();
    const float pitch           = pPitch->load();
    const float pitchCurveAmt   = pPitchCurveAmt->load();
    const int   pitchCurveShape = (int) pPitchCurveShape->load();
    const bool  sync            = pTempoSync != nullptr && (int) pTempoSync->load() == 1;

    // -- Impact timing (quarters in Sync, ms in Free) — same model as the engine
    double base, grid = 0.0, minGap;
    if (sync)
    {
        base   = RattleParams::syncDivisionQuarters ((int) pSyncDivision->load());
        grid   = RattleParams::syncGridQuarters     ((int) pSyncGrid->load());
        minGap = 0.0;
    }
    else
    {
        base   = (double) paceMs;
        minGap = 5.0;
    }

    double offsets[32];
    RattleParams::computeImpactOffsets (rattle, (double) drift, base, sync, grid, minGap, offsets);
    const double total = offsets[(std::size_t) juce::jmax (0, rattle - 1)];

    // -- Per-repeat gain + pitch -------------------------------------------
    struct Rep { float gain, pitchSt; };
    std::vector<Rep> reps ((std::size_t) rattle);
    for (int i = 0; i < rattle; ++i)
    {
        const float t = (rattle > 1) ? (float) i / (float)(rattle - 1) : 0.0f;
        reps[(std::size_t) i].gain = std::pow (1.0f - decay * 0.95f, (float) i);
        const float curve = (pitchCurveShape == 0) ? pitchCurveAmt * t : pitchCurveAmt * t * t;
        reps[(std::size_t) i].pitchSt = pitch + curve;
    }

    // -- Geometry ----------------------------------------------------------
    const auto  area    = bounds.reduced (5).toFloat();
    const float W       = area.getWidth();
    const float H       = area.getHeight();
    const float x0      = area.getX();
    const float yTop    = area.getY();
    const float yBot    = area.getBottom();
    const float maxBarH = H * 0.88f;

    // Sync: span snaps up to whole beats so the grid is complete. Free: 10% right pad.
    const double span = sync ? juce::jmax (1.0, std::ceil (total - 1e-6))
                             : ((rattle > 1 && total > 0.0) ? total / 0.90 : 1.0);

    auto repX = [&](int i) -> float
    {
        if (rattle == 1) return x0 + W * 0.08f;
        return x0 + (float)(offsets[(std::size_t) i] / span) * W;
    };

    // -- Subdivision grid (Sync only) --------------------------------------
    if (sync && grid > 0.0)
    {
        auto nearMul = [](double v, double b) { return std::abs (v / b - std::round (v / b)) < 1e-6; };
        for (int n = 0; (double) n * grid <= span + 1e-9; ++n)
        {
            const double v  = (double) n * grid;
            const float  gx = x0 + (float)(v / span) * W;

            juce::Colour gc; float gw = 1.0f;
            if      (nearMul (v, 1.0))  { gc = juce::Colour (0xff56566f); gw = 1.2f; } // beat
            else if (nearMul (v, 0.5))  { gc = juce::Colour (0xff40405a); }            // 1/8
            else if (nearMul (v, 0.25)) { gc = juce::Colour (0xff34344a); }            // 1/16
            else                        { gc = juce::Colour (0xff2a2a36); }            // finer

            g.setColour (gc);
            g.fillRect (gx, yTop, gw, yBot - yTop);
        }
    }

    const float barW = juce::jlimit (2.0f, 9.0f, W / (float)(rattle * 3));

    // -- Amplitude bars (decay) — small floor so every impact stays visible on the grid
    for (int i = 0; i < rattle; ++i)
    {
        const float gain = reps[(std::size_t) i].gain;
        const float barH = juce::jmax (3.0f, gain * maxBarH);

        const float bx  = repX (i);
        const float by  = yBot - barH;
        const auto  col = juce::Colour (0xff5e5ef0).withAlpha (0.3f + 0.7f * gain);

        g.setGradientFill (juce::ColourGradient (col,               bx, by,
                                                 col.darker (0.55f), bx, yBot,
                                                 false));
        g.fillRect (bx - barW * 0.5f, by, barW, barH);
    }

    // -- Pitch centre reference line (dashed, always) ----------------------
    const float pitchCentreY = yBot - H * 0.50f;

    g.setColour (juce::Colour (0x22ffffff));
    for (float dx = x0; dx < x0 + W; dx += 9.0f)
        g.fillRect (dx, pitchCentreY - 0.5f, 5.0f, 1.0f);

    // -- Pitch curve overlay -----------------------------------------------
    const float pitchScale  = (H * 0.42f) / 12.0f;
    const bool  hasPitch    = (std::fabs (pitch) > 0.05f || std::fabs (pitchCurveAmt) > 0.05f);

    if (hasPitch)
    {
        auto clampY = [&](float st) -> float
        {
            return pitchCentreY - juce::jlimit (-12.0f, 12.0f, st) * pitchScale;
        };

        juce::Path line;
        for (int i = 0; i < rattle; ++i)
        {
            const float px = repX (i);
            const float py = clampY (reps[(std::size_t) i].pitchSt);
            if (i == 0) line.startNewSubPath (px, py);
            else        line.lineTo (px, py);
        }

        g.setColour (juce::Colour (0xffff8f40));
        g.strokePath (line, juce::PathStrokeType (2.0f,
            juce::PathStrokeType::mitered,
            juce::PathStrokeType::rounded));

        g.setColour (juce::Colour (0xffffa060));
        for (int i = 0; i < rattle; ++i)
            g.fillEllipse (repX (i) - 2.5f, clampY (reps[(std::size_t) i].pitchSt) - 2.5f, 5.0f, 5.0f);
    }

    // -- Corner labels -----------------------------------------------------
    g.setFont (juce::FontOptions (9.5f));
    g.setColour (juce::Colour (0xff45454f));
    g.drawText ("+12 st", (int) x0,       (int)(yBot - H),          40, 12, juce::Justification::centredLeft);
    g.drawText ("0",      (int) x0,       (int)(pitchCentreY - 6.f), 14, 12, juce::Justification::centredLeft);
    g.drawText ("-12 st", (int) x0,       (int)(yBot - 12.f),        40, 12, juce::Justification::centredLeft);
    g.drawText ("Time →", (int)(x0 + W - 38.f), (int)(yBot - 12.f), 38, 12, juce::Justification::centredRight);
}
