#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_data_structures/juce_data_structures.h>
#include <memory>

//==============================================================================
// Holds up to 8 sample slots used by the rattle round-robin engine.
// Each slot stores raw float PCM at the file's native sample rate.
// The playback engine compensates for rate differences via varispeed.
// Serialises to/from a ValueTree using FLAC-compressed base-64 blobs.
//==============================================================================
class SampleSet
{
public:
    static constexpr int maxSlots = 8;

    struct Slice
    {
        int  start   { 0 };
        int  end     { 0 };     // exclusive; 0 = full buffer length
        bool garbage { false }; // true = skipped in playback (right-click "X" in the UI)
    };

    SampleSet() = default;

    bool loadFile (int slot, const juce::File& file, juce::AudioFormatManager& fmt);
    void clear    (int slot);
    void clearAll ();

    const juce::AudioBuffer<float>* getBuffer        (int slot) const;
    double                          getFileSampleRate (int slot) const;
    bool                            hasSlot           (int slot) const;

    // Shared-ownership handle for the audio thread to snapshot at trigger time.
    // A voice that holds this keeps the buffer alive even if the UI clears the
    // slot mid-playback (the memory is freed when the last holder drops it).
    std::shared_ptr<const juce::AudioBuffer<float>> getBufferShared (int slot) const;

    // Guards slot mutation (UI thread) against the trigger-time read (audio
    // thread). Mutators below take it; RattleEngine::resolveUnits try-locks it.
    juce::SpinLock& getLock() noexcept { return dataLock; }

    // Display name (file name with extension) of the loaded sample, or "".
    const juce::String& getFileName (int slot) const;

    // Per-slot playback gain in dB (±20, default 0). Saved with the sample,
    // not an automatable parameter.
    float getGainDb (int slot) const;
    void  setGainDb (int slot, float db);

    // Slice markers within a slot (sample positions, exclusive end).
    void                      setSlices   (int slot, std::vector<Slice> s);
    const std::vector<Slice>& getSlices   (int slot) const;
    void                      clearSlices (int slot);

    // Toggle a slice's "garbage" flag (skipped in playback). No-op if out of range.
    void                      toggleSliceGarbage (int slot, int sliceIndex);

    // Scan buffer for amplitude onsets and populate this slot's slice list.
    // Returns the number of slices detected. threshold is 0–1 normalised amplitude.
    int autoDetectSlices (int slot, float threshold = 0.1f);

    juce::ValueTree toValueTree   ()                         const;
    void            fromValueTree (const juce::ValueTree& v);

private:
    struct Slot
    {
        std::shared_ptr<juce::AudioBuffer<float>> buffer;
        double                   fileSampleRate { 44100.0 };
        bool                     loaded         { false   };
        std::vector<Slice>       slices;
        juce::String             fileName;
        float                    gainDb         { 0.0f    };
    };

    void clearSlotLocked (int slot); // resets a slot; caller must hold dataLock

    Slot slots[maxSlots];
    const std::vector<Slice> emptySlices; // returned when slot has no slices
    const juce::String       emptyName;   // returned for unloaded / out-of-range slots
    juce::SpinLock           dataLock;    // serialises slot mutation vs trigger read

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SampleSet)
};
