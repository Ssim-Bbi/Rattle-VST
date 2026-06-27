#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_basics/juce_audio_basics.h>

//==============================================================================
// Displays a waveform from a loaded AudioBuffer with optional slice markers.
// Stores pre-computed min/max peaks so paint() is cheap.
//
// Gestures:
//   - Left-drag a marker to move it; drag it off the component to delete it.
//   - Double-click adds a marker, or deletes the one under the cursor.
//   - Right-click toggles "garbage" on the slice under the cursor (a sliced slot),
//     or mutes the whole slot (an unsliced sample). Garbage/muted regions are
//     grayed out with a big X and skipped in playback.
//==============================================================================
class WaveformView : public juce::Component
{
public:
    WaveformView() = default;

    // Called when a new buffer or slot is selected. Pass nullptr to clear.
    void setBuffer (const juce::AudioBuffer<float>* buffer, double sampleRate);

    // Set normalised slice marker positions (0.0 = buffer start, 1.0 = end).
    void setMarkers (const std::vector<float>& normPositions);
    std::vector<float> getMarkers() const { return markerNorm; }

    // Per-slice garbage flags, aligned to the markers (slice k starts at marker k).
    void setSliceGarbage (const std::vector<bool>& garbage);

    // Whole-slot mute overlay (for an unsliced sample). Repaints on change.
    void setSlotMuted (bool muted);

    // Sample name drawn top-right (pass already-stripped display name, or "").
    void setSampleName (const juce::String& name);

    // Called when the user adds, moves, or deletes a marker. Positions normalised [0, 1].
    std::function<void (const std::vector<float>&)> onMarkersChanged;

    // Right-click on a slice region of a sliced slot — toggle that slice's garbage flag.
    std::function<void (int sliceIndex)> onSliceGarbageToggled;

    // Right-click on an unsliced sample — toggle the whole slot's mute.
    std::function<void ()> onSlotMuteToggled;

    void clear();

    void paint           (juce::Graphics& g) override;
    void mouseDown       (const juce::MouseEvent& e) override;
    void mouseDrag       (const juce::MouseEvent& e) override;
    void mouseUp         (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;

private:
    static constexpr int peakResolution = 512;
    static constexpr int dragThreshPx   = 8;

    // Finds the slice region [marker k, marker k+1) containing normX, or -1.
    int regionAt (float normX) const;

    std::vector<float> peaks;        // interleaved min/max pairs, length = peakResolution * 2
    std::vector<float> markerNorm;   // slice marker positions, normalised [0, 1]
    std::vector<bool>  sliceGarbage; // per-slice garbage flags, aligned to markerNorm
    bool               slotMuted    { false };
    int                draggedIdx   { -1 };
    bool               didDrag      { false }; // a real move happened this gesture
    bool               dragOutside  { false }; // pointer left the component during the drag
    juce::String       sampleName;   // shown top-right when a sample is loaded

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveformView)
};
