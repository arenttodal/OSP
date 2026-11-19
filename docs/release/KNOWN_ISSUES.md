# ANDOR OSP 0.9.0-beta.1: known issues

What we already know about this beta. Please do not report these again, but do tell us if
one affects you more than described.

## Content

- **No factory instruments or recordings yet.** This beta ships the seven built-in Starting
  States. It has no factory sound packs: make instruments from your own recordings. Factory
  packs arrive in a later beta.

## Interface

- **Resizing the window is slow while dragging.** Each frame is redrawn at the new size; it
  settles when you let go. A faster resize is in development.
- **Memory after resizing.** Resizing OSP's window several times can raise its memory use to
  about 280 MB. It is released when the OSP window closes.
- **The maker is listed as "OSP".** Hosts list the plug-in as **OSP › OSP**, not ANDOR. This
  is deliberate: it keeps every project made with earlier builds working. A later version may
  change how the name is shown, never the identity projects rely on.

## Arpeggiator: custom patterns (new)

- **New feature.** Custom patterns (record, import, edit and keep) are new in this beta: your
  reports on them are especially welcome.
- **Velocity and timing.** Each step of a custom pattern keeps the velocity it was played
  with, but the editor does not yet show or edit velocity and fine timing.

## Installing

- **Earlier test builds.** If you installed an earlier OSP test build, your DAW may find two
  copies of OSP and load the old one. Run the installer again with **Customize › Move earlier
  OSP test builds aside**.
- **Disk space.** The Audio Unit, the VST3 and the app each contain the whole interface, so
  installing all three takes about three times the space of one.

## Not tested yet

These configurations were not tested before this beta. They are expected to work, and your
reports are welcome:
- hosts other than those listed as tested in the release's verification report;
- macOS versions other than the ones tested;
- every host on Intel Macs.
