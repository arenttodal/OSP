# Signal chain pedal light (17 November 2026)

**Cause.** A pedal tile cached its bypass state (`FxRoutingView::Tile::bypassed`). It read the
state only when the board was laid out: when an effect was added, removed or moved, or a subtitle
changed. Nothing laid it out again in these cases, so the light kept its old state:
- a click on the light of a new effect (it flipped the ON parameter, then only repainted);
- automation;
- a macro's lamp or the rack's own switch;
- undo or redo;
- a session or preset recall.

The view's 8 Hz check (`signature()`) never included the ON parameters.

**Fix** (`shared/andor-ui/Source/FxRouting.cpp`, shared by ANDOR OSP and ANDOR ANALOG):
- `liveBypassed(id)` reads the authority: a new effect's ON parameter, or a built-in effect's node
  bypass in the route.
- `refreshBypass()` brings every tile's light, display and knobs to that state. It runs at once
  after a click on the light, and on the view's existing timer for every other source.
- A click on the light of a new effect is its own undo step ("Bypass EQ" / "Enable EQ").
- Bypassed, only the pedal's display (its motif) dims, to 40 %. The cream veil over the whole
  face is gone. The name, mode ("BYPASSED") and knobs stay legible (knobs at 80 %).
- The light: ON is lit with its glow; OFF is a dark lens with no glow.
- No DSP and no layout changes.

**Tests:** `osp_plugin_tests "[pedal-led]"`:
- a real click on the light flips only that instance, opens nothing and changes no selection;
- the lens is dark and the motif's contrast falls to 30–55 %, while the enamel keeps its colour;
- automation, another control writing the parameter, undo/redo, a session restore, a reorder,
  and a Mixer closed then reopened;
- a built-in effect (SPACE) with undo.

**Screenshots:** `before/` and `after/`, from `OSP_PEDAL_SHOTS=<dir> osp_plugin_tests
"[pedal-led-shots]"` (the `before/` set was made with the previous `FxRouting.cpp`).
- `pedal-03`: COMPRESSOR switched off by automation. Before, it still reads CLEAN with its light
  on. After, it reads BYPASSED with a dark light and a dimmed display.
