#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
RattleAudioProcessor::RattleAudioProcessor()
    : AudioProcessor (BusesProperties()
                     #if ! RATTLE_IS_INSTRUMENT
                      .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                     #endif
                      .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "RATTLE_STATE", RattleParams::createParameterLayout())
{
    // Section 1 — Input
    inputGainParam   = apvts.getRawParameterValue (RattleParams::ID::inputGain);
    velocityParam    = apvts.getRawParameterValue (RattleParams::ID::velocity);
    spreadParam      = apvts.getRawParameterValue (RattleParams::ID::spread);
    midiChannelParam = apvts.getRawParameterValue (RattleParams::ID::midiChannel);
    centerNoteParam  = apvts.getRawParameterValue (RattleParams::ID::centerNote);
    filterTypeParam  = apvts.getRawParameterValue (RattleParams::ID::filterType);
    filterFreqParam  = apvts.getRawParameterValue (RattleParams::ID::filterFreq);
    thresholdParam   = apvts.getRawParameterValue (RattleParams::ID::threshold);
    sensitivityParam = apvts.getRawParameterValue (RattleParams::ID::sensitivity);

    // Section 2 — Rattle
    rattleParam          = apvts.getRawParameterValue (RattleParams::ID::rattle);
    decayParam           = apvts.getRawParameterValue (RattleParams::ID::decay);
    paceParam            = apvts.getRawParameterValue (RattleParams::ID::pace);
    paceCurveParam       = apvts.getRawParameterValue (RattleParams::ID::paceCurve);
    tempoSyncParam       = apvts.getRawParameterValue (RattleParams::ID::tempoSync);
    syncDivisionParam    = apvts.getRawParameterValue (RattleParams::ID::syncDivision);
    syncGridParam        = apvts.getRawParameterValue (RattleParams::ID::syncGrid);
    playOrderParam       = apvts.getRawParameterValue (RattleParams::ID::playOrder);
    sampleIterParam      = apvts.getRawParameterValue (RattleParams::ID::sampleIter);
    panIterParam         = apvts.getRawParameterValue (RattleParams::ID::panIter);
    panSpreadParam       = apvts.getRawParameterValue (RattleParams::ID::panSpread);
    pitchParam           = apvts.getRawParameterValue (RattleParams::ID::pitch);
    pitchCurveAmtParam   = apvts.getRawParameterValue (RattleParams::ID::pitchCurveAmt);
    pitchCurveShapeParam = apvts.getRawParameterValue (RattleParams::ID::pitchCurveShape);

    // Section 3 — Output
    mixParam         = apvts.getRawParameterValue (RattleParams::ID::mix);
    outputLevelParam = apvts.getRawParameterValue (RattleParams::ID::outputLevel);

    for (int i = 0; i < SampleSet::maxSlots; ++i)
        slotMuteParams[i] = apvts.getRawParameterValue (RattleParams::ID::slotMute (i));
}

//==============================================================================
SequenceVoice::Params RattleAudioProcessor::buildParams (float rawVelocity) const
{
    SequenceVoice::Params p;

    // VELOCITY param (0 = flat / no dynamics, 1 = full dynamics tracking).
    const float velMod = velocityParam->load();
    p.velocity = 1.0f - velMod + velMod * rawVelocity; // lerp(1, rawVelocity, velMod)

    const int rattle  = juce::jmax (1, (int) rattleParam->load());
    p.rattle          = rattle;
    p.decay           = decayParam->load();

    // ---- Impact spacing → per-impact intervals (samples), Free or Sync ----
    {
        const double sr    = currentSampleRate;
        const double drift = (double) paceCurveParam->load();   // −1..+1
        const bool   sync  = tempoSyncParam != nullptr && (int) tempoSyncParam->load() == 1;

        double baseInterval, grid = 0.0, minGap;
        if (sync)
        {
            const double quarter = 60.0 / juce::jmax (20.0, currentBpm) * sr;
            baseInterval = RattleParams::syncDivisionQuarters ((int) syncDivisionParam->load()) * quarter;
            grid         = RattleParams::syncGridQuarters     ((int) syncGridParam->load())     * quarter;
            minGap       = 0.0;                                   // grid snap enforces the minimum
        }
        else
        {
            baseInterval = (double) paceParam->load() / 1000.0 * sr;
            minGap       = sr * 0.005;                            // 5 ms floor (Free)
        }

        double offsets[32];
        RattleParams::computeImpactOffsets (rattle, drift, baseInterval, sync, grid, minGap, offsets);
        for (int i = 0; i < rattle - 1 && i < 31; ++i)
            p.repInterval[i] = (int) juce::jmax (1.0, offsets[i + 1] - offsets[i]);
    }

    p.playOrder       = playOrderParam  ? (int) playOrderParam->load()  : 0;
    p.sampleIter      = sampleIterParam ? (int) sampleIterParam->load() : 1;
    p.panIter         = panIterParam    ? (int) panIterParam->load()    : 0;
    p.panSpread       = panSpreadParam  ? panSpreadParam->load()        : 0.0f;

    uint8_t mutedMask = 0;
    for (int i = 0; i < SampleSet::maxSlots; ++i)
        if (slotMuteParams[i] != nullptr && slotMuteParams[i]->load() > 0.5f)
            mutedMask |= (uint8_t) (1u << i);
    p.mutedMask       = mutedMask;

    p.pitchSt         = pitchParam->load();
    p.pitchCurveAmt   = pitchCurveAmtParam->load();
    p.pitchCurveShape = (int) pitchCurveShapeParam->load();
    p.hostSampleRate  = currentSampleRate;
    // repSlices / repPan (per-impact buffers, slices, gains, pan) are filled by
    // RattleEngine::triggerNow from the round-robin / iteration state.
    return p;
}

//==============================================================================
void RattleAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    engine.prepare (sampleRate, samplesPerBlock);
    dryBuffer.setSize      (2, samplesPerBlock, false, true, true);
    filteredMono.setSize   (1, samplesPerBlock, false, true, true);
    auditionBuffer.setSize (2, samplesPerBlock, false, true, true);
    preFilter.prepare (sampleRate);
    onsetDetector.prepare (sampleRate);
}

void RattleAudioProcessor::releaseResources()
{
    engine.releaseResources();
    preFilter.reset();
    onsetDetector.reset();
}

bool RattleAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

   #if ! RATTLE_IS_INSTRUMENT
    if (layouts.getMainInputChannelSet() != layouts.getMainOutputChannelSet())
        return false;
   #endif

    return true;
}

//==============================================================================
void RattleAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                         juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    const int   numSamples = buffer.getNumSamples();
    const float outGain    = juce::Decibels::decibelsToGain (outputLevelParam->load());

    // Track host tempo for Sync spacing (falls back to last known if unavailable).
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto bpm = pos->getBpm())
                currentBpm = *bpm;

    // UI trigger (message thread → audio thread via atomic flag).
    if (pendingTrigger.exchange (false, std::memory_order_acquire))
        engine.triggerNow (buildParams (1.0f));

    // UI audition: preview a single slot's sample (message thread → audio thread).
    const int auditionReq = pendingAudition.exchange (-1, std::memory_order_acquire);
    if (auditionReq >= 0)
        engine.auditionSlot (auditionReq, currentSampleRate);

   // ==========================================================================
   #if RATTLE_IS_INSTRUMENT
   // ==========================================================================

    buffer.clear();

    // Read input-section params for MIDI filtering.
    const int   midiCh   = (int) midiChannelParam->load(); // 0=Omni, 1–16
    const int   centerN  = (int) centerNoteParam->load();  // 0–127
    const float spread   = spreadParam->load();            // 0–1

    // SPREAD maps 0=exact center note only, 1=full register (±64 semitones).
    const int   windowSt = (spread < 0.01f) ? 0 : (int) (spread * 64.0f);

    for (const auto& meta : midiMessages)
    {
        const auto msg = meta.getMessage();
        if (! msg.isNoteOn()) continue;

        // Channel filter
        if (midiCh != 0 && msg.getChannel() != midiCh)
            continue;

        // Note-window filter
        if (std::abs (msg.getNoteNumber() - centerN) > windowSt)
            continue;

        engine.triggerNow (buildParams (msg.getFloatVelocity()));
    }

    engine.processBlock (buffer, numSamples);
    buffer.applyGain (outGain);

   // ==========================================================================
   #else // FX
   // ==========================================================================

    juce::ignoreUnused (midiMessages);

    const float mix      = mixParam->load();
    const int   numOutCh = buffer.getNumChannels();

    // 1. Save dry signal before any processing.
    for (int ch = 0; ch < numOutCh && ch < dryBuffer.getNumChannels(); ++ch)
        dryBuffer.copyFrom (ch, 0, buffer, ch, 0, numSamples);

    // 2. Update pre-filter: SPREAD → Q (0=narrow/high-Q, 1=wide/low-Q).
    const float spreadVal = spreadParam->load();
    const float filterQ   = 10.0f - spreadVal * 9.5f; // 10.0 → 0.5
    preFilter.setParameters ((int) filterTypeParam->load(),
                              filterFreqParam->load(),
                              filterQ);

    const float thr       = thresholdParam->load();
    const float sens      = sensitivityParam->load();
    const float boost     = juce::Decibels::decibelsToGain (inputGainParam->load());
    const bool  listening = filterListenMode.load (std::memory_order_relaxed);
    float* filteredPtr    = filteredMono.getWritePointer (0);

    // Feed FFT ring buffer (raw input, pre-filter/pre-boost) for spectrum display —
    // only while the spectrum view is active (Sample view / closed editor skip it).
    const bool fftOn = fftActive.load (std::memory_order_relaxed);
    int wp = fftWritePos.load (std::memory_order_relaxed);

    // 3. Per-sample: FFT fifo ← raw input; filter + boost + onset detect; store for listen mode.
    for (int i = 0; i < numSamples; ++i)
    {
        float mono = 0.0f;
        for (int ch = 0; ch < numOutCh; ++ch)
            mono += buffer.getReadPointer (ch)[i];
        mono /= (float) numOutCh;

        if (fftOn)
        {
            fftInputFifo[wp] = mono;
            wp               = (wp + 1) & (fftSize - 1);
        }

        const float filtered   = preFilter.processSample (mono) * boost;
        filteredPtr[i]         = filtered;

        const float triggerVel = onsetDetector.processSample (filtered, thr, sens);
        if (triggerVel > 0.0f && ! listening)
            engine.triggerNow (buildParams (triggerVel));
    }

    if (fftOn)
        fftWritePos.store (wp, std::memory_order_release);

    buffer.clear();

    if (listening)
    {
        // 4a. Listen mode: output the pre-filtered mono signal for diagnostic monitoring.
        for (int ch = 0; ch < numOutCh; ++ch)
            buffer.copyFrom (ch, 0, filteredMono, 0, 0, numSamples);
        buffer.applyGain (outGain);
    }
    else
    {
        // 4b. Generate wet rattle.
        engine.processBlock (buffer, numSamples);

        // 5. Three-phase blend:
        //   0–0.5 → dry=1, wet rises 0→1   (add wet without touching dry level)
        //   0.5–1 → wet=1, dry falls 1→0   (remove dry without touching wet level)
        float wetGain, dryGain;
        if (mix <= 0.5f) { wetGain = mix * 2.0f;               dryGain = 1.0f; }
        else             { wetGain = 1.0f; dryGain = 1.0f - (mix - 0.5f) * 2.0f; }

        for (int ch = 0; ch < numOutCh; ++ch)
        {
            buffer.applyGain (ch, 0, numSamples, wetGain * outGain);
            const int dryCh = juce::jmin (ch, dryBuffer.getNumChannels() - 1);
            buffer.addFrom (ch, 0, dryBuffer, dryCh, 0, numSamples, dryGain * outGain);
        }
    }

   #endif

    // ---- Audition (post-mix): preview voices, always audible at output level ----
    auditionBuffer.clear();
    engine.processAudition (auditionBuffer, numSamples);
    {
        const int nOut   = buffer.getNumChannels();
        const int nAudCh = auditionBuffer.getNumChannels();
        for (int ch = 0; ch < nOut; ++ch)
            buffer.addFrom (ch, 0, auditionBuffer, juce::jmin (ch, nAudCh - 1), 0, numSamples, outGain);
    }
}

//==============================================================================
const juce::String RattleAudioProcessor::getName() const
{
   #if RATTLE_IS_INSTRUMENT
    return "RATTLE Inst";
   #else
    return "RATTLE FX";
   #endif
}

bool RattleAudioProcessor::loadSample (const juce::File& file, int slot)
{
    const bool ok = engine.loadFile (slot, file);
    if (ok) sendChangeMessage();
    return ok;
}

int RattleAudioProcessor::loadSamplesFillingEmpty (const juce::Array<juce::File>& files)
{
    juce::Array<juce::File> sorted (files);
    std::sort (sorted.begin(), sorted.end(),
               [] (const juce::File& a, const juce::File& b)
               { return a.getFileName().compareNatural (b.getFileName()) < 0; });

    auto& ss = engine.getSampleSet();
    int lastFilled = -1;
    int slot = 0;

    for (const auto& file : sorted)
    {
        while (slot < SampleSet::maxSlots && ss.hasSlot (slot))
            ++slot;                       // skip to next empty slot
        if (slot >= SampleSet::maxSlots)
            break;                        // no empty slots left

        if (engine.loadFile (slot, file))
            lastFilled = slot;
        ++slot;
    }

    if (lastFilled >= 0)
        sendChangeMessage();
    return lastFilled;
}

void RattleAudioProcessor::setSampleGainDb (int slot, float db)
{
    // No change broadcast: the editor's knob is the live source, and avoiding it
    // keeps the waveform from recomputing its peaks on every drag tick.
    engine.getSampleSet().setGainDb (slot, db);
}

float RattleAudioProcessor::getSampleGainDb (int slot) const
{
    return engine.getSampleSet().getGainDb (slot);
}

void RattleAudioProcessor::setSlicesForSlot (int slot, const std::vector<int>& markerSamples)
{
    auto&       ss  = engine.getSampleSet();
    const auto* buf = ss.getBuffer (slot);
    if (buf == nullptr) return;

    const int totalSmp = buf->getNumSamples();

    // Preserve "garbage" flags across marker edits: a new slice inherits the flag
    // of an existing slice that starts at the same sample (so dragging/adding a
    // marker doesn't wipe the slices you already marked).
    const auto oldSlices = ss.getSlices (slot); // copy before we replace them

    std::vector<SampleSet::Slice> slices;
    slices.reserve (markerSamples.size());

    for (int k = 0; k < (int) markerSamples.size(); ++k)
    {
        const int start = markerSamples[k];
        const int end   = (k + 1 < (int) markerSamples.size()) ? markerSamples[k + 1] : totalSmp;

        bool garbage = false;
        for (const auto& os : oldSlices)
            if (os.start == start) { garbage = os.garbage; break; }

        slices.push_back ({ start, end, garbage });
    }

    ss.setSlices (slot, std::move (slices));
    sendChangeMessage();
}

void RattleAudioProcessor::toggleSliceGarbage (int slot, int sliceIndex)
{
    engine.getSampleSet().toggleSliceGarbage (slot, sliceIndex);
    sendChangeMessage();
}

void RattleAudioProcessor::autoDetectSlices (int slot)
{
    engine.getSampleSet().autoDetectSlices (slot);
    sendChangeMessage();
}

void RattleAudioProcessor::clearSlot (int slot)
{
    engine.getSampleSet().clear (slot);
    sendChangeMessage();
}

//==============================================================================
void RattleAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree state = apvts.copyState();
    state.appendChild (engine.getSampleSet().toValueTree(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void RattleAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        if (xml->hasTagName (apvts.state.getType()))
        {
            juce::ValueTree state = juce::ValueTree::fromXml (*xml);
            apvts.replaceState (state);

            const auto samplesTree = state.getChildWithName ("Samples");
            if (samplesTree.isValid())
                engine.getSampleSet().fromValueTree (samplesTree);

            sendChangeMessage();
        }
    }
}

//==============================================================================
juce::AudioProcessorEditor* RattleAudioProcessor::createEditor()
{
    return new RattleAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new RattleAudioProcessor();
}
