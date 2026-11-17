# ANDOR shared design system audit (Package C, 17 Nov 2026)

## 1. Repository state (fetched 17 Nov 2026)

| | Branch | Commit | Notes |
|---|---|---|---|
| ANDOR OSP: default branch | `claude/charming-lovelace-n25fte` | `08ebd01` (PR #24, 17 Nov) | The shipping line. It is 157 commits ahead of this branch's fork point (`c49d77f`, 9 Nov) |
| ANDOR OSP: this branch | `claude/friendly-pascal-0y48r0` | `45d710d` + this checkpoint | Website, Blender campaign and knob asset work. It is 32 commits ahead of the fork point |
| ANDOR ANALOG: `main` | `main` | `4bc6c41` | Pins OSP `adf8ed6` (9 Nov, `claude/space-presets` merge) |
| ANDOR ANALOG: newest line | `feature/aux-live-input` | `e3a8090` (PR #12, 17 Nov) | Pins OSP `b522a06` (16 Nov). It is 103 commits ahead of ANALOG `main` |
| ANDOR ANALOG: this session's branch | `claude/friendly-pascal-0y48r0` | `0beac6d` | Older; untouched |

The shared UI comes in through the `external/osp` submodule (branch `claude/andor-analog-shell`). The newest ANALOG line pins `b522a06`, which is on the OSP default branch's history.

## 2. Compact bottom edge in ANALOG: actual status

- **Done upstream, on a different implementation.** OSP commit `4489e33` (16 Nov) gives the shell a compact bottom edge: the canvas is 1050 high (was 1086), there is no footer status line, and messages appear as brief notices.
  - It arrived on the default branch through `feature/aux-live-input`.
  - It came together with the keyboard SETTINGS / INFO keys (`7984022`), which replaced Advanced, and compact wheel / ADVANCED popovers.
- **In ANALOG** (checked by commit ancestry):
  - ANALOG's `feature/aux-live-input` pins `b522a06`, which contains `4489e33`, so it has the compact bottom edge.
  - ANALOG `main` pins `adf8ed6`, which does not.
  - So the integration is waiting only on merging `feature/aux-live-input` into ANALOG `main`. That is a release decision for the owner.
- **Superseded:** the trimmed-footer commits on this branch (`dc75a09`, `26d1389`, `ec63f01`, `1daa605`, `9933db1`, `ef5c0c3`) are in neither repository's shipping line. **They should not be integrated.** The earlier cherry-pick plan (gate C1 notes) is withdrawn.
- **Not yet verified here:** a built ANALOG editor at `e3a8090` has not been captured in this session. OSP's own editor at `08ebd01` was built and captured (`cur-hero-3L`). Building and capturing ANALOG is the first item for the next Workstream C pass.

## 3. Control families (OSP `08ebd01`, ANALOG `e3a8090`)

There is **one rotary painter**. Every knob in both products is drawn by `OspLookAndFeel::drawRotarySlider` → `design::draw::knob`, and ANALOG adds none of its own.
- **Sprites:** a sprite replaces only the cap; the ticks, arcs and halos stay vector.
- **Product differences:** ANALOG differs only through `Materials` (copper accent, amber layer A, macro colours, graphite on wells, walnut skin pack).
- **Sizes** below are body radius in reference px (1448 wide).

| Family | Where | Body r | Count | Proposed asset |
|---|---|---|---|---|
| **Macro** (LIFE, DRIVE, FILTER, MOVEMENT, ECHO, SPACE; shell user macros) | InstrumentFrame.cpp:289-351 | ≈46.5 | 6 | `macro-ivory`: rendered (A shipping; **D2** proposed) |
| **Source dial** (START, TUNE, PAN, LEVEL, REIMAGINED) | EngineCard.cpp:1168-1226 | ≈32 (≈28 in the 87 % five-knob row) | 5 per card, 15 at three layers | `layer-ivory`: own geometry (fewer, broader flutes; smaller bevel; shorter shadow), with the layer colour still a vector arc |
| **Mini knob** (rack cells, popover cells, amp envelope, mod panel, ARP, SUB, satellite, wheel / ADVANCED popups, FX routing) | `MiniKnob` (ShapingPopups.cpp:440), RackControls.h:178 | 9.6 to 25 (rack 24.6, envelope 20, popover 13 / 9.6) | about 150 call sites | `mini-ivory`: one simple cap (no flutes below about 14 px on screen; shoulder and pointer only), two levels |
| **Graphite on the dark wells** (GRANULAR; ANALOG oscillator / hybrid wells) | EngineCard.cpp:1996; analog AnalogWellOverlays.cpp | 13 to 18 | 5 to 6 per card | `mini-graphite`: the mini geometry in graphite |

Non-rotary families:
- **Already rendered from the ceramic skin pack:** keys, raised cards and panels, display bezels, layer badges, volume fader, macro and ARP LEDs, keybed, housing.
- **Vector:** segmented switches, toggles, icon buttons, the keyboard keys, the PITCH/MOD wheels and the A/B blend slider.

The skin pack is already the right home for refined buttons and bezels: nine-slice pieces with state variants.

## 4. Proposed implementation order

1. **Macro D2:** final review in Package B; ship by replacing the two packs. Format and code are unchanged.
2. **Source dial:** the most visible knobs after the macros; 15 instances in the hero state.
3. **Mini family:** a single asset covers about 150 call sites. Judge it at the smallest runtime size (popover secondary row, r 9.6 → about 13 px at 869 wide).
4. **Graphite well variant:** a material swap on step 3.
5. **Ceramic keys and bezels:** a refinement of existing skin pieces (Blender renders into the same nine-slice slots), with states normal / hover / pressed / selected / disabled.
6. **Keyboard and PITCH/MOD:** last, from the approved physical modelling. The vector keyboard component stays interactive.

**Memory guard:**
- The six macro sprites hold up to 36.7 MB at 2× when every frame is decoded (`[knob-perf]`).
- Steps 2 to 4 must share one asset per family across all instances.
- Each step must measure held memory with every family visible before it ships.

## 5. Known blockers

1. **Branches.**
   - GUI asset work must land on the OSP default line.
   - This session may push only `claude/friendly-pascal-0y48r0`, which is 157 commits behind and carries the superseded trimmed footer.
   - The packs and the `tools/blender-assets` changes are self-contained files, so they move cleanly. They still need a branch off `claude/charming-lovelace-n25fte`, and creating one needs the owner's go-ahead.
2. **ANALOG `main`:** still on the 9 Nov pin until `feature/aux-live-input` is merged.
3. **No Retina display:**
   - The interaction sequence is rendered headless at 2× by the real editor code.
   - Judging motion on a real Retina Mac remains a manual step.

## 6. Recommended integration approach

- **Marketing:** the Blender campaign stays on this branch (it touches only `andor-web/`).
- **GUI assets:** a new branch from the OSP default line carrying only `tools/blender-assets/` (`shadow_trim`, config) and `shared/andor-ui/Resources/knobs/macro-ivory/*.zip`.
  - The code and pack format are unchanged, so it is one small PR.
  - The existing `[sprite-knob]` tests and the shell's 0-pixel visual regression guard it.
- **ANALOG:** gets the same packs through its submodule pin when it next moves.
  - Its macro sprite is the same asset, and its colours are vector.
  - So no ANALOG-specific asset is needed for the macros.
