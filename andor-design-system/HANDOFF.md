# HANDOFF: building a new ANDOR instrument

You are making a sister product of ANDOR OSP. It must be unmistakably the same instrument
family: the same ceramic object, the same controls, the same behaviour. It is made by
**composing ANDOR OSP's own interface code**, not by imitating it. This file says what exists,
what you change, what you never change, and how to prove you did it right.

## What you get

| Piece | Where | What it is |
|---|---|---|
| The shared interface | `shared/andor-ui/` | ANDOR OSP's components, look, frame, parameter schema, sample loader and rendered assets. ANDOR OSP itself is built from it, and its renders are pixel-identical to before the extraction. |
| The ANDOR Instrument Shell | `shared/andor-shell/` | A complete plugin around your engine: processor, editor (OSP's layout), presets, state, MIDI, the DSP boundary, a labelled diagnostic engine, the CMake function `andor_add_instrument`. |
| The bootstrap tool | `tools/create_andor_instrument.py` | Writes `products/andor-<slug>/` and registers its identifiers. It refuses clashes and never overwrites. |
| A complete example | `products/andor-example/` | ANDOR Example, made by the tool. It builds as its own AU/VST3/Standalone, loads sounds, plays MIDI through the diagnostic engine, saves and reloads presets, and passes the visual regression. |
| The design bible | `guide/` (start with `guide/index.html`) | Tokens, components, interaction, assets and code, with real screenshots. |
| The visual regression | `regression/README.md` | Your product against ANDOR OSP, pixel by pixel, in 18 states. |
| The registry | `product-registry.json` | Every ANDOR plugin's identifiers. |
| Asset licensing | `assets/manifest.json` | What each embedded asset is, and what may be done with it. |

## Start a product (10 minutes)

```sh
python3 tools/create_andor_instrument.py --name "ANDOR Glass" --slug glass \
    --plugin-code Agls --bundle-id com.osp.andorglass --tagline "Struck and bowed glass"
cmake -B build-plugin -G Ninja -DOSP_BUILD_PLUGIN=ON
cmake --build build-plugin --target ANDOR_Glass_All andor_glass_tests
ctest --test-dir build-plugin -R "andor_glass|osp_visual_reference" --output-on-failure
```

Check the output against the following:
- Run with `--dry-run` first to see what will be written.
- Each identifier must be unique: name, slug, plugin code, bundle ID, state type and preset extension. The tool refuses one that another ANDOR plugin uses: OSP `Osp1`, Drums `Adrm`, Grain `Agrn`, Phrase `Aphr`, Example `Aexm`, and every registered product. CMake checks the registry again at configure time.
- The identity word after `ANDOR/` is at most 8 characters. The product's own test measures the real glyphs against the preset bar.

## What you change

1. **`Source/Product.cpp`**:
   - **Identity**: the product word and the tagline.
   - **Macros**: the six macros' parameter IDs, names and hints, and their popovers (`ProductInfo::macroPopover`, built from `MiniPanel`, `MiniKnob` and `SegmentedControl`).
   - **Engine**: `createEngine()` returns your engine.
2. **Your engine** (`andor::shell::Engine`, `shared/andor-shell/Source/ShellEngine.h`):
   - `boundParameters()` lists the parameters you actually read. Everything else is shown disabled. This is how the instrument stays honest.
   - `prepare()` allocates and caches raw parameter pointers.
   - `setSource()` and `process()` run on the audio thread: no allocation, no locks, no I/O.
   - Randomness comes only from `osp::Prng` seeded from `seed` (CLAUDE.md rules 1 and 2).
3. **Factory presets**: files with your extension in `Documents/<Product>/Presets/`.

## What you never change

See the rules below.
- If your product needs something the shared interface does not have, **add it to `shared/andor-ui`**. Make it neutral for ANDOR OSP. Prove OSP's renders are unchanged with the design capture (`[design-capture]`) and the visual regression.
- Never fork, copy or restyle a component in your product folder.

---

## ABSOLUTE DESIGN RULES FOR NEW ANDOR PRODUCTS

These are not guidelines. A product that breaks one is not an ANDOR product.

1. **ANDOR OSP is the source of truth.** The current ANDOR OSP interface (code and renders) defines the design. This is not a redesign. Do not reinterpret, modernise, simplify or embellish anything.
2. **Compose, never copy.** Build the interface only from `shared/andor-ui`: `frame::*`, the components, `OspLookAndFeel`, `design::*`, `skin::*`. Never copy their code into a product, and never keep a parallel version that can drift.
3. **No new visual vocabulary.** Do not introduce any of the following. The existing ones are the only ones.
   - new colours;
   - new fonts or font weights;
   - new knob designs;
   - new button designs;
   - new textures, panel materials or shadows;
   - new corner radii;
   - new icon families;
   - new decorative elements;
   - new layout proportions;
   - new visual interaction patterns.
4. **Do not adjust spacing.** Positions and sizes are ANDOR OSP's reference pixels (`design::layout`, `frame::place`, `frame::sourceColumn`). The canvas is 1448 × 1086, scaled as one piece.
5. **Use the real assets.** The wood, ceramic, keys, lenses and ivory knob caps are the rendered assets in `shared/andor-ui/Resources`.
   - Never replace wood with gradients.
   - Never approximate the Blender-rendered knobs with JUCE primitives.
   - Never create UI graphics with AI image generation.
   - New rendered parts come only from `tools/blender-assets`, with the same scene, materials and lighting.
6. **The identity has one form.** **ANDOR** in Outfit ExtraBold, then **/PRODUCT** in Outfit ExtraLight (capitals, at most 8 characters), then a sentence-case tagline in Inter. Same place, same size. Nothing else in the header changes.
7. **The macro row has six positions.** Each keeps its identity colour, lens, knob and place (ECHO's position sits before SPACE's). A product renames them and binds them; it does not add, remove, resize or recolour them.
8. **Never present an unconnected control as working.**
   - Every control the engine does not read is shown disabled, in ANDOR OSP's existing disabled look (`boundParameters`).
   - A diagnostic or placeholder engine says so on screen (`Description::diagnostic` and `statusLine`), and is never presented as a production instrument.
9. **Identifiers are forever.** Plugin code, bundle ID, parameter IDs, state type and preset extension must never change once shipped. The parameter schema is ANDOR OSP's (`tests/plugin/golden/osp-parameter-schema.txt`): append only, never rename or re-range (CLAUDE.md rule 8).
10. **Prove it.** A product ships only when all of these pass:
    - `andor_<slug>_tests`;
    - `andor_<slug>_tests_visual`: 0 differing pixels outside the three approved masks (identity, preset name, status line), at tolerance 0;
    - ANDOR OSP's own suite.

    Never widen a mask or raise the tolerance to make a product pass.
11. **Do not touch ANDOR OSP while building a product.** Its identity, audio, preset schema, automation IDs, sample loading, modulation behaviour and default appearance must not change. Changes to `shared/andor-ui` are regression-tested against OSP before anything else.
12. **Licensed material stays home.** Do not publish or bundle the rendered artwork, or any audio of unknown rights, in another public repository or package without the owner's authorization (`assets/manifest.json`). Fonts travel only with their OFL license files.

---

## How to prove a change to the shared interface is safe

1. Before changing anything, capture OSP:
   `OSP_DESIGN_CAPTURE=/tmp/before xvfb-run -a build-plugin/apps/plugin/osp_plugin_tests "[design-capture]"`.
2. Make the change, rebuild, capture to `/tmp/after`. Every state and component crop must be
   byte-identical, or the difference must be exactly the intended one (say so in the commit).
3. Run `ctest --test-dir build-plugin`. This covers:
   - OSP's suite;
   - the schema golden test;
   - every product's tests and visual regression.

## Known limitations and next steps

- **Host-visible parameters.** Every product exposes the full ANDOR schema (350 parameters) to the host, including those its engine ignores. The interface disables those, but hosts list them.
  - Next step: a grouped layout (`createParameterLayout (groups)`), plus components that tolerate absent parameters, behind the same golden test.
- **What the shell does not have.** It has no Library window and no Samples inspector, and it loads one file per layer (no multi-sample sets). Those are ANDOR OSP features. If a product needs them, move them into `shared/andor-ui` the same way.
- **OSP behaviour the shell does not copy.** ANDOR OSP picks a granular POS from its analysis; the shell keeps the default 50 %. This is engine behaviour. A product's engine may do the same.
- **Platforms.** The macOS CI run (AU validation of ANDOR Example with `auval`) is the first check on a Mac. The visual regression has been run here on Linux (Xvfb). It is platform-independent by design (OSP and the product render on the same machine), but its macOS result comes from CI.
- **The repository is public.** The rendered artwork already in it is publicly readable. Whether it may be reused elsewhere is the owner's decision (see `assets/manifest.json`).
