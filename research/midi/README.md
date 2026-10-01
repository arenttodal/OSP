# research/midi

Standard MIDI fixtures (fixture version 1), written with reference note C4 by

```sh
./build/apps/research-renderer/research-renderer --write-fixtures research/midi
```

They are frozen: a unit test checks that these files match the generators in
`src/midi/MidiFixtures.cpp`. The renderer normally generates the same sequences at the
source's detected root (`--fixture <name>`); use these files with `--midi` or in a DAW.
120 BPM, 960 PPQ. See `docs/testing.md` for what each fixture contains.
