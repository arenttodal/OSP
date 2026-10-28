# research/configs

Render configurations for `research-renderer --config <file>` (schemaVersion 1).
Missing fields keep their defaults. `baseline-a.json` and `baseline-b.json` are the
reference baselines every experiment is compared against; add new files for
experiments instead of editing these.

`arp-c-updown-16th.json` puts the arpeggiator ahead of engine C. Any config may carry the
same `"arp"` block (baselines A and B included): the fixture's notes are arpeggiated exactly
as the plugin does it, before the engine plays them.

`mod-c-lfo-cutoff.json` runs the modulation system headless: LFO 1 on the CHARACTER cutoff
and ENV 1 on layer A's level (the `"modulation"` block: `lfo1`/`lfo2`, `env1`/`env2`, and
`routes` by source name and destination id, see `docs/reports/modulation.md`).

`eq-c-hp-bell.json` gives layer A its EQ (the `"eq"` block: `enabled`, then `bands` by name,
`hp`, `lowShelf`, `bell`, `highShelf`, `lp`, each with `frequencyHz`, `gainDb`, `q` or `steep`;
a band listed is on). Without the block the EQ is off and nothing changes.

`granular-c-layers.json` plays up to three source layers (the `"layers"` array: A first, it is
the render's `--source`; B and C name their own `"source"`, relative to the config file).
Each layer takes `sourceMode` (`oneShot` / `granular`), `granular` (`position` 0..1,
`sizeSeconds` 0.02..0.4, `density` 4..40, `tuneSemitones` -12..12, `spread` 0..1, the
plugin's ranges) and its controls (`start`, `tuneSemitones`, `pan`, `levelDb`, `reverse`,
`loop`, `follow`, `reimagined`). `"mix"` places the layers (`blend` for two, `x`/`y` in the
triangle for three); `"reimaginedRouting": "perLayer"` gives each layer its own Reimagined
stage. A missing layer source leaves that layer empty. The paths in this example point into
the local corpus (not in the repository): replace them with your own recordings.

The unified-engine baseline is not a config file: its scenes are code
(`src/research/UnifiedScenes.cpp`) so that tests and the CLI render exactly the same thing:
`research-renderer --unified <dir>` (see `docs/research/unified-andor/phase-0-baseline.md`).
