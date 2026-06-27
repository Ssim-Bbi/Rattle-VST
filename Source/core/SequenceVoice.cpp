#include "SequenceVoice.h"
#include <cmath>

void SequenceVoice::start (const Params& p, uint64_t age)
{
    snap             = p;
    voiceAge         = age;
    repIndex         = 0;
    samplesUntilNext = 0;
    hitActive        = false;
    readPos          = 0.0;
    playbackRate     = 1.0;
    currentGain      = p.velocity;
    active           = true;

    curBuffer        = nullptr;
    curStart         = 0;
    curEnd           = 0;

    // Centre pan until the first impact sets its own.
    panL = panR = std::cos (juce::MathConstants<float>::pi * 0.25f);
}

void SequenceVoice::stop()
{
    active    = false;
    hitActive = false;
}

//==============================================================================
float SequenceVoice::computeDecayGain (int repIdx) const
{
    return snap.velocity * std::pow (1.0f - snap.decay * 0.95f, (float) repIdx);
}

float SequenceVoice::computePitchOffset (int repIdx) const
{
    if (snap.pitchCurveAmt == 0.0f || snap.rattle <= 1)
        return 0.0f;

    const float t = (float) repIdx / (float)(snap.rattle - 1);
    if (snap.pitchCurveShape == 0)
        return snap.pitchCurveAmt * t;
    else
        return snap.pitchCurveAmt * t * t;
}

//==============================================================================
void SequenceVoice::processBlock (juce::AudioBuffer<float>& out, int numSamples)
{
    if (! active)
        return;

    const int numOutCh = out.getNumChannels();

    for (int i = 0; i < numSamples; ++i)
    {
        // ---- Fire next impact when countdown reaches zero ----
        if (samplesUntilNext <= 0 && repIndex < snap.rattle)
        {
            const int   u    = juce::jlimit (0, juce::jmax (0, snap.repSliceCount - 1), repIndex);
            const auto& unit = snap.repSlices[u];

            // Resolve this impact's buffer, slice range and gain. The shared_ptr
            // lives in snap.repSlices, so this raw pointer stays valid for the hit.
            curBuffer = unit.buffer.get();
            curStart  = unit.start;
            curEnd    = (unit.end > unit.start)
                          ? unit.end
                          : (curBuffer != nullptr ? curBuffer->getNumSamples() : 0);
            if (curBuffer != nullptr)
                curEnd = juce::jmin (curEnd, curBuffer->getNumSamples());

            // Light the slot being triggered (UI indicator).
            if (hitSink != nullptr && unit.slot >= 0 && unit.slot < 8)
                hitSink[unit.slot].fetch_add (1, std::memory_order_relaxed);

            currentGain = computeDecayGain (repIndex) * unit.gain;

            const float  pitchSt   = snap.pitchSt + computePitchOffset (repIndex);
            const double rateRatio = unit.fileSampleRate / snap.hostSampleRate;
            playbackRate           = rateRatio * std::pow (2.0, (double) pitchSt / 12.0);

            // Constant-power stereo pan for this impact.
            const float panPos = snap.repPan[u];
            const float angle  = juce::MathConstants<float>::pi * 0.25f * (1.0f + panPos);
            panL = std::cos (angle);
            panR = std::sin (angle);

            if (repIndex + 1 < snap.rattle)
                samplesUntilNext = juce::jmax (1, snap.repInterval[repIndex]);

            repIndex++;
            readPos   = 0.0;
            hitActive = true;
        }

        // ---- Render the current impact ----
        if (hitActive)
        {
            const int sliceLen = curEnd - curStart;

            if (curBuffer == nullptr || sliceLen <= 1 || readPos >= (double)(sliceLen - 1))
            {
                hitActive = false;
            }
            else
            {
                const int   numInCh   = curBuffer->getNumChannels();
                const int   localIdx0 = (int) readPos;
                const int   localIdx1 = juce::jmin (localIdx0 + 1, sliceLen - 1);
                const float frac      = (float)(readPos - (double) localIdx0);

                const int absIdx0 = curStart + localIdx0;
                const int absIdx1 = curStart + localIdx1;

                if (numOutCh >= 2)
                {
                    for (int ch = 0; ch < numOutCh; ++ch)
                    {
                        const float* src = curBuffer->getReadPointer (juce::jmin (ch, numInCh - 1));
                        const float  smp = (src[absIdx0] + frac * (src[absIdx1] - src[absIdx0])) * currentGain;
                        const float  panGain = (ch == 0) ? panL : panR;
                        out.getWritePointer (ch)[i] += smp * panGain;
                    }
                }
                else
                {
                    // Mono output — no pan applied.
                    const float* src = curBuffer->getReadPointer (0);
                    out.getWritePointer (0)[i] +=
                        (src[absIdx0] + frac * (src[absIdx1] - src[absIdx0])) * currentGain;
                }

                readPos += playbackRate;
            }
        }

        --samplesUntilNext;

        if (repIndex >= snap.rattle && ! hitActive)
        {
            active = false;
            return;
        }
    }
}
