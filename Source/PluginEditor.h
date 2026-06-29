#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <cstdint>
#include "PluginProcessor.h"
#include "ui/WaveformView.h"
#include "ui/RattleGraph.h"
#include "ui/FFTView.h"
#include "ui/ChoiceToggleGroup.h"
#include "ui/LedDot.h"
#include "ui/SlotButton.h"

//==============================================================================
class RattleAudioProcessorEditor : public juce::AudioProcessorEditor,
                                    public juce::ChangeListener,
                                    public juce::FileDragAndDropTarget,
                                    public juce::Timer
{
public:
    explicit RattleAudioProcessorEditor (RattleAudioProcessor&);
    ~RattleAudioProcessorEditor() override;

    void paint   (juce::Graphics&) override;
    void resized () override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    // Drag-and-drop sample files.
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped           (const juce::StringArray& files, int x, int y) override;

private:
    using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    void updateWaveform();
    void openFileChooser();
    void selectSlot (int slot);
    void refreshSlotButtons();
    void applyView();                            // FX: swap Input/Sample views
    void syncModeChanged();                      // Free/Sync: swap Spacing control
    void updateLoopVisibility();                 // Play Order: show Loop direction only under Loop
    void resizedFX   (juce::Rectangle<int> area);
    void resizedInst (juce::Rectangle<int> area);

    RattleAudioProcessor& processorRef;

    // Views
    WaveformView waveformView;
    FFTView      fftView;
    RattleGraph  rattleGraph;

    // Slot strip (8 numbered buttons) + per-slot trigger LEDs
    static constexpr int numSlots = 8;
    SlotButton       slotButtons[numSlots];
    LedDot           slotLeds[numSlots];
    float            slotGlow[numSlots] {};
    uint32_t         lastHits[numSlots] {};
    int              selectedSlot { 0 };
    uint8_t          lastMuteMask { 0 };  // tracks per-slot mute to refresh button strikes

    // Sample-area buttons
    juce::TextButton loadButton    { "Load"    };
    juce::TextButton clearButton   { "Clear"   };
    juce::TextButton autoButton    { "Auto"    }; // auto-detect slices
    juce::TextButton triggerButton { "Trigger" }; // FX only
    juce::TextButton viewButton    { "Sample"  }; // FX: Input <-> Sample switch
    bool             showSampleView { true };  // FX starts on the Sample view (input filtering is moot with no sample)

    // Audition: when toggled on, clicking a slot button previews that sample.
    juce::ShapeButton auditionButton { "Audition",
                                       juce::Colour (0xff8a8a93),
                                       juce::Colour (0xffb0b0bc),
                                       juce::Colour (0xffb86c00) };

    // Per-sample gain (rotary, over the waveform bottom-right)
    juce::Slider sampleGainSlider;

    // ---- Section 1: Input (FX) -------------------------------------------
    juce::TextButton  filterListenButton { "LISTEN FILTER" };
    juce::Label       inputGainLabel, filterTypeLabel, filterFreqLabel, thresholdLabel, sensitivityLabel;
    ChoiceToggleGroup filterTypeGroup;
    juce::Slider      inputGainSlider, filterFreqSlider, thresholdSlider, sensitivitySlider;
    std::unique_ptr<SA> inputGainAttachment, filterFreqAttachment, thresholdAttachment, sensitivityAttachment;

    // ---- Section 1: Input (Inst) -----------------------------------------
    juce::Label    midiChannelLabel, centerNoteLabel;
    juce::ComboBox midiChannelBox;
    juce::Slider   centerNoteSlider;
    std::unique_ptr<CA> midiChannelAttachment;
    std::unique_ptr<SA> centerNoteAttachment;

    // ---- Section 1: Input (shared) ---------------------------------------
    juce::Label  spreadLabel, velocityLabel;
    juce::Slider spreadSlider, velocitySlider;
    std::unique_ptr<SA> spreadAttachment, velocityAttachment;

    // ---- Section 2: Rattle -----------------------------------------------
    juce::Label  rattleLabel, decayLabel, paceLabel, paceCurveLabel;
    juce::Slider rattleSlider, decaySlider, paceSlider, paceCurveSlider;
    std::unique_ptr<SA> rattleAttachment, decayAttachment, paceAttachment, paceCurveAttachment;

    juce::Label       playOrderLabel, sampleIterLabel, panIterLabel, panSpreadLabel;
    ChoiceToggleGroup playOrderGroup, sampleIterGroup, panIterGroup;
    juce::Slider      panSpreadSlider;
    std::unique_ptr<SA> panSpreadAttachment;

    juce::Label       loopDirLabel;   // "Loop" direction sub-control, shown only when Play Order = Loop
    ChoiceToggleGroup loopDirGroup;
    std::unique_ptr<juce::ParameterAttachment> playOrderWatcher; // shows/hides loopDir on Play Order change
    bool              loopActive { false };

    // Tempo sync (shares the Spacing cell with paceSlider in Sync mode)
    juce::Label       tempoSyncLabel, gridLabel;
    ChoiceToggleGroup tempoSyncGroup, syncGridGroup;
    juce::ComboBox    divisionBox;
    std::unique_ptr<CA> divisionAttachment;
    std::unique_ptr<juce::ParameterAttachment> tempoSyncWatcher;
    bool              syncOn { false };

    // ---- Section 2: Pitch ------------------------------------------------
    juce::Label       pitchLabel, pitchCurveAmtLabel, pitchCurveShapeLabel;
    juce::Slider      pitchSlider, pitchCurveAmtSlider;
    ChoiceToggleGroup pitchCurveShapeGroup;
    std::unique_ptr<SA> pitchAttachment, pitchCurveAmtAttachment;

    // ---- Section 3: Output -----------------------------------------------
    juce::Label  outputLevelLabel, mixLabel;
    juce::Slider outputLevelSlider, mixSlider;
    std::unique_ptr<SA> outputLevelAttachment, mixAttachment;

    // Divider Y positions (set in resized, used in paint)
    int sep1Y { 0 }, sep2Y { 0 }, sep3Y { 0 }, sep4Y { 0 };

    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RattleAudioProcessorEditor)
};
