# Release interface check: the model's face texture against the beta candidate

**Status:** **approved, applied and frozen (20 Nov).** See *Freeze (R4)* at the end for the re-check at the beta head `6ef3376`. The owner approved `32e0bee` (0.9.0-beta.1) as the release interface. It is now the one fixed interface for all launch photography and animation.

## Applied

- **Source:** the beta capture is in `assets/source/captures/osp-0.9.0-beta.1/` (sha256 of `cur-hero-3L-nocaps@4x.png`: `4d4d78433ac8df34…`).
  - Like every source capture, it is not committed (`assets/source/` is git-ignored; 222 MB).
  - The capture test that makes it is committed instead, in `tools/model-capture/`, so the capture can be rebuilt from `32e0bee`.
- **Textures:** `prep_textures.py` wrote `blender/textures_0.9.0-beta.1/`:
  - `osp_face.png` `62dc7d85cd120b92…`;
  - `geometry.json` `0bb7f55cdf67cea3…`;
  - `teak_strip.png` `cb7ecafcb641b7be…` (unchanged).
  
  **Both texture sets are committed** (5.7 MB each), so the approved interface and its archive do not depend on rebuilding a capture:
  - `textures_0.9.0-beta.1/`, the release interface;
  - `textures_current/`, the 17 Nov archive.
  
  They were git-ignored until now.
- **Default:** `andor_scene.py --textures` now defaults to the release textures, and so do the Virtual Studio prep, the performance prep, the asset manifest generator and the GPU benchmark bundle.
- **Recorded per render:** `cine_rig.py` passes `--textures` through and writes the directory it used (`interface_textures`) into every render's metadata.
- **Archived:** `textures_current/`, the 17 Nov interface (`08ebd01`), is kept unchanged. It regenerates byte for byte from the stored 17 Nov capture (`assets/source/captures/cur-hero-3L*`) and is still selectable with `--textures product-renders/blender/textures_current`, so every earlier render can be reproduced.
- **Reference renders** (old and new textures, same spec):
  - **D2 and H01 B:** no visible difference at their framing; under 0.01 % of pixels differ by more than 20 levels. The changes are small in the frame and partly under the modelled knob skirts.
  - **A close-up of the left of the macro row:** the CORE / USER tab and the harmonised arc colours (LIFE sage instead of orange) are clearly there.
- **Earlier renders:** everything delivered before 20 Nov 10:15 UTC used the 17 Nov interface, including the R2 style frames, the R2 motion drafts and the first H01 stills. Every R3 draft and the H01 finals use the release interface.

## Before approval

## The two versions

| | Today's model texture | The candidate |
|---|---|---|
| Commit | `08ebd01`, the default branch on 17 Nov | **`32e0bee`**, branch `claude/macos-release` |
| Version | before versioning | **ANDOR OSP 0.9.0-beta.1**: `packaging/release/products/osp/version.env` with `PRODUCT_PRERELEASE=beta.1` and `PRODUCT_CHANNEL=beta` |
| Relation | | Contains the whole default branch (head `6d07440`, ARP STRUM and the menu refinement merged) plus 12 commits: Custom ARP CP3/CP4, the shell window fix, the macOS release infrastructure |

**Why this is the candidate.** It is the only commit in the repository that names a release version. The repository has no tags.
- It is not merged into the default branch.
- If the beta is cut from a different commit, the capture below is repeated for that commit. It is one command.

## How the capture was made

The capture uses the same local, uncommitted capture test as the 17 Nov capture: Gate C2.5's hero, with three layers, a chord held, and the ceramic look at 1448 px. It exports the frame with and without knob caps, plus the knob geometry and every component's bounds. It ran:
- in a detached worktree of `32e0bee`;
- with JUCE 8.0.9, the same as before;
- with only one other local change: the capture's "without caps" switch. The later knob-review experiments in the 17 Nov worktree were **not** carried over, so the beta capture is the beta's committed interface.

## What changed

**Overall.** The editor is the same size: 1448 × 986 px. Only 2,294 pixels differ at 1×, which is 0.16 %.

**Knob geometry.** Identical: the 25 knobs have the same positions, radii and angles.

| | `08ebd01` | `0.9.0-beta.1` |
|---|---|---|
| Macro bank | No tab | A **CORE / USER** tab above the macro row, with CORE selected (`MacroBankTab`) |
| CORE macro arcs | Orange, blue, teal, … | **The harmonised palette**: sage, ochre, umber, … (`c010a8f`) |
| Footer keys | Settings, Info | **Library**, Settings, Info. The new Library key moves the other two to the left |
| Text, labels, displays, cards, keyboard | | Unchanged |
| ARP STRUM key | Not visible in the hero state | Not visible in the hero state: the ARP card is not open in this capture |
| Menu refinement | Not visible in a still capture: menus are pop-ups | Not visible in a still capture: menus are pop-ups |

The zoomed regions are in `images/interface_08ebd01_vs_0.9.0-beta.1.jpg`; the whole editor side by side is in `…_full.jpg`.

## What a re-sync would change in the model

`prep_textures.py` was run on both captures, writing into scratch only:
- **Provenance verified.** Run on the stored 17 Nov capture (`assets/source/captures/cur-hero-3L*`, not committed), it reproduces `textures_current/` byte for byte: sha256 `e2a8abc7…`, `f13d38ea…` and `cb7ecafc…`.
- **On the beta capture:**
  - `osp_face.png` changes: the tab, the arc colours and the footer icons;
  - `geometry.json` gains **one** raised button: the Library key, derived from its component bounds like every other key;
  - the knobs, cards, displays, keyboard, panel and teak strip are identical.

No other geometry or material changes. Every approved camera, light and material carries over as it is.

## Proposed procedure, after approval

1. Store the beta capture in `assets/source/captures/` and record its commit, so the version is fixed rather than following the default branch.
2. Write the textures to a **new** directory, `textures_0.9.0-beta.1/`.
3. Keep `textures_current/` (the 17 Nov version) archived, unchanged, for reproducibility.
4. Point the scene at the new directory, then re-render one reference still for comparison before any final render.

## Freeze (R4, 20 Nov): the beta head re-checked

The R3 review asked for one immutable, approved final texture commit, compared against the current beta release interface before any final rendering.

**The beta branch had moved.** `claude/macos-release` was at `6ef3376`, three commits past `32e0bee`:
- MIDI Learn (`e655115`);
- the granular POS-line flicker fix (`c74b6af`);
- the Custom Pattern UX redesign R2 (`6ef3376`).

The version is unchanged: 0.9.0-beta.1, build 1.

**Captured at `6ef3376`** with the same capture test and scaffolding as before (`tools/model-capture/`). The knob-review patch in the capture worktree was set aside first, so this is the beta's committed interface.

| Check | Result |
|---|---|
| Hero capture with caps, 1x and 2x | **Identical**: 0 pixels differ |
| Hero capture without caps, 4x (the texture source) | **Identical**: 0 pixels differ |
| Knob geometry and component bounds (JSON) | Identical |
| `prep_textures.py` output | **Byte-identical**: `osp_face.png` `62dc7d85…`, `geometry.json` `0bb7f55c…`, `teak_strip.png` `cb7ecafc…` |

**Every visible difference between the beta head and the texture**, and why none of them reaches the model:

| Change | Where it shows | In the model's state? |
|---|---|---|
| MIDI Learn | A MIDI LEARN section in a control's right-click menu (a pop-up), and a notice line while learning | No: menus and notices are not part of the hero state |
| Custom Pattern UX R2 | The ARP card: one Custom Patterns row, a pattern browser, a grid-first editor | No: the ARP card is closed in the hero state |
| Granular POS line | The position line in a granular display no longer jumps between two places while playing | No: identical pixels in the captured frame |

**Frozen:** `textures_0.9.0-beta.1/` is the one approved interface texture set. Its checksums, the approved commit (`32e0bee`) and the verification at `6ef3376` are in `textures_0.9.0-beta.1/FROZEN.json`.

**Enforced:** `cine_rig.py` refuses any final-quality render whose textures don't match `FROZEN.json`. A render counts as final-quality when it asks for `--final`, or is at least 1920 px wide with at least 96 samples. Two tests:
- the archived 17 Nov set has no manifest and is refused;
- every render records `interface_frozen` and `interface_commit` in its metadata.

**If the beta moves again:** re-capture its head.
- If the textures come out byte-identical, add the commit under `verified_identical_at`.
- If anything differs, write a new directory and ask for approval before any final render uses it.
