# NEW_INSTRUMENT_PROMPT

A prompt to paste into a coding session (human or AI) that starts a new ANDOR instrument.
Fill in the bracketed parts; leave the rest as written.

---

You are building **ANDOR [NAME]**, a new instrument in the ANDOR family, in the repository
`arenttodal/OSP`. Its sound engine: **[one paragraph: what it does musically, what it plays
from, what the musician controls]**.

Read first, in this order:
1. `andor-design-system/HANDOFF.md`, all of it, especially "ABSOLUTE DESIGN RULES FOR NEW ANDOR PRODUCTS";
2. `andor-design-system/guide/` (01 → 06) and `guide/index.html`;
3. `CLAUDE.md` (audio thread safety, determinism, schema versioning);
4. `shared/andor-shell/Source/ShellEngine.h` and `ShellProduct.h`;
5. `products/andor-example/`, a complete product made by the tool.

Then:
1. **Create the product:**
   `python3 tools/create_andor_instrument.py --name "ANDOR [NAME]" --slug [slug] --plugin-code [Xxxx] --bundle-id com.osp.andor[slug] --tagline "[tagline, ≤ 32 characters]" --dry-run`.
   Check the plan, then run it without `--dry-run`. Do not hand-edit the registry; use the tool.
2. **Build and test before changing anything:** `cmake --build build-plugin --target ANDOR_[Name]_All andor_[slug]_tests`, then
   `ctest --test-dir build-plugin -R "andor_[slug]|osp_visual_reference" --output-on-failure`.
3. **Write the engine** in `products/andor-[slug]/Source/` as an `andor::shell::Engine`:
   - `boundParameters()` lists exactly the parameters it reads (IDs from `AndorParameters.h`);
   - `prepare()` allocates;
   - `setSource()` and `process()` neither allocate, lock nor do I/O;
   - randomness comes only from `osp::Prng`.
   Replace the diagnostic engine in `Product.cpp` only when yours makes sound.
4. **Name and bind the six macros** in `Product.cpp` (`ProductInfo::macros`). Keep the six
   positions and their colours. If a macro has settings, give it a popover built from
   `MiniPanel`, `MiniKnob` and `SegmentedControl` (`ProductInfo::macroPopover`).
5. **Test the engine headless.** Add tests next to `tests/shell/ShellTests.cpp`'s patterns:
   - deterministic output;
   - no allocation in `process`;
   - silence, odd files and huge files fail beautifully.
6. **Before every commit, keep green:**
   - `andor_[slug]_tests`;
   - `andor_[slug]_tests_visual` (0 pixels outside the masks);
   - ANDOR OSP's suite.

Never:
- copy or restyle a shared component;
- add colours, fonts, knobs, buttons, textures, shadows, radii, icons, decorations or spacing;
- replace rendered assets with drawn approximations or AI images;
- show a control the engine ignores as if it worked;
- change ANDOR OSP's identity, sound, IDs, schema or appearance;
- widen a regression mask or raise its tolerance;
- publish the rendered artwork elsewhere without the owner's authorization.

If the product truly needs something the shared interface lacks, propose it first. Then
add it to `shared/andor-ui` neutrally for OSP, and prove OSP's renders are unchanged
(`[design-capture]` before/after, and the visual regression).
