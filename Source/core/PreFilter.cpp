#include "PreFilter.h"

void PreFilter::prepare (double sampleRate)
{
    sr       = sampleRate;
    lastType = -1; // force coefficient recalculation on first call
    reset();
}

void PreFilter::reset()
{
    filter.reset();
}

void PreFilter::setParameters (int filterType, float freqHz, float q)
{
    if (filterType == lastType
        && std::abs (freqHz - lastFreq) < 0.5f
        && std::abs (q      - lastQ)    < 0.01f)
        return;

    freqHz = juce::jlimit (20.0f, (float)(sr * 0.499), freqHz);
    q      = juce::jmax   (0.1f, q);

    juce::IIRCoefficients coeffs;
    switch (filterType)
    {
        case 1:  coeffs = juce::IIRCoefficients::makeBandPass (sr, freqHz, q); break;
        case 2:  coeffs = juce::IIRCoefficients::makeHighPass  (sr, freqHz, q); break;
        default: coeffs = juce::IIRCoefficients::makeLowPass   (sr, freqHz, q); break;
    }
    filter.setCoefficients (coeffs);

    lastType = filterType;
    lastFreq = freqHz;
    lastQ    = q;
}

float PreFilter::processSample (float monoInput)
{
    return filter.processSingleSampleRaw (monoInput);
}
