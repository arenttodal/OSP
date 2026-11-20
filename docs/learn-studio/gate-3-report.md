# ANDOR Learn: full capture inventory and the Learn website preview

_20 Nov 2026 · branch `feature/learn-capture-studio` · capture pin `6ef3376` (provisional) ·
owner request after Gate 1: capture the full inventory, then build the Gate 2 preview._

## Summary

- **30 clips, one per guide, all captured automatically from the real ANDOR OSP editor** and all
  passing every state assertion and video check: the seven Gate 1 pilots plus 23 new scenes. They cover
  the Gate 0 inventory that can be shown through the real interface, plus **Pedal Loop**, which the
  Gate 0 matrix had missed.
- **The runner now drives the window system itself (XTest).** Menus, submenus and the editor's
  outside-click watcher read the real X pointer, so peer-level events were not enough for right-click
  menus, submenus or the SUB row. Every click, drag, scroll and keystroke is now an X server event,
  and clicks are quick press-release pairs in real time (see "Mistakes caught").
- **The Learn website preview is built and tested locally:** Astro + MDX + Pagefind, 30 guides with a
  URL per step (113 steps), 12 reference articles, search with synonyms, the ANDOR look. 28/28 browser
  tests, 0 broken links among 2,995. Not deployed.
- **Two suspected product issues found by the capture, to verify on macOS** (not changed): a click on
  "Custom Patterns ›" does not leave the browser open; the first click on ••• after saving a preset is
  ignored.

## Results (final run, everything as committed)

| Check | Result |
|---|---|
| Final full capture, all 30 scenes, XTest input | **30/30 passed** (KEY DNA recaptured once more for framing: passed) |
| State assertions | **117/117** across the 30 scenes |
| Video checks per clip (frame counts, sizes, encoders, blank frames, frozen runs ≤ 2.5 s or the longest declared pause + 1 s, duration, poster) | all passed |
| Negative scenarios (`run-negative.sh`) | 3/3 failed as intended, no assets |
| Diagnostics (`learn/capture/tests/diag-*`) | ••• menu opens on a fresh instrument; first click after saving ignored, second opens it |
| Site: content check | 30 guides, 12 reference articles, 113 steps match captured, validated scenes |
| Site: links | 2,995 internal links in 165 pages, none broken |
| Site: browser tests | 28/28 |
| Total | 324 s of video; 30.5 MB of WebM + MP4; web assets 38 MB; slowdown ×4.7–8.1 per scene |

Earlier runs of the same scenarios (before the XTest and quick-click changes) are superseded; the
committed clips all come from the final run.

## 1. The inventory

| Group | Guide | Scene | What the clip shows |
|---|---|---|---|
| Make an instrument | Load a sound and play it | a-load-and-play | drop → ready → three keys |
| | Turn a phrase into an instrument | b-phrase-map-notes | Identify notes? → MAP NOTES → NOTES → keys |
| | Record from an audio input | c1-record-input | Record… → RECORD → STOP → 5 notes |
| | Grab what you just played | c2-grab-last | REC → GRAB LAST 10 s → 5 notes |
| | Save the sound as a preset | e1-save-preset | LIBRARY → Save… → name → Save to Library (file checked) |
| | Export a portable instrument | e5-export-instrument | ••• → Export portable instrument… (the save dialog is not filmed) |
| Play and layer | Switch between One Shot and Granular | h1-oneshot-granular | the playback key's menu → Granular |
| | **Hold a note with the sustain pedal** | p1-pedal-loop-pingpong | LOOP off → PEDAL LOOP Ping-Pong → note → pedal down (CC 64) → the read head turns back and forth → pedal up |
| | Add a second and third sound | e2-add-layers | drops onto ADD LAYER B / C (the editor's own drag handlers) |
| | Balance, mute and solo layers | e3-mix-layers | B LEVEL, S, S, M (NOT SOLOED / MUTED drawn by OSP) |
| | Load a starting state | e4-starting-state | LIBRARY → Frozen ("Loaded Frozen" checked) |
| | Add vibrato with the mod wheel | j1-vibrato | MOD → VIBRATO ON → note + mod wheel (CC 1) |
| | Control a knob from a MIDI controller | j2-midi-learn | right-click FILTER → Learn MIDI CC → CC 74 moves it |
| SYNTH & IMPRINT | Blend in SYNTH | f1-synth-blend | SYNTH page → AMOUNT → Triangle → −1 → Held |
| | Colour SYNTH with IMPRINT | f2-synth-imprint | IMPRINT mode → IMPRINT → MOTION |
| Arpeggiator | Arpeggiate held notes | g1-arp-basics | ARP opens (the window grows) → on → Up/Down → 1/16 → 2 octaves |
| | Give each layer its own arpeggio | d1-arp-per-source | LINK off → EDIT B Down → EDIT C Up/Down → A off under PLAYS |
| | Strum a chord | d2-arp-strum | Chord → STRUM → on → DOWN → SPREAD |
| | Move each layer in the stereo field | d3-satellite | SATELLITE (ARP off) → on → ORBIT → RATE |
| | Edit a custom pattern | g2-arp-custom | the custom pattern on the card → pencil → editor |
| | Add SUB under your keys | g3-sub | SUB → on → chord → LEVEL → SAW |
| Granular & KEY DNA | Shape the grains | h2-grains | hover → POS, SIZE, DENS |
| | Vary the sound from key to key | h3-key-dna | KEY DNA → ON → a stroke drawn across the graph → DEPTH → keys |
| Shape and finish | Reimagine a sound | i1-reimagined | label → Tape Frame (menu) → amount |
| | Add MOVEMENT | i2-movement | MOVEMENT → Chorus → amount |
| | Use a SHAPER pattern | i3-shaper | Shaper → EUCLID 5 → amount |
| | Make a control move with an LFO | i4-modulation | LFO 1 → dragged onto FILTER → ring |
| | Assign a USER macro | i5-user-macro | USER → right-click LEVEL → Assign to Macro ▸ Macro 1 → turn |
| | Add ECHO and SPACE | i6-echo-space | ECHO → SPACE → Plate → amount |
| | Shape the tone with FILTER | i7-filter | FILTER down → BP12 → sweep |

**Not filmed, on purpose:** native file dialogs (Browse…, Add source…, Export, Import MIDI) and DAW
routing. Their guides stop at the menu item and say what the dialog does. The "preset vs exported
instrument" topic is a reference article (`osp.ref.presets`).

## 2. Mistakes the checks and the review caught (and what changed)

| What happened | How it was caught | Fix |
|---|---|---|
| A held "click" lasted ~0.8 s of real time (frames take ~0.2 s), so a menu opening under the pointer took the release as a choice and the SYNTH chip saw a long press | ••• menu, SYNTH page and REIMAGINED modes never opened | clicks are 70 ms press-release pairs in real time; the press is still drawn |
| Menus, submenus and the outside-click watcher read the real X pointer, not peer events | code map; SUB, right-click menus | input through XTest |
| A drag resting over FILTER's knob for > 550 ms of real time opened FILTER's popover | review of the modulation clip | drags onto a target travel at an even speed and release on arrival |
| Tooltips appeared because the pointer "rested" longer in real time | review | tooltips off in the runner (disclosed) |
| RATE dragged one step gave 1/16D, not 1/16 | `paramText` expectation | three steps |
| The arpeggiator's stored pattern index differs from the list order | Gate 1 | `paramText` checks |
| KEY DNA with a flat curve showed no key-to-key difference | review | a stroke drawn across the graph |
| The starting-state click looked like nothing happened | review | `libraryStatus: "Loaded Frozen"` expectation |
| A third layer shrinks the window; ARP grows it; menus open beyond it | writer refusal | one canvas per scene, desktop margins |

## 3. Findings for the product (not changed; in `beta-readiness-in-plugin-learn.md`)

- **Custom Patterns › may not open on a click** (real X events, quick and long clicks; matches the
  code: the popover anchors to an invisible component and `mouseDownAnywhere` closes it). The guide
  shows the pencil route, which works; its set-up chose the pattern through the browser's own code
  (disclosed in the clip's record).
- **First click on ••• after Save to Library is ignored**; a second opens the menu (reproduced).
- **CAPTURE A label overlap** on a card's CAPTURE page (Gate 1) — still to confirm on macOS.
- **Pedal Loop has no in-plugin lesson or INFO topic of its own.**
- **"Needs review"** is OSP's status after mapping the tutorial phrase and the input take; the guides
  do not promise "Ready".

## 4. The Learn website preview (Gate 2)

`learn/site/` (README there). Astro 7.4.7, @astrojs/mdx 8.1.3, Pagefind 1.5.2, Inter Tight and
Geist Mono self-hosted; ANDOR web tokens.

| Page | URL | Notes |
|---|---|---|
| Home | `/learn/osp/` | title, one-line introduction, search with frequent searches, Quick Start, "Go straight to" (Phrase, Record, GRAB LAST, Pedal Loop, per-layer ARP, STRUM, SATELLITE), all guides in six groups |
| Group | `/learn/osp/<group>/` | the group's guides with posters |
| Guide | `/learn/osp/<group>/<guide>/` | the clip, numbered steps (instruction + result), previous/next, related, reference link, preconditions, host caveat where needed |
| Step | `/learn/osp/<group>/<guide>/<step>/` and `#<step>` | opens on that step; plays only that step's segment |
| Reference | `/learn/osp/reference/<article>/` | function, location, controls table, needs, guides |
| Search | `/learn/osp/search/?q=` | Pagefind; step pages, guides and reference are indexed with synonyms |

**Content model in practice:** guide front matter names one scene; its step IDs are the scene's
marks; `check-content.mjs` refuses a build if a scene is missing, failed, has other steps or was
verified at another commit. Videos, posters and step stills are copied from the manifest; failed
scenes never reach the site.

**Tests run** (`npm run build && npm test`): content check (30 guides, 12 articles, 113 steps); link
check (2,995 internal links in 165 pages); browser tests 28/28 — 14 searches each return the
actionable guide or step in the top three ("record", "live input", "grab last", "phrase transformer",
"map notes", "strum", "satellite", "imprint", "key dna", "pedal loop", "ping pong", "sustain pedal",
"sub bass", "reverb"); step URL and fragment selection; a step plays its own segment; reduced motion
does not autoplay; no horizontal scroll at 390 px; lang, one h1, alt text, labelled video and inputs.

**Not done (Gate 3 publishing, owner decisions):** hosting and the `/learn` vs `learn.` choice;
re-pin to the frozen beta UI and regenerate; owner approval per clip (`approved` is false for all);
moving the videos out of Git history (LFS or object storage) before the library is published.

## 5. Commands

```sh
cmake -B build-plugin -G Ninja -DOSP_BUILD_PLUGIN=ON -DOSP_BUILD_LEARN_CAPTURE=ON
cmake --build build-plugin --target osp_learn_capture
LEARN_SOURCE_COMMIT=6ef3376 learn/capture/tools/capture.sh        # all 30 scenes
learn/capture/tests/run-negative.sh                               # must fail closed
python3 learn/review/build.py                                     # review board
cd learn/site && npm install && npm run build && npm test && npm run preview
```
