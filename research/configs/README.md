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
