# Release interface check: the model's face texture against the beta candidate

**Status:** compared; awaiting the owner's approval of the release interface. The model and its textures are unchanged.

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
- **Provenance verified.** Run on the committed 17 Nov capture (`assets/source/captures/cur-hero-3L*`), it reproduces `textures_current/` byte for byte: sha256 `e2a8abc7…`, `f13d38ea…` and `cb7ecafc…`.
- **On the beta capture:**
  - `osp_face.png` changes: the tab, the arc colours and the footer icons;
  - `geometry.json` gains **one** raised button: the Library key, derived from its component bounds like every other key;
  - the knobs, cards, displays, keyboard, panel and teak strip are identical.

No other geometry or material changes. Every approved camera, light and material carries over as it is.

## Proposed procedure, after approval

1. Commit the beta capture as `assets/source/captures/` with its commit recorded in the asset manifest, so the version is fixed rather than following the default branch.
2. Write the textures to a **new** directory, `textures_0.9.0-beta.1/`.
3. Keep `textures_current/` (the 17 Nov version) archived, unchanged, for reproducibility.
4. Point the scene at the new directory, then re-render one reference still for comparison before any final render.
