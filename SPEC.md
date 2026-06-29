# RATTLE — VST3 Plug-in Specification

> Status: **Draft v0.9.2** · Date: 2026-06-18 · Author: Simon (design) + Claude (drafting)
> This is the agreed design contract. Implementation should not start until this is signed off.

---

## Revision History

| Version | Date | Summary |
|---|---|---|
| v0.1 | 2026-06-16 | Initial specification drafted. |
| v0.2 | 2026-06-16 | Phase 1–2 design confirmed: APVTS skeleton, sample loading, waveform display, one-shot varispeed playback. |
| v0.3 | 2026-06-18 | **Pitch range narrowed to ±12 st** (was ±24) for both Pitch and Pitch Curve Amount — §5.2 and §7 updated. **RattleGraph visualization added** (§9.1): live amplitude-bar + pitch-curve overlay that redraws from param values at 30 Hz. |
| v0.4 | 2026-06-18 | **Phase 4 — Input section implemented.** FX path: `PreFilter` (LP/BP/HP biquad, lazy coefficient update) + `OnsetDetector` (envelope follower, rising-edge detection, 50 ms retrigger guard, velocity from envelope); SPREAD controls filter Q (0=narrow/high-Q→10.0, 1=wide/low-Q→0.5). Inst path: MIDI channel filter (Omni/1–16) + center-note ± window (SPREAD maps 0–1 → 0–64 semitones each side). Both paths: VELOCITY lerps from flat (0) to full dynamics (1). UI: 2-column Input section added at bottom of editor; window expanded to 560×840 (FX) / 560×800 (Inst). Plugin version bumped to 0.4.0. |
| v0.4.1 | 2026-06-18 | **FX trigger fix + listen mode.** Threshold default lowered 0.5→0.2 (better default for typical -6 to -12 dBFS drum loops). Added **LISTEN FILTER** toggle button (§9.2): routes the pre-filtered mono signal to audio output instead of rattle for diagnostic tuning of filter type/freq and threshold. Onset detection still runs internally while listening. §4.2 Threshold/Sensitivity definitions expanded with precise attack-time semantics. |
| v0.5.0 | 2026-06-18 | **Real-time FFT spectrum view** (§9.3): shares the waveform area, toggled via new "FFT" button (green when active). Shows raw pre-filter input, 20 Hz–20 kHz log X / –80 to 0 dB Y, 150 ms temporal smoothing, indigo fill. `juce::dsp::FFT` (order 11, 2048 bins) added via new `juce_dsp` link dependency. Audio thread writes a ring buffer; UI reads at 30 Hz. Sensitivity default raised to 1.0 (confirmed correct for short transients). |
| v0.5.1 | 2026-06-18 | **Input Gain** (0–40 dB, §4.2): pre-amplifies the filtered signal before onset detection, raising effective sensitivity when working with quiet or narrow-band signals. **FFT overlays** (§9.3): amber vertical line at filter center frequency + dashed amber horizontal line at effective threshold (accounting for Input Gain), both drawn on the FFT view for at-a-glance detection calibration. **Mix curve rework** (§6): three-phase behavior — 0–50% adds wet while dry stays at unity; 50–100% fades dry while wet stays at unity (avoids gain pumping when blending in the rattle). |
| v0.5.2 | 2026-06-18 | **Velocity decoupled from threshold** (§4.2): onset detector now uses a separate slow (5 ms attack / 300 ms release) envelope follower for velocity, independent of the fast detection follower. Previously the velocity was the fast follower value at threshold-crossing time (≈ threshold level), causing louder rattle at higher threshold settings. Now velocity reflects the signal's sustained amplitude regardless of gate position. **Mix default** raised to 50% (three-phase midpoint where both dry and wet are at unity). |
| v0.6.0 | 2026-06-18 | **Phase 5 — Round-robin & slicing.** Multi-slot loading (up to 8 slots); slot strip UI (numbered buttons 1–8, selected slot highlighted, loaded slots tinted). Load/Clear/Auto buttons operate on the selected slot. **Round-robin slot selection** wired (Sequential / Random / Random No-Repeat), RR Order combobox added to Rattle section. **Pan Spread** wired (0–1, alternating L/R constant-power pan per voice); Pan Spread slider added to Rattle section. **Slice markers** per slot: auto-detect via amplitude-follower onset scanner (Auto button, 40 ms guard); draggable amber vertical lines on WaveformView (right-click removes); serialised in plugin state ValueTree. **SequenceVoice** plays each rattle repetition from the next slice in the cycle (`repSlices[repIdx % sliceCount]`). **RattleEngine** now slot-aware: `pickSlot()` + `fillRepSlices()` + `nextPanPosition()` at trigger time; `processBlock` no longer passes a buffer (each voice uses its own snapshotted buffer pointer). Window: 560×980 (FX) / 560×870 (Inst). |
| v0.7.0 | 2026-06-18 | **Phase 6 — Sample workflow & round-robin overhaul (design).** Multi-file loading (multi-select dialog + drag-and-drop, fills **empty slots only**, filename order; single file dropped on a slot replaces it). Per-slot **sample name** shown top-right of the waveform; per-slot **sample gain** (±20 dB, non-automatable state, knob bottom-right of waveform, rides with each impact). **Manual slice add** via double-click on the waveform (drag-move / right-click-remove unchanged). **Iteration model:** new `Sample Iteration` (Trigger / Impact) and `Pan Iteration` (Trigger / Impact) choice params — *Impact* advances the round-robin every playback event (each "impact" = one repetition) over a **unified pool** of all slices across all loaded slots (an unsliced slot = one whole-sample unit); *Trigger* advances once per Rattle sequence. `RR Order` renamed **Play Order**, reduced to **Seq / Rnd** (pure Random dropped; Rnd = random-no-immediate-repeat). **UI restructure:** the FX "FFT" button becomes an **Input ⇄ Sample** view switch — Input view shows the FFT viewer + the full detection chain beside it; Sample view shows the waveform + 8 slots + Load/Auto/Clear. All small enum selectors (Filter Type, Pitch Curve Shape, Play Order, both iteration toggles) rendered as **amber toggle-button groups**. Trigger button rehomed to the top header. FX window ~560×860. §5.1/§5.3/§7/§9/§10 updated. |
| v0.7.1 | 2026-06-18 | **Crash fix — clearing/replacing a sample while it plays.** `SampleSet` slot buffers are now `std::shared_ptr<AudioBuffer>`; `SequenceVoice::RepSlice` holds a `shared_ptr`, so a running rattle sequence keeps its sample alive even if the slot is cleared or replaced mid-playback (memory freed only when the last voice drops it — no use-after-free). A `juce::SpinLock` in `SampleSet` serialises slot mutation (UI thread: `loadFile`/`clear`/`setSlices`/`setGainDb`/state-load take `ScopedLock`) against the trigger-time read (audio thread `RattleEngine::resolveUnits` takes `ScopedTryLock`, skipping the trigger on rare contention). File decode and onset scanning happen outside the lock; only the slot swap is locked. Clearing a sounding slot now lets the in-flight sequence finish, then stops cleanly. |
| v0.7.2 | 2026-06-18 | **Defaults + trigger indicators + audition.** New defaults: **Velocity 0.5** (was 1.0), **Sensitivity 0.75** (was 1.0), **Decay 0.3** (was 0.5). **Per-slot trigger LEDs** (§9.8): each slot button shows a small amber dot that lights when an impact sourced from that slot fires (engine bumps per-slot atomic counters; editor polls at 30 Hz and fades). **Audition** (§9.8): a speaker toggle (`ShapeButton`) next to Load — when on, clicking a slot previews its whole sample once at the slot's own gain (UI sets an atomic slot index; audio thread spawns a one-shot voice into the wet bus). Engine adds `auditionSlot()`, `allocVoice()`, `getSlotHits()`; `SequenceVoice::RepSlice` gains a `slot` index. |
| v0.8.0 | 2026-06-18 | **Host-tempo Sync + audition routing + Pace/Pace Curve rename.** **Tempo Sync** (§5.4): a Free/Sync selector — in Sync, `Spacing` becomes a note **Division** (1/4…1/32, incl. dotted & triplet) of the host tempo, and `Drift` warps the impact spacing which then **snaps to a Grid** (1/8…1/64) so impacts wander onto neighbouring subdivisions (finer Grid = more wander). Grid is **trigger-relative**. Engine: impact spacing is precomputed per burst into `Params::repInterval[]` (drifted cumulative offsets, snapped in Sync), read by `SequenceVoice`; processor reads host BPM from the playhead each block. **Audition now post-mix**: preview voices render in a separate pool (`RattleEngine::processAudition`) added after the dry/wet Mix, so a slot is always audible regardless of Mix. **Renames**: `Pace`→**Spacing**, `Pace Curve`→**Drift** (param IDs unchanged). UI: Sync + Grid toggle groups and a Division combo that shares the Spacing cell; windows 560×800 (FX) / 560×860 (Inst). |
| v0.8.1 | 2026-06-18 | **Drift model rework + Sync-accurate RattleGraph.** Drift now uses a **center-pivoted linear gap ramp** (gaps fan from base·(1−drift) to base·(1+drift) around the middle) within a **fixed total length**, replacing the old start-pivoted formula — so every impact (not just the last) responds to Drift, neither end produces an oversized gap, and the burst length no longer changes with Drift. Applies to both Free and Sync. A shared `RattleParams::computeImpactOffsets()` computes the warped + snapped offsets for **both** the audio engine (samples) and the **RattleGraph**, so the visualiser matches the sound. The RattleGraph now **draws the subdivision grid in Sync** (beat / 1/8 / 1/16 / finer lines) with the impact bars on the grid; a small minimum bar height keeps quiet (decayed) impacts visible. Validated via an interactive preview before porting. |
| v0.8.2 | 2026-06-18 | **FFT peak display + active-only feed.** The input spectrum now shows **instantaneous peak** magnitudes (removed the 150 ms temporal smoothing) so transients are visible for threshold setting — tagged "PEAK". The audio thread fills `fftInputFifo` **only while the spectrum view is on screen** (`RattleAudioProcessor::fftActive` atomic, set by `FFTView::visibilityChanged()` / cleared in its destructor); no FFT feed work in Sample view or with the editor closed. |
| v0.8.3 | 2026-06-18 | **FFT peak-hold + Drift inversion + value display formatting.** FFT spectrum now uses a **peak-hold** (instant attack, ~0.3 s decay) so transients linger to read against the threshold. **Drift inverted**: −Drift now condenses impacts toward the **start**, +Drift toward the **end** (dial direction = where they bunch); single-line change in `computeImpactOffsets` (`1 − drift·centered`), so engine + RattleGraph stay in sync. **Value display** (via `AudioParameterFloatAttributes`, so host + sliders both update): all 0–1 knobs (Threshold, Sensitivity, Velocity, Spread, Decay, Pan Spread, Mix) and Drift (±1) now show as **integer percent** (0.72 → "72", −1 → "−100"); Pitch, Pitch Curve Amt, and Output Level show **one decimal** (11.3). |
| v0.8.4 | 2026-06-18 | **Unit display formatting.** Spacing, Input Gain, and Filter Freq now display as **integer + unit** (`100 ms`, `20 dB`, `1000 Hz`) instead of raw 2-decimal floats, via the same `AudioParameterFloatAttributes` mechanism (host + sliders). |
| v0.9.0 | 2026-06-23 | **Garbage slices + automatable per-slot mute.** Slices can be marked **garbage** (`bool` on `SampleSet::Slice`, serialized) — skipped in playback and shown grayed with a big **X**. Whole slots have an automatable **mute** (8 `AudioParameterBool` `slotMuteN`, folded into `SequenceVoice::Params::mutedMask`) — right-click an *unsliced* sample to mute it (the temporary-mute / automation use case); right-click a *slice* of a sliced slot to garbage it. The engine excludes muted slots and garbage slices from both the unified pool and the per-trigger slice walk; a slot with no playable units is skipped (silent if it's the only one). Garbage flags survive marker edits (preserved by start-position match). **New waveform gestures**: right-click = garbage/mute, double-click = add/delete a marker, drag a marker off the view = delete. |
| v0.9.1 | 2026-06-27 | **Loop traversal control.** One sequencing control (Section 2): **Loop** (`loopMode`) — **Off** / **Loop FW** / **Ping-Pong** / **Loop BW** — governs how the **Seq** unit/slice walk traverses. *Off* = continuous round-robin (cursor persists across triggers, forward; the prior default). The three active modes **restart each trigger**: *Loop FW* from the first unit looping forward, *Ping-Pong* from the first bouncing back down, *Loop BW* from the **last** unit looping backward (e.g. replay a sliced one-shot last→first). Applies to Seq in both Impact (pool walk via free-running `poolCursor` → `loopIndex`/`loopPeriod`) and Trigger (slice walk) modes; active modes also reset the slot RR. Random order ignores it. Default Off reproduces prior behavior. One Section-2 row added (560×835 FX / 560×895 Inst). |
| v0.9.2 | 2026-06-28 | **FX/Inst polish.** **Velocity is now Inst-only**: in Inst it scales the RATTLE output down from full by MIDI velocity (amount set by the Velocity param, as before); in **FX the sequence always generates at full intensity** — Velocity control removed from the FX UI, output forced to 100% (mirrors how Mix is FX-only / forced in Inst). **Clearing a slot resets its mute** so a reloaded sample starts enabled instead of inheriting a prior right-click mute. **FX default view is now Sample** (was Input — input filtering is moot before a sample is loaded). **Display strings switched to ASCII** (em-dash/arrow → `-` / `->`): the rendering font lacks those glyphs, so `/utf-8` fixed the encoding but they still showed as boxes. **Play Order + Loop merged** into one control: Play Order is now **Continuous / Random / Loop**, and a **Loop direction** sub-control (Forward / Ping-Pong / Backward, `loopDir`) appears only when Loop is selected — masked otherwise, like the Free/Sync swap. This drops the dead Random×Loop combinations of the previous two-control design (`playOrder` Seq/Rnd + `loopMode` Off/FW/PP/BW): Continuous = the old Loop-Off, Loop = restart-per-trigger. Sequencing is only meaningful with ≥2 playable units (single unsliced sample → all orders inert; documented, controls stay active). |

---

## 1. Concept

**RATTLE** generates a *rattle sequence* in response to a trigger. A rattle sequence is one or
more short audio samples played in a rapid, repeating burst that decays over its length — like the
tail of a tap-delay, except instead of echoing the **input** sound it plays back **user-selected
sample(s)**. Use cases: adding sub "oomph" under a kick, metallic/glass shimmer as ear-candy,
buzzing/rattling textures, round-robin percussion bursts.

The trigger comes from one of two sources, shipped as **two separate plug-ins sharing one DSP core**:

- **RATTLE Inst** — a MIDI-triggered instrument (note on → rattle sequence).
- **RATTLE FX** — an audio-insert effect (detected transient in the incoming audio → rattle sequence).

---

## 2. Locked decisions (from design interview)

| Topic | Decision |
|---|---|
| Framework | **JUCE**, using the **free closed-source commercial tier** (verify current revenue threshold before release). |
| Formats / platform | **VST3 only**, **Windows only** to start. macOS can be added later from the same codebase. |
| Packaging | **Two separate plug-ins** (`RATTLE Inst` instrument + `RATTLE FX` effect) over a **shared DSP core**. |
| Rattle timing | **Free time (milliseconds)** only — no host-tempo sync. |
| Pitch shifting | **Varispeed / playback-rate** (resampling). Higher pitch = shorter, faster sample. |
| Sample storage | **Embedded into plug-in state** — presets/projects are fully portable. |
| Polyphony | **Up to 8** overlapping rattle sequences; new triggers steal the **oldest** voice past the limit. |
| Bundled content | **Ship a small starter pack** of samples (sub sine, glass, metallic shimmer, noise burst). |
| SPREAD control | **Width / Q of the trigger band around a center.** FX: bandwidth/Q of the pre-filter (narrow = isolate just the kick or just the snare; wide = cover both). Inst: width of the note window around a center note (single note → full register). Same knob, same model in both. |
| Play Order (round-robin) | **Selectable**: sequential cycle / random-no-immediate-repeat (shown **Seq / Rnd**). Pure random dropped. |
| Stereo placement | **Stereo with a pan-spread control** (mono → wide scatter of voices). |
| Onset detection | **Auto-detect + manual override** (draggable slice markers; **double-click adds/deletes**, drag-off deletes; right-click toggles garbage/mute; threshold/sensitivity knob). |
| Sample iteration | **Selectable** per-sequence (**Trigger**) or per-playback-event (**Impact**) round-robin; Impact walks a unified pool of all slices across all loaded slots. Same Trigger/Impact choice offered for **pan**. |
| Per-sample gain | Each slot has a ±20 dB trim, saved with the sample (non-automatable state). |
| Multi-load | Multi-select + drag-and-drop fill **empty slots only**; single drop on a slot replaces it. |

---

## 3. Signal flow

```
                 ┌─────────────────────────── SECTION 1: INPUT ───────────────────────────┐
  MIDI in  ──▶   │  [Inst] channel filter → center-note ± SPREAD window → velocity        │
                 │                                                                          │──┐
  Audio in ──▶   │  [FX]  pre-filter (LP/BP/HP, freq) → onset detector (threshold,          │  │  trigger
                 │        SPREAD=detection-band Q) → envelope follower (VELOCITY)           │  │  events
                 └──────────────────────────────────────────────────────────────────────────┘  │  (+ strength)
                                                                                                 ▼
                 ┌─────────────────────────── SECTION 2: RATTLE ──────────────────────────┐
                 │  Voice allocator (≤8). Per trigger spawn a Sequence:                    │
                 │   • pick sample (round-robin order) / multi-hit slice                   │
                 │   • PITCH (base) + PITCH CURVE (per-repeat offset)                       │
                 │   • RATTLE (repeat count / feedback) · DECAY (fade length)              │
                 │   • PACE (base interval ms + accel/decel curve)                         │
                 │   • pan-spread placement                                                 │
                 └──────────────────────────────────────────────────────────────────────────┘
                                                                                                 ▼
                 ┌─────────────────────────── SECTION 3: OUTPUT ──────────────────────────┐
  Audio in ──┐   │                                                                          │
             ├──▶│  MIX (dry input ↔ wet rattle)  →  output level                          │──▶ Audio out
  wet  ──────┘   │  (Inst mode has no dry signal: MIX acts as wet level)                    │
                 └──────────────────────────────────────────────────────────────────────────┘
```

---

## 4. Section 1 — Input & control

### 4.1 RATTLE Inst (MIDI mode)
- **MIDI channel** — Omni (default) or 1–16. Notes on other channels are ignored.
- **Center note** — the note the trigger window is centered on (default C3 / 60).
- **SPREAD (note-window width)** — how many notes around the center will trigger.
  - Narrow (min): only the center note triggers (single-note trigger).
  - Wide (max): the full register triggers.
  - Symmetric with the FX pre-filter width — same knob, same mental model (center + width).
- **VELOCITY** — how much MIDI note velocity modulates output level.
  - 0%: flat output regardless of velocity. 100%: output level tracks velocity fully.

### 4.2 RATTLE FX (audio-insert mode)
- **Pre-filter** — isolates the part of the input that should drive triggering (like a ducker's
  sidechain filter). Type = **Low-pass / Band-pass / High-pass**, with **adjustable frequency**
  (20 Hz–20 kHz). The filtered signal feeds the detector only; it does not alter the dry path.
- **Input Gain** — pre-amplifies the filtered signal before it enters the onset detector.
  Range: **0–40 dB**, default 0 dB. Useful when targeting a narrow band-pass or other low-energy
  signal where the raw amplitude would require an impractically low Threshold setting.
  The FFT spectrum view shows the *raw* (pre-gain) input, but the threshold overlay line shifts
  downward to reflect the effective detection level after the boost is applied.
- **Onset / transient detector** — generates a trigger event on each rising edge that crosses the
  detection threshold. Two parameters control it:
  - **Threshold** — the *amplitude gate*. The internal envelope follower must rise above this level
    to fire. Lower = triggers more easily (more hits); higher = only the loudest hits trigger.
    Range 0–1, default 0.2.
  - **Sensitivity** — the *attack-time* of the envelope follower, i.e. how fast it tracks rising
    transients.  At maximum (1.0) the attack is ~0.5 ms: the follower reacts near-instantaneously,
    so even brief ghost notes or hi-hat hits spike the envelope and can cross threshold. At minimum
    (0.0) the attack is ~30 ms: the follower is sluggish, so only hits that sustain at high amplitude
    for roughly 30 ms drive the envelope above threshold — a short snare crack may not register even
    if its peak amplitude is high enough. Range 0–1, default 0.5.
  - **Together** they form a 2-D gate: Threshold = "how loud", Sensitivity = "how sharp/fast". For
    kick detection, use the LP pre-filter to remove hi-hats and snare content so Sensitivity can be
    set freely without false triggers from high-frequency material.
  - A **50 ms re-trigger guard** prevents double-firing on a single hit.
  - **Velocity tracking is decoupled from the detection gate.** A separate slow envelope follower
    (5 ms attack / 300 ms release) measures the signal's sustained amplitude independently of where
    the threshold is set. This ensures that changing Threshold only affects whether/how often a
    trigger fires, not the output level of the rattle.
- **SPREAD (pre-filter width / Q)** — sets the **bandwidth (Q)** of the pre-filter around its
  frequency. Narrow = isolate a tight band (e.g. just the kick, or just the snare, of a drum loop);
  wide = cover a broad range (e.g. kick *and* snare). For BP this is band width; for LP/HP it sets
  cutoff resonance/emphasis.
- **VELOCITY** — an envelope follower on the (filtered) input sets each trigger's strength.
  - 0%: every trigger fires the rattle at a constant level. 100%: rattle level tracks input dynamics.

---

## 5. Section 2 — Rattle (the heart)

### 5.1 Sample management
- **Up to 8 sample slots**, used as a round-robin set.
- Load user files: **WAV, AIFF, FLAC, OGG, MP3** (JUCE basic formats).
- **Loading**: single load via the Load button; **multi-file load** (select several files in the
  dialog, or **drag-and-drop** them onto the plug-in) fills **empty slots only**, left-to-right, in
  filename order (non-destructive). Dropping a **single** file onto a specific slot button **replaces**
  that slot.
- Each loaded sample is shown as a **waveform display**, with its **file name** (extension stripped,
  ellipsised if long) right-aligned in the **top-right** corner and a **per-sample gain** knob
  (±20 dB, default 0) **bottom-right**. Sample gain is per-slot **state** (saved with the sample, not
  an automatable parameter) and rides with the sample through the round-robin — so each sample keeps
  its own level even when impacts switch samples.
- **Multi-hit slicing**: a file containing several hits can be sliced into separate hits via onset
  detection (**Auto**). **Slice markers** are drawn on the waveform and can be **dragged** (left-drag
  to move), **added/deleted** (**double-click**), or **deleted by dragging off** the view. *(v0.9.0
  moved deletion off right-click; right-click now toggles garbage/mute — see §9.9.)*
- **Play Order**: **Seq** (sequential cycle) / **Rnd** (random, no immediate repeat) — selectable;
  governs *how* the round-robin advances.
- **Sample Iteration** — *when* the round-robin advances, and over *what* pool:
  - **Trigger**: the slot-level round-robin advances **once per Rattle sequence**. The whole burst
    plays the chosen slot; if that slot is sliced, its slices still play in order across the impacts
    (ordered sub-sequence).
  - **Impact**: a **unified pool** of every slice of every loaded slot (an unsliced slot contributes
    one whole-sample unit) is walked **one unit per impact** — the sample/slice can change on every
    repetition. This is the round-robin-per-slice mode. ("Impact" = each generated sample playback
    event, like each bounce of a ball.)
  - The pool cursor **persists across triggers** (consecutive bursts keep cycling). Default **Impact**.
- **Play Order** *(v0.9.2)*: single control for how the unit/slice walk traverses (the old separate Seq/Rnd Play Order and Off/FW/PP/BW Loop are merged here, dropping the dead Random×Loop combinations):
  - **Continuous** — round-robin that advances across triggers and never restarts (the prior default; cursor persists).
  - **Random** — random unit, no immediate repeat.
  - **Loop** — restarts the walk from its edge each trigger; direction set by the **Loop** sub-control (`loopDir`, shown only under Loop): **Fwd** (0,1,…,N-1,0,…), **Ping** (bounce 0,…,N-1,N-2,…), **Bwd** (start from the last: N-1,…,1,0,N-1,…). Loop also resets the slot round-robin.
  - Only meaningful with **≥2 playable units**; with a single unsliced sample all three are inert (no glitch — `loopIndex` clamps `n≤1` to 0; controls stay active, documented behavior). Shared `loopIndex` (fwd modulo / triangle / bwd modulo) + `loopPeriod` for the Impact per-impact pool walk, the **Trigger per-trigger slot walk** (the sample advances each trigger — Continuous/Loop directional, Random no-repeat), and the within-burst slice walk.
- **Embedding**: the raw audio of every loaded sample (plus its name, gain, and slice markers) is
  serialized into plug-in state, so presets and projects are self-contained.
- **Garbage slices / slot mute** *(v0.9.0)*: any slice can be flagged **garbage** (serialized per
  slice) and is skipped in playback — in both the unified pool (Impact) and the per-trigger slice
  walk (Trigger). Each slot also has an **automatable mute** (`slotMuteN` params → `mutedMask`) that
  drops the whole slot. A slot left with no playable units (muted, or every slice garbage) is
  skipped entirely; if nothing is playable the trigger is silent. This doubles as a temporary,
  automatable per-sample mute for unsliced slots.

### 5.2 Per-sequence controls
| Control | Behavior | Range / default (proposed) |
|---|---|---|
| **PITCH** | Base transpose of the sample, varispeed (resampling). | **−12…+12 semitones**, default 0 |
| **PITCH CURVE** | Pitch offset applied progressively across repetitions in one sequence. Default flat. Positive = rises each repeat, negative = falls. | **−12…+12 semitones** total over the sequence; curve shape: linear (default), exp; default amount 0 |
| **RATTLE** | Like a delay's feedback: number of repetitions generated by one trigger. Lowest = single hit. | 1…32 repeats, default ~4 |
| **DECAY** | Length of the decay envelope — how quickly repetitions fade across the sequence. | short…long (maps to fade curve), default medium |
| **PACE** | Base spacing between repetitions plus a curve: constant, gradually closer (accelerando), or gradually wider (ritardando). | base 10–500 ms; curve −100%…+100% (− = accelerate, + = decelerate), default constant |
| **Pan-spread** | Stereo scatter of voices / round-robins (mono → wide). | 0–100%, default ~30% |
| **Play Order** | Round-robin pick order through the sample/slice pool. | Seq / Rnd (no-repeat) |
| **Sample Iteration** | Advance the sample round-robin per Rattle sequence (Trigger) or per impact (Impact). | Trigger / Impact, default Impact |
| **Pan Iteration** | Recompute pan once per sequence (Trigger) or per impact (Impact). | Trigger / Impact, default Trigger |

### 5.3 Voice / sequence engine
- A **trigger event** (from Section 1, carrying a strength 0–1) spawns a **Sequence voice**.
- Up to **8** sequences run concurrently; beyond that, steal the **oldest**.
- **At trigger time** the engine resolves, for each of the burst's impacts, a per-impact unit
  `{ buffer, fileSampleRate, start, end, gain }` plus a per-impact pan position — snapshotting them
  into the voice so a running sequence is immune to later parameter or sample changes:
  - *Sample Iteration = Trigger*: one slot is picked (Play Order across loaded slots); every impact
    uses that slot's buffer/gain, walking its slices in order.
  - *Sample Iteration = Impact*: the unified slice pool is walked one unit per impact (Play Order),
    so consecutive impacts may pull different buffers (and sample rates / gains).
  - *Pan Iteration = Trigger*: one pan for the burst; *= Impact*: a fresh pan per impact.
- Each Sequence schedules its impacts on a sample-accurate internal clock from PACE; each impact
  plays its unit's slice range at PITCH + PITCH CURVE (resample, using that unit's sample rate),
  scaled by DECAY envelope × trigger strength (VELOCITY) × the unit's per-sample gain, placed at its
  pan position, and mixed into the wet bus.

### 5.4 Tempo sync *(added v0.8.0)*

A **Free / Sync** selector controls how impact spacing is timed:
- **Free** — `Spacing` is a base interval in **milliseconds**; `Drift` warps it across the burst (− accelerando, + ritardando).
- **Sync** — `Spacing` becomes a note **Division** of the host tempo (1/4, 1/4., 1/4T, 1/8, 1/8., 1/8T, 1/16, 1/16., 1/16T, 1/32). Impacts are spaced at that division; `Drift` still warps the spacing, but each impact is then **snapped to a Grid** (1/8 / 1/16 / 1/32 / 1/64) — so Drift pushes impacts onto neighbouring subdivisions, producing stepped rhythmic variation. **Finer Grid = more wander.** The grid is **trigger-relative** (anchored to each trigger with tempo-locked spacing, not bar-aligned), keeping impacts tight to the transient.
- **Drift model** *(v0.8.1, direction inverted v0.8.3)*: a **center-pivoted linear gap ramp** within a **fixed total length**, so every impact responds to Drift and the burst length is Drift-independent (replaces the old start-pivoted curve). The gaps fan around the middle so **−Drift condenses impacts toward the start, +Drift toward the end** (dial direction = where they bunch). In Sync each offset then snaps to the Grid.
- Implementation: both Free and Sync use the shared `RattleParams::computeImpactOffsets()`. Spacing is precomputed per burst into `Params::repInterval[]` at trigger time; `SequenceVoice` schedules from it. Host BPM comes from the playhead each block (falls back to the last known value when unavailable). The **RattleGraph** calls the same helper and draws the subdivision grid in Sync, so on-screen positions match the sound.

---

## 6. Section 3 — Output
- **MIX** [FX only] — three-phase blend of dry input and wet rattle. **Hidden in Inst mode.**
  - **0–50%** (parallel blend): dry stays at unity (0 dB); wet fades in from 0 to unity. Both signals
    at full level at the midpoint.
  - **50–100%** (dry fade): wet stays at unity; dry fades from unity to silence. At 100% the output
    is rattle-only.
  - Rationale: avoids gain pumping when "dialing in" the rattle — the source audio is unaffected
    until you actively choose to reduce it past the midpoint.
- **Output level** — final output trim (both modes; default 0 dB). The primary level control in
  Inst mode.

---

## 7. Parameter list (for APVTS / automation)

Shared unless marked [Inst] or [FX]. Final IDs assigned at implementation.

| # | Param | Section | Type | Notes |
|---|---|---|---|---|
| 1 | Mode (compile-time per target) | — | — | Inst vs FX is the target, not a user param |
| 2 | MIDI Channel [Inst] | 1 | choice | Omni/1–16 |
| 3 | Center Note [Inst] | 1 | int | 0–127, default 60 (C3); window width = Spread (#10) |
| 5 | Pre-filter Type [FX] | 1 | choice | LP/BP/HP |
| 6 | Pre-filter Freq [FX] | 1 | float | 20–20k Hz, log |
| 6b | Input Gain [FX] | 1 | float | 0–40 dB; applied after pre-filter, before detector |
| 7 | Threshold [FX] | 1 | float | detector |
| 8 | Sensitivity [FX] | 1 | float | detector |
| 9 | Velocity [Inst] | 1 | float | 0–100%; Inst-only — scales output down from full by MIDI velocity. FX always 100% (no control). |
| 10 | Spread | 1 | float | 0–100%; FX: pre-filter Q/width · Inst: note-window width |
| 11 | Pitch | 2 | float | **−12…+12 st** |
| 12 | Pitch Curve Amt | 2 | float | **−12…+12 st** |
| 13 | Pitch Curve Shape | 2 | choice | linear/exp |
| 14 | Rattle | 2 | int | 1–32 |
| 15 | Decay | 2 | float | 0–100% |
| 16 | Spacing | 2 | float | base ms (Free); replaced by Division in Sync |
| 17 | Drift | 2 | float | −100…+100% — − condenses impacts to start, + to end; snaps to Grid in Sync |
| 17a | Tempo Sync | 2 | choice | Free / Sync |
| 17b | Division | 2 | choice | note value (Sync): 1/4…1/32 incl. dotted & triplet |
| 17c | Grid | 2 | choice | snap granularity (Sync): 1/8…1/64 |
| 18 | Play Order | 2 | choice | Continuous / Random / Loop — unit-walk traversal; Loop restarts each trigger (direction via Loop sub-control) |
| 19 | Pan Spread | 2 | float | 0–100% |
| 19b | Sample Iteration | 2 | choice | Trigger / Impact (per-sequence vs per-impact round-robin); default Impact |
| 19c | Pan Iteration | 2 | choice | Trigger / Impact; default Trigger |
| 19d | Loop (dir) | 2 | choice | Forward / Ping-Pong / Backward — direction when Play Order = Loop (`loopDir`; UI shows it only then); default Forward |
| 20 | Mix [FX] | 3 | float | 0–100%; hidden & forced 100% in Inst |
| 21 | Output Level | 3 | float | dB trim, both modes |
| 22 | Slot 1–8 Mute | 2 | bool ×8 | `slotMuteN`; mutes the whole slot (automatable) |

*(Sample slots, slice markers + per-slice garbage flags, per-sample gain, and embedded audio live in
non-automatable state, not the parameter tree. The per-slot **mute** above is automatable.)*

---

## 8. Architecture

```
RATTLE_VST/
├─ CMakeLists.txt              # top-level; pulls JUCE, defines both targets
├─ JUCE/                       # JUCE framework (git clone, shallow)
├─ Source/
│  ├─ core/                    # SHARED DSP — no JUCE plugin types
│  │   ├─ RattleEngine.*       # owns voices, mixes wet bus
│  │   ├─ SequenceVoice.*      # one rattle sequence (scheduling, decay)
│  │   ├─ SampleSet.*          # slots, round-robin, slices, resampling
│  │   ├─ OnsetDetector.*      # transient detect (trigger + slicing)
│  │   ├─ EnvelopeFollower.*   # velocity from audio
│  │   ├─ TriggerSource.*      # MIDI vs audio → unified trigger events
│  │   └─ Params.h            # parameter IDs, ranges, layout helper
│  ├─ PluginProcessor.*        # AudioProcessor; MODE set by compile flag
│  ├─ PluginEditor.*           # 3-panel UI
│  └─ ui/
│      ├─ WaveformView.*       # waveform + draggable slice markers
│      ├─ Knob.* / Panels.*    # input/rattle/output panels
│      └─ LookAndFeel.*
└─ Resources/                  # starter-pack samples → BinaryData
```

- **Two CMake targets** via `juce_add_plugin`, sharing `Source/core` + processor/editor.
  - `RATTLE FX`: `IS_SYNTH FALSE`, `NEEDS_MIDI_INPUT FALSE`, category `Fx`, stereo in/out.
  - `RATTLE Inst`: `IS_SYNTH TRUE`, `NEEDS_MIDI_INPUT TRUE`, category `Instrument`, (no audio in).
  - A compile definition (e.g. `RATTLE_MODE_INST`) switches the processor's trigger source and bus
    layout.
- **State**: `AudioProcessorValueTreeState` for params + a custom `ValueTree` branch holding
  embedded sample audio (base64/FLAC-compressed) and slice markers.
- **Sample thread-safety** *(v0.7.1)*: `SampleSet` slot buffers are `shared_ptr`-owned; voices hold a
  `shared_ptr` for the life of a sequence, and a `SpinLock` serialises UI-thread slot mutation against
  the audio-thread trigger read — so loading or clearing a slot while it is sounding is safe.

---

## 9. UI Design decisions

All UI changes are logged here as they are decided, to keep the visual design reviewable in one place.

### 9.1 RattleGraph — sequence visualizer *(added v0.3)*

A 110 px tall live-preview component placed between the load/trigger buttons and the rattle parameter knobs.  It recomputes and redraws the sequence at **30 Hz** directly from the current APVTS parameter values (no audio-thread involvement).

**Amplitude bars**
- Vertical bars rise from the bottom of the component.
- Bar **height** = `velocity × (1 − decay × 0.95)^repeatIndex` — the same decay formula used by the DSP engine.
- Bar **X position** = cumulative pace timing, which reflects the Pace Curve (bars cluster right for accelerando, spread left for ritardando).
- Color: indigo gradient (matching the waveform display), alpha-faded on quieter repeats.

**Pitch curve overlay** (shown only when Pitch ≠ 0 or Pitch Curve Amount ≠ 0)
- An **orange line** connects each repeat's computed pitch value at its X position.
- Dots mark individual repeat positions on the line.
- A subtle **dashed center line** marks 0 semitones and acts as the pitch reference.
- Y-axis range: **±12 semitones** (top = +12, bottom = −12, center dashed = 0).
- When Pitch Curve Shape = *Linear* the line is straight between points; *Exp* produces a curve.

**Labels**: `+12 st` / `0` / `−12 st` on the left edge; `Time →` on the bottom-right corner.

**Sync grid** *(v0.8.1)*: in Sync mode the graph overlays the host subdivision grid (beat / 1/8 / 1/16 / finer lines) and places each impact bar on the grid via the shared `RattleParams::computeImpactOffsets()`, so the visual matches the engine exactly. Bars keep their decay heights with a small minimum so quiet impacts stay visible on the grid. Free mode is unchanged (gridless).

### 9.3 FFT spectrum view *(added v0.5.0)*

A real-time spectrum analyzer that shares the 80 px waveform display area in FX mode only. Toggled by a small **FFT** button (green when active) in the button row alongside Load / Trigger. When FFT is off the waveform view is shown; when on the FFT view replaces it — no extra screen real estate used.

**Display:**
- X axis: 20 Hz – 20 kHz, **log scale**. Grid lines at 50 / 100 / 200 / 500 / 1 k / 2 k / 5 k / 10 k Hz. Labels at 100 / 500 / 1 k / 5 k / 10 k.
- Y axis: **–80 to 0 dB**. Indigo gradient fill + lighter stroke.
- **Temporal smoothing** with ~150 ms time constant (coeff 0.80 at 30 Hz) so the display is readable without being sluggish.

**Signal source:** raw pre-filter input (before LP/BP/HP and Input Gain). Shows the full frequency content of the input so you can spot peaks and place the pre-filter accordingly.

**Overlays** *(added v0.5.1)*:
- **Amber vertical line** — marks the current pre-filter center frequency with a small frequency label above. Updates in real-time as the Filter Freq knob moves, making it easy to see which spectral peak the filter is targeting.
- **Amber dashed horizontal line** — marks the effective detection threshold: `dB = gainToDecibels(threshold / inputGainLinear × 0.5)`. The factor 0.5 accounts for the Hann-window normalization of the FFT magnitudes. When Input Gain is raised, the line moves down (lower dB = easier to trigger), giving an at-a-glance view of how much headroom each frequency peak has relative to the detection threshold. Labeled "THR" on the right edge.

**Implementation:** `juce::dsp::FFT` (order 11, `fftSize = 2048`). Audio thread writes mono-summed input to a 2048-sample ring buffer (`fftInputFifo`, `fftWritePos` atomic). FFTView reads a snapshot at 30 Hz, applies a Hann window, runs the forward FFT, applies gain-to-dB mapping, and renders the log-scaled path. The timer starts/stops with component visibility to avoid idle CPU cost.

**Peak display + gating** *(v0.8.2, peak-hold v0.8.3)*: the spectrum shows **peak** magnitudes — the previous 150 ms temporal smoothing was removed (tagged "PEAK" top-left). A **peak-hold** (instant attack, ~0.3 s per-bin decay) lets each transient peak linger so it can be read against the threshold line instead of flickering at the 30 Hz frame rate. Additionally, the audio thread only fills `fftInputFifo` when the spectrum is actually on screen: FFTView sets `RattleAudioProcessor::fftActive` (atomic) from its `visibilityChanged()` (and clears it in its destructor), and the per-sample fill is skipped when false — so no FFT feed work happens in Sample view or when the editor window is closed (the heavy FFT compute was already gated by the visibility timer).

### 9.2 LISTEN FILTER button *(added v0.4.1)*

A full-width toggle button at the top of the Input section (FX mode only). When active (amber):

- The pre-filtered mono signal is routed to both output channels instead of the rattle. Output level param still applies; Mix is ignored.
- Onset detection continues running internally so the detector state (envelope, re-trigger guard) stays warm — no discontinuity when toggling back.
- Rattle voices are not triggered while listening.

**Purpose:** diagnostic only. Lets the user hear exactly what the onset detector "sees" — enabling precise tuning of Filter Type / Freq / Spread before committing to Threshold and Sensitivity values. The button is deliberately not an automatable parameter (not in APVTS) and does not persist in plugin state.

### 9.4 Input / Sample view switch *(added v0.7.0)*

The FX "FFT" toggle is repurposed into a **two-state view switch** for the top section, so the panel reflects where you are in the workflow. A single button flips its label between **Input** and **Sample** (it names the view you'll switch *to*):

- **Input view** — the **FFT viewer** on the left with the full detection chain raised beside it on the right: Input Gain, Filter Type, Filter Freq, Threshold, Sensitivity, Spread, Velocity, and the LISTEN FILTER button. (The input section no longer lives at the bottom of the window.)
- **Sample view** — the **waveform** (with sample name + gain knob) on the left, and the **8 slot buttons** + **Load / Auto / Clear** on the right.

The Rattle / Pitch / Output sections stay fixed below both views. The manual **Trigger** button (FX) moves to the top header so it's reachable from either view. Relocating the input section frees the bottom rows, so the FX window shrinks to ~**560×860**. *(Instrument mode keeps its existing single layout for now — its Input/Sample workflow is a later pass.)*

### 9.5 Toggle-button groups *(added v0.7.0)*

All small enumerated selectors are rendered as **amber segmented toggle-button groups** (active segment filled amber, inactive segments dark) instead of drop-down combo boxes, for faster, more tactile switching: **Filter Type** (LP/BP/HP), **Pitch Curve Shape** (Lin/Exp), **Play Order** (Seq/Rnd), **Sample Iteration** (Trigger/Impact), **Pan Iteration** (Trigger/Impact). Each group is backed by the same `AudioParameterChoice` as before, so host automation is unchanged; only the on-screen control differs. (Many-option selectors such as the Inst MIDI Channel keep a combo box.)

### 9.6 Sample name + per-sample gain *(added v0.7.0)*

On the waveform display:
- **Sample name** — the loaded file's name (extension stripped, ellipsised if long) is drawn small, right-aligned, in the **top-right** corner.
- **Per-sample gain** — a small rotary knob in the **bottom-right** sets a ±20 dB trim for the selected slot (default 0 dB). It is per-slot **state** (serialized with the sample, not an automatable parameter) and follows the selected slot. Because the gain rides with the sample into the round-robin pool, each sample keeps its own level even in Impact mode where every hit may be a different sample.

### 9.7 Multi-file load & manual slicing *(added v0.7.0)*

- **Multi-file load** — the Load dialog allows selecting several files at once; files can also be **dragged-and-dropped** onto the plug-in. A batch fills **empty slots only**, left-to-right, in filename order (non-destructive). A **single** file dropped onto a specific slot button **replaces** that slot.
- **Manual slicing** — **double-click** an empty spot to add a slice marker (in addition to Auto-detect), or double-click a marker to delete it. Markers are kept sorted; left-drag moves a marker, and **dragging it off the waveform deletes** it. *(Right-click is reserved for garbage/mute — §9.9.)*

### 9.8 Trigger indicators & audition *(added v0.7.2)*

- **Trigger LEDs** — each slot button carries a small amber dot (top-right corner) that lights the moment an impact sourced from that slot fires, then fades over ~150 ms. The engine bumps a per-slot atomic counter on each impact (`SequenceVoice` → `RattleEngine::slotHits[8]`); the editor polls at 30 Hz and drives a `LedDot` overlay per slot. Shown in the Sample view (FX) / always (Inst).
- **Audition** — a speaker toggle (`ShapeButton`) next to Load. When enabled, clicking a slot button previews that slot's whole sample once at the slot's own gain (centred), so per-sample levels can be set by ear. The click sets an atomic slot index; the audio thread spawns a single one-shot voice into the wet bus (so in FX keep Mix > 0 to hear it; Inst is always wet).

### 9.9 Garbage slices & per-slot mute *(added v0.9.0)*

- **Right-click a slice** of a *sliced* slot → toggles that slice's **garbage** flag: it's drawn grayed with a big red **X** and skipped in playback (both Impact pool and Trigger walk). Serialized per slice; survives marker edits (matched by start position).
- **Right-click an *unsliced* sample** → toggles that slot's **mute** — an automatable `slotMuteN` `AudioParameterBool`, so a whole sample can be muted/automated as a performance/arrangement tool. A muted slot is grayed with a full-view X; the overlay tracks the param live (30 Hz poll).
- A slot with **no playable units** (muted, or all slices garbage) is skipped by the engine; if nothing is playable the trigger is silent. Both mechanisms are reflected by the slot's trigger LED staying dark.

---

## 10. Build plan (phased, each phase ends in something loadable/testable)

- **Phase 0 — Toolchain & scaffold.** Install MSVC build tools + CMake; clone JUCE; CMake project
  builds **two empty VST3s** that scan and load in a host (e.g. validator + a DAW).
- **Phase 1 — Skeleton signal path.** APVTS params, FX dry passthrough, MIX, output level; verify
  state save/load and automation.
- **Phase 2 — Sampling.** Load a file, embed in state, draw waveform; MIDI note (Inst) / manual test
  trigger plays the sample one-shot at PITCH.
- **Phase 3 — Rattle engine.** ✅ SequenceVoice with RATTLE/DECAY/PACE/PITCH CURVE; 8-voice allocator. RattleGraph visualizer. Pitch narrowed to ±12 st.
- **Phase 4 — Input section.** ✅ Inst: channel/range/SPREAD/VELOCITY. FX: pre-filter + onset detector
  + SPREAD band-Q + envelope-follower VELOCITY.
- **Phase 5 — Round-robin & slicing.** ✅ Multiple slots, RR order, multi-hit slice markers
  (auto + draggable), pan-spread.
- **Phase 6 — Sample workflow & round-robin overhaul.** *(in progress)* Multi-file load + drag-and-drop
  (empty-fill); sample name + per-sample gain; manual slice add; Play Order (Seq/Rnd); Sample
  Iteration & Pan Iteration (Trigger/Impact) with the unified per-impact slice pool; Input ⇄ Sample
  view switch; amber toggle-button groups. FX target.
- **Phase 7 — Content & polish.** Starter-pack samples, factory presets, UI styling, validation pass;
  Instrument-mode UI workflow pass.

---

## 11. Open items / defaults to confirm
These were defaulted by the drafter; flag any you want changed:
1. Numeric ranges in §5.2/§7 are proposals — tune to taste during Phase 3.
2. Max sample length before we down-convert/compress for embedding (proposed: warn > ~10 s).
3. ✅ Starter-pack: **Simon provides the samples** (confirm they're redistributable for bundling).
4. ✅ UI: **fixed-size window to start**. Visual style still TBD — propose during Phase 1/6.
5. ✅ Inst **MIX hidden & forced 100%**; **Output Level** is the Inst level control.
6. ✅ Multi-hit slices: ordered sub-sequence (Trigger iteration) **plus** round-robin-per-slice switch (Impact iteration) — delivered in Phase 6 (§5.1).
7. ✅ SPREAD unified as **center + width** in both modes (FX: filter freq + Q/width; Inst: center
   note + note-window width). Replaces the old Note Low/High pair. Asymmetric note ranges dropped
   for now — revisit only if needed.
