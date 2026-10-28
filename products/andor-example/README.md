# ANDOR Example

An ANDOR instrument built from the ANDOR Instrument Shell (`shared/andor-shell`), written by
`tools/create_andor_instrument.py` on 2026-10-28.

| | |
|---|---|
| Product name | ANDOR Example |
| Slug | `example` |
| Plugin code | `Aexm` (manufacturer `Ospx`) |
| Bundle ID | `com.osp.andorexample` |
| Version | 0.1.0 |
| CMake target | `ANDOR_Example` (tests: `andor_example_tests`) |
| Presets | `Documents/ANDOR Example/Presets/*.andorexample` |
| Saved state | `<ANDOR_EXAMPLE product="example">` |

Build: `cmake -B build-plugin -G Ninja -DOSP_BUILD_PLUGIN=ON && cmake --build build-plugin --target ANDOR_Example_All`
Test: `ctest --test-dir build-plugin -R andor_example_tests --output-on-failure`

What to change, and what never to change: `andor-design-system/HANDOFF.md`.
- `Source/Product.cpp`: the identity, the macros, the engine.
- The engine: implement `andor::shell::Engine` (`shared/andor-shell/Source/ShellEngine.h`).
  It names the parameters it reads; every other control is shown disabled.
- The look is ANDOR OSP's, from `shared/andor-ui`: never copy or restyle it here.
