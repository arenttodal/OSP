# ANDOR OSP beta testing

## For testers

Thank you for testing ANDOR OSP. Install it as **INSTALL_GUIDE.md** describes, make music
with it, and tell us what happens.

**Please try:**
- Load OSP in the DAW you use most, and on its own (the app).
- Make an instrument from one of your own recordings: a sung note, a held note of any
  instrument.
- Play it, try the Starting States, the macros, SPACE / ECHO / MOVEMENT, the arpeggiator
  (including custom patterns: record one from your keys), PEDAL LOOP.
- Save a project in your DAW, quit, reopen it: does OSP come back exactly as you left it?
- Save a preset, load it again.

**When you report a problem, include:**
- your Mac (Apple silicon or Intel), the macOS version (Apple menu › About This Mac);
- the DAW and its version, or "the app";
- OSP's version (main menu › SUPPORT › About ANDOR OSP, for example "0.9.0-beta.1 (build 1, …)");
- what you did, what you expected, what happened. A screenshot or a short screen recording
  helps;
- for a crash: Console.app › Crash Reports, the newest report mentioning OSP or your DAW.

## For the owner: the manual checklist

The release command proves everything it can automatically, and its `VERIFICATION_REPORT.md`
says what ran. The following needs a person, a DAW, or a second Mac. Tick only what was
actually done, and note the Mac (Apple silicon or Intel), the macOS version and the host
version.

### Before inviting testers (beta 1 gate)

| # | Check | Mac / macOS / host | Result |
|---|---|---|---|
| 1 | Fresh install on a Mac that never had OSP (or after the uninstall steps): installer opens without any warning, all three formats install | | |
| 2 | Upgrade over an earlier unsigned test build, with "Move earlier OSP test builds aside" ticked: the old copies are in `~/Library/Application Support/ANDOR/Previous Test Builds`, only one OSP in the host | | |
| 3 | The same upgrade with the option unticked: note which OSP the host loads (expected: possibly the old one; the welcome page explains) | | |
| 4 | Logic Pro: OSP appears under AU Instruments › OSP; loads; plays | | |
| 5 | Ableton Live (or REAPER): OSP appears as a VST3; loads; plays | | |
| 6 | The standalone app opens from Applications, no Gatekeeper warning | | |
| 7 | Drop a recording onto source A: it loads and plays across the keyboard | | |
| 8 | The seven Starting States load from the menu | | |
| 9 | Save a preset, load another, load the saved one: identical | | |
| 10 | Save a DAW project with OSP (a changed sound, an arpeggiator custom pattern), quit, reopen: identical | | |
| 11 | Automate a macro in the DAW: it moves while playing back | | |
| 12 | Standalone: RECORD from the microphone: macOS asks once; the take becomes an instrument | | |
| 13 | A DAW project saved with an earlier OSP test build opens with this version, sound intact | | |
| 14 | With Wi-Fi off: the app and the plug-ins open and play (no network needed) | | |
| 15 | Existing user content (presets, samples, packs from earlier builds) still in the Library after installing | | |

### Architectures

The binaries are universal. Each architecture needs its own run:
- **Apple silicon:** the owner's Mac, checks 1–15.
- **Intel:** a second Mac, at least checks 1, 4 or 5, 6 and 7. A Logic Rosetta session on Apple
  silicon does not count as an Intel test.

### Record

Save the filled table beside the release folder (`dist/release/andor-osp/<version>/`) as
`MANUAL_TESTS.md`.
