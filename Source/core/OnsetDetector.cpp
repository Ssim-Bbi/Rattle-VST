#include "OnsetDetector.h"
#include <cmath>

void OnsetDetector::prepare (double sampleRate)
{
    sr = sampleRate;
    reset();
}

void OnsetDetector::reset()
{
    envelope           = 0.0f;
    velEnvelope        = 0.0f;
    retriggerCountdown = 0;
    wasAbove           = false;
}

float OnsetDetector::processSample (float monoFiltered, float threshold, float sensitivity)
{
    const float rectified = std::fabs (monoFiltered);

    // -- Fast follower: onset detection ----------------------------------
    // Attack time: 30 ms (sensitivity = 0) → 0.5 ms (sensitivity = 1).
    const float attackMs    = 30.0f - sensitivity * 29.5f;
    const float attackCoeff = std::exp (-1.0f / ((float) sr * attackMs * 0.001f));
    const float relCoeff    = std::exp (-1.0f / ((float) sr * 0.150f));  // 150 ms release

    if (rectified > envelope)
        envelope = attackCoeff * envelope + (1.0f - attackCoeff) * rectified;
    else
        envelope = relCoeff * envelope;

    // -- Slow follower: velocity (decoupled from threshold) --------------
    // 5 ms attack / 300 ms release — tracks the signal's sustained amplitude,
    // not the instant of threshold crossing, so velocity is independent of
    // where the threshold gate is set.
    const float velAtkCoeff = std::exp (-1.0f / ((float) sr * 0.005f));
    const float velRelCoeff = std::exp (-1.0f / ((float) sr * 0.300f));

    if (rectified > velEnvelope)
        velEnvelope = velAtkCoeff * velEnvelope + (1.0f - velAtkCoeff) * rectified;
    else
        velEnvelope = velRelCoeff * velEnvelope;

    // -- Re-trigger guard ------------------------------------------------
    if (retriggerCountdown > 0)
    {
        --retriggerCountdown;
        wasAbove = (envelope > threshold);
        return 0.0f;
    }

    // -- Rising-edge detection -------------------------------------------
    const bool isAbove = (envelope > threshold);
    const bool fired   = isAbove && !wasAbove;
    wasAbove = isAbove;

    if (fired)
    {
        retriggerCountdown = (int) (sr * 0.050); // 50 ms guard
        return juce::jmin (1.0f, velEnvelope);   // velocity from slow follower
    }

    return 0.0f;
}
