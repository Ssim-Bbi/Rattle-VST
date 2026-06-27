#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

//==============================================================================
// Mono biquad pre-filter used by the FX onset-detection path.
// Runs on the audio thread only. Recalculates coefficients lazily when
// any parameter changes by more than a small epsilon.
//==============================================================================
class PreFilter
{
public:
    PreFilter() = default;

    void  prepare (double sampleRate);
    void  reset   ();

    // filterType: 0 = LP, 1 = BP, 2 = HP
    // q: 0.3 (wide / warm) … 10.0 (narrow / resonant)
    void  setParameters (int filterType, float freqHz, float q);

    float processSample (float monoInput);

private:
    juce::IIRFilter filter;
    double sr       { 44100.0 };
    int    lastType { -1 };
    float  lastFreq { -1.0f };
    float  lastQ    { -1.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PreFilter)
};
