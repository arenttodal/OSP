# Testing Phase 5 in Ableton Live

A short route through what Phase 5 adds, for a musical test in Live. It takes about 20 minutes.
This is a development build: unsigned and not notarised. It is not a release.

## Install

1. From the CI run of this commit (GitHub → Actions → CI → the run → Artifacts), download
   **OSP-macOS-test** (AU, VST3, Standalone).
2. Unzip it and double-click `install.command`. If macOS refuses, right-click → Open → Open.
3. In Live:
   - open Settings → Plug-Ins, keep "Use Audio Units v2" and/or "VST3 System Folders" on, and
     press **Rescan**;
   - drag **OSP** (AU or VST3, under Plug-Ins) onto a MIDI track.

## 1. An ordinary sample is unchanged (workflow A)

- Drop a one-shot onto the display. It plays as before.
- There is no NOTES key, nothing is analysed, and the ⋮ menu offers **Map notes…**.

## 2. A phrase becomes an instrument (workflow B)

1. Drop a sung or played melodic phrase, monophonic, up to a few minutes. It first plays as one
   sample.
2. ⋮ → **Map notes…**. The display says ANALYZING and the header "Analyzing".
3. Within a moment the notes are on their keys (header "N notes · Playable"); "Ready" or
   "Needs review" follows.
4. Play the keyboard:
   - strongly tinted keys are recorded notes (a number counts takes, played in turn);
   - pale keys are filled from a neighbour;
   - struck keys are silent.
5. Hold a note while the analysis finishes. It must not click or drop out.

## 3. Correct it (NOTE MAP)

1. Press **NOTES** in the card header. The page opens where the waveform is.
2. Click a note: it plays as recorded, and the inspector shows it. An amber dot is a doubt; the
   words appear only when the note is selected.
3. Try each correction:
   - **PITCH / OCT / FINE**: an octave error is one click;
   - **PLAYS ON** a different key, then **AUTO** to send it back;
   - drag a note's **edge** to trim it;
   - **double-click** inside a note to split it; **MERGE** joins two neighbours;
   - a dashed line with a tab is "maybe two notes": click the tab to split, or **KEEP ONE**;
   - **INCLUDED** to leave a note out.
4. Undo and redo each edit with the page's arrows, or ⌘Z / ⇧⌘Z with the plug-in window in
   front. Each edit is one step. Live's own Undo covers Live's changes, not the plug-in's edits.
5. With nothing selected, try:
   - **DETECTION** FEWER / NORMAL / MORE. Your corrections stay; the summary says where the new
     detection disagrees.
   - **FILL** on/off and its reach (±st).
   - **TUNING**: in tune, or as played.
6. Read a held note's line under the inspector. It says why the note sustains or plays as
   recorded: under 0.3 s of stable sound, decaying, or LOOP off.

## 4. Granular and KEY DNA (workflow C)

1. Switch the layer to **Granular** (the mode menu). Play chords.
2. Rest the pointer on the display: POS SIZE DENS TUNE SPREAD and, at the end, **KEY DNA**.
3. Click **KEY DNA**. The editor covers the upper part; the keyboard stays live below, and each
   bar sits over its key.
4. Turn it **ON**. Pick SIZE, raise **VARIATION**, and draw a falling **curve**: low keys get long
   grains, high keys short ones.
5. Hold a chord and move **AMOUNT**. It changes live with no clicks.
6. Automate AMOUNT from Live: in the device's Configure mode, touch AMOUNT, then draw an
   envelope.
7. **RANDOMIZE** gives every key another character; the seed is saved.

KEY DNA AMOUNT is not in the modulation bay in this build (its destinations are frozen).
Automate it in Live instead.

## 5. Three sources together (workflow D)

- Load three sounds: A a mapped phrase, B a one-shot, C Granular with KEY DNA.
- Play them together. Each card shows only its own controls.
- The keyboard shows the map of the layer you last clicked.

## 6. Save and reopen

- Save the Live set, close it, reopen it. Expect:
  - the notes, corrections, FILL, KEY DNA curves and seeds come back;
  - the audio comes from OSP's sample store (the set itself holds no audio).
- Freeze or bounce a clip twice: the results should be identical.

## What to listen for (and report)

- Notes cut in the wrong place, or one note split in two. Was the doubt shown?
- Wrong octaves. Did OCT fix them in one click?
- Clicks or dropouts while editing with notes held.
- How short notes sustain (0.3 s rule) versus playing as recorded.
- Whether KEY DNA's curve and variation feel musical across the keyboard.
