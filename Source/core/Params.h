#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace RattleParams
{
    // String IDs for every automatable parameter.
    namespace ID
    {
        // Section 1 — Input
        inline constexpr const char* inputGain      = "inputGain";
        inline constexpr const char* midiChannel    = "midiChannel";
        inline constexpr const char* centerNote     = "centerNote";
        inline constexpr const char* filterType     = "filterType";
        inline constexpr const char* filterFreq     = "filterFreq";
        inline constexpr const char* threshold      = "threshold";
        inline constexpr const char* sensitivity    = "sensitivity";
        inline constexpr const char* velocity       = "velocity";
        inline constexpr const char* spread         = "spread";

        // Section 2 — Rattle
        inline constexpr const char* pitch          = "pitch";
        inline constexpr const char* pitchCurveAmt  = "pitchCurveAmt";
        inline constexpr const char* pitchCurveShape = "pitchCurveShape";
        inline constexpr const char* rattle         = "rattle";
        inline constexpr const char* decay          = "decay";
        inline constexpr const char* pace           = "pace";
        inline constexpr const char* paceCurve      = "paceCurve";
        inline constexpr const char* tempoSync      = "tempoSync";
        inline constexpr const char* syncDivision   = "syncDivision";
        inline constexpr const char* syncGrid       = "syncGrid";
        inline constexpr const char* playOrder      = "playOrder";
        inline constexpr const char* sampleIter     = "sampleIter";
        inline constexpr const char* panIter        = "panIter";
        inline constexpr const char* panSpread      = "panSpread";
        inline constexpr const char* loopMode       = "loopMode";

        // Section 3 — Output
        inline constexpr const char* mix            = "mix";
        inline constexpr const char* outputLevel    = "outputLevel";

        // Per-slot mute (8 slots) — automatable bool; mutes the whole slot in playback.
        inline juce::String slotMute (int slot) { return "slotMute" + juce::String (slot); }
    }

    // Sync Division choice index → length in quarter notes (for impact spacing).
    inline double syncDivisionQuarters (int idx) noexcept
    {
        switch (idx)
        {
            case 0: return 1.0;        // 1/4
            case 1: return 1.5;        // 1/4.
            case 2: return 2.0 / 3.0;  // 1/4T
            case 3: return 0.5;        // 1/8
            case 4: return 0.75;       // 1/8.
            case 5: return 1.0 / 3.0;  // 1/8T
            case 6: return 0.25;       // 1/16
            case 7: return 0.375;      // 1/16.
            case 8: return 1.0 / 6.0;  // 1/16T
            case 9: return 0.125;      // 1/32
            default: return 0.25;
        }
    }

    // Sync Grid (granularity) choice index → snap resolution in quarter notes.
    inline double syncGridQuarters (int idx) noexcept
    {
        switch (idx)
        {
            case 0: return 0.5;     // 1/8
            case 1: return 0.25;    // 1/16
            case 2: return 0.125;   // 1/32
            case 3: return 0.0625;  // 1/64
            default: return 0.125;
        }
    }

    // Per-impact offsets (in any consistent unit) for one burst: a center-pivoted
    // linear gap ramp warped by drift (−1..+1), optionally snapped to a grid. The
    // gaps fan around the middle so that −drift condenses the impacts toward the
    // start and +drift condenses them toward the end (dial direction = where they
    // bunch). Fills offsets[0..rattle-1] (offsets[0] = 0); baseInterval / grid /
    // minGap share one unit. Used by both the audio engine (samples) and the
    // RattleGraph (quarters/ms) so the on-screen positions exactly match the sound.
    inline void computeImpactOffsets (int rattle, double drift, double baseInterval,
                                      bool sync, double grid, double minGap, double* offsets)
    {
        offsets[0] = 0.0;
        if (rattle <= 1) return;

        double prevOff = 0.0, cum = 0.0;
        for (int i = 0; i < rattle - 1; ++i)
        {
            const double centered = (rattle > 2) ? (2.0 * (double) i / (double) (rattle - 2) - 1.0) : 0.0;
            double gap = baseInterval * (1.0 - drift * centered);
            if (gap < minGap) gap = minGap;
            cum += gap;

            double off = cum;
            if (sync && grid > 0.0)
            {
                off = (double) juce::roundToInt (cum / grid) * grid;  // snap to grid
                if (off <= prevOff) off = prevOff + grid;             // keep strictly increasing
            }
            offsets[i + 1] = off;
            prevOff = off;
        }
    }

    inline juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
        using namespace juce;
        std::vector<std::unique_ptr<RangedAudioParameter>> p;

        // Display helpers: 0–1 (and ±1) knobs as integer percent (0.72 → "72", −1 → "−100");
        // pitch / output level with one decimal (11.3); ms / dB / Hz knobs as integer + unit.
        using Attr = AudioParameterFloatAttributes;
        const auto pct = Attr()
            .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100.0f)); })
            .withValueFromStringFunction ([] (const String& t) { return t.getFloatValue() * 0.01f; });
        const auto oneDp = Attr()
            .withStringFromValueFunction ([] (float v, int) { return String (v, 1); });
        const auto msUnit = Attr()
            .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " ms"; })
            .withValueFromStringFunction ([] (const String& t) { return t.getFloatValue(); });
        const auto dbUnit = Attr()
            .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " dB"; })
            .withValueFromStringFunction ([] (const String& t) { return t.getFloatValue(); });
        const auto hzUnit = Attr()
            .withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v)) + " Hz"; })
            .withValueFromStringFunction ([] (const String& t) { return t.getFloatValue(); });

        // ---------- Section 1: Input ----------

        // Input Gain [FX]: 0–40 dB boost applied to the pre-filtered signal before onset detection.
        // Useful when the filtered band has low energy (e.g. narrow BP). Default 0 dB.
        p.push_back (std::make_unique<AudioParameterFloat> (
            ParameterID { ID::inputGain }, "Input Gain",
            NormalisableRange<float> (0.0f, 40.0f), 0.0f, dbUnit));

        // MIDI Channel [Inst]: Omni, 1–16
        {
            StringArray ch { "Omni" };
            for (int i = 1; i <= 16; ++i)
                ch.add (String (i));
            p.push_back (std::make_unique<AudioParameterChoice> (
                ParameterID { ID::midiChannel }, "MIDI Channel", ch, 0));
        }

        // Center Note [Inst]: 0–127, default 60 (C3)
        p.push_back (std::make_unique<AudioParameterInt> (
            ParameterID { ID::centerNote }, "Center Note", 0, 127, 60));

        // Pre-filter Type [FX]: LP / BP / HP
        p.push_back (std::make_unique<AudioParameterChoice> (
            ParameterID { ID::filterType }, "Filter Type",
            StringArray { "LP", "BP", "HP" }, 0));

        // Pre-filter Freq [FX]: 20–20 000 Hz, log-curve skew
        p.push_back (std::make_unique<AudioParameterFloat> (
            ParameterID { ID::filterFreq }, "Filter Freq",
            NormalisableRange<float> (20.0f, 20000.0f, 0.0f, 0.25f), 1000.0f, hzUnit));

        // Threshold [FX]: 0–100 %
        p.push_back (std::make_unique<AudioParameterFloat> (
            ParameterID { ID::threshold }, "Threshold",
            NormalisableRange<float> (0.0f, 1.0f), 0.2f, pct));

        // Sensitivity [FX]: 0–100 %  (1.0 = 0.5 ms attack, best for short transients)
        p.push_back (std::make_unique<AudioParameterFloat> (
            ParameterID { ID::sensitivity }, "Sensitivity",
            NormalisableRange<float> (0.0f, 1.0f), 0.75f, pct));

        // Velocity: 0–100 %
        p.push_back (std::make_unique<AudioParameterFloat> (
            ParameterID { ID::velocity }, "Velocity",
            NormalisableRange<float> (0.0f, 1.0f), 0.5f, pct));

        // Spread: 0–100 %
        p.push_back (std::make_unique<AudioParameterFloat> (
            ParameterID { ID::spread }, "Spread",
            NormalisableRange<float> (0.0f, 1.0f), 0.5f, pct));

        // ---------- Section 2: Rattle ----------

        // Pitch: −12…+12 semitones
        p.push_back (std::make_unique<AudioParameterFloat> (
            ParameterID { ID::pitch }, "Pitch",
            NormalisableRange<float> (-12.0f, 12.0f), 0.0f, oneDp));

        // Pitch Curve Amount: −12…+12 semitones
        p.push_back (std::make_unique<AudioParameterFloat> (
            ParameterID { ID::pitchCurveAmt }, "Pitch Curve Amt",
            NormalisableRange<float> (-12.0f, 12.0f), 0.0f, oneDp));

        // Pitch Curve Shape: Linear / Exp
        p.push_back (std::make_unique<AudioParameterChoice> (
            ParameterID { ID::pitchCurveShape }, "Pitch Curve Shape",
            StringArray { "Linear", "Exp" }, 0));

        // Rattle: 1–32 repeats
        p.push_back (std::make_unique<AudioParameterInt> (
            ParameterID { ID::rattle }, "Rattle", 1, 32, 4));

        // Decay: 0–100 %
        p.push_back (std::make_unique<AudioParameterFloat> (
            ParameterID { ID::decay }, "Decay",
            NormalisableRange<float> (0.0f, 1.0f), 0.3f, pct));

        // Spacing base interval: 10–500 ms, mild log skew (was "Pace")
        p.push_back (std::make_unique<AudioParameterFloat> (
            ParameterID { ID::pace }, "Spacing",
            NormalisableRange<float> (10.0f, 500.0f, 0.0f, 0.5f), 100.0f, msUnit));

        // Drift: −100…+100 % — warps impact spacing; − condenses toward the start, + toward the end (was "Pace Curve")
        p.push_back (std::make_unique<AudioParameterFloat> (
            ParameterID { ID::paceCurve }, "Drift",
            NormalisableRange<float> (-1.0f, 1.0f), 0.0f, pct));

        // Tempo Sync: Free (ms spacing) / Sync (host-tempo subdivisions)
        p.push_back (std::make_unique<AudioParameterChoice> (
            ParameterID { ID::tempoSync }, "Tempo Sync",
            StringArray { "Free", "Sync" }, 0));

        // Sync Division: base impact spacing as a note value (used in Sync mode)
        p.push_back (std::make_unique<AudioParameterChoice> (
            ParameterID { ID::syncDivision }, "Division",
            StringArray { "1/4", "1/4.", "1/4T", "1/8", "1/8.", "1/8T",
                          "1/16", "1/16.", "1/16T", "1/32" }, 6));

        // Sync Grid: how fine Drift snaps impacts (finer = more rhythmic wander)
        p.push_back (std::make_unique<AudioParameterChoice> (
            ParameterID { ID::syncGrid }, "Grid",
            StringArray { "1/8", "1/16", "1/32", "1/64" }, 2));

        // Play Order: Seq (sequential cycle) / Rnd (random, no immediate repeat)
        p.push_back (std::make_unique<AudioParameterChoice> (
            ParameterID { ID::playOrder }, "Play Order",
            StringArray { "Seq", "Rnd" }, 0));

        // Sample Iteration: Trigger (advance per Rattle sequence) / Impact (advance per playback event)
        p.push_back (std::make_unique<AudioParameterChoice> (
            ParameterID { ID::sampleIter }, "Sample Iteration",
            StringArray { "Trigger", "Impact" }, 1));

        // Pan Iteration: Trigger (one pan per sequence) / Impact (fresh pan per playback event)
        p.push_back (std::make_unique<AudioParameterChoice> (
            ParameterID { ID::panIter }, "Pan Iteration",
            StringArray { "Trigger", "Impact" }, 0));

        // Loop: how the Seq unit/slice walk traverses across a burst and across triggers.
        //   Off       = continuous round-robin (cursor persists, forward; never restarts).
        //   Loop FW   = restart from the first unit each trigger, loop forward.
        //   Ping-Pong = restart from the first, bounce back down through the units.
        //   Loop BW   = restart from the last unit each trigger, loop backward.
        p.push_back (std::make_unique<AudioParameterChoice> (
            ParameterID { ID::loopMode }, "Loop",
            StringArray { "Off", "Loop FW", "Ping-Pong", "Loop BW" }, 0));

        // Pan Spread: 0–100 %
        p.push_back (std::make_unique<AudioParameterFloat> (
            ParameterID { ID::panSpread }, "Pan Spread",
            NormalisableRange<float> (0.0f, 1.0f), 0.3f, pct));

        // ---------- Section 3: Output ----------

        // Mix [FX]: 0 = full dry, 1 = full wet. Hidden/forced 1.0 in Inst.
        p.push_back (std::make_unique<AudioParameterFloat> (
            ParameterID { ID::mix }, "Mix",
            NormalisableRange<float> (0.0f, 1.0f), 0.5f, pct));

        // Output Level: −36…+12 dB trim
        p.push_back (std::make_unique<AudioParameterFloat> (
            ParameterID { ID::outputLevel }, "Output Level",
            NormalisableRange<float> (-36.0f, 12.0f), 0.0f, oneDp));

        // Per-slot mute (automatable) — mutes the whole slot; right-click an unsliced
        // sample to toggle. Sliced slots use per-slice "garbage" flags instead.
        for (int i = 0; i < 8; ++i)
            p.push_back (std::make_unique<AudioParameterBool> (
                ParameterID { ID::slotMute (i) }, "Slot " + String (i + 1) + " Mute", false));

        AudioProcessorValueTreeState::ParameterLayout layout;
        for (auto& param : p)
            layout.add (std::move (param));
        return layout;
    }
}
