#pragma once

#include <atomic>
#include <juce_gui_basics/juce_gui_basics.h>

//==============================================================================
// Live visualisation of the rattle sequence computed from the current params.
// Runs a 30 Hz timer on the message thread; safe to read std::atomic<float>*.
//
// Layout:
//   - Amplitude bars rise from the bottom, height = decay gain for each repeat.
//   - A dashed centre line marks 0 semitones.
//   - An orange line overlays the pitch value at each repeat's X position.
//==============================================================================
class RattleGraph : public juce::Component,
                    private juce::Timer
{
public:
    RattleGraph();

    void setParams (std::atomic<float>* rattle,
                    std::atomic<float>* decay,
                    std::atomic<float>* pace,
                    std::atomic<float>* paceCurve,
                    std::atomic<float>* pitch,
                    std::atomic<float>* pitchCurveAmt,
                    std::atomic<float>* pitchCurveShape,
                    std::atomic<float>* tempoSync,
                    std::atomic<float>* syncDivision,
                    std::atomic<float>* syncGrid);

    void paint (juce::Graphics&) override;

private:
    void timerCallback() override { repaint(); }

    std::atomic<float>* pRattle         { nullptr };
    std::atomic<float>* pDecay          { nullptr };
    std::atomic<float>* pPace           { nullptr };
    std::atomic<float>* pPaceCurve      { nullptr };
    std::atomic<float>* pPitch          { nullptr };
    std::atomic<float>* pPitchCurveAmt  { nullptr };
    std::atomic<float>* pPitchCurveShape{ nullptr };
    std::atomic<float>* pTempoSync      { nullptr };
    std::atomic<float>* pSyncDivision   { nullptr };
    std::atomic<float>* pSyncGrid       { nullptr };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RattleGraph)
};
