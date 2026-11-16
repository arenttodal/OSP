# Gate C · Checkpoint C1: art direction (16 Nov 2026)

**Model:** the approved Gate B master, unchanged. Every Gate C setup renders the same product as
`andor_master_gateB_neutral.blend`: `andor_scene.py` with `--knob hd --hd 0.30 --frame-h 14.4 --finish refined --teak warm`. Only
the stage, lights, camera, float height, exposure and output differ, and they all live in one file:
`blender/config/campaign_setups.json` (setups `ANDOR_GALLERY`, `ANDOR_FLOATING`, `ANDOR_MATERIAL`). The neutral reference is
still `--light sculpt` with no `--setup`; it is untouched.

```
python product-renders/blender/scripts/andor_scene.py -- --setup ANDOR_GALLERY  --shot A1 --out A1.png
python product-renders/blender/scripts/andor_scene.py -- --setup ANDOR_FLOATING --shot B1 --out B1.png
python product-renders/blender/scripts/andor_scene.py -- --setup ANDOR_MATERIAL --shot C1 --out C1.png
# studies: --dist-scale (projected size by camera distance only) and --float (metres)
```

Each saved scene (`andor_gateC_<SETUP>.blend`) keeps the product, the stage and the lights in separate collections ("OSP
product", "Stage"). The lights and the camera are named after the setup and the shot. There is one instrument per file and
no duplicates.

**Reference:** only a crop of the Granello knob exists in this workspace, not the full advertisement. The floating direction
follows the brief's analysis instead:

- the product's scale in the frame;
- a three-quarter view that shows the sidewalls;
- contact shadows under the knobs;
- sculptural light;
- directional background lines;
- room for type.

Nothing from Granello's interface, type, colours or hardware is used.

## The five previews

Previews are draft quality at 56 samples (denoised). They are large enough to judge composition, light and materials, but
are not final exports.

### A1 · The Gallery, editorial hero (16:9, 2560 × 1440)

- **Camera:** 85 mm at 1.78 m; 38° above the floor (pitch 52); yaw −30°; product turned −11°. A horizontal shift of −0.13
  frame moves the instrument into the lower right. The upper-left 36 % × 42 % is reserved for the headline (`text_safe`).
- **Stage:** the product stands on its feet (no float) on warm pale stone (0.62 / 0.595 / 0.555, roughness 0.78), never
  white. The floor's tone comes only from the lights' falloff.
- **Light:**
  - a large soft key (1.4 × 1.0 m, upper left, 30 W) keeps the ivory's shape and lays a long gradient across the stone;
  - a narrow edge strip behind right (1.6 × 0.08 m, 22 W) draws thin highlights on the teak, the ceramic edges and the
    knob rims;
  - a weak overhead (3 W) and a cool fill (2 W) support it;
  - a low front kicker (6 W) draws the key fronts and the lip.
- **Exposure:** −0.42 (Gate B neutral: −0.55). Gallery light is a deliberate grade; the wood shader is unchanged.

### A2 · The Gallery, product hero (3:2, 2400 × 1600)

The same stage and light as A1. The camera is 85 mm at 1.36 m, pitch 50, yaw −26, and the product is centred: the full
silhouette, the cheeks and the front return at the largest size that keeps a margin.

### B1 · The Floating Instrument, cinematic still (4:5, 1800 × 2250)

- **Camera:** dedicated (not camera B). 85 mm at 1.20 m; 32° above the floor (pitch 58); yaw −36°; product turned −16°.
  The lower, more frontal view shows the front return, the cheek's end grain and the keyboard's depth. The steeper first
  draft made the ivory a bright flat rectangle, which the brief rules out.
- **Size:** +15 % projected size over a framing with margin (study below).
- **Float:** 100 mm (study below).
- **Floor:** the established dark architectural floor of peat and walnut planks with thin metal inlay lines, running
  diagonally (−95°).
- **Light:** product and floor are lit separately (light linking) but from the same side, so the shadow agrees with the
  key.
  - Product:
    - key 0.9 × 0.5 m from upper left-behind (20 W): the knobs, flutes and card bevels model;
    - rim strip behind right (28 W): the silhouette and the teak separate from the floor;
    - a top line (4.5 W): a long reflection on the ceramic bevels and the glass edges;
    - front kicker 7 W, fill 0.8 W.
  - Floor: overhead (34 W) from the key's side, and a low warm rake (26 W) that brings out the planks.
- **Exposure:** +0.10. The ivory never becomes a flat bright plane.

### B2 · The Floating Instrument, typography-safe advertising (9:16, 1440 × 2560)

The B1 stage and light. The camera is 85 mm at 1.36 m, with the shift placing the product in the lower half. The top 6–30 %
is kept for copy (`text_safe`). The copy is a separate transparent layer (`B2_type_layer.png`); the master has no text.

### C1 · The Material Study, macro knobs (1:1, 2000 × 2000)

- **Camera:** 100 mm at 0.185 m on the CHARACTER and MOVEMENT knobs; pitch 60, yaw −18; f/8. The pointers, flutes, arcs
  and dots stay sharp, and the keys fall softly out of focus.
- **Light:** a low grazing key from the left (0.6 × 0.4 m, 26 W), so the 0.252 mm flutes and the engraved pointers read.
  An edge strip draws the rim, and a weak overhead and fill support it.
- **Stage:** the gallery stone (out of frame).

## Studies (Direction B)

- **Size:** baseline, +15 % and +25 % (camera distance only). +25 % starts to crowd the frame edges; **+15 %** chosen.
- **Float:** 50, 100 and 180 mm. At 50 mm the shadow almost touches, so it reads as resting. At 180 mm it reads as
  detached. **100 mm** chosen: a clear, soft, physically placed shadow.
- These drafts used the earlier, steeper B camera. B1 and B2 use the lower camera.

## Typography

- Copy previews for A1 and B2 use the site's own type: Inter Tight (ANDOR OSP) and EB Garamond ("One sound. / An entire
  instrument.").
- Each is a separate layer, composited only for the preview. The masters stay text-free, and the layer files go to Framer
  as they are.

## Known issues

- **PITCH / MOD wheels are still flat artwork.** They are visible but small in A1, A2, B1 and B2; no macro of them is
  made. Proposed physical geometry is a C2 item. The GUI draws them as recessed slots with a coloured wheel face, so the
  model would be a shallow slot with a short, edge-on wheel segment. That is an interpretation to approve first.
- The product appears as the plugin's three-layer hero state (A/B/C cards, the macro set): the real capture, as approved.
- The previews are 56-sample drafts. Final exports (C3) will be checked at 100 % for denoiser softening of fine GUI text.

## Recommendation

- **Website lead: A1.** It is light and editorial, like a gallery piece. The interface stays legible, and it has built-in
  headline space with the instrument grounded on its feet. A2 is its gallery and product-page companion.
- **Paid advertising lead: B1** (feed 4:5), **with B2** for Stories and Reels (9:16). They have the most presence and
  drama, and the floating shadow and the inlay lines make them recognisable in a feed.
- **Material study:** C1 sets the macro language for feature transitions and the film. C2–C5 follow in the same light.

## Parallel engineering: ANALOG footer integration (plan; not started in git)

- **Pinned shell:** ANALOG builds `external/osp` at `de3253c` (the `claude/andor-analog-shell` line, product racks and FX
  faceplates).
- **Footer commits, oldest first:** `dc75a09`, `26d1389`, `ec63f01`, `1daa605`, `9933db1`, `ef5c0c3`. `a412f31` is
  andor-web only and is not needed.
- **Minimal integration:** apply only those six commits onto ANALOG's OSP line, not a whole-branch merge. A whole-branch
  merge conflicts in 8 files (12 hunks); the six commits touch 4 files. A trial on a scratch copy was discarded.
- **The conflicts are mechanical but meaningful.** ANALOG's line added `rackShift` and `sourceTrim` to
  `frame::paint` / `frame::instrumentHeight`, and the footer adds `trimmedFooter` to the same signatures. The resolution
  keeps all three:
  - `instrumentHeight (arpShown, rackShift, sourceTrim, trimmedFooter)`;
  - the apron's top moves with `rackShift − sourceTrim`, as the keys do.
  - The rest is test `#include` lists. `WebCaptureTests.inc` was removed on ANALOG's line, so the footer's capture edits
    stay out.
- **ANALOG's own controls:** SUB, AUX and the product racks are left as they are. Product-specific layouts are not forced
  into OSP's arrangement.
- **Verification:**
  - ANALOG plugin build;
  - its tests: `analog_dsp_tests` and the product's tests;
  - OSP's shell parity and trimmed-footer tests on the integrated shell;
  - before/after screenshots at every scale, plus label clearance.
- **Needs from the owner:**
  - permission to commit;
  - the branch that carries it. The session may push only `claude/friendly-pascal-0y48r0`, and the brief keeps this work
    off the campaign branch: either allow a new OSP branch (e.g. from `de3253c`) plus an ANALOG submodule bump, or name
    the branch to use.
- **Rollback point:** ANALOG's current pin `de3253c` stays recorded.

## Seen at 100 % in the C1 previews (for C2)

- **B1/B2:** the ivory runs slightly hot under the floating key, and fine GUI labels (START, TUNE…) wash out. About
  −0.3 EV on the product key, floor unchanged.
- **C1:** f/8 at 0.185 m blurs the CHARACTER / MOVEMENT labels more than the brief allows. A sharp alternative at about
  f/14 is needed for feature explanations; keep f/8 as the emotional version.
