# Model capture: the editor capture behind the marketing model's face texture

`blender/textures_*` are made by `product-renders/blender/scripts/prep_textures.py` from one editor capture: the
three-layer hero (chord held, ceramic look, 1448 px) with and without knob caps, the knobs' geometry and every
component's bounds. This folder keeps the capture test so a texture set can be rebuilt from its OSP commit.

| Texture set | OSP commit | Interface |
|---|---|---|
| `textures_0.9.0-beta.1/` | `32e0bee` (`claude/macos-release`); verified byte-identical at `6ef3376` | ANDOR OSP 0.9.0-beta.1: **the frozen release interface** (approved 20 Nov; `FROZEN.json`) |
| `textures_current/` | `08ebd01` (default branch, 17 Nov) | archived: every render before 20 Nov |

To rebuild (from a detached worktree of the commit, never committed there):

1. Copy `LocalWebCapture.inc` to `tests/plugin/` and add `#include "LocalWebCapture.inc"` at the end of
   `tests/plugin/PluginProcessorTests.cpp`.
2. In `shared/andor-ui/Source/Design.cpp` (namespace `osp::plugin::design::draw`), add
   `bool captureWithoutCaps = false;` before `void knob (...)`, and `if (captureWithoutCaps) return;` just before
   the rendered cap is drawn (the comment "A rendered cap: its own light, shadow and indicator").
3. Build `osp_plugin_tests` (`-DOSP_BUILD_PLUGIN=ON`, Release) and run
   `OSP_LOCAL_CAPTURE=<dir> xvfb-run -a build-plugin/apps/plugin/osp_plugin_tests "[local-capture]"`.
4. `python3 product-renders/blender/scripts/prep_textures.py --capture <dir>/cur-hero-3L-nocaps@4x.png
   --knobs <dir>/cur-hero-3L-nocaps.json --bounds <dir>/cur-hero-3L-bounds.json --out <texture dir>`.

Checksums of the committed sets are in `product-renders/documentation/interface_release_check.md`; the frozen set's are also in
`textures_0.9.0-beta.1/FROZEN.json`, which `cine_rig.py` checks before any final-quality render (it refuses a set that does not match).

When the beta branch moves, re-capture its head and compare: if `prep_textures.py` writes byte-identical files, record the commit
under `verified_identical_at` in `FROZEN.json`; if anything differs, write a new texture directory and ask for approval before
any final render uses it.
