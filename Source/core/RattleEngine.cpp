#include "RattleEngine.h"

RattleEngine::RattleEngine()
    : rng (std::random_device{}())
{
    formatManager.registerBasicFormats();
    for (auto& v : voices)
        v.setHitSink (slotHits);
    for (auto& v : auditionVoices)
        v.setHitSink (slotHits);
}

void RattleEngine::prepare (double /*sampleRate*/, int /*maxBlockSize*/)
{
    for (auto& v : voices)         v.stop();
    for (auto& v : auditionVoices) v.stop();
}

void RattleEngine::releaseResources()
{
    for (auto& v : voices)         v.stop();
    for (auto& v : auditionVoices) v.stop();
}

bool RattleEngine::loadFile (int slot, const juce::File& file)
{
    return sampleSet.loadFile (slot, file, formatManager);
}

//==============================================================================
int RattleEngine::pickSlot (int playOrder, const int* loaded, int numLoaded)
{
    if (numLoaded <= 1) return numLoaded == 1 ? loaded[0] : -1;

    if (playOrder == 1) // Rnd — random, avoid immediate repeat
    {
        int idx = (int)(rng() % (unsigned) numLoaded);
        if (loaded[idx] == lastSlotUsed)
            idx = (idx + 1) % numLoaded;
        lastSlotUsed = loaded[idx];
        return loaded[idx];
    }

    // Seq — sequential cycle
    const int slot = loaded[rrCursor % numLoaded];
    rrCursor = (rrCursor + 1) % numLoaded;
    lastSlotUsed = slot;
    return slot;
}

void RattleEngine::buildPool (SequenceVoice::RepSlice* pool, int& poolSize,
                              const int* loaded, int numLoaded)
{
    poolSize = 0;

    for (int li = 0; li < numLoaded; ++li)
    {
        const int  slot = loaded[li];
        auto       buf  = sampleSet.getBufferShared (slot);
        if (buf == nullptr || buf->getNumSamples() == 0)
            continue;

        const double rate   = sampleSet.getFileSampleRate (slot);
        const float  gain   = juce::Decibels::decibelsToGain (sampleSet.getGainDb (slot));
        const int    nSmp   = buf->getNumSamples();
        const auto&  slices = sampleSet.getSlices (slot);

        if (slices.empty())
        {
            if (poolSize < kMaxPool)
                pool[poolSize++] = { buf, rate, 0, nSmp, gain, slot };
        }
        else
        {
            for (const auto& sl : slices)
            {
                if (sl.garbage) continue;            // skip slices marked "garbage"
                if (poolSize >= kMaxPool) return;
                pool[poolSize++] = { buf, rate, sl.start, sl.end, gain, slot };
            }
        }
    }
}

int RattleEngine::pickPoolIndex (int playOrder, int poolSize)
{
    if (poolSize <= 1) return 0;

    if (playOrder == 1) // Rnd — random, avoid immediate repeat
    {
        int idx = (int)(rng() % (unsigned) poolSize);
        if (idx == lastPoolIdx)
            idx = (idx + 1) % poolSize;
        lastPoolIdx = idx;
        return idx;
    }

    // Seq — sequential cycle
    const int idx = poolCursor % poolSize;
    poolCursor = (poolCursor + 1) % poolSize;
    lastPoolIdx = idx;
    return idx;
}

float RattleEngine::nextPanPosition (float panSpread)
{
    if (panSpread < 0.001f) return 0.0f;

    // Alternate left/right with increasing spread positions:
    // cursor 0 → +spread, 1 → −spread, 2 → +spread*0.5, 3 → −spread*0.5, etc.
    const float sign   = (panCursor % 2 == 0) ? 1.0f : -1.0f;
    const float scale  = 1.0f / (float)(panCursor / 2 + 1);
    panCursor          = (panCursor + 1) % 8; // wrap at 8 to keep spread fresh
    return sign * scale * panSpread;
}

//==============================================================================
bool RattleEngine::resolveUnits (SequenceVoice::Params& params, int rattle)
{
    // Serialise against UI-thread slot mutation. If the UI is mid clear/load,
    // skip this trigger rather than read half-mutated state. Held only for this
    // brief snapshot; the voice then plays from its own shared_ptr handles.
    const juce::SpinLock::ScopedTryLockType lock (sampleSet.getLock());
    if (! lock.isLocked())
        return false;

    // Collect playable slot indices: loaded, not muted, and with at least one
    // playable unit (unsliced = 1; sliced = number of non-garbage slices).
    int loaded[SampleSet::maxSlots];
    int numLoaded = 0;
    for (int i = 0; i < SampleSet::maxSlots; ++i)
    {
        if (! sampleSet.hasSlot (i))            continue;
        if (params.mutedMask & (1u << i))       continue; // whole slot muted (automatable)

        const auto& slices = sampleSet.getSlices (i);
        int playable = (int) slices.size();
        if (slices.empty())
            playable = 1;                                  // unsliced → one whole-buffer unit
        else
            for (const auto& sl : slices) if (sl.garbage) --playable;

        if (playable > 0)
            loaded[numLoaded++] = i;
    }

    if (numLoaded == 0) return false;

    // ---- Pan position(s) for the burst ----
    if (params.panIter == 1) // Impact — fresh pan per impact
    {
        for (int r = 0; r < rattle; ++r)
            params.repPan[r] = nextPanPosition (params.panSpread);
    }
    else // Trigger — one pan for the whole burst
    {
        const float pan = nextPanPosition (params.panSpread);
        for (int r = 0; r < rattle; ++r)
            params.repPan[r] = pan;
    }

    // ---- Per-impact playback units ----
    if (params.sampleIter == 1) // Impact — walk the unified pool per impact
    {
        SequenceVoice::RepSlice pool[kMaxPool];
        int poolSize = 0;
        buildPool (pool, poolSize, loaded, numLoaded);
        if (poolSize == 0) return false;

        for (int r = 0; r < rattle; ++r)
            params.repSlices[r] = pool[pickPoolIndex (params.playOrder, poolSize)];
    }
    else // Trigger — one slot for the whole burst, walk its slices in order
    {
        const int slot = pickSlot (params.playOrder, loaded, numLoaded);
        auto buf = sampleSet.getBufferShared (slot);
        if (buf == nullptr || buf->getNumSamples() == 0) return false;

        const double rate   = sampleSet.getFileSampleRate (slot);
        const float  gain   = juce::Decibels::decibelsToGain (sampleSet.getGainDb (slot));
        const int    nSmp   = buf->getNumSamples();
        const auto&  slices = sampleSet.getSlices (slot);

        // Collect this slot's non-garbage slice indices (unsliced → whole buffer).
        int playIdx[kMaxPool];
        int nPlay = 0;
        for (int s = 0; s < (int) slices.size() && nPlay < kMaxPool; ++s)
            if (! slices[(size_t) s].garbage)
                playIdx[nPlay++] = s;

        for (int r = 0; r < rattle; ++r)
        {
            SequenceVoice::RepSlice u { buf, rate, 0, nSmp, gain, slot };
            if (nPlay > 0)
            {
                const auto& sl = slices[(size_t) playIdx[r % nPlay]];
                u.start = sl.start;
                u.end   = sl.end;
            }
            params.repSlices[r] = u;
        }
    }

    params.repSliceCount = rattle;
    return true;
}

void RattleEngine::triggerNow (SequenceVoice::Params params)
{
    const int rattle = juce::jlimit (1, 32, params.rattle);
    params.rattle = rattle;

    if (! resolveUnits (params, rattle))
        return; // no sample loaded

    if (auto* target = allocVoice())
        target->start (params, ++voiceCounter);
}

SequenceVoice* RattleEngine::allocFrom (SequenceVoice* pool, int count)
{
    SequenceVoice* target = nullptr;
    for (int i = 0; i < count; ++i)
        if (! pool[i].isActive()) { target = &pool[i]; break; }

    if (target == nullptr)
        for (int i = 0; i < count; ++i)
            if (target == nullptr || pool[i].getAge() < target->getAge())
                target = &pool[i];

    return target;
}

void RattleEngine::processAudition (juce::AudioBuffer<float>& out, int numSamples)
{
    for (auto& v : auditionVoices)
        v.processBlock (out, numSamples);
}

void RattleEngine::auditionSlot (int slot, double hostSampleRate)
{
    const juce::SpinLock::ScopedTryLockType lock (sampleSet.getLock());
    if (! lock.isLocked())
        return;

    auto buf = sampleSet.getBufferShared (slot);
    if (buf == nullptr || buf->getNumSamples() == 0)
        return;

    // A single full-sample hit at the slot's own gain, centred.
    SequenceVoice::Params p;
    p.velocity       = 1.0f;
    p.rattle         = 1;
    p.decay          = 0.0f;
    p.hostSampleRate = hostSampleRate;
    p.repSlices[0]   = { buf, sampleSet.getFileSampleRate (slot), 0, buf->getNumSamples(),
                         juce::Decibels::decibelsToGain (sampleSet.getGainDb (slot)), slot };
    p.repPan[0]      = 0.0f;
    p.repSliceCount  = 1;

    if (auto* target = allocFrom (auditionVoices, maxAudition))
        target->start (p, ++voiceCounter);
}

int RattleEngine::activeVoiceCount() const
{
    int n = 0;
    for (const auto& v : voices)
        if (v.isActive()) ++n;
    return n;
}

//==============================================================================
void RattleEngine::processBlock (juce::AudioBuffer<float>& wetBus, int numSamples)
{
    for (auto& v : voices)
        v.processBlock (wetBus, numSamples);
}
