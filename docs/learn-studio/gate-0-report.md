# ANDOR Learn — Public Visual Guides and Capture Studio: Gate 0 report

_20 Nov 2026. Discovery and architecture only: no product code, test code or website code was
written. Companion files: `curriculum-coverage.md` (coverage matrix and curriculum),
`content-and-scenario-model.md` (schemas), and the wireframes page published for review._

## Summary

- **The capture system can be built almost entirely from what exists.** Learn 2.0's semantic
  targets and states, the hidden `[learn-shots]` / `[design-capture]` renders, the real-window
  event driver in `PopoverMacroTests.inc`, the deterministic tutorial sounds and the live-capture
  test path cover most of what a scenario runner needs. Nothing has to be added to the shipping
  plugin for the first gate.
- **Two decisions are needed before Gate 1:** which product commit the guides document (§2), and
  how `/learn` is hosted next to the Framer site (§6).
- **The in-plugin catalog is ahead of its documentation.** `OspLearn.cpp` has 15 lessons and 40 Info
  topics; `docs/learn/osp-learn.md` still describes 11 lessons and 32 topics. The lesson set has
  no coverage for live capture / GRAB LAST, per-source arpeggiators, Custom Patterns, STRUM,
  SATELLITE or SYNTH / IMPRINT (§3, `curriculum-coverage.md`).
- **Host routing for live input is unverified.** OSP's own STATUS says routing a sidechain into OSP
  in Logic, Ableton and Reaper has not been tried (needs a Mac). The GRAB LAST guide can be
  captured authentically from the test host, but its "route a track into OSP" step must stay
  flagged host-pending until someone runs it.

## 1. Repository and working tree

| Repo | Checkout | State |
|---|---|---|
| `arenttodal/OSP` | `/home/user/OSP`, branch `claude/busy-faraday-zo6se5` at `6d07440` | clean; equals the default branch |
| `arenttodal/analog` | `/home/user/analog` at `1c0cb6d` | clean; not touched |
| `arenttodal/andor-arp` | `/home/user/andor-arp` at `1afec67` | clean; not touched |

No uncommitted work from another agent was found in these checkouts. Gate 0 used a separate,
detached worktree at `/home/user/osp-learn-studio` (commit `6ef3376`) so the main checkout was never
moved.

**Branches that matter** (`origin`, fetched 20 Nov 2026):

| Branch | Head | Relation to default | What it is |
|---|---|---|---|
| `claude/charming-lovelace-n25fte` (default) | `6d07440` | — | Last PR-merged integration: PR #32 ARP STRUM, #31 menus, #29 PEDAL × REIMAGINED, #27 final polish |
| `claude/custom-arp` | `e39b0bb` | +11, linear | Custom ARP CP2–CP4 |
| `claude/macos-release` | `6ef3376` | +15, linear (contains custom-arp) | `32e0bee` = **ANDOR OSP 0.9.0-beta.1** + release infrastructure; then MIDI Learn (`e655115`), granular POS flicker fix (`c74b6af`), Custom Pattern UX R2 (`6ef3376`). **No PR into the default branch.** |
| `claude/analog-parity-r1-custom-arp` | `d1bf2d2` | +19 | ANALOG parity line incl. Custom ARP |
| `claude/friendly-pascal-0y48r0`, `claude/motion-library` | — | diverged (+196/−72, +196/−47) | Website + Blender marketing (`andor-web/`, `marketing/motion/`). Not a base. Its interface re-sync names `32e0bee`. |
| Open PRs #26, #28, #30 | — | — | Rendered macro knob D2, layer-ivory source knobs, ceramic cache. Visual: merging #26/#28 would change every frame of every capture. |

## 2. Source commit: options and recommendation

| Option | Commit | Has STRUM, SATELLITE, MAP NOTES, live capture | Has Custom Patterns | Version shown in About | Approval status |
|---|---|---|---|---|---|
| A. Default integration | `6d07440` | yes | **no** | pre-beta | PR-approved |
| B. Beta.1 version commit | `32e0bee` | yes | yes (CP4 UI, since redesigned) | 0.9.0-beta.1 | not merged; what the marketing re-sync uses |
| C. Release line head | `6ef3376` | yes | yes (**R2 UI**) | 0.9.0-beta.1 | not merged; newest owner-spec UI |

**Recommendation: C (`6ef3376`) for Gate 1, re-pinned to the frozen beta UI before Gate 3.**
The beta testers will receive the release line, not the default branch, and the Custom Patterns
UI in B has already been replaced, so B would produce a guide for an interface nobody ships.
Every capture records its exact SHA, so moving the pin later only regenerates stale scenes.

**Owner decision D1:** confirm C, or name the release candidate. Also say whether PRs #26/#28
(knob skins) are expected in the beta: if yes, Gate 1 footage will be regenerated once they land.

Branch plan once D1 is answered: create `feature/learn-capture-studio` from the chosen commit in
the existing worktree; all Learn Studio work stays on that branch; no merges from other feature
branches; no automatic merge anywhere.

## 3. Existing infrastructure audit

### Learn 2.0 (shared, `shared/andor-learn/`)

| Piece | Verified behaviour | Reuse for capture |
|---|---|---|
| `TargetRegistry` | semantic target → locator (called each time), optional part bounds, reveal, readiness; named UI states | **Primary locator** for scenario targets and expectations. No new ID scheme. |
| `LessonRunner` | step completes only on a user-origin event + postcondition | Not used for capture; its states/conditions are reused as expectations |
| `ParameterGestureBridge` | reports editor-control gestures as `param:<id>`; automation/presets report nothing | Lets the runner prove a change came from a UI gesture |
| Catalog validation | unknown targets/states/parameters fail `[learn]` | Scenario validation calls the same registry |
| Rules | interface only, message thread only, nothing on the audio thread | Capture inherits them |

### OSP adapter (`apps/plugin/Source/OspLearn.cpp`, 1315 lines)

- **15 lessons** in this order: Quick Start, From a phrase to an instrument, One Shot & Granular,
  REIMAGINED, Layering sounds, Multi-sample instruments, LIFE, KEY DNA, Arpeggiator, SUB, Add
  vibrato, Modulation, Shape the sound, Create a macro, Presets and instruments.
- **40 Info topics.** Targets include `osp.source.mapNotes`, `osp.source.notes`, `osp.sub.*`,
  `osp.vibrato.*`, `usermacro.*`, `macros.bank`, `keyboard.*` — none of which the 15 Nov document lists.
- **Doc drift:** `docs/learn/osp-learn.md` (15 Nov) lists 11 lessons, 32 topics, "8 cases, 438
  assertions". Today `OspLearnTests.inc` has 12 `[learn]` cases plus the hidden `[learn-shots]`;
  the whole `[learn]` tag runs 15 cases / 654 assertions (§9).
- **Learn has no coverage of** the ARP panel's PLAYS / LINK / EDIT, STRUM, SATELLITE, Custom
  Patterns, SYNTH / IMPRINT, REC / CAPTURE or MIDI Learn: no lesson, no target, no Info topic.
  Most of the ARP panel is painted (chips, list, octave keys), so its parts are found through the
  panel's public geometry accessors (`cardChipArea`, `linkArea`, `editTabArea`, `customRowArea`,
  `strumKey`, `satelliteKey`), test-side only.
  Recorded as a proposed doc fix, not changed here.

### Tutorial sounds (`apps/plugin/Source/TutorialSounds.*`)

Six synthetic examples from fixed seeds: `bowed` (C3), `pluck` (A3), `voice` (A3), `layered`,
`pluckSet` (G3 C4 G4 D5 × 2 takes), and **`phrase`** (a sung phrase of seven notes, added for the
phrase lesson; the header comment's list still names five). Byte-identical on regeneration (tested).
They are our own audio, so they can appear on a public site. They are not recordings of real
instruments and the guides must not call them that.

### Screenshot and render tests (all hidden, all in `osp_plugin_tests`)

| Tag | Env | What it renders | Gaps for tutorials |
|---|---|---|---|
| `[learn-shots]` | `OSP_LEARN_SHOTS` | every step of every lesson at 1086 and 869 px, with the Learn note and outline, plus browser, Info, reset, set-up, blocked, example | **Not 1448 px.** Includes the Learn overlay (we want clean UI + our own highlight). Sets state with direct parameter writes (`userSet`) — fine for stills, not for instructional motion. Single frames, no motion. |
| `[design-capture]` | `OSP_DESIGN_CAPTURE` | 1448 px states, crops, every component's bounds as JSON, 1.5×/2× | Good model for bounds export; static states only |
| `[visual-reference]` | `ANDOR_VISUAL_REFERENCE_DIR` | shared states for the cross-product pixel regression (ctest) | Approved-frame comparison pattern to reuse |
| `[capture-shots]` | `OSP_CAPTURE_SHOTS` | the CAPTURE page states | Stills of RECORD / GRAB LAST |
| `[custom-arp-shots]`, `[sub-shots]`, `[menu-shots]`, `[popover-shots]`… | various | feature stills | Reference for expected frames |
| Real-window driver (`PopoverMacroTests.inc`) | `DISPLAY` / `OSP_REAL_EVENTS=1` | puts the editor on a desktop peer (Xvfb) and sends **real mouse events** through `ComponentPeer::handleMouseEvent` | **The basis of authentic interaction.** Already proven for presses, drags, right-clicks, Escape |
| `showMenuForTest` hooks | editor, modulation overlay | intercept `PopupMenu`s instead of showing them | For capture we need the menu **shown**; see §5 |
| `menuvisual::shot` (`MenuVisualShots.inc`) | — | opens a real `PopupMenu` on the desktop and **composites the menu windows** onto the editor image | **Solves menu capture**; reuse directly |
| `SourceDisplay::softwareCaches` (`tests/support/VisualStates.h`) | — | makes the cards' cached pictures bit-repeatable | turn on in the runner |
| `CaptureTests.inc` helpers | — | `connectInput` (bus layout + prepare), `feedInput` (input through `processBlock`), `clickPart` on the CAPTURE page, the 1600-samples-per-refresh pacing of `[capture-shots]` | **the live-input fixture path**, already proven |

The website's earlier feature images came from a **local, uncommitted** `[feature-shots]` rig and a
`[web-capture]` test that exists only on the marketing branch. Its three clips moved knobs by
automation ("REIMAGINED automated from 0 to 100 %"): acceptable for marketing, not for instructions.
Nothing from that branch is needed; the pattern (Learn rig + tutorial examples) is.

**Executed during Gate 0:** see §9.

## 4. Coverage

See `curriculum-coverage.md`: the full matrix (each proposed scene → in-plugin lesson, Info topic,
targets, interaction class, capture feasibility), and the proposed public curriculum.

## 5. Capture architecture

```
scenario.json ──► learn-capture runner (C++, test-only target, Xvfb)
                    │  real OspAudioProcessor + real editor on a desktop peer
                    │  actions → window events (click, drag, right-click, keys)
                    │  audio pump: processBlock per frame (MIDI + input fixture)
                    │  expectations: Learn states, parameters, notes, visibility
                    │  per frame: editor snapshot (+ open menu windows composited)
                    ▼
                 frames/*.png + actions.json (pointer path, targets' bounds per frame)
                    ▼
                 post (Python + FFmpeg): crop/frame → pointer & highlight overlay →
                 holds & loop → encode WebM/MP4 → poster → checks
                    ▼
                 learn/assets/osp/<scene>/ + manifest.json (andor.learn.assets)
```

**Where it lives.** A new CMake target `osp_learn_capture`, built only with
`-DOSP_BUILD_LEARN_CAPTURE=ON` (off by default, never part of the plugin bundles or the release
script). It links `OSP_Plugin`'s shared code as `osp_plugin_tests` does. Scenario files, post-processing
and the website live under `learn/`. The shipping plugin's sources are untouched; a CI check can
assert that no file under `apps/plugin/Source`, `shared/`, `src/` changed on the branch.

**Reused, not rebuilt:**

| Need | Reused from |
|---|---|
| Find a control | Learn `TargetRegistry` IDs; `binding::findControl (root, parameterId)` for anything unnamed |
| Open a panel for set-up | Learn reveals (navigation only, never a parameter write) |
| Prove a state | Learn states (`osp.source.mapped`, `osp.arp.panel.open`, …) and parameters |
| Real mouse | `PopoverMacroTests.inc`'s peer + `handleMouseEvent` path on Xvfb |
| Load + wait | `addLayers` / `filesDropped`, `waitForLoads`, `pollLoads` (LearnRig) |
| Example audio | `TutorialSounds` (incl. `phrase`) |
| Live input | the `[capture]` tests' input-bus path (see §7) |
| Bounds per frame | `[design-capture]`'s bounds walk |
| Approved-frame diff | `scripts/visual-compare.py` method |

**Interaction classes and how each is driven authentically:**

| Class | Example | Driven by |
|---|---|---|
| Button / key | ARP, MAP NOTES, RECORD, GRAB LAST, S | real click on the target's bounds |
| Knob / slider | START, RATE, REIMAGINED | real press-drag-release (the editor's own slider drag) |
| Custom popover | REIMAGINED modes, LIFE, SUB, STRUM panel | real click; the popover is an editor child, so it is in the snapshot |
| `juce::PopupMenu` | PATTERN list, card ⋮ menu, ••• menu, right-click Assign | real click opens a real menu window on Xvfb; frames composite the menu windows onto the editor as `menuvisual::shot` already does; the item is clicked through the menu window's own peer |
| On-screen keys | playing notes | real clicks on the keyboard component, or MIDI into `processBlock` (shown only as lit keys) |
| File drop | load a one-shot / phrase | `filesDropped` at the drop point. An OS drag-and-drop cannot be synthesised on Xvfb; the overlay draws a file badge travelling to the drop point and the step text says "Drag a file onto…". Alternative: film **Browse…** with the file chooser, which is a native dialog and not capturable headlessly. **Owner decision D3** (below). |
| Asynchronous work | analysis, MAP NOTES, example load | `waitFor` on Learn states with a timeout; frames keep rendering so the real progress UI is filmed; timeout fails the scene |

**Message loop.** Headless, neither the editor's timers nor the processor's timer fire, and some
closes are posted with `MessageManager::callAsync` (a kept take closing the CAPTURE page, the MAP
NOTES answer). The runner owns the loop: per frame it pumps audio, calls the processor's
`pollLoads()`, the editor's `refreshNow()` and the CAPTURE page's `refresh()`, and dispatches posted
messages, so the UI advances exactly as its own timers would advance it. `prepareToPlay` is called
once per scene: it clears the capture history, so the existing `playNote` helpers (which call it)
cannot be used inside a capture scene; notes go into the same `processBlock` loop as the input.

**Timing.** Some UI timing is wall-clock based (the card's granular strip appears after 380/550 ms of
hover, readouts hold for 700 ms, notices for 3.5 s, popover animations). The runner therefore paces
frames in real time (one frame every 33.3 ms at 30 fps, rendering in between) rather than
running faster than real time. Audio is pumped in blocks of exactly 1600 samples (48 kHz) per
frame, so meters and read heads move in step with the video. Exact pixel reproducibility of
intermediate frames is not promised; end states are asserted, and approved frames are compared
with a tolerance.

**Video and editing (post).** 30 fps for all tutorial clips (the website's earlier clips were 24 fps;
30 suits UI motion and is easier to step through). Capture at 1448 px, scale 2 (2896 px masters,
not committed). Then:

- **Frame:** the union of the scene's targets plus 25 % context, snapped to a 16:9 or 4:3 frame;
  at most one slow re-frame per step, eased over 0.6 s; never cuts mid-action.
- **Pointer:** a small neutral arrow drawn from the recorded event path (the exact coordinates sent);
  a soft ring on press. No pointer for MIDI-played notes.
- **Highlight:** a thin outline in ANDOR terracotta at the active target's bounds, fading in 0.3 s
  before the action and out after the result.
- **Holds:** 0.8 s before each action, 1.2 s after its result; scenes 6–15 s.
- **Loop:** the last 0.4 s crossfades to the first frame (both are rest states).
- **Poster:** the first frame of the final hold whose target is visible and nothing is animating
  (lowest frame-to-frame difference), at the full crop.
- **Encode:** WebM (VP9, CRF ~32, no audio) as primary, MP4 (H.264 High, yuv420p, faststart, no
  audio) as fallback, poster as WebP + PNG, optional PNG frame sequence; commands logged in the
  manifest.

**Validation, per scene:** scenario schema; every target resolves and is visible inside the crop;
every expectation holds; no error notice on screen; no blank or frozen run of frames (> 1 s unchanged
outside declared holds); frame count = duration × fps; poster exists and differs from a blank
frame; approved-frame diff under threshold when an approved set exists; manifest checksums match.

## 6. Website hosting

**Facts:** the marketing site is built in **Framer** (layout, CMS, publishing). The repo side
(`andor-web/` on `claude/friendly-pascal-0y48r0`) holds React code components, tokens
(`shared/tokens/tokens.css`: `--bg #F7F6F2`, Inter Tight, Geist Mono, terracotta accent), an asset
pipeline and tests. This environment cannot open framer.com (the web team's Q-02), and that work is
on an unmerged branch.

**Can Framer host Learn cleanly?** Partly. Framer CMS can hold guide pages, but the requirements
that matter here fit it poorly:

| Requirement | Framer | Small static docs site |
|---|---|---|
| Content in version control, reviewed with the code | manual paste or CMS import | yes (MDX/JSON in the repo) |
| Pages built from the generated asset manifest | no build step | yes; build fails on a missing/stale asset |
| Per-step URLs and fragments | limited | yes |
| Search over steps, control names, synonyms | basic site search | Pagefind (static index, no server, synonyms) |
| Link checks, search tests, a11y and reduced-motion tests in CI | no | yes |
| Same look as the marketing site | native | reuse the same tokens and fonts |

**Recommendation:** a small static documentation site — **Astro** with MDX content collections
(schemas validated at build), **Pagefind** for search, and the ANDOR tokens and fonts copied from
`andor-web` (OFL fonts, self-hosted) with a check that keeps them in step. No client framework
beyond small islands (step selector, video player). It is a manual, not a second marketing site:
the header links back to the Framer site, and the Framer site links to `/learn`.

**Serving it at `/learn`** (owner decision D2):

1. **Subdomain** `learn.<domain>` on any static host (Cloudflare Pages, Netlify): simplest, no change
   to Framer; URL is not literally `/learn`.
2. **Path routing** `<domain>/learn/*` via a small edge rule (e.g. a Cloudflare Worker in front of
   the domain: `/learn/*` → the static site, everything else → Framer). Requires the domain's DNS to
   be proxied by Cloudflare; Framer's own hosting must allow a fronting proxy — to be verified with
   Framer before committing to it.
3. **Framer CMS** with exported content: not recommended (loses the manifest-driven build and tests).

Gate 2 is built and reviewed locally / as a non-production preview either way; nothing is
published to the production domain before Gate 3 approval.

## 7. Live audio capture fixture

**Path that exists:** each loaded card's **REC** key (a dot alone on narrow three-layer cards) opens
the CAPTURE page in the card's display: "CAPTURE A", an **INPUT** meter, a scrolling level picture,
**RECORD** / **STOP**, **DISCARD** (while recording) and **GRAB LAST** with **10 s / 20 s / 30 s /
60 s**. An empty instrument has **Record…** beside Browse… and Load example; the card's ⋮ menu has
"Record audio…" and the ••• menu "Record new source…". Input arrives on a stereo bus
named **Sidechain** — off by default in a plugin (the host routes a track to it; VST3 reports it as
an aux bus), on in the standalone app (the chosen microphone/interface). `CaptureBuffer` keeps 64 s;
a kept take is trimmed, stored and run through MAP NOTES automatically.

**Fixture:** a deterministic multi-note input (the tutorial `phrase`, or a dedicated fixture
generated the same way) fed into the processor's input bus in the same blocks as the frame pump —
exactly what the plugin receives from a host. The meter, level picture, trim, analysis and note
mapping are all the real code's output.

- **RECORD scene:** click REC → CAPTURE opens → click RECORD → feed the fixture for its length →
  click STOP → wait for `osp.source.mapped` → play mapped keys.
- **GRAB LAST scene:** the input is fed for ~12 s *before* anything is clicked (a connected input
  history), then click REC → GRAB LAST 10 s → the layer becomes the mapped phrase.
- **Already proven in tests:** `CaptureTests.inc` feeds a deterministic phrase through the input bus
  and drives the REC key and CAPTURE page (RECORD, STOP, GRAB) — headless, but through component
  handlers rather than window events. Gate 1 adds the real-window clicks and the frame pump.
- **Not deterministic, to handle:** the take's name is made from the wall clock
  (`take-YYYYMMDD-HHMMSS`) and can appear in the card's menu header; the REC dot's pulse is
  wall-clock timed. Neither is in the cropped frames of the planned scenes; the runner checks.
- **Truthfulness flags:** the guide's routing step ("In your DAW, send a track to OSP's Sidechain
  input" / "In the standalone app, choose your input") is **host-pending** until run in Logic,
  Ableton and Reaper on a Mac, per OSP's own STATUS and `docs/release/OSP_COMPLETION_AUDIT.md`
  ("BLOCKER: DAW host matrix … live-capture routing"). There is no host routing guide in `docs/`,
  and `BETA_TESTING.md` check 12 covers standalone RECORD only.
- **Label mismatches found** (to fix in the plugin or avoid in copy; proposed, not changed): the
  plugin's no-input hint says "Choose an audio input in Settings (the menu: Audio / MIDI settings)",
  but JUCE's standalone window shows **Options → Audio/MIDI Settings...**; STATUS calls the header
  item "Record (into a new layer)…", the code says **Record new source…**.

## 8. Effort estimate and blockers

Estimates in focused agent working sessions (≈ half a day each), excluding owner review time.

| Gate | Work | Estimate |
|---|---|---|
| 1 | runner target, real-window driver, menu compositing, frame pump, input fixture, post-processing, manifest, validation; scenes A–D | 5–7 sessions |
| 2 | Astro site, three page types, search + synonyms, manifest consumption, a11y/reduced-motion/link tests, local preview | 3–4 sessions |
| 3 | ~22–26 approved scenes (several guides share a capture), fact sheets, copy, reference articles | 6–9 sessions |
| 4 | INFO/Learn → public links (needs approval: plugin change), staleness CI, runbook | 2–3 sessions |

**Blockers and risks**

| # | Item | Effect | Mitigation |
|---|---|---|---|
| B1 | Commit to document (D1) | blocks branch creation and Gate 1 | answer D1 |
| B2 | Host routing of the Sidechain input unverified | GRAB LAST/RECORD guide's routing step | flag host-pending; owner or tester runs it on a Mac |
| B3 | Linux software rendering ≠ macOS Retina rendering | text antialiasing differs slightly from what testers see | capture at 2×; the marketing site already accepted these renders; optional Mac capture later |
| B4 | Real windows on Xvfb | bare Xvfb breaks real-window tests (measured, §9) | Xvfb `-noreset` + a minimal window manager; menu compositing already proven by `[menu-visual]` |
| B4b | Native dialogs (Browse…, Export, Import MIDI) | not capturable headlessly | film up to the dialog; describe the dialog step in text |
| B5 | OS file drag cannot be synthesised (D3) | "drop a file" is the one action not driven by a real pointer | drawn file badge + `filesDropped`; disclosed in the scene manifest |
| B6 | Knob-skin PRs #26/#28 | all footage changes if merged | regeneration; decide before Gate 3 |
| B7 | Synthetic example sounds in waveforms | guides show synthetic sounds | fine for instructions; owner may supply recordings (web team's A-01) |
| B8 | Wall-clock UI timing | intermediate frames not bit-exact | real-time pacing, end-state assertions, tolerant comparison |

## 9. What was executed in Gate 0

All on Linux (Ubuntu container), commit `6ef3376`, Release build of `osp_plugin_tests` in the
separate worktree (`build-plugin/`, git-ignored). Container packages installed for the build: the
JUCE Linux headers listed in `.github/workflows/ci.yml`, plus `openbox` (see below).

| Command (under `xvfb-run -a -s "-noreset"`) | Result |
|---|---|
| `osp_plugin_tests "[learn]"` | **passed**: 654 assertions, 15 cases |
| `osp_plugin_tests "[capture]"` | **passed**: 145 assertions, 8 cases |
| `osp_plugin_tests "[menu-visual]"` (real menu windows, composited) | **passed** with `-noreset`; **failed** (X `BadAtom`) without it |
| `osp_plugin_tests "[popover]"` (real window + real mouse events) | **failed** on a bare Xvfb (X `BadAtom` in `ChangeProperty`, all 6 cases); with a window manager (`openbox`) running: run 1 **5/6 passed**, run 2 **6/6 passed** (238 assertions). One intermittent failure, not investigated further |
| `OSP_LEARN_SHOTS=… "[learn-shots]"` | **passed**: 547 assertions, **238 images** at 1086 and 869 px, 2 min 10 s |
| `OSP_CAPTURE_SHOTS=… "[capture-shots]"` | **passed**: 7 images (no input, listening, recording; 1 and 3 layers; empty instrument) |

Findings from running them:

- **The real-window path needs a window manager in Xvfb** (and `-noreset`). JUCE looks up some
  window-manager atoms only if they exist; the tests' `prepareXServer()` interns them, but that is
  not enough for the editor's own window. The capture runner will start Xvfb with a minimal window
  manager. This also explains why `[popover]` is not part of routine CI runs on Linux.
- **Possible UI defect to verify (not changed):** in `capture-1-recording.png` the CAPTURE page's
  top-left tab label ("CAPTURE A") appears overdrawn by the card's tool icons. Needs a check in the
  real standalone app before it is reported as a bug.
- Nothing was committed to, pushed to or changed on any existing branch.

## 10. Phased plan

| Gate | Deliverables | Stops for |
|---|---|---|
| 0 | this report, coverage, schemas, wireframes | owner review (now) |
| 1 | `osp_learn_capture` + post pipeline; scenes **A** load & play, **B** phrase → MAP NOTES → play, **C** RECORD + GRAB LAST with input fixture, **D** per-source ARP + STRUM + SATELLITE; for each: WebM, MP4, poster, scenario, validation report, command | review of footage and approach |
| 2 | Astro preview of Learn OSP home, guide view, reference view, search, navigation, using pilot content only | review of the preview |
| 3 | approved beta curriculum, fact sheets, copy; release UI pinned; publish only after approval | publication approval |
| 4 | proposed INFO → public links (separate approved plugin change), stale-scene CI, maintenance runbook | — |

## Decisions requested from the owner

| ID | Decision | Recommendation |
|---|---|---|
| D1 | Product commit for the guides | `6ef3376` (release line head), re-pinned at beta freeze; say whether #26/#28 land first |
| D2 | `/learn` hosting | static Astro site; path routing via an edge rule if the domain is on Cloudflare, else `learn.` subdomain |
| D3 | How to show "load a file" | drop with a drawn file badge (real `filesDropped`), noted as such |
| D4 | Example sounds in guides | the shipped synthetic tutorial sounds for Gate 1; owner recordings later if wanted |
| D5 | Frame rate and size | 30 fps, 1448 px captured at 2× |
| D6 | Proposed plugin follow-ups (separate, later) | see `curriculum-coverage.md` §"Proposed in-plugin changes" — none are part of this project's first gates |
