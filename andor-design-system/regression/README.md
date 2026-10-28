# Visual regression: every ANDOR product against ANDOR OSP

A product built from the ANDOR Instrument Shell must look exactly like ANDOR OSP. This
regression proves it: the same states, rendered by both, compared pixel by pixel.

## How it works

1. **The states.** `tests/support/VisualStates.h` defines one scenario that OSP and the shell
   both run (the calls are the same). It produces 18 states:
   - empty;
   - one, two and three layers (three also at 2×);
   - window widths 869 and 1810;
   - the six modulation tabs;
   - EQ, DAMPEN, Granular;
   - keys held;
   - the ARP editor open.
   The stand-in sounds are deterministic test signals.
2. **OSP's side.** The ctest `osp_visual_reference` runs `osp_plugin_tests "[visual-reference]"`
   and writes `build-plugin/andor-visual-regression/osp/<state>.png`.
3. **The product's side.** The ctest `andor_<slug>_tests_visual` runs the product's
   `"[visual-regression]"` test (it requires the reference, a CTest fixture). It renders:
   - `parity/`: the same states with an engine that binds every parameter, so every control
     is enabled as in OSP. This test-only engine is never shipped.
   - `shipped/`: the product as it ships, with its unbound controls disabled. For review and
     the gallery; not compared.
   It then compares `parity/` with OSP's renders and writes `diff/<state>.png` and `report.md`.

## The rule

- **Tolerance: 0.** Every pixel outside the masks must be exactly OSP's (any channel
  differing by more than 0 fails). The shell draws with the same code and the same rendered
  assets, so nothing else is acceptable.
- **The masks** are the approved dynamic regions, the only places a product may differ
  (`visualstates::masks`):

| Mask | Area (reference px) | Why |
|---|---|---|
| `identity` | From the logo's left edge (x 45) to just before the preset bar (x 495), y 21–108 | The product's word after "ANDOR/", and its tagline |
| `preset-name` | The preset bar less 46 px each side, and 4 px top and bottom | Each product's own presets |
| `status-line` | The footer's status line, ± 2 px (moves down with the ARP editor) | It names the engine and the sounds' state |

- **State normalisation.** ANDOR OSP chooses a granular POS from its analysis the first time
  a layer turns Granular; that is its engine's behaviour, not the interface. The scenario
  sets POS to 50 % in both before the Granular state. No other state is adjusted.

## Reading a diff

`diff/<state>.png` is the reference in grey:
- **red:** a pixel that differs outside the masks (a failure);
- **blue tint:** the masks.

`report.md` lists, per state, the differing pixels outside the masks (must be 0) and inside
them (allowed, for information).

## Running it

```sh
cmake --build build-plugin
ctest --test-dir build-plugin -R "osp_visual_reference|_tests_visual" --output-on-failure
open build-plugin/andor-visual-regression/ANDOR_Example/report.md
```

CI uploads `build/andor-visual-regression/` as the artifact `andor-visual-regression-<os>`.

## Result at the time of writing

ANDOR Example against ANDOR OSP, on Linux (Xvfb):
- **0 differing pixels outside the masks in all 18 states**, at tolerance 0;
- see `../reports/validation.md`.

Getting there took four fixes in the shell:
- the shell runs the loader's later model stages, as OSP does (the card's sustain-loop bracket);
- it opens ANDOR OSP's Advanced popover (shared now), so the Advanced key is live as in OSP;
- adding a layer sets the mix as in OSP (`InstrumentHost::makeLayerAudible`, shared now);
- the scenario sets the granular POS to the same value in both (OSP otherwise picks it
  from its analysis, see "State normalisation").
