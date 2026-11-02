# SPACE: three more reverb engines (plan)

Status: R1 (ABYSS) is built. VINTAGE and BLOOM are still a plan. ABYSS was built leaner than
section 4 describes:
- ROOMS stays in `engine/SpaceReverb`. ABYSS is `engine/AbyssReverb`, and PostProcessor's two
  crossfading slots each hold both engines.
- The new parameters are appended with version hint 23 rather than a state-version bump: a
  session without them opens as ROOMS, in ANDOR OSP and now in the shell too.
- In the popover, ABYSS is picked from the same list as the rooms. FLOW / FREEZE replaces the
  tabs, and GRAVITY and RESONANCE replace LOW and HIGH CUT, which remain on the display's handles.
- ABYSS's variants (DEEP / DARK / INVERSE / ENDLESS) are not built yet. Applies to ANDOR OSP and every shell product that
uses OSP's shared effects; ANDOR ANALOG is the first.
Date: 2026-11-02.

## 1. Goal

SPACE should have three reverbs with their own character, next to the four rooms it has today
(ROOM, HALL, PLATE, SPRING):

| Working name | Inspired by | Character in one line |
|---|---|---|
| **VINTAGE** | the early-1980s digital hall (the Lexicon 224 / 224X family) | grainy, warm, slightly lo-fi, a tail that chorus-shimmers instead of ringing |
| **BLOOM**   | a diffusion delay turned reverb (the "Diffusion" side of Native Instruments' Replika XT) | the repeats smear into a wash; tempo-synced, can be clean, vintage or tape |
| **ABYSS**   | an enormous modulated FDN space (the Eventide Blackhole idea) | huge, dark or inverted ("gravity"), freezes, never stops being musical |

Product names, preset names and UI text must never use "Lexicon", "224", "Replika" or
"Blackhole". Those are other companies' trademarks; they appear only in this internal doc,
as reference points.

## 2. Ground rules (legal and engineering)

**Nothing is copied.**
- `joelanders/reflecticon-224` has **no license**, so all rights are reserved. It is also a
  firmware emulator: an 8080 CPU and the TTL microcode machine, which run Lexicon's own ROMs.
  Those ROMs are not in the repo and are copyrighted.
  - We only read it for **published facts about how the hardware behaved** (word lengths,
    rates, coefficient grids, which tricks give the sound).
  - Its code, microcode tables and coefficient lists are never transcribed.
- The Blackhole spec we were given was reverse-engineered from a commercial binary. Its plugin
  licence most likely forbids that.
  - We use only the **textbook ideas** it confirms: a feedback delay network with a
    Householder matrix, EQ in the loop, modulated delays, a limiter in the loop and freeze.
    These are in Jot (1991), Schlecht & Habets, Dattorro (1997) and others.
  - We choose **our own** delay lengths, matrix sizes, EQ corners and constants. The extracted
    numbers (the 60.0–84.0 ms line set, the 87.2–153.2 / 90.6–159.4 ms stages, the 350 Hz /
    2 kHz corners) must not appear in our code.
- Replika XT is a closed product. We use only what its manual and the public demos describe.

**The CLAUDE.md rules apply as for every DSP feature:**
- **Real time:** `prepare()` allocates every engine's buffers, and `process()` never allocates,
  locks or does I/O.
- **Determinism:** every random modulation comes from `osp::Prng` with a stored seed, so bounces
  repeat exactly.
- **Headless first:** each engine renders through `research-renderer` and unit tests before any
  UI exists.
- **Preserve:** the four existing rooms stay bit-exact, and old sessions and presets sound
  exactly as before.
- **A/B:** every engine is compared with the current HALL and PLATE using the standard fixtures,
  plus baselines A and B on the instrument side.

## 3. Where it plugs in today (audit)

- `src/engine/SpaceReverb.{h,cpp}` is one class (627 lines).
  - ROOM, HALL and PLATE share an 8-line FDN with an allpass in each line, input diffusion and a
    two-band decay. SPRING is a dispersive allpass loop.
  - API: `prepare` / `reset` / `configure` (structural: type and size) / `tune` (smooth) /
    `process` (wet only) / `skip` (asleep), plus `portrait()` for the display.
- `src/engine/PostProcessor` holds **two** `SpaceReverb` instances and crossfades when the
  settings are `structurallyDifferent`.
  - It puts the reverb to sleep after 0.25 s below −120 dBFS.
  - The SPACE macro is the send level.
- **Settings** live in `Shaping` (`spaceType`, decay, pre-delay, size, damping, modulation,
  width, low cut, high cut).
  - OSP parameters: `space.type` (a 4-way choice) and `space.decay`, plus the v12 ids
    `space.preDelay` … `space.highCut`.
  - ANALOG reads the same ids (`AnalogParameters`, `iSpaceType` …), and its patches carry
    `Patch::effects` (a `Shaping`).
- **UI:** the shared SPACE popover (`shared/andor-ui/Source/MacroPopups.cpp`) has a header
  dropdown (type), the impulse display with the LOW/HIGH CUT handles, a four-tab type row and
  eight knobs.
- **Defaults (as of 2026-11-02):** HALL, 3.0 s, HIGH CUT 5 kHz.

## 4. Architecture

### 4.1 One more parameter: the ENGINE

- **Do not grow `space.type`.**
  - Hosts automate a choice parameter on its normalised value, so going from 4 to 7 entries
    would move every recorded automation lane.
  - Instead add **`space.engine`**: ROOMS / VINTAGE / BLOOM / ABYSS, with ROOMS as the default.
  - `space.type` stays the variant *within* ROOMS, exactly as now.
- Each new engine gets a **variant** choice of its own, plus at most three controls of its own:

| Engine | Variant (`space.<engine>.variant`) | Own controls |
|---|---|---|
| VINTAGE | HALL, PLATE, CHAMBER, ROOM | `space.vintage.model` (EARLY / LATER: the lower or the higher internal rate), `space.vintage.diffusion` |
| BLOOM   | CLEAN, VINTAGE, TAPE, WASH | `space.bloom.time` (synced division or ms), `space.bloom.sync`, `space.bloom.diffusion` |
| ABYSS   | DEEP, DARK, INVERSE, ENDLESS (starting points) | `space.abyss.gravity` (−100..+100 %), `space.abyss.freeze`, `space.abyss.resonance` |

- The eight shared controls (PRE-DELAY, SIZE, DECAY, DAMP, MOD, WIDTH, LOW CUT, HIGH CUT) keep
  their ids **and their meaning** on every engine. Each engine maps them, as in section 5.
- One sound never needs two parameter sets. Switching engine keeps the shared eight, so a
  user's HIGH CUT 5 kHz stays 5 kHz.

### 4.2 DSP shape

```
src/engine/space/
  SpaceEngine.h          the common API (as SpaceReverb's today) + Portrait
  RoomsReverb.{h,cpp}    today's SpaceReverb, moved and renamed: bit-exact
  VintageReverb.{h,cpp}
  BloomReverb.{h,cpp}
  AbyssReverb.{h,cpp}
  SpaceSlot.{h,cpp}      one of each, preallocated; routes to the selected engine
```

- **`SpaceSlot`** replaces each of PostProcessor's two `SpaceReverb`s.
  - It owns all four engines, each prepared in `prepare()`.
  - `configure()` resets the engine it will play.
  - A change of engine counts as "structurally different", so the **existing crossfade**
    between the two slots handles it without a click.
  - The sleep logic and the `skip()` contract stay unchanged.
- **No virtual dispatch per sample.** `SpaceSlot::process` switches on the engine once per
  block, and each engine runs its own sample loop.
- **Memory:** about 3 extra delay sets per slot.
  - ABYSS is the largest: a 16-line FDN plus two 8-line stages, up to about 0.5 s per line at
    SIZE 100 % at 96 kHz, which is roughly 32 × 48k floats ≈ 6 MB per slot.
  - That is acceptable. A product that does not need an engine can opt out of preparing it
    (`ProductInfo`), so its memory is never allocated.
- **Portrait:** each engine fills the existing `Portrait` (early reflections, damping, width,
  diffusion time), so the display draws it with no new UI code.

## 5. The three engines

### 5.1 VINTAGE (early-1980s digital hall character)

The facts that make that sound (from public descriptions of the hardware, confirmed by the
emulator's documentation, never from its code):

- A **low internal rate**: about 20 kHz on the early model and about 34 kHz on the later one.
  Steep analog anti-alias filters sit around it, so there is no air above about 10 kHz (early)
  or 15 kHz (later).
- **Coarse coefficients:** gains on a 1/32 grid. Allpass and decay gains are therefore
  quantised, which gives the "stepped" feel when DECAY moves.
- **Short words:** a 16-bit delay memory, a saturating accumulator and a gain-ranged 12-bit
  converter, so the quiet tail grows a gentle grain.
- **Chorus by tap crossfading:** two adjacent delay taps whose gains sum to one, crossfaded
  slowly and randomly. The tail shimmers without the pitch wobble of an LFO-modulated read.
- **Two mirrored halves** (left and right "tanks" of delays and allpass cells) cross-fed into
  each other: wide, with no ring.

**Our design (original):**
- A Dattorro-style figure-eight tank in two halves.
  - Each half has 4 delays and 3 nested allpasses, with our own prime lengths.
  - Inputs cross-feed through a gain on the 1/32 grid.
- It runs at a **fixed internal rate**: 20.48 kHz (EARLY) or 34.13 kHz (LATER).
  - Polyphase half-band or Kaiser resampling sits on each side of it. OSP's `SincInterpolator`
    kernel can serve, prepared once.
  - In front of it is a 7th-order elliptic low-pass at 0.45 × the internal rate (EARLY about
    9.2 kHz), as the anti-alias board did.
- **Quantisation, applied in the loop and switchable for A/B:**
  - gains rounded to n/32;
  - the delay memory rounded to 16 bits, with TPDF dither from `osp::Prng`;
  - the input through a 12-bit gain-ranging stage (4 ranges, 6 dB apart).
- **CHORUS (MOD):** tap-pair crossfades on 4 of the 8 tank delays, moved by seeded random
  targets at about 0.5–2 Hz. It needs no fractional reads, so it is cheap.
- **Mapping of the shared controls:**
  - DECAY sets the mid RT60 (0.6–70 s on the original; we clamp to SPACE's range).
  - DAMP sets the high RT relative to the mid ("TREBLE DECAY").
  - SIZE scales the delay lengths 0.5–1.6×.
  - PRE-DELAY, WIDTH, LOW CUT and HIGH CUT work as in ROOMS.
  - `diffusion` sets the input allpass gains (n/32).
- **CPU target:** at most 1.3 × today's HALL. The low internal rate pays for the resamplers.

### 5.2 BLOOM (diffusion delay to wash)

The idea: a stereo delay whose feedback path carries a growing chain of allpass diffusers. At 0
the repeats are clean echoes; as DIFFUSION rises they smear until the delay is a reverb that
pulses at the tempo.

**Our design (original):**
- Two delay lines (L and R) with ping-pong cross-feed. In each feedback path:
  - 4 to 8 allpasses, their gains set by `diffusion`, their lengths spread over 1–40 % of the
    delay time and modulated very slowly;
  - a one-pole tone filter;
  - the variant's character stage.
- **Variants (the character stage):**
  - **CLEAN:** nothing added. The repeats are as clean as ECHO's.
  - **VINTAGE:** BBD-style band limit and companding hiss. It reuses ECHO's BBD code path:
    the same `EchoDelay` building blocks, prepared separately.
  - **TAPE:** ECHO's tape wow and flutter and saturation in the loop.
  - **WASH:** diffusion pinned high, plus a second diffusion stage *after* the loop. Almost no
    discrete repeats: an ambient reverb that still breathes at the tempo.
- **Mapping of the shared controls:**
  - DECAY sets the feedback, as a time for the wash to fall 60 dB, so it reads like a reverb.
  - SIZE scales the diffuser lengths.
  - `time` / `sync` set the delay, synced to the host tempo with ECHO's division table.
  - DAMP is the loop's tone.
  - MOD is the diffuser and tape modulation.
  - PRE-DELAY, WIDTH, LOW CUT and HIGH CUT work as in ROOMS.
- **Its relation to ECHO:** ECHO stays the discrete delay send. BLOOM is SPACE's own tempo
  reverb, and both can play at once.
- **CPU target:** at most 1.0 × HALL.

### 5.3 ABYSS (a vast modulated FDN)

**Our design (original; the textbook structure, our own numbers):**
- **Input:** a pre-delay, then 4 allpass diffusers per side.
- **Stage 1:** a 16-line FDN with a Householder feedback matrix (O(N): one sum, no matrix
  multiply).
  - The line lengths are our own: 16 mutually prime lengths from a geometric series, scaled by
    SIZE.
  - Each line has a two-band shelf decay (LOW, MID, HIGH RT from DECAY and DAMP) and a
    modulated read: a slow sine plus a seeded wander, as ROOMS's "lush" network.
- **Stages 2 and 3:** two 8-line FDNs (L and R) with longer lengths, fed from stage 1. They
  give the second, larger "bloom" of the space.
- **GRAVITY** (−100..+100 %):
  - At 0 the decay is normal.
  - Positive values lengthen the tail and darken it in the loop.
  - Negative values **invert the envelope**. The input feeds the network through an
    exponentially *rising* gain window (a reverse envelope) and the loop decays fast, so the
    tail swells towards the end instead of starting loud.
- **FREEZE:** the input to the loop is muted and the loop gain set to 1 (an energy-preserving
  Householder, losses bypassed).
  - Modulation keeps it alive, so it never sounds static.
  - Release returns to the set DECAY over about 200 ms.
- **RESONANCE:** a gentle peak in the loop EQ. The corners are our own and track SIZE.
- **Safety:** a soft limiter inside the loop (stereo-linked, about −1 dBFS), plus a feedback
  ceiling of 0.9995 except under FREEZE.
  - A NaN or Inf guard resets the network and counts the event. A test asserts the count stays
    0.
  - Denormals are flushed (ROOMS's guard).
- **Mapping of the shared controls:**
  - SIZE sets the lengths, from 0.25× to a 4× "hole".
  - DECAY sets the mid RT, up to the 8 s range; a later range extension is listed under open
    question 4.
  - DAMP is the HIGH RT ratio. MOD sets depth and rate together, on one curve.
  - WIDTH, LOW CUT and HIGH CUT work as in ROOMS.
- **CPU target:** at most 2.0 × HALL. It is the heaviest engine, so it is the first candidate
  for SIMD (4 lines per vector, as `Float4`).

## 6. UI

- **The header dropdown** (top right of the SPACE popover, "HALL ˅" today) becomes the
  **engine** list in two groups:
  - ROOMS: Room, Hall, Plate, Spring;
  - CHARACTER: Vintage, Bloom, Abyss.
  - Choosing a room sets `space.engine = ROOMS` plus `space.type`.
- **The tab row** shows the selected engine's variants (4 each, so the layout never moves).
- **Knobs:** the eight shared knobs stay where they are. An engine's own controls (at most
  three) replace WIDTH's and MOD's row positions with their captions, or go in a third small
  row. This is to be decided with screenshots.
  - FREEZE is a key in the header beside the dropdown, shown only on ABYSS.
- **The display** draws each engine's portrait. ABYSS with negative GRAVITY draws a rising
  envelope.
- The same component serves OSP and ANALOG, as today. **OSP must stay pixel-identical while
  ROOMS is selected.**

## 7. State and presets

- **OSP:** stateVersion 14 → 15.
  - New ids are added with version hint 15. Missing in an older state means ROOMS, so every
    old session plays exactly as before.
  - Each new engine's controls default to that engine's own sensible defaults.
- **ANALOG:** stateVersion 8 → 9.
  - The new ids are appended to `AnalogParameters` (`iSpaceEngine` …), with `version9Ids`.
  - `Shaping` gains the fields, so `Patch::effects` carries them, and factory patches can set
    them.
- **Research configs:** `space.engine` and the per-engine keys in `RenderConfig`, documented in
  `docs/analysis-schema.md`.
- **Presets:** 4 per engine in each product, plus a few ANALOG factory sounds rebuilt around
  them (a VINTAGE hall pad, a BLOOM synced arp, an ABYSS drone).

## 8. Tests and evidence

**Unit tests, per engine:**
- **RT60:** the measured decay is within ±15 % of DECAY over its range, on the 500 Hz and
  2 kHz bands.
- **Stability:** 10 minutes of pink noise at maximum DECAY and SIZE (and FREEZE for ABYSS).
  The peak never exceeds the limiter, with no NaN or Inf.
- **Determinism:** two renders with the same seed are identical, and a different seed gives a
  different render.
- **Real time:** a run under the allocation guard (ANALOG's `RealtimeTests` pattern; OSP's
  `PostProcessorTests` allocation check) allocates nothing.
- **Freeze:** energy stays within ±1 dB over 30 s, and the tail decays again after release.
- **Gravity < 0:** the envelope peaks later than 40 % of DECAY.
- **Switching:** engine and variant changes while playing cause no clicks (the existing
  crossfade test, extended).
- **ROOMS stays bit-exact:** today's goldens are unchanged after the move to `src/engine/space/`.

**Listening:**
- A/B renders of each engine against HALL and PLATE: the standard fixtures, a pad, a plucked
  arp and drums.
- Also on an ANALOG patch (a Juno pad).

**CPU:**
- `research-renderer --benchmark` per engine, at 48 and 96 kHz.
- The ANALOG worst case is re-measured (it is 41.5 % today, 16 voices at 4×).

## 9. Phases

| Phase | What | Done when |
|---|---|---|
| R0 | `src/engine/space/` with ROOMS moved, `SpaceSlot`, `space.engine` (ROOMS only), state v15 / v9, dropdown grouping | all goldens and OSP pixels unchanged; old sessions load the same |
| R1 | **ABYSS** (the most specified; the biggest wow) | tests in section 8, A/B renders, CPU ≤ 2 × HALL |
| R2 | **VINTAGE** (internal rate, quantisation, tap chorus) | same, plus A/B of its quantisation on and off |
| R3 | **BLOOM** (shares ECHO's tape and BBD blocks) | same, plus tempo sync tests |
| R4 | UI polish, presets, ANALOG factory sounds, docs, STATUS, Mac/Windows builds | listening sign-off |

Each phase is one PR per repo. OSP's comes first; ANALOG then pins the submodule.

## 10. Open questions for the product owner

1. Names: are **VINTAGE / BLOOM / ABYSS** good, or do you want others?
2. Should the engine-specific controls (GRAVITY, FREEZE, TIME, …) go in a third row, or
   replace MOD and WIDTH on those engines?
3. Should BLOOM's tempo sync follow the host only, or also have free ms like ECHO?
4. Should DECAY's range extend beyond 8 s for ABYSS (say to 60 s on a second curve)?
