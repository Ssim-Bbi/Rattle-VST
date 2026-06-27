#include "SampleSet.h"

bool SampleSet::loadFile (int slot, const juce::File& file, juce::AudioFormatManager& fmt)
{
    if (slot < 0 || slot >= maxSlots)
        return false;

    std::unique_ptr<juce::AudioFormatReader> reader (fmt.createReaderFor (file));
    if (reader == nullptr)
        return false;

    const int numCh  = (int) reader->numChannels;
    const int numSmp = (int) reader->lengthInSamples;

    // Decode into a fresh buffer OUTSIDE the lock (this is the slow part).
    auto newBuf = std::make_shared<juce::AudioBuffer<float>> (numCh, numSmp);
    reader->read (newBuf.get(), 0, numSmp, 0, true, true);

    // Swap it in under the lock. Any voice still playing the previous buffer
    // keeps it alive via its own shared_ptr, so this never frees data in use.
    const juce::SpinLock::ScopedLockType sl (dataLock);
    slots[slot].buffer         = std::move (newBuf);
    slots[slot].fileSampleRate = reader->sampleRate;
    slots[slot].loaded         = true;
    slots[slot].slices.clear(); // clear old markers when new file is loaded
    slots[slot].fileName       = file.getFileName();
    slots[slot].gainDb         = 0.0f; // reset trim for a freshly loaded sample
    return true;
}

void SampleSet::clear (int slot)
{
    if (slot < 0 || slot >= maxSlots)
        return;
    const juce::SpinLock::ScopedLockType sl (dataLock);
    clearSlotLocked (slot);
}

void SampleSet::clearAll()
{
    const juce::SpinLock::ScopedLockType sl (dataLock);
    for (int i = 0; i < maxSlots; ++i)
        clearSlotLocked (i);
}

void SampleSet::clearSlotLocked (int slot)
{
    slots[slot].buffer.reset(); // drop our ref; a playing voice keeps its own alive
    slots[slot].loaded   = false;
    slots[slot].slices.clear();
    slots[slot].fileName = {};
    slots[slot].gainDb   = 0.0f;
}

const juce::AudioBuffer<float>* SampleSet::getBuffer (int slot) const
{
    if (slot >= 0 && slot < maxSlots && slots[slot].loaded)
        return slots[slot].buffer.get();
    return nullptr;
}

std::shared_ptr<const juce::AudioBuffer<float>> SampleSet::getBufferShared (int slot) const
{
    if (slot >= 0 && slot < maxSlots && slots[slot].loaded)
        return slots[slot].buffer;
    return nullptr;
}

double SampleSet::getFileSampleRate (int slot) const
{
    return (slot >= 0 && slot < maxSlots) ? slots[slot].fileSampleRate : 44100.0;
}

bool SampleSet::hasSlot (int slot) const
{
    return slot >= 0 && slot < maxSlots && slots[slot].loaded;
}

const juce::String& SampleSet::getFileName (int slot) const
{
    if (slot >= 0 && slot < maxSlots && slots[slot].loaded)
        return slots[slot].fileName;
    return emptyName;
}

float SampleSet::getGainDb (int slot) const
{
    return (slot >= 0 && slot < maxSlots) ? slots[slot].gainDb : 0.0f;
}

void SampleSet::setGainDb (int slot, float db)
{
    if (slot < 0 || slot >= maxSlots)
        return;
    const juce::SpinLock::ScopedLockType sl (dataLock);
    slots[slot].gainDb = juce::jlimit (-20.0f, 20.0f, db);
}

//==============================================================================
void SampleSet::setSlices (int slot, std::vector<Slice> s)
{
    if (slot < 0 || slot >= maxSlots)
        return;
    const juce::SpinLock::ScopedLockType sl (dataLock);
    slots[slot].slices = std::move (s);
}

const std::vector<SampleSet::Slice>& SampleSet::getSlices (int slot) const
{
    if (slot >= 0 && slot < maxSlots)
        return slots[slot].slices;
    return emptySlices;
}

void SampleSet::clearSlices (int slot)
{
    if (slot < 0 || slot >= maxSlots)
        return;
    const juce::SpinLock::ScopedLockType sl (dataLock);
    slots[slot].slices.clear();
}

void SampleSet::toggleSliceGarbage (int slot, int sliceIndex)
{
    if (slot < 0 || slot >= maxSlots)
        return;
    const juce::SpinLock::ScopedLockType sl (dataLock);
    auto& slices = slots[slot].slices;
    if (sliceIndex >= 0 && sliceIndex < (int) slices.size())
        slices[(size_t) sliceIndex].garbage = ! slices[(size_t) sliceIndex].garbage;
}

int SampleSet::autoDetectSlices (int slot, float threshold)
{
    if (slot < 0 || slot >= maxSlots || ! slots[slot].loaded)
        return 0;

    const auto bufPtr = slots[slot].buffer; // shared copy keeps data alive while scanning
    if (bufPtr == nullptr || bufPtr->getNumSamples() == 0)
        return 0;

    const auto&  buf         = *bufPtr;
    const int    numSamples  = buf.getNumSamples();
    const int    numCh       = buf.getNumChannels();
    const double sr          = slots[slot].fileSampleRate;

    // Simple envelope follower — 1 ms attack, 80 ms release.
    const float atkCoeff = (float) std::exp (-1.0 / (sr * 0.001));
    const float relCoeff = (float) std::exp (-1.0 / (sr * 0.080));

    float envelope   = 0.0f;
    bool  wasAbove   = false;
    int   guardCount = 0;
    const int guardSamples = (int) (sr * 0.040); // 40 ms minimum slice gap

    std::vector<int> onsets;

    for (int i = 0; i < numSamples; ++i)
    {
        float rect = 0.0f;
        for (int ch = 0; ch < numCh; ++ch)
            rect += std::fabs (buf.getReadPointer (ch)[i]);
        rect /= (float) numCh;

        if (rect > envelope)
            envelope = atkCoeff * envelope + (1.0f - atkCoeff) * rect;
        else
            envelope = relCoeff * envelope;

        if (guardCount > 0) { --guardCount; wasAbove = (envelope > threshold); continue; }

        const bool isAbove = (envelope > threshold);
        if (isAbove && ! wasAbove)
        {
            onsets.push_back (i);
            guardCount = guardSamples;
        }
        wasAbove = isAbove;
    }

    // Convert onset positions to Slice ranges (empty list if none found).
    std::vector<Slice> slices;
    slices.reserve (onsets.size());
    for (int k = 0; k < (int) onsets.size(); ++k)
    {
        const int start = onsets[k];
        const int end   = (k + 1 < (int) onsets.size()) ? onsets[k + 1] : numSamples;
        slices.push_back ({ start, end });
    }

    const juce::SpinLock::ScopedLockType sl (dataLock);
    slots[slot].slices = std::move (slices);
    return (int) slots[slot].slices.size();
}

//==============================================================================
juce::ValueTree SampleSet::toValueTree() const
{
    juce::ValueTree v { "Samples" };

    for (int i = 0; i < maxSlots; ++i)
    {
        if (! slots[i].loaded || slots[i].buffer == nullptr)
            continue;

        const auto& buf = *slots[i].buffer;

        juce::MemoryBlock flacData;
        {
            std::unique_ptr<juce::OutputStream> mos = std::make_unique<juce::MemoryOutputStream> (flacData, false);
            juce::FlacAudioFormat flac;

            auto writer = flac.createWriterFor (mos,
                juce::AudioFormatWriterOptions{}
                    .withSampleRate      (slots[i].fileSampleRate)
                    .withNumChannels     (buf.getNumChannels())
                    .withBitsPerSample   (24)
                    .withQualityOptionIndex (5));

            if (writer != nullptr)
                writer->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
        }

        if (flacData.getSize() == 0)
            continue;

        juce::ValueTree slot { "Slot" };
        slot.setProperty ("index",      i,                           nullptr);
        slot.setProperty ("sampleRate", slots[i].fileSampleRate,     nullptr);
        slot.setProperty ("flac",       flacData.toBase64Encoding(), nullptr);
        slot.setProperty ("name",       slots[i].fileName,           nullptr);
        slot.setProperty ("gainDb",     slots[i].gainDb,             nullptr);

        // Serialise slice markers.
        if (! slots[i].slices.empty())
        {
            juce::ValueTree slicesNode { "Slices" };
            for (const auto& sl : slots[i].slices)
            {
                juce::ValueTree slNode { "S" };
                slNode.setProperty ("s", sl.start, nullptr);
                slNode.setProperty ("e", sl.end,   nullptr);
                if (sl.garbage)
                    slNode.setProperty ("g", true, nullptr); // omit when false to keep state clean
                slicesNode.appendChild (slNode, nullptr);
            }
            slot.appendChild (slicesNode, nullptr);
        }

        v.appendChild (slot, nullptr);
    }

    return v;
}

void SampleSet::fromValueTree (const juce::ValueTree& v)
{
    clearAll();

    for (int c = 0; c < v.getNumChildren(); ++c)
    {
        const auto child = v.getChild (c);
        if (! child.hasType ("Slot"))
            continue;

        const int idx = (int) child["index"];
        if (idx < 0 || idx >= maxSlots)
            continue;

        juce::MemoryBlock flacData;
        if (! flacData.fromBase64Encoding (child["flac"].toString()))
            continue;

        auto* mis = new juce::MemoryInputStream (flacData.getData(),
                                                  flacData.getSize(),
                                                  true /*keepCopy*/);
        juce::FlacAudioFormat flac;
        std::unique_ptr<juce::AudioFormatReader> reader (flac.createReaderFor (mis, true));

        if (reader == nullptr)
            continue;

        const int n  = (int) reader->lengthInSamples;
        const int ch = (int) reader->numChannels;

        auto newBuf = std::make_shared<juce::AudioBuffer<float>> (ch, n);
        reader->read (newBuf.get(), 0, n, 0, true, true);

        // Parse slice markers.
        std::vector<Slice> slices;
        const auto slicesNode = child.getChildWithName ("Slices");
        if (slicesNode.isValid())
            for (int s = 0; s < slicesNode.getNumChildren(); ++s)
            {
                const auto slNode = slicesNode.getChild (s);
                slices.push_back ({ (int) slNode["s"], (int) slNode["e"],
                                    (bool) slNode.getProperty ("g", false) });
            }

        const juce::SpinLock::ScopedLockType sl (dataLock);
        slots[idx].buffer         = std::move (newBuf);
        slots[idx].fileSampleRate = reader->sampleRate;
        slots[idx].loaded         = true;
        slots[idx].fileName       = child.getProperty ("name", juce::String());
        slots[idx].gainDb         = (float) child.getProperty ("gainDb", 0.0);
        slots[idx].slices         = std::move (slices);
    }
}
