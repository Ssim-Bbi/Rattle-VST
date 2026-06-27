#include "FFTView.h"
#include "../PluginProcessor.h"

FFTView::~FFTView()
{
    if (proc != nullptr)
        proc->fftActive.store (false, std::memory_order_relaxed);
}

void FFTView::setProcessor (RattleAudioProcessor* p)
{
    proc = p;
}

void FFTView::visibilityChanged()
{
    const bool on = isVisible();

    // Tell the audio thread whether to bother filling the FFT ring buffer.
    if (proc != nullptr)
        proc->fftActive.store (on, std::memory_order_relaxed);

    if (on) startTimerHz (30);
    else    stopTimer();
}

void FFTView::timerCallback()
{
    computeFrame();
    repaint();
}

//==============================================================================
void FFTView::computeFrame()
{
    if (proc == nullptr) return;

    // Unroll the ring buffer into fftData[0..fftSize-1], oldest sample first.
    const int wp = proc->fftWritePos.load (std::memory_order_acquire);
    for (int i = 0; i < fftSize; ++i)
        fftData[i] = proc->fftInputFifo[(wp - fftSize + i + fftSize) & (fftSize - 1)];

    // Apply Hann window.
    for (int i = 0; i < fftSize; ++i)
    {
        const float w = 0.5f * (1.0f - std::cos (juce::MathConstants<float>::twoPi
                                                   * (float) i / (float)(fftSize - 1)));
        fftData[i] *= w;
    }

    // Zero the imaginary workspace in the second half.
    std::fill (fftData.begin() + fftSize, fftData.end(), 0.0f);

    // Forward FFT — writes magnitudes for positive frequencies into fftData[0..numBins-1].
    fft.performFrequencyOnlyForwardTransform (fftData.data(), true);

    // Peak-hold: jump instantly to a higher magnitude, then decay slowly so a
    // transient peak lingers long enough to read against the threshold line.
    constexpr float holdDecay = 0.90f;          // ~0.3 s to fall to 1/e at 30 Hz
    for (int i = 0; i < numBins; ++i)
    {
        const float mag = fftData[i] / (float) fftSize;
        magnitudes[i] = (mag >= magnitudes[i]) ? mag : magnitudes[i] * holdDecay;
    }
}

//==============================================================================
void FFTView::paint (juce::Graphics& g)
{
    const float w = (float) getWidth();
    const float h = (float) getHeight();

    // Background
    g.setColour (juce::Colour (0xff1e1e24));
    g.fillRect (getLocalBounds());

    if (proc == nullptr) return;

    const float sr = (float) proc->getSampleRate();
    if (sr <= 0.0f) return;

    constexpr float minFreq = 20.0f;
    constexpr float maxFreq = 20000.0f;
    const float     binHz   = sr / (float) fftSize;
    const float     logRange = std::log10 (maxFreq / minFreq);

    auto freqToX = [&] (float freq) -> float
    {
        return std::log10 (freq / minFreq) / logRange * w;
    };

    // ---- Subtle vertical grid lines --------------------------------------
    g.setColour (juce::Colour (0xff242430));
    for (float freq : { 50.f, 100.f, 200.f, 500.f, 1000.f, 2000.f, 5000.f, 10000.f })
        g.drawVerticalLine ((int) freqToX (freq), 0.0f, h - 12.0f);

    // ---- Spectrum path ---------------------------------------------------
    juce::Path spectrum;
    bool started = false;

    for (int bin = 1; bin < numBins; ++bin)
    {
        const float freq = bin * binHz;
        if (freq < minFreq || freq > maxFreq) continue;

        const float x  = freqToX (freq);
        const float dB = juce::Decibels::gainToDecibels (magnitudes[bin], -80.0f);
        const float y  = juce::jmap (dB, -80.0f, 0.0f, h - 12.0f, 0.0f);

        if (! started) { spectrum.startNewSubPath (x, h - 12.0f); spectrum.lineTo (x, y); started = true; }
        else             spectrum.lineTo (x, y);
    }

    if (started)
    {
        spectrum.lineTo (w, h - 12.0f);
        spectrum.closeSubPath();

        g.setGradientFill (juce::ColourGradient (
            juce::Colour (0xff5e5ef0).withAlpha (0.85f), 0.0f, 0.0f,
            juce::Colour (0xff2a2a60).withAlpha (0.40f), 0.0f, h,
            false));
        g.fillPath (spectrum);

        g.setColour (juce::Colour (0xff9090ff).withAlpha (0.75f));
        g.strokePath (spectrum, juce::PathStrokeType (1.0f));
    }

    // ---- Frequency labels -----------------------------------------------
    g.setFont (juce::FontOptions (10.0f));
    for (auto [freq, label] : std::initializer_list<std::pair<float, const char*>> {
            { 100.f, "100" }, { 500.f, "500" }, { 1000.f, "1k" },
            { 5000.f, "5k" }, { 10000.f, "10k" } })
    {
        const int lx = (int) freqToX (freq);
        g.setColour (juce::Colour (0xff50505c));
        g.drawText (label, lx - 10, (int)(h - 12.0f), 20, 11, juce::Justification::centred);
    }

    // ---- Mode tag (top-left) --------------------------------------------
    g.setFont (juce::FontOptions (9.0f));
    g.setColour (juce::Colour (0xff50505c));
    g.drawText ("PEAK", 4, 2, 40, 10, juce::Justification::centredLeft);

    // ---- Filter centre-frequency line (amber vertical) ------------------
    {
        const float fx = freqToX (juce::jlimit (minFreq, maxFreq, proc->getFilterFreqHz()));
        g.setColour (juce::Colour (0xffcc8800).withAlpha (0.85f));
        g.drawLine (fx, 0.0f, fx, h - 12.0f, 1.5f);

        // Small label above the line
        juce::String freqLabel;
        const float fHz = proc->getFilterFreqHz();
        freqLabel = (fHz >= 1000.0f) ? (juce::String (fHz / 1000.0f, 1) + "k")
                                      : juce::String ((int) fHz);
        g.setFont (juce::FontOptions (9.0f));
        g.setColour (juce::Colour (0xffcc8800));
        g.drawText (freqLabel, (int) fx - 14, 2, 28, 9, juce::Justification::centred);
    }

    // ---- Effective threshold line (amber dashed horizontal) -------------
    // The threshold is compared to the envelope of the boosted filtered signal.
    // Effective signal amplitude needed to trigger = threshold / boostLinear.
    // FFT magnitudes are normalised by fftSize; for a tone of amplitude A the peak
    // bin shows ~A/2, so the line sits at dB = 20*log10(effectiveAmp / 2).
    {
        const float thr        = proc->getThresholdValue();
        const float boostLin   = juce::Decibels::decibelsToGain (proc->getInputGainDb());
        const float effAmp     = thr / boostLin;
        const float threshDb   = juce::Decibels::gainToDecibels (effAmp * 0.5f, -80.0f);
        const float ty         = juce::jmap (threshDb, -80.0f, 0.0f, h - 12.0f, 0.0f);

        // Dashed line
        g.setColour (juce::Colour (0xffb86c00).withAlpha (0.85f));
        for (float x = 0.0f; x < w; x += 8.0f)
            g.fillRect (x, ty - 0.5f, 5.0f, 1.0f);

        // "THR" label on the right
        g.setFont (juce::FontOptions (9.0f));
        g.setColour (juce::Colour (0xffb86c00));
        g.drawText ("THR", (int)(w - 26.0f), (int)(ty - 10.0f), 24, 9,
                    juce::Justification::centred);
    }
}
