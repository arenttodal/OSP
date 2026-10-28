# 06 — Code: the shared interface, the shell, the products

```
shared/andor-ui/          the ANDOR interface, shared as source (JUCE compiles per plugin)
  AndorUi.cmake           andor_ui_add_to(target): sources, include dir, AndorUiData (fonts, sprites, skin)
  Source/InstrumentHost   what the components need from an instrument (pure virtuals) plus every
                          parameter-only behaviour, implemented once: routes and links, rules,
                          MUTE/SOLO, LINK, the mix when layers are added, the curve codec
  Source/AndorParameters  the ANDOR parameter schema: ANDOR OSP's 350 parameters (IDs frozen)
  Source/InstrumentFrame  the frame: canvas, painting, placement, macro row, keyboard,
                          drop zone, popover placement, window shape
  Source/Binding.h        "andorParams" tags: which parameters a control shows
  Source/Design, OspLookAndFeel, CeramicSkin, SpriteKnobRenderer   the look
  Source/EngineCard, MainSections, ModulationPanel, EqEditor, ArpSection, ShapingPopups   the components
  Source/InstrumentLoader, LoadedInstrument   the sources: import, analysis, stages (background thread)
  Resources/              fonts, knob sprites, ceramic skin
shared/andor-shell/       the ANDOR Instrument Shell
  AndorInstrument.cmake   andor_add_instrument(...) and andor_add_instrument_tests(...)
  Source/ShellEngine.h    the DSP boundary (Engine)
  Source/ShellProduct.h   what a product defines: ProductInfo, createEngine()
  Source/ShellProcessor   InstrumentHost + AudioProcessor: parameters, sources, presets, MIDI, state
  Source/ShellEditor      ANDOR OSP's layout from the shared frame and components
  Source/DiagnosticEngine the labelled stand-in engine (baseline A resampler / test tone)
  template/               what tools/create_andor_instrument.py writes for a new product
products/andor-<slug>/    products (ANDOR Example: products/andor-example)
apps/plugin/              ANDOR OSP: its processor, editor, Library, popovers (uses shared/andor-ui)
```

## Dependency rule

```
ANDOR OSP (apps/plugin) ─┐
                         ├─> shared/andor-ui ─> src/ (osp_dsp: engine, model, analysis; no JUCE)
products/* ─> shared/andor-shell ─┘
```

- `shared/andor-ui` never includes anything from `apps/plugin` or a product.
- Every shared component takes an `InstrumentHost&`, never a concrete processor.

## The DSP boundary (ShellEngine.h)

```cpp
class Engine
{
    virtual Description describe() const;                  // name; diagnostic?; status line
    virtual juce::StringArray boundParameters() const;     // the IDs it reads ("arp.*" patterns allowed)
    virtual void prepare (double sampleRate, int maxBlock, juce::AudioProcessorValueTreeState&);   // allocate; cache raw parameter pointers
    virtual void setSource (int layer, const LoadedInstrument*) noexcept;                          // audio thread
    virtual void process (juce::AudioBuffer<float>&, const juce::MidiBuffer&, const Performance&) noexcept;  // audio thread
};
```

- **Sources.** They reach the audio thread only through `osp::ModelExchange`, one per layer.
  - The shell calls `setSource` at the start of a block.
  - Once `setSource` has returned, the engine no longer reads the old source.
  - Old sources are freed on the message thread.
- **Audio thread.** No allocation, locks, file I/O or blocking in `setSource` or `process` (CLAUDE.md rule 1).
- **Determinism.** Any randomness uses `osp::Prng` seeded from the `seed` parameter (rule 2).
- **Bound parameters decide the interface.** Every control tagged with a parameter the
  engine does not list is disabled (`ShellEditor::applyBindings`). The Advanced popover and
  the modulation routes follow the same rule:
  - **Modulation drags** are connected only if the engine binds `mod.route*`.
  - **Wheels** are enabled only if it binds `midi.pitchWheel` / `midi.modWheel`.

## The parameter schema

Every product builds `InstrumentHost::createParameterLayout()`: ANDOR OSP's 350 parameters,
with the same IDs, ranges and defaults.
- The shared components bind to these IDs.
- `tests/plugin/golden/osp-parameter-schema.txt` pins the schema. Both OSP and every product
  check against it (the `[parameter-schema]` test and `shell: the parameter schema is ANDOR OSP's`).
- Adding a parameter means appending it in `AndorParameters.cpp` with a new version hint,
  then `OSP_UPDATE_GOLDEN=1`, and documenting why (CLAUDE.md rule 8). Never rename or re-range.

Known limitation: a product exposes all 350 parameters to the host, including those its
engine ignores. The interface disables those, but the host still lists them. Trimming the
host-visible set needs a per-group layout and components that tolerate absent parameters.
That work is recorded in HANDOFF.md, "Next steps".

## State and presets

- **State.** A product saves `<STATE_TYPE schemaVersion="1" product="slug">` containing:
  - the APVTS tree (parameters and ModLinks);
  - its sources (content hash, path, filename);
  - its curves;
  - its view.
- It refuses any other product's state, ANDOR OSP's included.
- **Presets.** Files `Documents/<Product>/Presets/*.andor<slug>`; the sample store is
  `<app data>/<Product>/Samples`. Tests set `ANDOR_DOCUMENTS_DIR` and `ANDOR_SAMPLE_STORE`.

## Tests

| Test (ctest) | What |
|---|---|
| `plugin` | ANDOR OSP's full suite, including the parameter schema golden test. |
| `osp_visual_reference` | OSP renders the 18 shared states (`tests/support/VisualStates.h`). |
| `andor_<slug>_tests` | The product: identity and registry, schema, engine boundary (sources, MIDI, determinism, MUTE/LEVEL), hand-over, bad files, presets and sessions, bindings. |
| `andor_<slug>_tests_visual` | The product against OSP's renders, pixel by pixel outside the masks. |
| `andor_bootstrap_tool` | `tools/create_andor_instrument.py` in a sandbox. |
