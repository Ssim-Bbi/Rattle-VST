#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "core/Params.h"
#include "core/RattleEngine.h"
#include "core/PreFilter.h"
#include "core/OnsetDetector.h"

#ifndef RATTLE_IS_INSTRUMENT
 #define RATTLE_IS_INSTRUMENT 0
#endif

//==============================================================================
class RattleAudioProcessor : public juce::AudioProcessor,
                              public juce::ChangeBroadcaster
{
public:
    RattleAudioProcessor();
    ~RattleAudioProcessor() override = default;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==========================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    //==========================================================================
    const juce::String getName() const override;
    bool acceptsMidi() const override  { return RATTLE_IS_INSTRUMENT != 0; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 5.0; }

    //==========================================================================
    int getNumPrograms() override                              { return 1; }
    int getCurrentProgram() override                           { return 0; }
    void setCurrentProgram (int) override                      {}
    const juce::String getProgramName (int) override           { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    //==========================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    static constexpr bool isInstrument() { return RATTLE_IS_INSTRUMENT != 0; }

    juce::AudioProcessorValueTreeState& getAPVTS() { return apvts; }
    RattleEngine& getEngine()                      { return engine; }

    bool loadSample (const juce::File& file, int slot = 0);

    // Loads files into empty slots (left→right, filename order). Returns the
    // index of the last slot filled, or -1 if nothing loaded.
    int  loadSamplesFillingEmpty (const juce::Array<juce::File>& files);

    // Per-sample gain (±20 dB) for a slot — UI thread; persists in state.
    void  setSampleGainDb (int slot, float db);
    float getSampleGainDb (int slot) const;

    void fireTrigger() { pendingTrigger.store (true, std::memory_order_release); }

    // Audition (preview) a slot's sample once — UI thread sets, audio thread plays.
    void auditionSlot (int slot) { pendingAudition.store (slot, std::memory_order_release); }

    // Per-slot trigger counters for the UI indicator (read by the editor's timer).
    const std::atomic<uint32_t>* getSlotHits() const noexcept { return engine.getSlotHits(); }

    // Routes the pre-filtered signal to output instead of rattle (FX diagnostic).
    void setFilterListen (bool b) noexcept { filterListenMode.store (b, std::memory_order_relaxed); }

    // Slice operations (UI thread — call before sendChangeMessage).
    void setSlicesForSlot  (int slot, const std::vector<int>& markerSamples);
    void autoDetectSlices  (int slot);
    void clearSlot         (int slot);
    void toggleSliceGarbage (int slot, int sliceIndex); // skip/unskip a slice in playback

    // Accessors for FFT overlay rendering (called from FFTView on the UI thread).
    float getFilterFreqHz()  const noexcept { return filterFreqParam  ? filterFreqParam->load()       : 1000.0f; }
    float getThresholdValue()const noexcept { return thresholdParam   ? thresholdParam->load()        : 0.2f;    }
    float getInputGainDb()   const noexcept { return inputGainParam   ? inputGainParam->load()        : 0.0f;    }
    int   getFilterTypeIdx() const noexcept { return filterTypeParam  ? (int)filterTypeParam->load()  : 0;       }

    // FFT display ring buffer — written by audio thread, read by FFTView at ~30 Hz.
    // UI reads are lock-free with acceptable display-only tearing.
    static constexpr int fftOrder = 11;
    static constexpr int fftSize  = 1 << fftOrder; // 2048 samples
    std::array<float, fftSize> fftInputFifo {};
    std::atomic<int>           fftWritePos  { 0 };
    // Set true by FFTView while it is visible; the audio thread only fills the
    // ring buffer when active, so no FFT feed work is done in Sample view or when
    // the editor window is closed.
    std::atomic<bool>          fftActive    { false };

private:
    // Snapshots all rattle / sequence params into a SequenceVoice::Params struct.
    // rawVelocity is blended with the VELOCITY param (0 = flat, 1 = full dynamic).
    SequenceVoice::Params buildParams (float rawVelocity) const;

    juce::AudioProcessorValueTreeState apvts;
    RattleEngine  engine;
    PreFilter     preFilter;
    OnsetDetector onsetDetector;

    double currentSampleRate { 44100.0 };
    double currentBpm        { 120.0 };  // from host playhead, for Sync spacing
    juce::AudioBuffer<float> dryBuffer;
    juce::AudioBuffer<float> filteredMono;   // [FX] stores per-block pre-filter output
    juce::AudioBuffer<float> auditionBuffer; // preview voices, added post-mix
    std::atomic<bool> pendingTrigger    { false };
    std::atomic<int>  pendingAudition   { -1 };    // slot to preview, or -1
    std::atomic<bool> filterListenMode  { false }; // [FX] diagnostic: output pre-filter instead of rattle

    // ---- Section 1: Input ------------------------------------------------
    std::atomic<float>* inputGainParam     { nullptr }; // [FX]
    std::atomic<float>* velocityParam      { nullptr }; // both modes
    std::atomic<float>* spreadParam        { nullptr }; // both modes
    std::atomic<float>* midiChannelParam   { nullptr }; // [Inst]
    std::atomic<float>* centerNoteParam    { nullptr }; // [Inst]
    std::atomic<float>* filterTypeParam    { nullptr }; // [FX]
    std::atomic<float>* filterFreqParam    { nullptr }; // [FX]
    std::atomic<float>* thresholdParam     { nullptr }; // [FX]
    std::atomic<float>* sensitivityParam   { nullptr }; // [FX]

    // ---- Section 2: Rattle -----------------------------------------------
    std::atomic<float>* rattleParam         { nullptr };
    std::atomic<float>* decayParam          { nullptr };
    std::atomic<float>* paceParam           { nullptr };
    std::atomic<float>* paceCurveParam      { nullptr };
    std::atomic<float>* tempoSyncParam      { nullptr };
    std::atomic<float>* syncDivisionParam   { nullptr };
    std::atomic<float>* syncGridParam       { nullptr };
    std::atomic<float>* playOrderParam      { nullptr };
    std::atomic<float>* sampleIterParam     { nullptr };
    std::atomic<float>* panIterParam        { nullptr };
    std::atomic<float>* panSpreadParam      { nullptr };
    std::atomic<float>* loopDirParam        { nullptr };
    std::atomic<float>* pitchParam          { nullptr };
    std::atomic<float>* pitchCurveAmtParam  { nullptr };
    std::atomic<float>* pitchCurveShapeParam{ nullptr };

    // ---- Section 3: Output -----------------------------------------------
    std::atomic<float>* mixParam         { nullptr };
    std::atomic<float>* outputLevelParam { nullptr };

    // Per-slot mute (automatable) — built into a bitmask passed to the engine.
    std::atomic<float>* slotMuteParams[SampleSet::maxSlots] {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RattleAudioProcessor)
};
