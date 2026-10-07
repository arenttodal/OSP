# SPACE v2, ECHO and SHAPER CUSTOM

This answers four requests:

1. **SPACE.**
   - A reverb with more than a length: high and low roll-off, drawn as a filter over the tail
     the way Ableton's reverb shows its EQ, plus the other key reverb controls.
   - Better algorithms: a lush, "Lexicon"-style tail, more realistic rooms, and HALL instead of
     CHAMBER, after the Berlin concert halls.
2. **SHAPER.**
   - A CUSTOM pattern you build yourself, as a step sequencer or drawn.
   - A magnifier for a large editor.
   - Saved patterns.
3. **ECHO.**
   - A tape and a bucket-brigade (BBD) delay, in parallel with the reverbs.
   - Where it belongs (perhaps its own macro, with its own colour, in the room the amp envelope
     gives up).
   - Options, a recommendation, and the implementation.

Everything below is implemented, tested and on the branch.

## 1. ECHO: where it lives (the options)

| | Placement | For | Against |
|---|---|---|---|
| **A** | **A sixth macro, ECHO**, between MOVEMENT and SPACE, with its own popover and colour | One knob, one idea: the same as every other macro. The send level is a performance control (automate it, map it to CC 26). It is parallel to SPACE by construction. The macro row was the obvious room (the envelope did not need its 506 px). | Six macros instead of five; the amp envelope is 450 px wide instead of 506. |
| B | Inside SPACE's popover (REVERB / ECHO tabs), the SPACE knob sending to both | No new macro | One knob for two effects (you cannot have a big room and a little echo, or the other way round); the delay is hidden two clicks deep. |
| C | A MOVEMENT mode | Reuses MOVEMENT's popover | MOVEMENT's modes are exclusive (no echo with DRIFT or SHAPER), and an echo is not movement. |
| D | Advanced | Out of the way | Nobody finds it, and you cannot perform with it. |

**Implemented: A.**
- **ECHO's colour is verdigris** (#5c9490): the patina on an old tape machine's copper. It
  sits at the same lightness as the other five (CIE L* 57.5) and is distinct from MOVEMENT's
  mineral blue and CHARACTER's moss.
- **ECHO sits before SPACE on screen** (the room stays last), and its data index is the sixth.
- **The macro at 0 % is exactly the old sound** (a test checks it), and older sessions open
  with ECHO off.

## 2. ECHO: the two machines

Both run in parallel with SPACE.
- **Signal path.** Both hear the dry sound after MOVEMENT. Neither feeds the other.
- **Macro.** The ECHO macro is the send level, perceptual: 50 % sits the repeats just under
  the dry sound.
- **Controls:**
  - TIME: free 20–1500 ms, or synced 1/16 to 1 bar with dotted and triplet values, following
    the host tempo.
  - FEEDBACK, TONE and AGE.
  - FREE / SYNC.
  - MONO / PING-PONG / WIDE.

**TAPE** (RE-201 / Echoplex family):
- **Record head.** Every pass is soft-saturated with a little bias, which adds even harmonics.
  The drive rises with AGE and with FEEDBACK.
- **Playback.**
  - A head bump of a few dB around 110 Hz.
  - A low cut that rises with wear.
  - A high cut that TONE moves (2.2–12 kHz, lower with AGE).
- **Repeats.** Each repeat is a little darker and thinner than the last.
- **Wow and flutter.**
  - AGE sets wow: a seeded random walk around a 0.8 Hz sine, 0.08–0.6 % pitch deviation.
  - AGE also sets flutter: 7.1 Hz, 0.03–0.28 %.
  - Moving TIME changes the tape speed, so the repeats glide in pitch, as on the machine.
- **FEEDBACK 100 %.** It runs away into warm compression, never into overload; a test checks
  that the peak stays bounded.

**BBD** (DM-2 / Memory Man family):
- **Clock and bandwidth.** The clock that sets the time also sets the bandwidth: 8192 stages,
  bandwidth about 0.38 × the clock / 2. Long times are darker, as on the real chip.
- **Filters.** A steep 4th-order anti-alias filter sits around the line, then the soft clip.
- **Character.** TONE sets the tone. Repeats are darker and grainier than tape. AGE adds the
  slow (0.45 Hz) chorus those pedals put on their repeats.

**Picture.** The ECHO display draws the sound at 0 and every repeat as a stroke:
- Each stroke is as loud as the feedback leaves it, and darker for every pass.
- PING-PONG alternates up (left) and down (right); WIDE splits two heads.
- TAPE's repeats smear (wow ghosts); BBD's speckle.
- Synced, the grid is the beat, and the header shows the note value and its ms.

**Measured** (impulse, 300 ms, FEEDBACK 60 %, PING-PONG):
- Repeats land at 301 / 602 / 902 ms, alternating sides.
- The level falls 3–4.5 dB per repeat.
- The BBD's spectral centroid falls 2385 → 1800 Hz over six repeats.

## 3. SPACE v2

### The engines

ROOM, HALL and PLATE now share one late network, built the way the classic studio halls are
lush.
- **Input diffusion.** Four allpasses per side, so the input is true stereo.
- **The network.** An 8-line feedback delay network whose lines each hold an allpass, so echo
  density builds inside the loop as well as before it.
- **The moving read.** Every line is read through Hermite (cubic) interpolation at a slowly
  moving position: a sine per line at its own rate (0.3–1.2 Hz) plus a seeded random wander.
  - The tail chorus is rich and never metallic.
  - Cubic rather than linear reads keep the moving lines bright.
- **Decay.** A two-band decay per line gives a low, mid and high reverberation time from:
  - DECAY;
  - the type's bass ratio;
  - DAMP.

  The structure stays stable for any setting; the analysis is in the code.
- **Input EQ.** Two 12 dB/oct Butterworths before everything, as on a hardware return.

The four types:
- **ROOM.** The early reflections are the image sources of a real shoebox: 5.2 × 4.1 × 2.9 m
  at SIZE 50 %, first and second order, 78 % pressure kept per bounce, spreading as 1/r. Their
  side decides their pan. They are followed by a short, dense tail with slightly more bass than
  mid.
- **HALL** (after the Berlin halls for orchestra).
  - About a 19 ms initial gap.
  - Strong lateral early reflections out to ~115 ms, alternating sides: the vineyard
    terraces of the Philharmonie, the shoebox walls of the Konzerthaus.
  - A soft build into a long, warm tail: bass RT 1.2 × mid, air absorbing the top.
  - The slow motion of a full hall.
- **PLATE.** No early reflections. The diffused sound is heard at once (instant density),
  bright, lean in the bass (0.8 × mid), wide, and the most modulated.
- **SPRING.** Kept: the dispersive allpass tank. It gains PRE-DELAY, SIZE (the spring
  lengths) and the EQ.

### Measured: old engine against the new one

Impulse responses at DECAY 2.2 s (ROOM 1.0 s), EQ open.
- **RT** is per octave (Schroeder T20).
- **Echo density** (NED) is ≈ 1 once the tail sounds like noise.
- **IACC** is the left/right correlation of the late tail (lower = wider).
- **Ripple** is the fine structure of the time-averaged tail spectrum. Persistent resonances
  (metallic ringing) keep it high; lower is smoother.

| | RT 125 Hz | RT 1 kHz | RT 8 kHz | NED at 20 / 50 / 100 ms | IACC | Ripple |
|---|---|---|---|---|---|---|
| ROOM before | 0.99 | 0.84 | 0.17 | 0.90 / 0.96 / 0.96 | 0.45 | 4.39 dB |
| ROOM now (DAMP 40 %) | 0.87 | 1.00 | 0.64 | 0.99 / 0.92 / 1.00 | 0.26 | **4.03 dB** |
| CHAMBER before | 2.30 | 2.03 | 0.57 | 0.54 / 0.97 / 0.98 | 0.19 | 2.65 dB |
| **HALL** now | **2.52** | 2.07 | 1.21 | 0.18 / 0.59 / 1.02 (a hall's soft build) | **0.06** | **2.39 dB** |
| PLATE before | 1.98 | 2.20 | 0.88 | 0.94 / 0.90 / 0.98 | 0.01 | 2.49 dB |
| PLATE now | 1.69 | 2.12 | 1.69 | 0.95 / 1.05 / 0.98 | 0.01 | **2.38 dB** |

What the numbers show:
- **Smoother.** Every type's tail is now smoother than its predecessor.
- **HALL.**
  - Its bass outlasts its air, by design (a test checks the ratio).
  - It is far wider (IACC 0.06).
  - It builds in like a hall rather than arriving dense.
- **ROOM** keeps its decay at 1.0 s and is no longer prematurely dull: DAMP sets that now.
- **PLATE** answers 2 ms after the pre-delay instead of 13.6 ms.
- **Levels.** They are matched across the types: −6.9 to −7.5 dB wet/dry at SPACE 50 % (the
  old engine ranged −5.2 to −8.9). Existing patches do not jump in level.

### Controls and display

The popover is 300 px wide:
- TYPE (ROOM / HALL / PLATE / SPRING tabs).
- PRE-DELAY (0–250 ms), SIZE, DECAY and DAMP on the first row.
- MOD, WIDTH, LOW CUT (20–2000 Hz) and HIGH CUT (1–20 kHz) below.

**The EQ is drawn on the tail itself.**
- The response is a line across the display (20 Hz–20 kHz, log), and what it removes is
  veiled.
- Its two corners are handles: drag them; double-click resets.
- LO / HI values sit above the handles; while dragging, they read LOW CUT / HIGH CUT.

**The tail picture now uses the real room:**
- the actual early-reflection pattern of ROOM (image sources) and HALL;
- PRE-DELAY's gap, SIZE, DAMP (how fast the colour cools) and WIDTH.

**What moves smoothly.**
- TYPE and SIZE crossfade to a freshly configured reverb over 250 ms. SIZE waits until the
  knob rests for ~60 ms.
- DECAY retunes gradually.
- PRE-DELAY glides; the EQ and MOD are smoothed. Nothing clicks.

## 4. SHAPER CUSTOM

**Choosing CUSTOM.**
- PATTERN's list ends with **CUSTOM** and a **SAVED** submenu.
- Picking a library pattern leaves CUSTOM.
- CUSTOM is its own parameter (`movement.shaper.custom`), so the pattern list and its host
  automation are unchanged.

**The display is the editor.**
- With CUSTOM, click or drag on it to set steps.
- Editing a library pattern turns it into CUSTOM, starting from that pattern.
- Right-click cycles a step's shape.

**The magnifier** (top right of the display) opens the large editor: 580 px wide, the same
popover. It shows step bars, beat numbers, the contour as heard (SMOOTH and DEPTH applied) and
the playhead. Its toolbar:
- **STEP** (one level per step, shaped by the brush: HOLD, FALL, RISE, PULSE, DIP, SOFT) or
  **DRAW** (a free line: each step's start and end follow the pen, rounded).
- **COPY ▾:** start from any library pattern.
- **CLEAR.**
- **SAVE…** to Documents/OSP/Shaper Patterns, as `*.ospshaper` JSON, schemaVersion 1.
- **LOAD ▾:** the saved patterns, and "Show folder".

**Behaviour.**
- Each gesture is one undo step.
- The pattern is part of the session and of presets (state property `shaperCustom`).
- On the audio thread it arrives as atomics: no locks, no allocation. A changed pattern
  re-shapes the playing one without clicks, behind the existing 2 ms ramps.

## 5. Compatibility

**New state and parameters.**
- **State version 10.**
- **Parameters at version hint 12:** `space.preDelay`, `space.size`, `space.damping`,
  `space.modulation`, `space.width`, `space.lowCut`, `space.highCut`, `echo`, `echo.type`,
  `echo.sync`, `echo.division`, `echo.time`, `echo.feedback`, `echo.tone`, `echo.age`,
  `echo.stereo`, `movement.shaper.custom`.

**Older sessions.**
- CHAMBER's index opens as HALL.
- The new EQ opens fully (20 Hz / 20 kHz), so nothing is filtered that never was.
- ECHO is off.
- CUSTOM starts as THREE.

**What changes for them.**
- The sound of their SPACE changes, because the engine is new; that is what was asked. The
  levels are matched.
- The REIMAGINED reference scenes include SPACE, so their stored levels were regenerated (see
  the commit).

**Other.**
- CC 26 is ECHO (CC 20–25 as before).
- The research renderer reads the new `space` keys, an `echo` block, and `"chamber"` as HALL.

## 6. CPU

Measured with callgrind (instructions per second of audio) for the plugin's whole
`processBlock`: 16 held notes, 48 kHz, 128-sample blocks.

| Case | Instructions / s | What the effect adds |
|---|---|---|
| SPACE 0 (no reverb), old / new | 894 M / 895 M | (none) |
| SPACE on, PLATE, old engine | 946 M | 52 M |
| SPACE on, PLATE, new engine | 995 M | **100 M** |
| SPACE on, HALL, new engine | 1006 M | 111 M |
| ECHO 50 TAPE / BBD (SPACE on as well) | 1034 M / 1035 M | **~38 M** |

**What it costs.**
- **The reverb costs about twice the old one.** That is the price of four extra diffusers,
  the allpass in every loop line, cubic moving reads and the two-band decay. It is about
  1 % of a core on the VM, ~10 % of the default patch's total.
- **ECHO costs less than half the reverb** (≈ 0.4 % of a core).

**What it saves.**
- **Off is free.** With the macro at 0, the delay isn't run at all.
- **Asleep in silence.** Like the reverb, ECHO goes to sleep in silence: once nothing has
  come in for longer than the line holds and the repeats are below −120 dBFS, only its clocks
  move.
- **Idle stays at ~0.2 %.**

## 7. What is not done yet

- **A listening round.** The metrics say smoother, wider and better behaved, but the ear
  decides. A HALL vs CHAMBER and PLATE vs PLATE A/B, with the standard fixtures, belongs in
  the next lab.
- **ECHO into SPACE** (repeats that bloom in the room). It is a few lines; the request was
  for parallel, so it is off. It would be a small SEND control in ECHO's popover.
- **SHAPER CUSTOM's length** is the 16 steps of the library patterns (one bar at 1/16). Odd
  lengths (polymeters) are a natural next step.
- **A freeze / infinite hold for SPACE**, and a tap-tempo for ECHO's FREE time.
