# research/configs

Render configurations for `research-renderer --config <file>` (schemaVersion 1).
Missing fields keep their defaults. `baseline-a.json` and `baseline-b.json` are the
reference baselines every experiment is compared against; add new files for
experiments instead of editing these.

`arp-c-updown-16th.json` puts the arpeggiator ahead of engine C. Any config may carry the
same `"arp"` block (baselines A and B included): the fixture's notes are arpeggiated exactly
as the plugin does it, before the engine plays them.
