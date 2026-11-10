---
title: Installation
kicker: Support
summary: Install ANDOR OSP on your Mac, find it in your DAW, and remove it if you need to.
---

> ANDOR OSP is in development. These steps describe the release installer. They will be
> checked on real Macs before release, and this page will be updated if anything changes.

## What you need

- A Mac with **macOS 11 (Big Sur) or later**, Apple Silicon or Intel. The plugin is a
  universal build, so it runs natively on both.
- A DAW that hosts **Audio Units** or **VST3** plugins, such as Logic Pro, Ableton Live or Reaper.
  You can also play OSP on its own with the **standalone app**.
- An audio interface or the Mac's built-in output. To record into OSP in the standalone app,
  you also need an input: a microphone or your interface.

Before release, OSP will be validated in Logic Pro, Ableton Live and Reaper. We will list every
host it has been tested in here.

## Install

1. Download the installer, `ANDOR-OSP-[[version]].pkg`.
2. Double-click it and follow the steps. It is signed and notarised by Apple, so macOS opens it
   without warnings.
3. The installer puts three things on your Mac:

   | What | Where |
   |---|---|
   | Audio Unit | `/Library/Audio/Plug-Ins/Components/OSP.component` |
   | VST3 | `/Library/Audio/Plug-Ins/VST3/OSP.vst3` |
   | Standalone app | `/Applications/OSP.app` |

4. Quit and reopen your DAW so it finds the new plugin.

## Find it in your DAW

- **Logic Pro:** create a Software Instrument track, then open the instrument slot →
  **AU Instruments → [[manufacturer name]] → OSP**.
- **Ableton Live:** in **Plugins**, open the VST3 (or Audio Units) folder and drag **OSP** onto
  a MIDI track. If it isn't listed, go to **Settings → Plug-Ins** and choose **Rescan**.
- **Reaper:** **Insert → Virtual instrument on new track**, then search for **OSP**. If it isn't
  listed, open **Preferences → Plug-ins → VST** and choose **Re-scan**.
- **Standalone:** open **OSP** from Applications. Choose your audio device in **Options →
  Audio/MIDI settings**.

## Recording into OSP

OSP can turn what you play into an instrument (**REC** on any card: RECORD, or GRAB the last
seconds).

- In the **standalone app**, macOS asks once for permission to use the microphone. OSP only
  listens while a REC page is open. Nothing is kept until you keep a take, and nothing leaves your Mac.
  To change the permission later, go to **System Settings → Privacy & Security → Microphone**.
- In a **DAW**, route a track into OSP's sidechain input. Your DAW decides what OSP hears, and
  no microphone permission is involved.

## Where your files are kept

| What | Where |
|---|---|
| Your Library (sounds, tags, collections, history) | `~/Library/Application Support/OSP/Library` |
| Copies of the sounds you load | `~/Library/Application Support/OSP/Samples` |
| Presets | `~/Documents/OSP/Presets` |
| Exported instruments (`.ospinstrument`) | `~/Documents/OSP/Instruments` |

Everything stays on your Mac. To move to another computer, use **⋯ → Back up the Library…**
and **Restore a backup…** (see [Documentation](/documentation)).

## Uninstall

Delete these, then restart your DAW:

- `/Library/Audio/Plug-Ins/Components/OSP.component`
- `/Library/Audio/Plug-Ins/VST3/OSP.vst3`
- `/Applications/OSP.app`

Your sounds, presets and Library are **not** removed, so a reinstall finds them again. To remove
them too, delete `~/Library/Application Support/OSP` and `~/Documents/OSP`. Back up first if you
might want them.

## If something doesn't work

- **The plugin doesn't appear.** Restart the DAW, then rescan plugins (steps above). In Logic,
  open the **Plug-in Manager** (Logic Pro → Settings → Plug-in Manager) and check that OSP
  passed validation. If it failed, select it and choose **Reset & Rescan Selection**.
- **No sound in the standalone app.** Check the output device in **Options → Audio/MIDI
  settings**, and play a key on screen.
- **Recording hears nothing.** Check the microphone permission (above) and the input device in
  **Options**.
- **Still stuck?** [Write to us](/contact) with your macOS version, your DAW and what happened.
