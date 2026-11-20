# ANDOR Learn: content model and capture-scenario model (Gate 0 proposal)

_Status: proposal for owner review. Nothing here is implemented yet._

Two kinds of version-controlled data, plus one generated file:

| Data | Written by | Kind (`kind`) | Where (proposed) |
|---|---|---|---|
| Guides (visual walkthroughs) | people, reviewed | `andor.learn.guide` | `learn/content/osp/guides/<group>/<slug>.mdx` |
| Reference articles | people, reviewed | `andor.learn.reference` | `learn/content/osp/reference/<slug>.mdx` |
| Capture scenarios | people, reviewed | `andor.learn.scenario` | `learn/capture/scenarios/<scenario-id>.json` |
| Asset manifest | the capture pipeline only | `andor.learn.assets` | `learn/assets/osp/manifest.json` |

Every file carries `schemaVersion` and `kind`. A reader refuses a newer `schemaVersion` instead of
guessing (the rule OSP and ANDOR ARP already follow for stored data). Adding an optional field is
documented here; changing or removing one bumps the version.

## Stable identifiers

IDs are lowercase, dot-separated, never reused, and never derived from titles.

| Thing | Pattern | Example |
|---|---|---|
| Guide | `osp.guide.<group>.<slug>` | `osp.guide.arp.strum` |
| Step (within a guide) | `<slug>` (kebab-case) | `choose-chord` |
| Reference article | `osp.ref.<slug>` | `osp.ref.satellite` |
| Scenario | `osp.scene.<nn>-<slug>` | `osp.scene.d1-arp-strum-satellite` |
| UI target | the Learn target ID, unchanged | `osp.arp.pattern`, `osp.source.mapNotes` |
| In-plugin lesson / step | the Learn catalog ID, unchanged | `osp.arp` / `pattern` |
| INFO topic | its Learn target ID | `osp.sub.row` |

Public URLs are built from the IDs, so a title can change without breaking links:

- `/learn/osp/` — Learn OSP home
- `/learn/osp/<group>/<slug>/` — a guide (first step selected)
- `/learn/osp/<group>/<slug>/#<step>` — a step (fragment; also served as `/learn/osp/<group>/<slug>/<step>/`
  so a shared link opens directly on the step with its own title and poster)
- `/learn/osp/reference/<slug>/` — a reference article

Groups (slugs fixed now): `make-an-instrument`, `play-and-layer`, `synth-imprint`, `arpeggiator`,
`granular-key-dna`, `shape-and-finish`.

## Guide (`andor.learn.guide`, schemaVersion 1)

Front matter (YAML in MDX; validated at build time with a JSON Schema):

```yaml
schemaVersion: 1
kind: andor.learn.guide
id: osp.guide.arp.strum
product: osp
group: arpeggiator
order: 60                         # position within the group
title: Strum a chord with the arpeggiator
purpose: Spread a held chord's notes over time, like a strummed guitar.
tags: [arp, strum, chord, pattern]
synonyms: [strum, strumming, guitar strum, chord spread]
controls: [osp.arp.control, osp.arp.pattern]     # Learn target IDs shown in this guide
learn:                                             # in-plugin links (facts shared, not paragraphs)
  lessons: [osp.arp]
  info: [osp.arp.control]
reference: osp.ref.strum
related: [osp.guide.arp.patterns-rate, osp.guide.arp.satellite]
preconditions:
  - A sound is loaded.
verified:                                          # editorial check against the code
  commit: <sha>
  date: 2026-11-20
  notes: docs/learn-studio/fact-sheets/strum.md
steps:
  - id: open-arp
    instruction: Click ARP beside the keyboard to open the arpeggiator.
    result: The ARP panel opens above the keyboard.
    targets: [osp.arp.control]
    scene: { id: osp.scene.d1-arp-strum-satellite, from: open-arp, to: open-arp }
  - id: choose-chord
    instruction: Open PATTERN and choose CHORD.
    result: Held notes play together on each step.
    targets: [osp.arp.pattern]
    scene: { id: osp.scene.d1-arp-strum-satellite, from: choose-chord, to: choose-chord }
```

- `instruction`, `result`, `preconditions` are separate fields (the brief's content fields). The body
  of the MDX file holds optional longer notes, never required reading.
- A step's `scene.from` / `to` name **marks** in the scenario (below), so one capture can serve
  several steps, and each step still has its own clip and poster.
- `verified.commit` must equal the pinned product commit for the page to build in release mode;
  otherwise the build lists the guide as **stale** (see "Staleness").

## Reference article (`andor.learn.reference`, schemaVersion 1)

```yaml
schemaVersion: 1
kind: andor.learn.reference
id: osp.ref.satellite
product: osp
title: SATELLITE
function: ...          # one paragraph, factual
location: ...          # where it is in the interface
parameters:            # name, range/values, what it does — from the parameter schema, not paraphrased
  - { name: ..., values: ..., does: ... }
dependencies: [...]    # preconditions (e.g. ARP on)
example: ...           # optional, short
guides: [osp.guide.arp.satellite]
parameterIds: [...]    # used by the staleness check
verified: { commit: <sha>, date: ..., notes: ... }
```

## Capture scenario (`andor.learn.scenario`, schemaVersion 1)

```json
{
  "schemaVersion": 1,
  "kind": "andor.learn.scenario",
  "id": "osp.scene.a1-load-and-play",
  "product": "osp",
  "editor": { "width": 1448, "scale": 2, "look": "ceramic" },
  "fps": 30,
  "start": {
    "state": "empty",
    "fixtures": [],
    "setup": []
  },
  "steps": [
    { "mark": "load",
      "do": [
        { "hold": 0.8 },
        { "drop": { "fixture": "tutorial:bowed", "on": "osp.source.dropZone" } },
        { "waitFor": { "state": "osp.source.ready", "timeout": 30 } },
        { "hold": 1.0 }
      ],
      "expect": [ { "state": "osp.source.ready" } ] },
    { "mark": "play",
      "do": [
        { "notes": { "keys": [48, 55, 60], "velocity": 96, "via": "keyboard", "length": 1.6 } },
        { "hold": 1.2 }
      ],
      "expect": [ { "notesPlayed": { "atLeast": 3 } } ] }
  ],
  "frame": { "mode": "auto", "context": 0.25, "follow": "ease" },
  "outputs": ["webm", "mp4", "poster", "frames"],
  "dependsOn": {
    "targets": ["osp.source.dropZone", "osp.keyboard"],
    "parameters": [],
    "sources": ["shared/andor-ui/Source/EngineCard.cpp", "apps/plugin/Source/PluginEditor.cpp"]
  }
}
```

### Actions (the runner's vocabulary)

Instructional actions are performed **through the real interface**. Each is sent as window events to
the editor's peer on a virtual display, the way `tests/plugin/PopoverMacroTests.inc` already does:

| Action | What the runner does | Recorded for the overlay |
|---|---|---|
| `click {target}` | mouse move → down → up at the target's centre (or a named part) | pointer path, press |
| `rightClick {target}` | as click, right button (opens the control's menu) | pointer path, press |
| `drag {target, dy\|dx, to}` | press, interpolated drags over the action's duration, release (a knob turned by the mouse) | pointer path, press, value before/after |
| `choose {target, item}` | click the dropdown, then click the item in the menu window that opened | both clicks |
| `drop {fixture, on}` | the editor's own `filesDropped` at the target point (an OS drag cannot be synthesised; see below) | pointer path with a file badge |
| `notes {keys, via}` | `via: keyboard` clicks the on-screen keys; `via: midi` sends MIDI to the processor (shown as the keyboard's lit keys, no pointer) | lit keys only |
| `input {fixture, seconds}` | feeds the fixture's audio into the processor's input bus, block by block | nothing (the meter shows it) |
| `hold {seconds}` | renders frames with nothing changing | — |
| `waitFor {state\|param, timeout}` | renders frames until a Learn state or parameter holds; **fails the scene** on timeout | — |

Set-up actions (allowed only in `start.setup`, never filmed as instructions):
`setParam`, `loadPreset`, `loadExample`, `openPanel` (a Learn reveal), `selectLayer`.
The runner refuses a set-up action inside `steps`, so a direct parameter write can never appear as a
visible mouse interaction.

### Expectations (fail instead of filming the wrong thing)

`state` (a Learn state, e.g. `osp.source.mapped`, `osp.arp.panel.open`), `param` (equals / changed /
range), `notesPlayed`, `targetVisible`, `layers`, `noErrorNotice`. A scene that fails any expectation
produces no video; the run report says which step and why, with the last frame attached.

## Asset manifest (`andor.learn.assets`, generated, schemaVersion 1)

One entry per scenario, written only by the pipeline:

```json
{
  "scenario": "osp.scene.a1-load-and-play",
  "scenarioDigest": "sha256:…",
  "product": "osp",
  "productCommit": "<sha>",
  "productVersion": "0.9.0-beta.1",
  "environment": { "os": "linux", "display": "Xvfb", "juce": "<tag>", "runner": "<sha>" },
  "fixtures": [ { "id": "tutorial:bowed", "sha256": "…" } ],
  "learn": { "lessons": ["osp.quickstart"], "steps": ["osp.quickstart/play"] },
  "targets": ["osp.source.dropZone", "osp.keyboard"],
  "video": {
    "width": 1600, "height": 900, "fps": 30, "frames": 312, "duration": 10.4,
    "files": {
      "webm": { "path": "osp/a1-load-and-play/a1.webm", "codec": "vp9", "bytes": 0, "sha256": "…" },
      "mp4":  { "path": "osp/a1-load-and-play/a1.mp4",  "codec": "h264", "bytes": 0, "sha256": "…" }
    }
  },
  "marks": { "load": [0.0, 4.8], "play": [4.8, 10.4] },
  "poster": { "path": "…/poster.webp", "frame": 288, "sha256": "…" },
  "validation": { "status": "passed", "checks": { "expectations": "12/12", "blankFrames": 0, "approvedFrameDiff": 0.4 } },
  "approved": false
}
```

The website reads only this manifest (no pasted filenames). `approved` is set by a separate command
after review; a release build of the site uses only approved, non-stale entries.

## Staleness and selective regeneration

A scene is **stale** when any of these differ from its manifest entry:

1. the scenario file's digest;
2. a fixture's digest;
3. a `dependsOn.sources` file changed between `productCommit` and the pinned commit (git diff);
4. a target ID it uses no longer exists, or a parameter it names changed in the parameter schema
   (`[parameter-schema]` golden);
5. the guide's `verified.commit` is older than the pinned commit and any of 1–4 hold.

`learn-capture status` lists stale scenes; `learn-capture run --stale` regenerates only those;
nothing is published automatically.

## Changes made while building Gates 1–3 (schemaVersion stays 1; all additive)

- **Guide → one scene.** A guide names exactly one scenario (`scene: osp.scene.…`) and its step IDs
  are that scene's marks, so the site plays each step's own segment and the build can prove the
  match (`learn/site/scripts/check-content.mjs`). The per-step `scene: { id, from, to }` form
  proposed above was not needed.
- **`quick: true`** puts a guide in the home page's "Go straight to" row.
- **`editorial.hostVerification`** carries a host-specific caveat that the guide shows as a note
  (RECORD and GRAB LAST: DAW routing not yet verified).
- **Scenario additions:** `editor.marginRight` / `marginBottom` (desktop around the window, where
  menus open), `editor.canvas: "arpExpanded"` (a window that grows), `frame.aspect: "auto"`,
  `maxSeconds`. Actions added: `hover`, `type`, `cc`, `scrollUntil`, `drag` with `to` / `from` /
  `toFrac`, `click` with `button: "right"` and `count: 2`. Conditions added: `paramText`,
  `libraryStatus`, `libraryStates`, `fileIn`, `popover`, `notesOpen`, `capturePage`, `recording`.
  Set-up actions added: `customPattern`, `inputHistory`, `holdNotes`.
