# ANDOR Memo Lab — MVP 0.1 brief (queued)

From the product owner, 2026-10-30. Status: **queued**. This is the brief as given, condensed
only in layout; requirements are unchanged.

**iCloud Voice Memos to playable ANDOR instruments.** Repository arenttodal/OSP. Target macOS /
Apple Silicon. A standalone test application sharing the existing ANDOR OSP engine and
instrument-building pipeline. Primary user: the artist developing ANDOR OSP. A private working
prototype, not a commercial release.

Goal: a genuinely working application that browses iCloud-synced Voice Memos on the Mac, lets
the user select a recording or passage, and turns it into a playable ANDOR instrument. The
deliverable is a working application, not another architecture document. Make reasonable
implementation decisions autonomously; prioritise a functional end-to-end workflow over
generality or feature completeness.

> "I want to record something on my iPhone, see it on my Mac, click one button, and play it as
> an ANDOR instrument." Implement the smallest reliable system that makes that possible.

## 1. The experience

Record a memo on iPhone → iCloud syncs it → launch Memo Lab → list of recordings → select and
listen → optionally select a passage → **ONE SHOT** or **MAP NOTES** → the existing ANDOR
engine builds an instrument → play it at once (computer keyboard or MIDI) → save it and open it
in the main ANDOR OSP plugin.

Main success story (the acceptance scenario everything serves):
1. Record a 10-second melody on iPhone.
2. Let it sync through Apple's normal Voice Memos sync.
3. Open Memo Lab on the Mac.
4. Find the recording.
5. Select it.
6. Click MAP NOTES.
7. Wait for Stage A to become playable.
8. Play several pitches from a MIDI controller.
9. Save the instrument.
10. Open it in the regular ANDOR plugin inside Ableton.

No AirDrop, no Dropbox, no manual slicing, no export from Voice Memos when direct Mac
integration works.

## 2. Architecture

A **separate macOS development application**, "ANDOR Memo Lab". Do not modify the main OSP
interface as the primary strategy, and do not create a new audio engine: Memo Lab is a
lightweight shell around existing OSP components.

Reuse:
- the engine and analysis: InstrumentEngine, InstrumentSet, Note Atlas, Phase 4B phrase
  analysis and note segmentation, the Atlas builder, Stage A / B preparation;
- the storage and formats: the sample store, portable instrument and preset infrastructure,
  library/asset hashing where appropriate;
- the JUCE MIDI/audio playback infrastructure.

Never duplicate core DSP.

Why separate:
- the plugin is complex and under visual refinement;
- a standalone app gets easier folder permissions, independent development, faster testing,
  and a place to prototype Voice Memos integration and to debug decoding and analysis;
- it avoids plugin-host sandbox complications;
- it allows direct musical testing.

Structure the browser so it can later move into OSP or The Library. No premature generalised
companion-service architecture.

## 3. Codebase audit first

Inspect the actual code:
- the OSP standalone target and audio-device setup;
- InstrumentEngine note on / off;
- instrument loading and InstrumentLoader;
- NoteAtlas, NoteAtlasJson, AtlasSet, PhraseAnalysis, AnalysisQueue, the Stage A / B builders;
- portable export / import, preset persistence, the managed sample-store layout;
- the MIDI keyboard components and the ANDOR design system.

Also review the Phase 3, 4 and 4B reports.

Identify the exact API that turns an immutable audio asset into a playable instrument, and how
to save it in a format the production plugin already understands. A raw `.andoratlas` is not
enough: atlas metadata has no audio.

Write a short architecture note, then build.

## 4. Voice Memos access (the most important integration)

**4.1 Primary method: local synced copies.** A commonly observed location is
`~/Library/Group Containers/group.com.apple.VoiceMemos.shared/Recordings/`. This is an Apple
implementation detail, not a public API. Discover and validate it on the real Mac. Do not
assume it exists on every macOS, do not assume every recording is downloaded, and never claim
access succeeded when it did not.

**4.2 Access strategy.** On first launch:
- check the known location: does it exist, is it readable, which supported files are there;
- if denied, explain why and offer an explicit folder choice;
- if a privacy authorisation is needed, explain the minimum permission.

Never bypass macOS permissions, never request Full Disk Access automatically, and never
require admin rights. A non-sandboxed personal build is acceptable, but access stays
user-authorised and restricted to the chosen folder. Store a persistent read-only
security-scoped bookmark where supported. If the protected container stays inaccessible, use
the user-selected folder mode and report the limitation clearly.

**4.3 Approved folders.**
- One or more: the Voice Memos directory, another synced folder, or an iCloud Drive folder.
- Add Folder and remove a folder; references are stored.
- Never scan outside approved locations; no whole-disk search.

**4.4 No reliance on the Voice Memos database.** CloudRecordings.db (or similar) may optionally
supply titles. Never write to it, never depend on it for the basic list, never run migrations,
and never assume its schema. The MVP works from audio files alone, with filename titles if
nothing reliable is available. Do not claim Voice Memos folders or names unless actually
resolved.

**4.5 Read-only.** Never delete, rename, move or modify originals or Apple metadata, never
reorganise Voice Memos, and never write Atlas JSON into the Voice Memos folder. Everything
generated lives in ANDOR-managed storage.

## 5. Recording discovery

**Initial scan on open:**
- enumerate the supported audio in approved folders, with basic metadata, newest first;
- show loading and error status;
- do not recurse into unrelated app directories and do not follow symlinks outside the roots;
- inaccessible entries never crash;
- a manual Refresh is enough (folder watching is optional, not a blocker).

**Metadata:** title or filename, date where available, duration, availability, and a simple
waveform when selected.

**Formats:** M4A / AAC (Voice Memos), WAV, AIFF; FLAC if the existing decoder supports it. Check
JUCE's macOS readers; if M4A is unreliable, add a small native AVFoundation / CoreAudio
decoding adapter. FFmpeg is not the first solution; no cloud conversion.

**Readiness:** do not analyse partially synced or still-written files. Validate that the file
opens, the duration is plausible, decoding succeeds, and the file is not changing during the
import. A file not available locally shows an actionable unavailable state. Never alter the
original.

## 6. The recordings browser

- **Left — Recordings:** a searchable list (name, date, duration, selected state), Refresh, Add
  Folder. No artwork, preview cards or dashboard.
- **Centre — Preview:** one large waveform with play / pause, scrub, duration, range selection,
  Use Entire Recording, Clear Selection, and precise start / end times.
- **Bottom — Make Instrument:** ONE SHOT and MAP NOTES; one conversion at a time.

Design: the ANDOR palette and typography; minimal, quiet, precise, warm, elegant, uncluttered,
focused on listening and creating. No complex navigation, no extra tabs, no fake physical panel
around every action, no new visual product. A clean development interface from existing ANDOR
components beats elaborate unfinished styling.

## 7. Audio preview

Click to select, Space to play / pause, click to seek, drag to select a range, play the range,
return to the whole recording, a playhead, duration and range times. Preview must not
interfere with the instrument engine: stop the preview when instrument audition starts, and the
other way round. With no range selected, use the whole recording (within capacity limits); with
a range, exactly that section, frame-accurate where practical. Never destructive: store the
passage metadata and create a managed source asset.

## 8. ONE SHOT

Select a memo or range, then click ONE SHOT:
1. decode;
2. create the managed immutable source asset;
3. run OSP's existing instrument builder;
4. publish safely;
5. show a keyboard;
6. play;
7. save.

Use the existing pitch / root inference, with a root adjustment only if needed. No second
sample player.

Acceptance:
- a sustained vocal plays at different pitches;
- a short pluck plays as a conventional sample;
- the source is unchanged.

## 9. MAP NOTES (the central feature)

The existing Phase 4B pipeline:
1. decode;
2. prepare the immutable source;
3. analyse the phrase;
4. detect notes;
5. generate a schema-2 Note Atlas;
6. build and publish the Stage A InstrumentSet;
7. run Stage B in the background;
8. publish safely without disrupting sounding voices.

No new pitch detector, no new note splitting, no accuracy research.

**Results:** the note count, pitch range, basic note labels, and whether analysis completed,
whether the recording was unsuitable, and whether a usable instrument is ready. It is playable
at once.

**Minimal editing:** change root pitch, exclude a note, select and audition a note, split,
merge adjacent. Reuse the existing NoteMapEditor if that is safe; do not reimplement NOTE MAP.
If reuse needs major architectural change, keep only pitch correction and exclusion, and
document the rest.

## 10. The playable instrument area

Show the instrument name, the source name, the mode (One Shot / Note Atlas), a small status, a
keyboard (preferably OSP's existing one), and SAVE INSTRUMENT. Support external MIDI through
JUCE MIDI input, with audible stereo output on the chosen device, plus basic audio-device and
MIDI-input selection if the standalone shell does not already provide it.

Use OSP's existing playback, polyphony, continuation, per-note pitch mapping, and velocity /
round robin. KEY DNA is not required; granular controls only if they are nearly free. No
effects or macro pages.

## 11. Save as an ANDOR instrument (critical)

The saved result **must load in the real ANDOR OSP plugin**; no temporary-only structure. First
inspect:
- portable export and import, atlas source packaging, sample-store hashes;
- instrument manifests, preset serialisation, per-layer state, asset resolution.

Reuse the supported format and do not invent a second one.

SAVE INSTRUMENT opens a save dialog and writes a portable bundle with the audio assets and the
Atlas / instrument metadata in the current OSP format, self-contained where the importer
supports it.

Import test in the real plugin:
1. open Ableton and load ANDOR OSP;
2. import the instrument and check the source is restored;
3. play it from MIDI;
4. save the Ableton project, reopen it, and check it still works.

If the portable format cannot represent the instrument, make the smallest compatible extension
to import / export, with tests. Do not redesign the plugin. Not complete until the round trip
works.

## 12. Storage and privacy

Originals stay untouched. Use the OSP managed asset store where practical, and Application
Support for app-specific data:
- approved folder references and minimal settings;
- imported audio assets;
- the analysis cache and Atlas metadata;
- temporary waveform thumbnails.

No cloud upload, telemetry, analytics, login, server, transcription or remote AI. Voice Memos
can be private: do not analyse every recording automatically. Browsing names and metadata after
access is granted is fine; full analysis starts only on a user's conversion request. Do not
inspect unrelated folders, and leave no unbounded temporary copies.

## 13. Failure handling

Simple, actionable messages:
- **Voice Memos unavailable:** choose the folder manually or adjust permissions.
- **Recording unavailable:** not downloaded or cannot be opened.
- **Unsupported format.**
- **No usable pitches:** try ONE SHOT.
- **Instrument build failed:** a concise reason; the original stays available.
- **Missing audio on load:** name the missing asset.

No crashes, endless spinners, silent failures or false success.

## 14. Performance

Browsing stays responsive. Scans never block the UI. Decoding stays off the audio thread, and
analysis runs on the existing background workers. Stage A is playable before Stage B finishes.
The audio thread never allocates or touches the filesystem. Never process the whole archive on
first launch, and never decode the same recording twice.

A typical 5–20 s memo should convert without distracting waiting. Measure on Apple Silicon
rather than promising a fixed latency.

## 15. Milestones

- **M0 — Verify Mac Voice Memos access.** A minimal diagnostic: locate, confirm permissions,
  enumerate, decode one real memo, report failures. If blocked, keep the approved-folder route
  and proceed. Do not get stuck reverse-engineering private databases.
- **M1 — Browser:** folder selection, scanning, the list, search, refresh, metadata.
- **M2 — Preview and selection:** waveform, play / pause, scrub, range, whole recording.
- **M3 — One Shot** through the existing OSP builder, playable.
- **M4 — Phrase to instrument:** Phase 4B plus staged Atlas preparation, playable from MIDI.
- **M5 — Export / import:** a portable instrument, loaded in the production plugin with its
  audio.
- **M6 — Quality pass** on the development Mac (crashes, permissions, decoding, loading states,
  interaction); a private development build. Stop for user testing.

## 16. Test matrix

- **Discovery:** accessible, missing, access denied, empty, new recording, still syncing,
  corrupt, many recordings, a non-audio file, a symlink outside the folder.
- **Audio:** mono M4A, stereo M4A, WAV, typical iPhone rates, short and long recordings,
  preview, range extraction.
- **Conversion:**
  - One Shot succeeds, Note Atlas succeeds;
  - unpitched content fails gracefully;
  - background preparation;
  - MIDI on / off, chords and sustained notes, with no stuck voices;
  - no audio-thread allocations.
- **Export:** save, reopen, load in the plugin, all assets resolve, the pitch mapping persists,
  the session saves and reloads, the original is unchanged.
- **Regression:**
  - OSP builds pass; Phase 0–4B audio tests unchanged; presets compatible;
  - no new host parameters;
  - the main plugin GUI is untouched except for essential compatible import support.

## 17. Visual direction

Functional MVP in the ANDOR design system: the recording list on the left, the waveform and
selection in the centre, ONE SHOT / MAP NOTES at the bottom, and after a build a compact
keyboard and SAVE INSTRUMENT.

Avoid:
- dashboards, category cards and giant buttons;
- AI-generated aesthetics and unnecessary tabs;
- new wooden hardware illustrations.

Reuse ANDOR's typography, neutral tones, graphite surfaces and restrained accents. Never block
function for polish.

## 18. User acceptance test

1. Record a sung or bowed melody in Voice Memos on iPhone.
2. Check it appears on the Mac after sync.
3. Open Memo Lab and find it without AirDrop or exporting.
4. Select it and listen; optionally select a short passage.
5. Click MAP NOTES.
6. Play the notes from MIDI.
7. Save the instrument.
8. Open the regular OSP plugin in Ableton, import it, and play it.
9. Save and reopen the Ableton project, and confirm all source audio resolves.

A separate ONE SHOT test must also work. If direct access cannot be authorised, run the same
test from a user-selected local folder and mark the direct Voice Memos path as unresolved.
Never report the iCloud integration as working unless a memo actually synced from the phone
and imported.

## 19. Deliverables

1. A working macOS application.
2. Source in the OSP repository, with no copied DSP.
3. Read-only Voice Memos discovery and approved-folder selection.
4. The recording browser, waveform preview and passage trimming.
5. ONE SHOT and MAP NOTES conversion.
6. Playable keyboard and MIDI output.
7. Save as a compatible ANDOR instrument, with OSP import verified.
8. Apple Silicon results and screenshots.
9. A short user testing guide and the known limitations.
10. A commit and branch.

Priorities:
- **P0:** read a synced recording, preview it, convert it to a playable instrument, save it and
  open it in regular OSP.
- **P1:** passage selection; search, refresh and remembered folders.
- **P2:** minor polish and optional folder watching.

Nothing else is required.

## 20. Final instructions

A narrowly scoped proof of concept: not a startup platform and not a new sampler. Do not add:
- speech transcription, AI search, texture classification or cloud infrastructure;
- an iOS app;
- a new NOTE MAP in the regular plugin, or a redesign of the plugin.

Never modify the original recordings. Do not stop at a browsing mockup: make the workflow
functional. Build, test, commit and provide the development application for musical
evaluation.

## Notes for whoever picks this up

- Development here runs in a Linux container. Everything that can be built and tested
  headlessly is (the scanner, decoding of WAV / AIFF, range extraction, conversion, portable
  save and plugin import), and macOS CI builds the app.
- These need the owner's Mac and must be reported as unverified until they are:
  - the real Voice Memos container, its permissions and M4A decoding of real memos;
  - Ableton;
  - the iPhone sync.
