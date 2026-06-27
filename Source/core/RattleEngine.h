#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <random>
#include <atomic>
#include <cstdint>
#include "SampleSet.h"
#include "SequenceVoice.h"

//==============================================================================
// Owns the sample data and 8 concurrent SequenceVoice instances.
// All public methods must be called from the audio thread ONLY.
// The UI-thread trigger path lives in PluginProcessor (atomic flag → buildParams).
//==============================================================================
class RattleEngine
{
public:
    static constexpr int maxVoices = 8;

    RattleEngine();

    void prepare         (double sampleRate, int maxBlockSize);
    void releaseResources();

    bool loadFile (int slot, const juce::File& file);

    // Called from the audio thread. Picks slot/slice/pan via RR logic, then
    // allocates or steals a voice.
    void triggerNow (SequenceVoice::Params params); // by value — engine fills slot/slice/pan

    // Adds wet (rattle) output to wetBus (does NOT clear it first).
    void processBlock (juce::AudioBuffer<float>& wetBus, int numSamples);

    // Adds audition (preview) output to its own bus, so the host can route it
    // post-mix (independent of the FX dry/wet Mix).
    void processAudition (juce::AudioBuffer<float>& out, int numSamples);

    // One-shot preview of a single slot's whole sample at its gain (UI audition).
    // Audio thread only. Plays into a separate audition voice pool.
    void auditionSlot (int slot, double hostSampleRate);

    SampleSet&       getSampleSet()       { return sampleSet; }
    const SampleSet& getSampleSet() const { return sampleSet; }
    int              activeVoiceCount()   const;

    // Per-slot trigger counters, bumped each impact; the UI polls these to light
    // the slot being played. Read-only; values only ever increase (with wrap).
    const std::atomic<uint32_t>* getSlotHits() const noexcept { return slotHits; }

private:
    static constexpr int kMaxPool = 256; // unified-pool unit cap (8 slots × 32 slices)

    // Fills params.repSlices / repPan for the whole burst per the iteration modes.
    // Returns false if no sample is loaded.
    bool resolveUnits (SequenceVoice::Params& params, int rattle);

    // Builds the unified per-impact pool (all slices across loaded slots; an
    // unsliced slot contributes one whole-buffer unit). Sets poolSize.
    void buildPool (SequenceVoice::RepSlice* pool, int& poolSize,
                    const int* loaded, int numLoaded);

    // Per-trigger slot pick (Trigger iteration). playOrder: 0=Seq, 1=Rnd no-repeat.
    int pickSlot (int playOrder, const int* loaded, int numLoaded);

    // Per-impact pool index pick (Impact iteration). playOrder: 0=Seq, 1=Rnd no-repeat.
    int pickPoolIndex (int playOrder, int poolSize);

    // Computes the next pan position from panSpread and panCursor.
    float nextPanPosition (float panSpread);

    // Returns a free voice from the pool, or steals the oldest active one.
    SequenceVoice* allocFrom (SequenceVoice* pool, int count);
    SequenceVoice* allocVoice() { return allocFrom (voices, maxVoices); }

    static constexpr int maxAudition = 4;

    juce::AudioFormatManager formatManager;
    SampleSet                sampleSet;

    SequenceVoice voices[maxVoices];
    SequenceVoice auditionVoices[maxAudition];
    uint64_t      voiceCounter { 0 };

    std::atomic<uint32_t> slotHits[SampleSet::maxSlots] {}; // per-slot impact counters (UI indicator)

    // Round-robin state (persists across triggers).
    int           rrCursor     { 0  }; // slot cursor for sequential per-trigger RR
    int           lastSlotUsed { -1 }; // for per-trigger random-no-repeat
    int           poolCursor   { 0  }; // pool cursor for sequential per-impact RR
    int           lastPoolIdx  { -1 }; // for per-impact random-no-repeat
    int           panCursor    { 0  }; // alternates L/R for spread
    std::mt19937  rng;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RattleEngine)
};
