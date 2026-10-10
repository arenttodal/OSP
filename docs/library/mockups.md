# Library mockups (received)

`mockups/library-mockups.png`: the user's ten-panel reference for the Library GUI (Stage 5
onward). The existing ANDOR/OSP visual system stays authoritative; these fix layout and
content.

| Panel | View | What it fixes | Stage |
|---|---|---|---|
| 1 Main interface | the instrument | **not adopted** (the user, 2026-10-10: ignore this panel). The instrument's current top bar and layout stay; the Library opens from the existing interface | - |
| 2 Presets view | A | one overlay titled ANDOR/OSP LIBRARY with tabs Presets / Templates / Sounds; left column: Collections (All Presets, Factory, User, Packs, Favorites, with counts) and Categories (Keys, Pads, Basses, Plucks, Rhythmic, Textures, Evolving, FX, Experimental, with counts); search; a table Name / Category / Origin / Rating / Date; a detail strip (artwork, name, origin, category, size, description, heart, Load, Replace, more) | 5 |
| 3 Templates view | B | the same frame; categories Evolving, Rhythmic, Granular, Keys, Basses, Textured, FX; detail: "Ready for your own sounds", Load Template | 5 |
| 4 Sounds view | C | left: Library (All Sounds, Recent, Favorites, Inbox), Collections (Factory, User, Flux, Field Recordings, Drums, Voices, Imported), Types (Bass, Keys, Pluck, Pad, Percussion, Vocal, Texture, Other), with counts; filters All Types / All Collections / All Origins / Any Length and a One-shots switch; rows with a waveform thumbnail, Type, Length, Origin, Date; the 3-slot audition area (A B C with waveforms, play, Load to Instrument) | 4, 5 |
| 5 Sound preview & tagging | D | a large waveform with playhead and time; Type, Collection, Tags (chips + add), Notes; Properties (length, channels, added, last used, sample rate, file size); Root note, Tune, Gain, Normalize; Load to A / B / C / First Empty; heart | 4, 5 |
| 6 Scan folder | F | Scan Folders / Import / Analysis Settings tabs; folder field + Choose; scan subfolders, find likely one-shots, skip files longer than 60 s; live counts (files found, analysed, likely one-shots, possible, multiple events, likely loops, other); progress, current file, Cancel | 6 |
| 7 Scan results | G | rows with checkboxes, classification chips (likely one-shot, multiple events, likely loop, possible), length, tags; Trim Selected | 6, 8 |
| 8 Inbox | H | Inbox / Importing / Processed; source folder, watch for new files; rows with status (Ready, Processing); a trim view with handles; Play, Trim to Selection, Import to Library | 7, 8 |
| 9 Context menu | - | the halo / knob modulation menu (already built: Assign MOD WHEEL / ENV / LFO, Remove, depth items) | done |
| 10 Save dialog | E | Name; Type: Preset (with sounds) / Template (no sounds, "audio files will not be included"); Origin; Category; Tags; "Include audio as portable copy (recommended for sharing)"; Cancel / Save to Library | 3, 5 |

## What they add to the data model

- **Rating** (0-5 stars) for presets and templates (and sounds): catalog v3 `assets.rating`.
- **Category** lists per type (presets, templates) and **sound Type** (Bass, Keys, Pluck, Pad,
  Percussion, Vocal, Texture, Other), separate from free tags: `assets.category` holds both
  (a sound's category is its Type).
- **Artwork** for presets and templates: the mockup shows photographs. Decided (D-09, the
  user, 2026-10-10): no images are attached or shipped; each preset and template shows a
  generated gradient placeholder (colours derived deterministically from its id, so it is
  stable), optionally textured from its sounds. Nothing is downloaded.
- **Size** of a preset (14.2 MB: its sounds) and of a template (45 KB): computed, not stored.
- **Inbox** as a Library section with its own count.
- **Save dialog** "Include audio as portable copy": saving a preset can also write the
  portable `.ospinstrument` beside it.
