# ANDOR Memo Lab — architecture note

Memo Lab is a standalone app (`apps/memo-lab`) around the real ANDOR OSP processor. It adds no
DSP, no sampler, no file format, and no change to the plugin.

## The one decision: host OSP's own processor

Memo Lab links the plugin's shared code (`OSP_Plugin`, as `osp_plugin_tests` does) and runs
one `OspAudioProcessor` headlessly, with no plugin editor. Everything musical is the plugin's
own code path:

| Need | OSP API it uses |
|---|---|
| ONE SHOT | `loadFile (wav, 0)`: the sample store by hash, analysis, the ordinary instrument |
| MAP NOTES | `analyseRecording (wav, 0)`: Phase 4B analysis, schema-2 atlas, Stage A then B. An unsuitable recording falls back to one ordinary sample with the reason in `statusMessage()` |
| Progress | `atlasActivity (0)` (analysing / preparing / enhancing / none), `statusMessage()` |
| Notes, pitch range | `currentInstrument (0)->atlas` |
| Corrections | the plugin's own `NoteMapEditor` (it depends only on `InstrumentHost`, which the processor implements): root pitch, exclude, audition, split, merge, all through `editAtlas` with undo |
| One-shot root | `changeRootOverride` |
| Playing | `AudioProcessorPlayer` on an `AudioDeviceManager`; MIDI inputs go to the player; the on-screen keyboard is the plugin's `OspKeyboard` on `keyboardState` |
| Save | `exportInstrument (file.ospinstrument)`: the portable instrument (manifest 2) with every sound and its checksum, which the plugin's `importInstrument` reads. Covered today by "a portable instrument carries an atlas's recordings" |

Because the saved file comes from the same code that the plugin uses to export, the plugin
opens it with no new import path. A test re-imports every Memo Lab save into a fresh processor,
with an empty sample store and the original recording deleted, and plays it.

## What Memo Lab adds (the app only)

1. **Sources** (`MemoSources`):
   - It finds the local Voice Memos copies (`~/Library/Group Containers/group.com.apple.VoiceMemos.shared/Recordings`)
     and reports exactly what happened: found, missing, empty, or not readable (macOS
     privacy).
   - It keeps a list of approved folders, which the user adds and removes, stored in
     `~/Library/Application Support/ANDOR/Memo Lab/settings.json`.
   - It scans only those folders, never following a link out of them, skipping hidden
     entries and app packages, newest first.
   - A file whose size or time changes between two looks, or that is an iCloud placeholder
     (`.name.icloud`), is listed as not ready.
   - It never opens a file for writing and never reads Apple's database.
2. **Decoding** (`MemoDecoder`):
   - It uses JUCE's readers: WAV, AIFF, FLAC, and on macOS CoreAudio (M4A / AAC, CAF).
   - On macOS, an AVFoundation fallback (`MemoDecoderMac.mm`) handles containers CoreAudio
     will not open (Voice Memos' newer `.qta`).
   - It decodes once per selection and keeps that recording in memory (at most 15 minutes,
     mono or stereo). Preview and conversion both use that buffer.
3. **The passage** (`MemoSession::writePassage`):
   - It writes the selected frames (or the whole recording) as a 32-bit float WAV under
     `Application Support/ANDOR/Memo Lab/Passages`, named after the recording and the
     range.
   - The processor copies it into OSP's sample store by content hash, so the instrument
     never depends on the memo or on this file.
   - The file is deleted once the instrument is published.
4. **Preview**:
   - An `AudioTransportSource` over the decoded buffer is a second callback on the same
     device; `AudioDeviceManager` sums callbacks.
   - Starting the preview silences the instrument (all notes off), and a played note stops
     the preview: an atomic flag set from the processor's keyboard-state listener, read on
     the message thread.
5. **The window**: the recording list (search, Refresh, Add Folder, folders); the waveform
   (seek, drag a range, start / end times, Whole recording, Clear); ONE SHOT / MAP NOTES; then
   the instrument (name, source, mode, status, NOTE MAP for atlases, the keyboard, Save
   Instrument, Audio / MIDI settings). It uses the shared ANDOR look (`OspLookAndFeel`,
   design tokens).

## Threads

- **Message thread:** UI, the processor's own load timer, starting conversions.
- **A scan thread:** folder walks and headers, sorted newest first.
- **A decode thread:** one selection at a time; a newer selection supersedes an older one.
- **OSP's loader and analysis threads:** unchanged.
- **The audio thread:** the processor's `processBlock` and the transport only; no file access
  or allocation added.

## Privacy

Nothing leaves the Mac, and there is no network code. A recording is decoded only when
selected and analysed only when the user converts it. Originals are only ever opened for
reading.

## What only the owner's Mac can prove

These must be reported as unverified until they are:
- the real Voice Memos container and its permission prompt;
- decoding of real iPhone memos (`.m4a`, `.qta`);
- CoreAudio device and MIDI behaviour;
- the Ableton round trip.

CI on macOS builds the app and decodes AAC files that `afconvert` makes (mono and stereo).
