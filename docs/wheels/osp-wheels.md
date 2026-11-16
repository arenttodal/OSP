# PITCH, MOD WHEEL and ADVANCED in ANDOR OSP

_16 Nov 2026. Branch `claude/osp-wheels`._

ANDOR OSP's current line now has the wheels' popovers and VIBRATO that ANDOR ANALOG has. The
PITCH BEND, MOD WHEEL and ADVANCED popovers are rebuilt at the scale of the macro popovers. The
popover code is shared (`shared/andor-ui`), so ANDOR ANALOG gets the same panels from its OSP
dependency.

## Gate 0: the audit

| Line | What it had |
|---|---|
| OSP `feature/aux-live-input` (`cd2291d`) | `WheelPopups.*`, `src/engine/Vibrato.h`, `vibrato.*`, the PITCH / MOD captions as doors (click versus drag), per-voice vibrato, the tests (`VibratoTests.cpp`, the `[wheels]` case). |
| ANDOR ANALOG `feature/aux-live-input` (`0ff1d47`) | Pins OSP `cd2291d`. It adds `analog.vibrato.sourceSub`, screenshots, demos and `docs/WHEELS_VIBRATO.md`. It has no popover-size work newer than the wheel line: the panels in its screenshots are the 420-unit-wide versions. |
| OSP default (`f9ccc23`) | SUB, the short-note LOOP fallback, Learn 2.0 and SATELLITE, but no wheels. |

The wheel line (`9c97d21..cd2291d`) is merged in; it is not a wholesale switch to the older
branch:
- **Kept:** SUB, LOOP's fallback, Learn 2.0 and SATELLITE.
- **Popup indices:** SATELLITE and SUB keep 13 and 14; PITCH and MOD take 15 and 16.
- **Parameters:** `vibrato.*` (version hint 28, ANDOR ANALOG's) is appended after SUB, so nothing
  before it moves.
- **Routes:** the destination list (`destExt`) gains VIBRATO DEPTH and VIBRATO AMOUNT at its end.
  Saved route indices are unchanged. A host's *normalised* automation of a `destExt` choice
  would shift, because the choice has more entries, as it does in ANDOR ANALOG. Sessions store
  the index, so they recall exactly.

## Gate 2: one popover scale

All sizes are reference px (design units), as the editor shows them:

| Popover | Card | Knob cell | Style |
|---|---|---|---|
| LIFE / DRIVE / FILTER / MOVEMENT | 232-270 x 170-214 | 80 x 58 | compact |
| ECHO / SPACE | 286-300 x 221-275 | 65-69 x 58 | compact |
| PITCH BEND, before | **436 x 350** | 160 x 152 | window (title, subtitle, close), boxed knobs |
| MOD WHEEL, before | **610 x 634** | 113 x 152 | window, boxed knobs |
| ADVANCED, before | **436 x 675** | 194 x 152 | window, boxed knobs |
| PITCH BEND, now | 280 x 142 | 80 x 58 | compact |
| MOD WHEEL, now | 340 x 256 | 63 x 58 | compact |
| ADVANCED, now | 340 x 249 | 72 x 58 | compact |

The old panels were laid out at a 0.69 unit and scaled up 1.45×. That is why their type, knobs
and margins were all larger than the macro popovers'.

**The shared measures** (`ShapingPopups.h`, `namespace popover`):

| Measure | Value |
|---|---|
| title row | 32 (13 px title, the selector on the right) |
| sides | 12 |
| bottom | 10 |
| gap | 8 |
| between rows | 6 |
| a row of tabs / keys | 21 |
| caption | 12 |
| knob cell | 58 (at most 80 wide) |
| caption and help type | 9.5 px |

MacroPanel uses the same constants. Their values are unchanged, so the macro popovers are
untouched.

## The panels

- **PITCH BEND** (a click on PITCH):
  - RANGE (`bendRange`: its ID, range, default and automation unchanged) beside the picture of
    the wheel's reach (±RANGE);
  - one line: "The wheel bends notes up or down by RANGE. It springs back to the centre when
    released."
  - It opens above the wheel, which stays in view and playable.
- **MOD WHEEL** (a click on MOD):
  - **title row:** VIBRATO OFF / ON on the right, where a macro popover's mode sits;
  - **the picture** of one note's pitch over 2.5 s, from the real settings: DELAY and RISE
    marks, DEPTH as the outline, the line as it plays now (the wheel's position, ALWAYS ON, or
    AMOUNT);
  - **knobs:** DEPTH, RATE, DELAY, RISE, AMOUNT in one row. AMOUNT is dimmed (still adjustable)
    unless CONTROL is ASSIGNED;
  - **CONTROL:** MOD WHEEL / ALWAYS ON / ASSIGNED;
  - **SOURCES:** A, B, C;
  - **help:** one or two lines on the chosen CONTROL.

  The MOD socket still drags a cable (a drag longer than 3 px never opens the popover).
- **ADVANCED** (its key), in one view with two sections. This was chosen over tabs because
  every setting fits at this scale, and nothing hides behind a tab.
  - **PLAYING:** FINE and GLIDE (GLIDE dimmed in POLY); VOICES (POLY / MONO) and PITCH
    CHARACTER (TAPE / NATURAL) beside them; MPE ("per-note bend, pressure and slide").
  - **DYNAMICS:** the VELOCITY curve (SOFT / LINEAR / HARD) on the section's row; AMOUNT, RANGE,
    TONE; SEED with Reseed.
  - There is no BEND: the bend's RANGE is under PITCH only.
  - It opens above its key, right edges aligned, and clear of the ARP / SUB housing
    (`frame::advancedPopoverOrigin`'s `clearOf`, in OSP and the shell).

**Unchanged:**
- every parameter, its ID, range, default and automation;
- the vibrato DSP (`osp::vibrato`), CC 1 routes, drag-to-assign and the wheels' spring-back;
- the popovers' closing: Escape, a click elsewhere, the caption or key again.

**Not present:** the Settings / Info keyboard icons from the shortcut redesign are not on this
line. ADVANCED's key stays its entry point, and its placement follows that key.

## Learn

- **Help:**
  - Pitch Bend, on the PITCH caption itself;
  - Mod Wheel, on the MOD caption;
  - Vibrato, on VIBRATO's controls;
  - Advanced Settings, which no longer mentions the bend range.

  The wording is the specification's.
- **Targets:**
  - `osp.wheel.pitch` and `osp.wheel.mod`: each caption, not the keyboard section;
  - `osp.vibrato.{enable, control, depth, rate, delay, rise, amount}` (by parameter) and
    `osp.vibrato.sources` (the A / B / C keys together);
  - the states `osp.popup.mod.open` and `osp.modwheel.up`.
- **New lesson, "Add vibrato to a recording"** (`osp.vibrato`, 3 min):
  1. set-up: the current sound or the bowed example, loaded only on the musician's choice;
  2. open MOD;
  3. VIBRATO ON;
  4. CONTROL MOD WHEEL (Continue when it already is);
  5. DEPTH;
  6. RATE;
  7. hold a note and raise the wheel;
  8. DELAY or RISE;
  9. SOURCES (an explanation).
- **The audit:** no lesson mentioned the bend range, Advanced's bend, CC 1 or vibrato. The
  modulation lesson's mention of the mod wheel as a source stays true.

## Tests

- **`osp_plugin_tests "[popover-scale]"`**, at 869, 1086, 1448 and 1810 px, for PITCH, MOD and
  ADVANCED:
  - compact;
  - at most 345 x 300 and at least LIFE's width;
  - knob cells 58 px tall and at most 80 wide (±2.5 px for fractional scales);
  - inside the window and the card, no two controls overlapping;
  - every control present (MOD: the five knobs, ON, CONTROL, A / B / C; ADVANCED: FINE,
    GLIDE, AMOUNT, RANGE, TONE, VOICES, PITCH CHARACTER, the curve, MPE, SEED; never BEND);
  - ADVANCED above ARP / SUB.
- **The vibrato lesson end to end:**
  - the highlight is the MOD caption (under 40 px tall);
  - automation never completes a step;
  - the wheel must actually be up.
- **From the wheel line, passing here:** `[wheels]` (the captions, never clipped; click versus
  drag; MOD's cable; one popover at a time; no BEND in Advanced) and `osp_tests [vibrato]`
  (vibrato DSP: off is bit-identical, per-note onset, the three CONTROL modes, the sources).
- **Still passing after the merge:** `[sub]`, `[short-loop]`, `[learn]`, `[phase2]` (the
  appended parameters, `vibrato.*` last), `[parameter-schema]` (the golden regenerated:
  `vibrato.*` and `destExt` only), the shell's parity and schema.
- **Hidden `[popover-scale]` measurement** (`OSP_POPOVER_SHOTS=<dir>`): every popover's measured
  size, with screenshots at 869, 1086 and 1448.

## Screenshots (`docs/wheels/screenshots/`)

- `before-after-1086.jpg`: the three popovers before (top) and now (bottom), at 1086 px.
- `after-1086.jpg`: LIFE, FILTER, MOVEMENT and SPACE, then PITCH, MOD and ADVANCED, at 1086 px.
- `after-869.jpg`, `after-1448.jpg`: PITCH, MOD and ADVANCED at 869 and 1448 px.
- `osp-1086-{life,pitch,mod,advanced}.jpg`: single screens.
- `shell-1086.jpg`: the same popovers in a shell product (ShellEditor's anchors: ANDOR ANALOG's).
- Hidden `[shell-popover-shots]` (`ANDOR_SHELL_POPOVER_SHOTS=<dir>`) makes the shell's.

## Not verified

- No real DAW, and no hardware MIDI controller (the wheel and CC 1 were tested through
  processBlock).
- **Retina / fractional scaling:** checked at the four widths in software, not on a Retina
  screen.
- **ANDOR ANALOG:** built against this branch, but its pin has not been moved.
  - Its engine, plugin, standalone and `analog_dsp_tests` build; that suite passes (321 cases,
    vibrato and wheel included).
  - `andor_analog_tests` does not compile yet: OSP's earlier shared-FX headers share file names
    with ANALOG's own (`FxRouting.h` and others; NEXT in STATUS).
  - The shared SUB panel's file is named `AndorSubPanel.h` for this reason.
  - ANALOG's panels are the same shared code: `shell-1086.jpg` shows them in a shell product.
- **The other OSP line.** `claude/friendly-pascal-0y48r0` (trimmed footer) also moves the
  PITCH / MOD captions (into its apron). The two lines meet in `Wheel` / `frame::place` and need
  a merge when both land.
