#include "PluginEditor.h"

//==============================================================================
static void styleLabel (juce::Label& label, const juce::String& text)
{
    label.setText (text, juce::dontSendNotification);
    label.setFont (juce::FontOptions (13.0f));
    label.setColour (juce::Label::textColourId, juce::Colour (0xff8a8a93));
    label.setJustificationType (juce::Justification::centredRight);
}

static void styleSlider (juce::Slider& s)
{
    s.setSliderStyle (juce::Slider::LinearHorizontal);
    s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 22);
    s.setColour (juce::Slider::backgroundColourId,     juce::Colour (0xff2e2e34));
    s.setColour (juce::Slider::trackColourId,          juce::Colour (0xff5e5ef0));
    s.setColour (juce::Slider::thumbColourId,          juce::Colours::white);
    s.setColour (juce::Slider::textBoxTextColourId,    juce::Colour (0xff8a8a93));
    s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
}

static void styleButton (juce::TextButton& b)
{
    b.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff3a3a48));
    b.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff5e5ef0));
    b.setColour (juce::TextButton::textColourOffId,  juce::Colour (0xffb0b0bc));
    b.setColour (juce::TextButton::textColourOnId,   juce::Colours::white);
}

static void styleCombo (juce::ComboBox& c)
{
    c.setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff2e2e34));
    c.setColour (juce::ComboBox::textColourId,       juce::Colour (0xffc0c0cc));
    c.setColour (juce::ComboBox::outlineColourId,    juce::Colour (0xff3a3a48));
    c.setColour (juce::ComboBox::arrowColourId,      juce::Colour (0xff5e5ef0));
}

//==============================================================================
RattleAudioProcessorEditor::RattleAudioProcessorEditor (RattleAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p)
{
    auto& apvts = p.getAPVTS();

    auto makeSlider = [&] (juce::Label& lbl, const juce::String& text,
                           juce::Slider& s, std::unique_ptr<SA>& att, const char* id)
    {
        styleLabel (lbl, text); styleSlider (s);
        addAndMakeVisible (lbl); addAndMakeVisible (s);
        att = std::make_unique<SA> (apvts, id, s);
    };

    auto makeGroup = [&] (juce::Label& lbl, const juce::String& text, ChoiceToggleGroup& grp,
                          const char* id, const juce::StringArray& opts)
    {
        styleLabel (lbl, text);
        addAndMakeVisible (lbl);
        grp.attach (apvts, id, opts);
        addAndMakeVisible (grp);
    };

    // ---- Waveform + FFT view ----------------------------------------------
    addAndMakeVisible (waveformView);

    waveformView.onMarkersChanged = [this] (const std::vector<float>& norm)
    {
        const auto* buf = processorRef.getEngine().getSampleSet().getBuffer (selectedSlot);
        if (buf == nullptr) return;

        const int totalSmp = buf->getNumSamples();
        std::vector<int> markerSamples;
        markerSamples.reserve (norm.size());
        for (float n : norm)
            markerSamples.push_back (juce::roundToInt (n * (float) totalSmp));

        processorRef.setSlicesForSlot (selectedSlot, markerSamples);
    };

    // Right-click a slice (sliced slot) → toggle its garbage flag.
    waveformView.onSliceGarbageToggled = [this] (int sliceIndex)
    {
        processorRef.toggleSliceGarbage (selectedSlot, sliceIndex);
    };

    // Right-click an unsliced sample → toggle the slot's (automatable) mute param.
    waveformView.onSlotMuteToggled = [this]
    {
        if (auto* prm = processorRef.getAPVTS().getParameter (RattleParams::ID::slotMute (selectedSlot)))
            prm->setValueNotifyingHost (prm->getValue() > 0.5f ? 0.0f : 1.0f);
    };

    // Per-sample gain knob (over the waveform).
    sampleGainSlider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    sampleGainSlider.setRange (-20.0, 20.0, 0.1);
    sampleGainSlider.setValue (0.0, juce::dontSendNotification);
    sampleGainSlider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 50, 14);
    sampleGainSlider.setTextValueSuffix (" dB");
    sampleGainSlider.setColour (juce::Slider::rotarySliderFillColourId,   juce::Colour (0xff5e5ef0));
    sampleGainSlider.setColour (juce::Slider::rotarySliderOutlineColourId,juce::Colour (0xff3a3a48));
    sampleGainSlider.setColour (juce::Slider::thumbColourId,              juce::Colours::white);
    sampleGainSlider.setColour (juce::Slider::textBoxTextColourId,        juce::Colour (0xff8a8a93));
    sampleGainSlider.setColour (juce::Slider::textBoxOutlineColourId,     juce::Colours::transparentBlack);
    sampleGainSlider.onValueChange = [this]
    {
        processorRef.setSampleGainDb (selectedSlot, (float) sampleGainSlider.getValue());
    };
    addAndMakeVisible (sampleGainSlider);

    if (! processorRef.isInstrument())
    {
        fftView.setProcessor (&p);
        addChildComponent (fftView);
    }

    rattleGraph.setParams (
        apvts.getRawParameterValue (RattleParams::ID::rattle),
        apvts.getRawParameterValue (RattleParams::ID::decay),
        apvts.getRawParameterValue (RattleParams::ID::pace),
        apvts.getRawParameterValue (RattleParams::ID::paceCurve),
        apvts.getRawParameterValue (RattleParams::ID::pitch),
        apvts.getRawParameterValue (RattleParams::ID::pitchCurveAmt),
        apvts.getRawParameterValue (RattleParams::ID::pitchCurveShape),
        apvts.getRawParameterValue (RattleParams::ID::tempoSync),
        apvts.getRawParameterValue (RattleParams::ID::syncDivision),
        apvts.getRawParameterValue (RattleParams::ID::syncGrid));
    addAndMakeVisible (rattleGraph);

    // ---- Slot strip -------------------------------------------------------
    for (int i = 0; i < numSlots; ++i)
    {
        slotButtons[i].setButtonText (juce::String (i + 1));
        slotButtons[i].setClickingTogglesState (false);
        slotButtons[i].setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff2a2a36));
        slotButtons[i].setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff5e5ef0));
        slotButtons[i].setColour (juce::TextButton::textColourOffId,  juce::Colour (0xff6a6a7a));
        slotButtons[i].setColour (juce::TextButton::textColourOnId,   juce::Colours::white);
        slotButtons[i].onClick = [this, i]
        {
            selectSlot (i);
            if (auditionButton.getToggleState())
                processorRef.auditionSlot (i);
        };
        // Right-click a slot button → mute/unmute the whole slot at once
        // (works for multi-slice slots without garbage-flagging each slice).
        slotButtons[i].onRightClick = [this, i]
        {
            if (! processorRef.getEngine().getSampleSet().hasSlot (i))
                return;                                      // nothing to mute on an empty slot
            if (auto* prm = processorRef.getAPVTS().getParameter (RattleParams::ID::slotMute (i)))
                prm->setValueNotifyingHost (prm->getValue() > 0.5f ? 0.0f : 1.0f);
            refreshSlotButtons();                            // immediate visual feedback
        };
        addAndMakeVisible (slotButtons[i]);
        addAndMakeVisible (slotLeds[i]); // drawn on top of the button (corner LED)
    }

    // ---- Audition (speaker) toggle ---------------------------------------
    {
        juce::Path sp;
        sp.startNewSubPath (0.06f, 0.40f);
        sp.lineTo (0.30f, 0.40f);
        sp.lineTo (0.54f, 0.16f);
        sp.lineTo (0.54f, 0.84f);
        sp.lineTo (0.30f, 0.60f);
        sp.lineTo (0.06f, 0.60f);
        sp.closeSubPath();
        auditionButton.setShape (sp, false, true, false);
        auditionButton.setClickingTogglesState (true);
        auditionButton.setOnColours (juce::Colour (0xffb86c00),
                                     juce::Colour (0xffd07c10),
                                     juce::Colour (0xffb86c00));
        auditionButton.shouldUseOnColours (true);
        addAndMakeVisible (auditionButton);
    }

    // ---- Sample-area buttons ----------------------------------------------
    styleButton (loadButton);
    loadButton.onClick = [this] { openFileChooser(); };
    addAndMakeVisible (loadButton);

    styleButton (clearButton);
    clearButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff3a2a2a));
    clearButton.onClick = [this]
    {
        processorRef.clearSlot (selectedSlot);
        refreshSlotButtons();
    };
    addAndMakeVisible (clearButton);

    styleButton (autoButton);
    autoButton.setColour (juce::TextButton::buttonColourId, juce::Colour (0xff2a3a2a));
    autoButton.onClick = [this] { processorRef.autoDetectSlices (selectedSlot); };
    addAndMakeVisible (autoButton);

    if (! processorRef.isInstrument())
    {
        styleButton (triggerButton);
        triggerButton.onClick = [this] { processorRef.fireTrigger(); };
        addAndMakeVisible (triggerButton);

        styleButton (viewButton);
        viewButton.onClick = [this] { showSampleView = ! showSampleView; applyView(); };
        addAndMakeVisible (viewButton);
    }

    // ---- Section 1: Input ------------------------------------------------
    if (! processorRef.isInstrument())
    {
        filterListenButton.setClickingTogglesState (true);
        filterListenButton.setColour (juce::TextButton::buttonColourId,   juce::Colour (0xff2e2e34));
        filterListenButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffb86c00));
        filterListenButton.setColour (juce::TextButton::textColourOffId,  juce::Colour (0xff8a8a93));
        filterListenButton.setColour (juce::TextButton::textColourOnId,   juce::Colours::white);
        filterListenButton.onClick = [this]
        {
            processorRef.setFilterListen (filterListenButton.getToggleState());
        };
        addAndMakeVisible (filterListenButton);

        makeSlider (inputGainLabel, "Input Gain", inputGainSlider,
                    inputGainAttachment, RattleParams::ID::inputGain);

        makeGroup (filterTypeLabel, "Filter", filterTypeGroup,
                   RattleParams::ID::filterType, { "LP", "BP", "HP" });

        makeSlider (filterFreqLabel,  "Filter Freq",  filterFreqSlider,  filterFreqAttachment,  RattleParams::ID::filterFreq);
        makeSlider (thresholdLabel,   "Threshold",    thresholdSlider,   thresholdAttachment,   RattleParams::ID::threshold);
        makeSlider (sensitivityLabel, "Sensitivity",  sensitivitySlider, sensitivityAttachment, RattleParams::ID::sensitivity);
    }
    else
    {
        midiChannelBox.addItem ("Omni", 1);
        for (int i = 1; i <= 16; ++i)
            midiChannelBox.addItem (juce::String (i), i + 1);
        styleLabel (midiChannelLabel, "MIDI Channel"); styleCombo (midiChannelBox);
        addAndMakeVisible (midiChannelLabel); addAndMakeVisible (midiChannelBox);
        midiChannelAttachment = std::make_unique<CA> (apvts, RattleParams::ID::midiChannel, midiChannelBox);

        makeSlider (centerNoteLabel, "Center Note", centerNoteSlider,
                    centerNoteAttachment, RattleParams::ID::centerNote);
    }

    makeSlider (spreadLabel,   "Spread",   spreadSlider,   spreadAttachment,   RattleParams::ID::spread);
    makeSlider (velocityLabel, "Velocity", velocitySlider, velocityAttachment, RattleParams::ID::velocity);

    // ---- Section 2: Rattle -----------------------------------------------
    makeSlider (rattleLabel,    "Rattle",     rattleSlider,    rattleAttachment,    RattleParams::ID::rattle);
    makeSlider (decayLabel,     "Decay",      decaySlider,     decayAttachment,     RattleParams::ID::decay);
    makeSlider (paceLabel,      "Spacing",    paceSlider,      paceAttachment,      RattleParams::ID::pace);
    makeSlider (paceCurveLabel, "Drift",      paceCurveSlider, paceCurveAttachment, RattleParams::ID::paceCurve);

    // Tempo sync controls (Sync toggle, Division combo shares the Spacing cell, Grid granularity).
    makeGroup (tempoSyncLabel, "Sync", tempoSyncGroup, RattleParams::ID::tempoSync, { "Free", "Sync" });

    styleLabel (gridLabel, "Grid");
    addAndMakeVisible (gridLabel);
    syncGridGroup.attach (apvts, RattleParams::ID::syncGrid, { "1/8", "1/16", "1/32", "1/64" });
    addAndMakeVisible (syncGridGroup);

    divisionBox.addItemList ({ "1/4", "1/4.", "1/4T", "1/8", "1/8.", "1/8T",
                               "1/16", "1/16.", "1/16T", "1/32" }, 1);
    styleCombo (divisionBox);
    addAndMakeVisible (divisionBox);
    divisionAttachment = std::make_unique<CA> (apvts, RattleParams::ID::syncDivision, divisionBox);

    tempoSyncWatcher = std::make_unique<juce::ParameterAttachment> (
        *apvts.getParameter (RattleParams::ID::tempoSync),
        [this] (float v) { syncOn = (v >= 0.5f); syncModeChanged(); });
    tempoSyncWatcher->sendInitialUpdate();

    makeGroup (playOrderLabel,  "Play Order", playOrderGroup,
               RattleParams::ID::playOrder,  { "Seq", "Rnd" });
    makeGroup (sampleIterLabel, "Sample Iter", sampleIterGroup,
               RattleParams::ID::sampleIter, { "Trigger", "Impact" });
    makeGroup (panIterLabel,    "Pan Iter", panIterGroup,
               RattleParams::ID::panIter,    { "Trigger", "Impact" });
    makeGroup (loopModeLabel, "Loop", loopModeGroup,
               RattleParams::ID::loopMode, { "Off", "FW", "P-P", "BW" });

    makeSlider (panSpreadLabel, "Pan Spread", panSpreadSlider, panSpreadAttachment, RattleParams::ID::panSpread);

    // ---- Section 2: Pitch -----------------------------------------------
    makeSlider (pitchLabel,         "Pitch (st)",    pitchSlider,         pitchAttachment,         RattleParams::ID::pitch);
    makeSlider (pitchCurveAmtLabel, "Pitch Crv Amt", pitchCurveAmtSlider, pitchCurveAmtAttachment, RattleParams::ID::pitchCurveAmt);

    makeGroup (pitchCurveShapeLabel, "Pitch Crv Shape", pitchCurveShapeGroup,
               RattleParams::ID::pitchCurveShape, { "Lin", "Exp" });

    // ---- Section 3: Output -----------------------------------------------
    makeSlider (outputLevelLabel, "Output Level", outputLevelSlider, outputLevelAttachment, RattleParams::ID::outputLevel);
    if (! processorRef.isInstrument())
        makeSlider (mixLabel, "Mix", mixSlider, mixAttachment, RattleParams::ID::mix);

    // -----------------------------------------------------------------------
    processorRef.addChangeListener (this);
    selectSlot (0);

    if (! processorRef.isInstrument())
        applyView();           // FX: start on the Input view
    updateWaveform();

    startTimerHz (30);         // poll per-slot trigger indicators
    setSize (560, processorRef.isInstrument() ? 895 : 835);
}

RattleAudioProcessorEditor::~RattleAudioProcessorEditor()
{
    stopTimer();
    processorRef.removeChangeListener (this);
}

//==============================================================================
void RattleAudioProcessorEditor::timerCallback()
{
    const auto* hits = processorRef.getSlotHits();
    if (hits == nullptr) return;

    for (int i = 0; i < numSlots; ++i)
    {
        const uint32_t h = hits[i].load (std::memory_order_relaxed);
        if (h != lastHits[i])
        {
            lastHits[i] = h;
            slotGlow[i] = 1.0f;            // a new impact fired from this slot
        }
        else
        {
            slotGlow[i] *= 0.80f;          // fade
            if (slotGlow[i] < 0.02f) slotGlow[i] = 0.0f;
        }
        slotLeds[i].setLevel (slotGlow[i]);
    }

    // Keep the mute overlay in sync with the automatable mute param (guards on change).
    if (auto* prm = processorRef.getAPVTS().getParameter (RattleParams::ID::slotMute (selectedSlot)))
        waveformView.setSlotMuted (prm->getValue() > 0.5f);

    // Reflect every slot's mute on its button strike (catches host/automation changes,
    // not just right-clicks). Only repaints the strip when the mask actually changes.
    uint8_t mask = 0;
    for (int i = 0; i < numSlots; ++i)
        if (auto* prm = processorRef.getAPVTS().getParameter (RattleParams::ID::slotMute (i)))
            if (prm->getValue() > 0.5f) mask |= (uint8_t) (1 << i);
    if (mask != lastMuteMask)
    {
        lastMuteMask = mask;
        refreshSlotButtons();
    }
}

//==============================================================================
void RattleAudioProcessorEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refreshSlotButtons();
    updateWaveform();
}

void RattleAudioProcessorEditor::applyView()
{
    const bool s = showSampleView;

    // Input view (FFT + detection chain).
    fftView.setVisible (! s);
    inputGainLabel.setVisible  (! s); inputGainSlider.setVisible  (! s);
    filterTypeLabel.setVisible (! s); filterTypeGroup.setVisible  (! s);
    filterFreqLabel.setVisible (! s); filterFreqSlider.setVisible (! s);
    thresholdLabel.setVisible  (! s); thresholdSlider.setVisible  (! s);
    sensitivityLabel.setVisible(! s); sensitivitySlider.setVisible(! s);
    spreadLabel.setVisible     (! s); spreadSlider.setVisible     (! s);
    velocityLabel.setVisible   (! s); velocitySlider.setVisible   (! s);
    filterListenButton.setVisible (! s);

    // Sample view (waveform + slots).
    waveformView.setVisible    (s);
    sampleGainSlider.setVisible(s);
    for (auto& b : slotButtons) b.setVisible (s);
    for (auto& d : slotLeds)    d.setVisible (s);
    auditionButton.setVisible (s);
    loadButton.setVisible  (s);
    autoButton.setVisible  (s);
    clearButton.setVisible (s);

    viewButton.setButtonText (s ? "Input" : "Sample");
    repaint();
}

void RattleAudioProcessorEditor::syncModeChanged()
{
    // The Spacing cell holds the ms slider (Free) or the Division combo (Sync);
    // the Grid granularity only applies in Sync.
    paceSlider.setVisible    (! syncOn);
    divisionBox.setVisible   (syncOn);
    syncGridGroup.setVisible (syncOn);
    gridLabel.setVisible     (syncOn);
    paceLabel.setText (syncOn ? "Division" : "Spacing", juce::dontSendNotification);
}

void RattleAudioProcessorEditor::selectSlot (int slot)
{
    selectedSlot = slot;
    refreshSlotButtons();
    updateWaveform();
}

void RattleAudioProcessorEditor::refreshSlotButtons()
{
    const auto& ss = processorRef.getEngine().getSampleSet();

    for (int i = 0; i < numSlots; ++i)
    {
        const bool loaded   = ss.hasSlot (i);
        const bool selected = (i == selectedSlot);

        slotButtons[i].setColour (juce::TextButton::buttonColourId,
            selected ? juce::Colour (0xff5e5ef0)
                     : (loaded ? juce::Colour (0xff3a3a56) : juce::Colour (0xff2a2a36)));
        slotButtons[i].setColour (juce::TextButton::textColourOffId,
            loaded ? juce::Colour (0xffc0c0d8) : juce::Colour (0xff5a5a6a));

        bool muted = false;
        if (auto* prm = processorRef.getAPVTS().getParameter (RattleParams::ID::slotMute (i)))
            muted = prm->getValue() > 0.5f;
        slotButtons[i].setMuted (muted && loaded);   // strike only meaningful on a loaded slot
    }
    repaint();
}

void RattleAudioProcessorEditor::updateWaveform()
{
    const auto& ss = processorRef.getEngine().getSampleSet();
    const auto* buf = ss.getBuffer (selectedSlot);
    waveformView.setBuffer (buf, ss.getFileSampleRate (selectedSlot));

    // Slice markers + per-slice garbage flags.
    if (buf != nullptr && buf->getNumSamples() > 0)
    {
        const int totalSmp = buf->getNumSamples();
        const auto& slices = ss.getSlices (selectedSlot);
        std::vector<float> normPositions;
        std::vector<bool>  garbage;
        normPositions.reserve (slices.size());
        garbage.reserve (slices.size());
        for (const auto& sl : slices)
        {
            normPositions.push_back ((float) sl.start / (float) totalSmp);
            garbage.push_back (sl.garbage);
        }
        waveformView.setMarkers (normPositions);
        waveformView.setSliceGarbage (garbage);
    }
    else
    {
        waveformView.setMarkers ({});
        waveformView.setSliceGarbage ({});
    }

    // Whole-slot mute overlay (the automatable per-slot mute param).
    if (auto* prm = processorRef.getAPVTS().getParameter (RattleParams::ID::slotMute (selectedSlot)))
        waveformView.setSlotMuted (prm->getValue() > 0.5f);

    // Sample name (extension stripped).
    const auto raw   = ss.getFileName (selectedSlot);
    const auto clean = raw.contains (".") ? raw.upToLastOccurrenceOf (".", false, false) : raw;
    waveformView.setSampleName (clean);

    // Per-sample gain knob follows the selected slot.
    sampleGainSlider.setValue (processorRef.getSampleGainDb (selectedSlot), juce::dontSendNotification);
}

void RattleAudioProcessorEditor::openFileChooser()
{
    fileChooser = std::make_unique<juce::FileChooser> (
        "Load Sample(s)",
        juce::File::getSpecialLocation (juce::File::userMusicDirectory),
        "*.wav;*.aiff;*.aif;*.flac;*.ogg;*.mp3");

    fileChooser->launchAsync (
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
            | juce::FileBrowserComponent::canSelectMultipleItems,
        [this] (const juce::FileChooser& fc)
        {
            const auto results = fc.getResults();
            if (results.isEmpty())
                return;

            if (results.size() == 1)
            {
                processorRef.loadSample (results[0], selectedSlot);
                selectSlot (selectedSlot);
            }
            else
            {
                const int last = processorRef.loadSamplesFillingEmpty (results);
                if (last >= 0) selectSlot (last);
            }
        });
}

//==============================================================================
bool RattleAudioProcessorEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
    {
        const auto ext = juce::File (f).getFileExtension().toLowerCase();
        if (ext == ".wav" || ext == ".aiff" || ext == ".aif"
            || ext == ".flac" || ext == ".ogg" || ext == ".mp3")
            return true;
    }
    return false;
}

void RattleAudioProcessorEditor::filesDropped (const juce::StringArray& files, int x, int y)
{
    juce::Array<juce::File> list;
    for (const auto& f : files)
    {
        juce::File jf (f);
        if (jf.existsAsFile())
            list.add (jf);
    }
    if (list.isEmpty())
        return;

    // A single file dropped onto a specific slot button replaces that slot.
    if (list.size() == 1)
    {
        for (int i = 0; i < numSlots; ++i)
            if (slotButtons[i].isVisible() && slotButtons[i].getBounds().contains (x, y))
            {
                processorRef.loadSample (list[0], i);
                selectSlot (i);
                return;
            }
    }

    // Otherwise fill empty slots, left-to-right.
    const int last = processorRef.loadSamplesFillingEmpty (list);
    if (last >= 0) selectSlot (last);
}

//==============================================================================
void RattleAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1b1b1f));

    auto bounds = getLocalBounds();

    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (36.0f).withStyle ("Bold"));
    g.drawFittedText ("RATTLE", bounds.removeFromTop (60), juce::Justification::centredBottom, 1);

    g.setColour (juce::Colour (0xff8a8a93));
    g.setFont (juce::FontOptions (13.0f));
    g.drawFittedText (processorRef.isInstrument()
                          ? "Instrument  —  MIDI-triggered"
                          : "Audio Insert  —  FX",
                      bounds.removeFromTop (26), juce::Justification::centredTop, 1);

    g.setColour (juce::Colour (0xff2a2a36));
    const int px = 24, pw = getWidth() - px * 2;
    if (sep1Y > 0) g.fillRect (px, sep1Y, pw, 1);
    if (sep2Y > 0) g.fillRect (px, sep2Y, pw, 1);
    if (sep3Y > 0) g.fillRect (px, sep3Y, pw, 1);
    if (sep4Y > 0) g.fillRect (px, sep4Y, pw, 1);
}

//==============================================================================
void RattleAudioProcessorEditor::resized()
{
    if (processorRef.isInstrument())
        resizedInst (getLocalBounds());
    else
        resizedFX (getLocalBounds());
}

void RattleAudioProcessorEditor::resizedFX (juce::Rectangle<int> area)
{
    const int padX = 24, labelW = 114, labelWC = 84, colGap = 10, rowH = 28, rowGap = 7, secGap = 16;

    // Header buttons (top-right).
    viewButton.setBounds    (getWidth() - padX - 76,            16, 76, 24);
    triggerButton.setBounds (getWidth() - padX - 76 - 8 - 72,   16, 72, 24);

    area = area.withTrimmedLeft (padX).withTrimmedRight (padX);
    area.removeFromTop (86); // title + subtitle

    // ---- Top band: viewer (left) + view-specific controls (right) ----
    area.removeFromTop (4);
    auto band = area.removeFromTop (200);

    const int viewerW = 280;
    auto viewer = band.removeFromLeft (viewerW);
    fftView.setBounds (viewer);
    waveformView.setBounds (viewer);
    sampleGainSlider.setBounds (viewer.getRight() - 56, viewer.getBottom() - 62, 50, 58);

    band.removeFromLeft (colGap);
    auto rightCol = band;

    // Input-view controls.
    {
        auto rc = rightCol;
        const int rh = 22, rg = 3, lw = 66;
        auto miniRow = [&] (juce::Label& l, juce::Component& c)
        {
            auto row = rc.removeFromTop (rh);
            l.setBounds (row.removeFromLeft (lw));
            c.setBounds (row);
            rc.removeFromTop (rg);
        };
        miniRow (inputGainLabel,   inputGainSlider);
        miniRow (filterTypeLabel,  filterTypeGroup);
        miniRow (filterFreqLabel,  filterFreqSlider);
        miniRow (thresholdLabel,   thresholdSlider);
        miniRow (sensitivityLabel, sensitivitySlider);
        miniRow (spreadLabel,      spreadSlider);
        miniRow (velocityLabel,    velocitySlider);
        filterListenButton.setBounds (rc.removeFromTop (22));
    }

    // Sample-view controls (same rect; only one set is visible).
    {
        auto rc = rightCol;
        auto btnRow = rc.removeFromBottom (26);
        rc.removeFromBottom (6);

        const int cols = 2, rows = 4, cellGap = 5;
        const int cellW = (rc.getWidth() - cellGap) / cols;
        const int cellH = (rc.getHeight() - cellGap * (rows - 1)) / rows;
        for (int i = 0; i < numSlots; ++i)
        {
            const int r = i / cols, c = i % cols;
            const juce::Rectangle<int> cell (rc.getX() + c * (cellW + cellGap),
                                             rc.getY() + r * (cellH + cellGap),
                                             cellW, cellH);
            slotButtons[i].setBounds (cell);
            slotLeds[i].setBounds (cell.getRight() - 12, cell.getY() + 4, 8, 8);
        }

        auditionButton.setBounds (btnRow.removeFromLeft (30));
        btnRow.removeFromLeft (6);
        const int bw = (btnRow.getWidth() - 2 * 6) / 3;
        loadButton.setBounds (btnRow.removeFromLeft (bw)); btnRow.removeFromLeft (6);
        autoButton.setBounds (btnRow.removeFromLeft (bw)); btnRow.removeFromLeft (6);
        clearButton.setBounds (btnRow);
    }

    // ---- Rattle graph ----
    area.removeFromTop (8);
    rattleGraph.setBounds (area.removeFromTop (110));

    auto makeRow = [&] (juce::Label& lbl, juce::Component& ctrl)
    {
        auto row = area.removeFromTop (rowH);
        lbl.setBounds (row.removeFromLeft (labelW));
        ctrl.setBounds (row);
        area.removeFromTop (rowGap);
    };
    auto makeRowPair = [&] (juce::Label& l1, juce::Component& c1, juce::Label& l2, juce::Component& c2)
    {
        auto row = area.removeFromTop (rowH);
        const int colW = (row.getWidth() - colGap) / 2;
        auto col1 = row.removeFromLeft (colW);
        l1.setBounds (col1.removeFromLeft (labelWC)); c1.setBounds (col1);
        row.removeFromLeft (colGap);
        l2.setBounds (row.removeFromLeft (labelWC)); c2.setBounds (row);
        area.removeFromTop (rowGap);
    };

    auto spacingRow = [&] ()
    {
        auto row = area.removeFromTop (rowH);
        const int colW = (row.getWidth() - colGap) / 2;
        auto col1 = row.removeFromLeft (colW);
        paceLabel.setBounds   (col1.removeFromLeft (labelWC));
        paceSlider.setBounds  (col1);
        divisionBox.setBounds (col1);          // same cell; visibility picks one
        row.removeFromLeft (colGap);
        paceCurveLabel.setBounds  (row.removeFromLeft (labelWC));
        paceCurveSlider.setBounds (row);
        area.removeFromTop (rowGap);
    };

    area.removeFromTop (secGap); sep1Y = area.getY() - 6;
    makeRowPair (rattleLabel,    rattleSlider,   decayLabel,     decaySlider);
    makeRowPair (tempoSyncLabel, tempoSyncGroup, gridLabel,      syncGridGroup);
    spacingRow();
    makeRowPair (playOrderLabel, playOrderGroup, panSpreadLabel, panSpreadSlider);
    makeRow     (sampleIterLabel, sampleIterGroup);
    makeRow     (panIterLabel,    panIterGroup);
    makeRow     (loopModeLabel, loopModeGroup);

    area.removeFromTop (secGap); sep2Y = area.getY() - 6;
    makeRowPair (pitchLabel, pitchSlider, pitchCurveAmtLabel, pitchCurveAmtSlider);
    makeRow     (pitchCurveShapeLabel, pitchCurveShapeGroup);

    area.removeFromTop (secGap); sep3Y = area.getY() - 6;
    makeRowPair (outputLevelLabel, outputLevelSlider, mixLabel, mixSlider);

    sep4Y = 0;
}

void RattleAudioProcessorEditor::resizedInst (juce::Rectangle<int> area)
{
    const int padX = 24, labelW = 114, labelWC = 84, colGap = 10, rowH = 28, rowGap = 7, secGap = 16;

    area = area.withTrimmedLeft (padX).withTrimmedRight (padX);
    area.removeFromTop (86);

    // Slot strip: 8 buttons + [Auto][Clear].
    {
        auto strip = area.removeFromTop (rowH);
        clearButton.setBounds (strip.removeFromRight (54));
        strip.removeFromRight (5);
        autoButton.setBounds  (strip.removeFromRight (54));
        strip.removeFromRight (8);
        const int slotBtnW = (strip.getWidth() - 7 * 3) / numSlots;
        for (int i = 0; i < numSlots; ++i)
        {
            auto cell = strip.removeFromLeft (slotBtnW);
            slotButtons[i].setBounds (cell);
            slotLeds[i].setBounds (cell.getRight() - 11, cell.getY() + 3, 7, 7);
            if (i < numSlots - 1) strip.removeFromLeft (3);
        }
    }
    area.removeFromTop (5);

    // Waveform + gain knob.
    auto wf = area.removeFromTop (110);
    waveformView.setBounds (wf);
    sampleGainSlider.setBounds (wf.getRight() - 56, wf.getBottom() - 62, 50, 58);
    area.removeFromTop (6);

    // Load + audition row.
    {
        auto lr = area.removeFromTop (rowH);
        loadButton.setBounds (lr.removeFromRight (80));
        lr.removeFromRight (6);
        auditionButton.setBounds (lr.removeFromRight (30));
    }

    area.removeFromTop (8);
    rattleGraph.setBounds (area.removeFromTop (110));

    auto makeRow = [&] (juce::Label& lbl, juce::Component& ctrl)
    {
        auto row = area.removeFromTop (rowH);
        lbl.setBounds (row.removeFromLeft (labelW));
        ctrl.setBounds (row);
        area.removeFromTop (rowGap);
    };
    auto makeRowPair = [&] (juce::Label& l1, juce::Component& c1, juce::Label& l2, juce::Component& c2)
    {
        auto row = area.removeFromTop (rowH);
        const int colW = (row.getWidth() - colGap) / 2;
        auto col1 = row.removeFromLeft (colW);
        l1.setBounds (col1.removeFromLeft (labelWC)); c1.setBounds (col1);
        row.removeFromLeft (colGap);
        l2.setBounds (row.removeFromLeft (labelWC)); c2.setBounds (row);
        area.removeFromTop (rowGap);
    };

    auto spacingRow = [&] ()
    {
        auto row = area.removeFromTop (rowH);
        const int colW = (row.getWidth() - colGap) / 2;
        auto col1 = row.removeFromLeft (colW);
        paceLabel.setBounds   (col1.removeFromLeft (labelWC));
        paceSlider.setBounds  (col1);
        divisionBox.setBounds (col1);
        row.removeFromLeft (colGap);
        paceCurveLabel.setBounds  (row.removeFromLeft (labelWC));
        paceCurveSlider.setBounds (row);
        area.removeFromTop (rowGap);
    };

    area.removeFromTop (secGap); sep1Y = area.getY() - 6;
    makeRowPair (rattleLabel,    rattleSlider,   decayLabel,     decaySlider);
    makeRowPair (tempoSyncLabel, tempoSyncGroup, gridLabel,      syncGridGroup);
    spacingRow();
    makeRowPair (playOrderLabel, playOrderGroup, panSpreadLabel, panSpreadSlider);
    makeRow     (sampleIterLabel, sampleIterGroup);
    makeRow     (panIterLabel,    panIterGroup);
    makeRow     (loopModeLabel, loopModeGroup);

    area.removeFromTop (secGap); sep2Y = area.getY() - 6;
    makeRowPair (pitchLabel, pitchSlider, pitchCurveAmtLabel, pitchCurveAmtSlider);
    makeRow     (pitchCurveShapeLabel, pitchCurveShapeGroup);

    area.removeFromTop (secGap); sep3Y = area.getY() - 6;
    makeRow (outputLevelLabel, outputLevelSlider);

    area.removeFromTop (secGap); sep4Y = area.getY() - 6;
    makeRowPair (midiChannelLabel, midiChannelBox, centerNoteLabel, centerNoteSlider);
    makeRowPair (spreadLabel,      spreadSlider,   velocityLabel,   velocitySlider);
}
