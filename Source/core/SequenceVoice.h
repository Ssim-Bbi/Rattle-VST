#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <memory>
#include <atomic>
#include <cstdint>

//==============================================================================
// One rattle sequence: schedules RATTLE repetitions at PACE intervals,
// applies DECAY gain and PITCH CURVE across the sequence.
// All parameters are snapshotted at trigger time; the running sequence
// is not affected by subsequent parameter changes.
//==============================================================================
class SequenceVoice
{
public:
    // One impact's playback unit: a slice [start, end) of a specific buffer,
    // carrying that sample's native rate and per-sample gain. Filled per impact,
    // so a single sequence can walk across different samples/slices.
    struct RepSlice
    {
        // shared_ptr so a running sequence keeps its sample alive even if the UI
        // clears or replaces the slot mid-playback (freed when the last voice drops it).
        std::shared_ptr<const juce::AudioBuffer<float>> buffer;
        double fileSampleRate { 44100.0 };
        int    start { 0 };
        int    end   { 0 };    // exclusive; ≤ start → treat as whole buffer
        float  gain  { 1.0f }; // linear per-sample gain
        int    slot  { -1 };   // source slot index, for the UI trigger indicator
    };

    struct Params
    {
        // --- Dynamics & sequencing ---
        float  velocity        { 1.0f };
        int    rattle          { 4    };   // total repetitions / impacts
        float  decay           { 0.5f };   // 0 = flat, 1 = fast fade
        float  pitchSt         { 0.0f };   // base pitch offset (semitones)
        float  pitchCurveAmt   { 0.0f };   // total pitch change over sequence
        int    pitchCurveShape { 0    };   // 0 = linear, 1 = exp
        double hostSampleRate  { 44100.0 };

        // --- Filled by buildParams / triggerNow: one entry per impact ---
        std::array<RepSlice, 32> repSlices   {}; // [0, repSliceCount) valid
        std::array<float, 32>    repPan      {}; // pan per impact, −1…+1
        std::array<int, 32>      repInterval {}; // samples from impact i to i+1
        int repSliceCount { 0 };

        // --- Consumed by RattleEngine::triggerNow (inputs) ---
        int   playOrder  { 0    }; // 0 = Seq, 1 = Rnd (no immediate repeat)
        int   sampleIter { 1    }; // 0 = Trigger (per sequence), 1 = Impact (per event)
        int   panIter    { 0    }; // 0 = Trigger, 1 = Impact
        float panSpread  { 0.0f };
        uint8_t mutedMask { 0   }; // bit i set = slot i muted → skipped in playback
        int   loopMode   { 0    }; // 0=Off (continuous fwd), 1=Loop FW, 2=Ping-Pong, 3=Loop BW
    };

    SequenceVoice() = default;

    void  start    (const Params& p, uint64_t age);
    void  stop     ();
    bool  isActive () const  { return active;    }
    uint64_t getAge() const  { return voiceAge;  }

    // Per-slot hit counters (owned by the engine) bumped each time this voice
    // fires an impact, so the UI can light the slot being triggered.
    void  setHitSink (std::atomic<uint32_t>* sink) noexcept { hitSink = sink; }

    // wetBus: the buffer to mix into. Per-impact buffers live in snap.repSlices.
    void processBlock (juce::AudioBuffer<float>& out, int numSamples);

private:
    float computeDecayGain   (int repIdx) const;
    float computePitchOffset (int repIdx) const;

    Params   snap;
    uint64_t voiceAge         { 0     };

    bool     active           { false };
    int      repIndex         { 0     };
    int      samplesUntilNext { 0     };
    bool     hitActive        { false };
    double   readPos          { 0.0   };
    double   playbackRate     { 1.0   };
    float    currentGain      { 1.0f  };
    float    panL             { 1.0f  };
    float    panR             { 1.0f  };

    // Resolved per impact at fire time.
    const juce::AudioBuffer<float>* curBuffer { nullptr };
    int      curStart         { 0     };
    int      curEnd           { 0     };

    std::atomic<uint32_t>* hitSink { nullptr }; // engine-owned per-slot hit counters

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SequenceVoice)
};
