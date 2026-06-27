#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

//==============================================================================
// Envelope-following onset (transient) detector for the FX trigger path.
//
// Call processSample() once per sample. Returns a velocity value in [0, 1]
// when a trigger fires, or 0 if no trigger this sample.
//
// threshold  (0–1): normalised peak level that must be exceeded.
// sensitivity (0–1): 0 = slow follower (only strong transients trigger);
//                    1 = fast follower (subtle transients trigger).
//
// A 50 ms re-trigger guard prevents machine-gunning on a single hit.
//==============================================================================
class OnsetDetector
{
public:
    OnsetDetector() = default;

    void  prepare (double sampleRate);
    void  reset   ();

    float processSample (float monoFiltered, float threshold, float sensitivity);

private:
    double sr                 { 44100.0 };
    float  envelope           { 0.0f };   // fast follower — onset detection only
    float  velEnvelope        { 0.0f };   // slow follower — decoupled velocity tracking
    int    retriggerCountdown { 0 };
    bool   wasAbove           { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OnsetDetector)
};
