# Template format: `.ospstate`

A template is a complete instrument without sounds: every setting (sources' modes, envelopes,
modulation and meta-modulation, effects, macros, ARP, EQ), and how many sources it expects.

- The state XML without `Instrument` / `InstrumentB` / `InstrumentC` and without window state
  (`uiScale`, `advancedOpen`, `arpExpanded`, `editLayer`, `program`), plus
  `startingState="1"` and `keptSlots` (the source count the template was made with).
- It never holds a sound, a content hash, a file name or a path (tested: Stage 3 checks the
  file for `contentHash`, `originalPath` and `filename`).
- Loading one keeps the sounds already loaded (more than its slots all stay); empty slots
  wait for sounds and are silent; modulation routes to a source become audible as soon as a
  sound is loaded.
- Factory templates are the seven built-in starting states (read-only); a user's edit saves as
  a new user template.
- Library metadata (name, category, tags, description, suggested source types) lives in the
  catalog, not in the file, so a template shared as a file carries only settings.
