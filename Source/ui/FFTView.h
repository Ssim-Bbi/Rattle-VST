#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

class RattleAudioProcessor;

//==============================================================================
// Real-time FFT spectrum display for the FX plugin's audio input.
// Reads the processor's fftInputFifo ring buffer at ~30 Hz.
// X axis: 20 Hz – 20 kHz (log scale).   Y axis: –80 to 0 dB.
//==============================================================================
class FFTView : public juce::Component,
                private juce::Timer
{
public:
    FFTView() = default;
    ~FFTView() override;

    void setProcessor (RattleAudioProcessor* p);

    void paint           (juce::Graphics&) override;
    void visibilityChanged()               override;

private:
    static constexpr int fftOrder  = 11;
    static constexpr int fftSize   = 1 << fftOrder; // 2048
    static constexpr int numBins   = fftSize / 2;   // 1024 positive-frequency bins

    juce::dsp::FFT fft { fftOrder };

    std::array<float, fftSize * 2> fftData    {};
    std::array<float, numBins>     magnitudes {};   // instantaneous (peak) per-bin magnitude

    RattleAudioProcessor* proc = nullptr;

    void timerCallback() override;
    void computeFrame();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FFTView)
};
