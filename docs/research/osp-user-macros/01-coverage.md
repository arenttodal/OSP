# ANDOR OSP — USER MACROS coverage (UM7)

Which OSP parameters a USER macro may move, generated from the build by the hidden test
`[usermacro-coverage]` (`OSP_USER_MACRO_COVERAGE=<file> osp_plugin_tests "[usermacro-coverage]"`).
The rule (`usermacros::eligible`, `OspAudioProcessor::userMacroReads`):

- a continuous control (`AudioParameterFloat`): never a switch or a choice;
- that the engine reads live through `LiveParam` (so the macros' offset reaches the sound with
  no new code path); a parameter read nowhere live (only at load or analysis) is excluded;
- never a USER macro itself, a modulation route (`mod.routeN.*`: it has its own depth editing),
  the performance seed or the grain budget.

**491 of 872 parameters may be moved by a USER macro.**

| Parameter | Name | Kind | USER macro | Why |
|---|---|---|---|---|
| `attack` | Attack | continuous | yes |  |
| `release` | Release | continuous | yes |  |
| `gain` | Output | continuous | yes |  |
| `velocityRange` | Velocity Range | continuous | yes |  |
| `fineTune` | Fine Tune | continuous | yes |  |
| `bendRange` | Bend Range | continuous | yes |  |
| `life` | Life | continuous | yes |  |
| `dynamics` | Dynamics | continuous | yes |  |
| `character` | Character | continuous | yes |  |
| `motion` | Movement | continuous | yes |  |
| `space` | Space | continuous | yes |  |
| `reimagined` | A Reimagined | continuous | yes |  |
| `pitchCharacter` | Pitch Character | choice | no | a switch or a choice (a macro moves continuous controls) |
| `sustain` | Sustain | choice | no | a switch or a choice (a macro moves continuous controls) |
| `seed` | Variation Seed | choice | no | structural (the performance seed / CPU budget) |
| `mpe` | MPE | switch | no | a switch or a choice (a macro moves continuous controls) |
| `life.mode` | Life Mode | choice | no | a switch or a choice (a macro moves continuous controls) |
| `life.pitch` | Life Pitch | continuous | yes |  |
| `life.tone` | Life Tone | continuous | yes |  |
| `life.attack` | Life Attack | continuous | yes |  |
| `dynamics.curve` | Dynamics Curve | choice | no | a switch or a choice (a macro moves continuous controls) |
| `dynamics.tone` | Dynamics Tone | continuous | yes |  |
| `character.type` | Character Filter | choice | no | a switch or a choice (a macro moves continuous controls) |
| `character.min` | Character Min | continuous | yes |  |
| `character.max` | Character Max | continuous | yes |  |
| `character.resonance` | Character Resonance | continuous | yes |  |
| `character.drive` | Character Drive | continuous | yes |  |
| `character.envAmount` | Character Envelope | continuous | yes |  |
| `character.envAttack` | Character Env Attack | continuous | yes |  |
| `character.envDecay` | Character Env Decay | continuous | yes |  |
| `movement.mode` | Movement Mode | choice | no | a switch or a choice (a macro moves continuous controls) |
| `space.type` | Space Type | choice | no | a switch or a choice (a macro moves continuous controls) |
| `space.decay` | Space Decay | continuous | yes |  |
| `movement.drift.speed` | Drift Speed | continuous | yes |  |
| `movement.drift.pitch` | Drift Pitch | continuous | yes |  |
| `movement.drift.tone` | Drift Tone | continuous | yes |  |
| `movement.tape.wow` | Tape Wow | continuous | yes |  |
| `movement.tape.flutter` | Tape Flutter | continuous | yes |  |
| `movement.tape.wear` | Tape Wear | continuous | yes |  |
| `movement.chorus.rate` | Chorus Rate | continuous | yes |  |
| `movement.chorus.width` | Chorus Width | continuous | yes |  |
| `movement.chorus.stereo` | Chorus Stereo | continuous | yes |  |
| `movement.pulse.rate` | Pulse Rate | continuous | yes |  |
| `movement.pulse.shape` | Pulse Shape | continuous | yes |  |
| `movement.pulse.stereo` | Pulse Stereo | continuous | yes |  |
| `movement.shaper.pattern` | Shaper Pattern | choice | no | a switch or a choice (a macro moves continuous controls) |
| `movement.shaper.rate` | Shaper Rate | choice | no | a switch or a choice (a macro moves continuous controls) |
| `movement.shaper.target` | Shaper Target | choice | no | a switch or a choice (a macro moves continuous controls) |
| `movement.shaper.smooth` | Shaper Smooth | continuous | yes |  |
| `ab.blend` | A/B Blend | continuous | yes |  |
| `layerA.sourceMode` | Layer A Source Mode | choice | no | a switch or a choice (a macro moves continuous controls) |
| `layerA.granular.position` | Layer A Grain Position | continuous | yes |  |
| `layerA.granular.size` | Layer A Grain Size | continuous | yes |  |
| `layerA.granular.density` | Layer A Grain Density | continuous | yes |  |
| `layerA.granular.tune` | Layer A Grain Tune | continuous | yes |  |
| `layerA.granular.spread` | Layer A Grain Spread | continuous | yes |  |
| `layerB.sourceMode` | Layer B Source Mode | choice | no | a switch or a choice (a macro moves continuous controls) |
| `layerB.granular.position` | Layer B Grain Position | continuous | yes |  |
| `layerB.granular.size` | Layer B Grain Size | continuous | yes |  |
| `layerB.granular.density` | Layer B Grain Density | continuous | yes |  |
| `layerB.granular.tune` | Layer B Grain Tune | continuous | yes |  |
| `layerB.granular.spread` | Layer B Grain Spread | continuous | yes |  |
| `layerC.sourceMode` | Layer C Source Mode | choice | no | a switch or a choice (a macro moves continuous controls) |
| `layerC.granular.position` | Layer C Grain Position | continuous | yes |  |
| `layerC.granular.size` | Layer C Grain Size | continuous | yes |  |
| `layerC.granular.density` | Layer C Grain Density | continuous | yes |  |
| `layerC.granular.tune` | Layer C Grain Tune | continuous | yes |  |
| `layerC.granular.spread` | Layer C Grain Spread | continuous | yes |  |
| `layerA.start` | Layer A Start | continuous | yes |  |
| `layerA.tune` | Layer A Tune | continuous | yes |  |
| `layerA.pan` | Layer A Pan | continuous | yes |  |
| `layerA.level` | Layer A Level | continuous | yes |  |
| `layerA.link` | Layer A Link | switch | no | a switch or a choice (a macro moves continuous controls) |
| `layerA.reverse` | Layer A Reverse | switch | no | a switch or a choice (a macro moves continuous controls) |
| `layerA.loop` | Layer A Loop | switch | no | a switch or a choice (a macro moves continuous controls) |
| `layerA.follow` | Layer A Follow | switch | no | a switch or a choice (a macro moves continuous controls) |
| `layerB.start` | Layer B Start | continuous | yes |  |
| `layerB.tune` | Layer B Tune | continuous | yes |  |
| `layerB.pan` | Layer B Pan | continuous | yes |  |
| `layerB.level` | Layer B Level | continuous | yes |  |
| `layerB.link` | Layer B Link | switch | no | a switch or a choice (a macro moves continuous controls) |
| `layerB.reverse` | Layer B Reverse | switch | no | a switch or a choice (a macro moves continuous controls) |
| `layerB.loop` | Layer B Loop | switch | no | a switch or a choice (a macro moves continuous controls) |
| `layerB.follow` | Layer B Follow | switch | no | a switch or a choice (a macro moves continuous controls) |
| `layerB.reimagined` | B Reimagined | continuous | yes |  |
| `layerC.start` | Layer C Start | continuous | yes |  |
| `layerC.tune` | Layer C Tune | continuous | yes |  |
| `layerC.pan` | Layer C Pan | continuous | yes |  |
| `layerC.level` | Layer C Level | continuous | yes |  |
| `layerC.link` | Layer C Link | switch | no | a switch or a choice (a macro moves continuous controls) |
| `layerC.reverse` | Layer C Reverse | switch | no | a switch or a choice (a macro moves continuous controls) |
| `layerC.loop` | Layer C Loop | switch | no | a switch or a choice (a macro moves continuous controls) |
| `layerC.follow` | Layer C Follow | switch | no | a switch or a choice (a macro moves continuous controls) |
| `layerC.reimagined` | C Reimagined | continuous | yes |  |
| `layerA.reimagined.mode` | A Reimagined Mode | choice | no | a switch or a choice (a macro moves continuous controls) |
| `layerA.reimagined.kaleidoscope.focus` | A Kaleidoscope Focus | continuous | yes |  |
| `layerA.reimagined.kaleidoscope.spread` | A Kaleidoscope Spread | continuous | yes |  |
| `layerA.reimagined.tapeFrame.age` | A Tape Frame Age | continuous | yes |  |
| `layerA.reimagined.tapeFrame.stability` | A Tape Frame Stability | continuous | yes |  |
| `layerA.reimagined.tapeFrame.frame` | A Tape Frame Length | choice | no | a switch or a choice (a macro moves continuous controls) |
| `layerA.reimagined.toybox.motion` | A Toybox Motion | continuous | yes |  |
| `layerA.reimagined.toybox.digital` | A Toybox Digital | continuous | yes |  |
| `layerA.reimagined.toybox.play` | A Toybox Play | choice | no | a switch or a choice (a macro moves continuous controls) |
| `layerA.reimagined.mosaic.detail` | A Mosaic Detail | continuous | yes |  |
| `layerA.reimagined.mosaic.motion` | A Mosaic Motion | continuous | yes |  |
| `layerA.reimagined.mosaic.model` | A Mosaic Model | choice | no | a switch or a choice (a macro moves continuous controls) |
| `layerA.reimagined.mirage.clock` | A Mirage Clock | continuous | yes |  |
| `layerA.reimagined.mirage.filter` | A Mirage Filter | continuous | yes |  |
| `layerA.reimagined.mirage.tone` | A Mirage Tone | choice | no | a switch or a choice (a macro moves continuous controls) |
| `layerB.reimagined.mode` | B Reimagined Mode | choice | no | a switch or a choice (a macro moves continuous controls) |
| `layerB.reimagined.kaleidoscope.focus` | B Kaleidoscope Focus | continuous | yes |  |
| `layerB.reimagined.kaleidoscope.spread` | B Kaleidoscope Spread | continuous | yes |  |
| `layerB.reimagined.tapeFrame.age` | B Tape Frame Age | continuous | yes |  |
| `layerB.reimagined.tapeFrame.stability` | B Tape Frame Stability | continuous | yes |  |
| `layerB.reimagined.tapeFrame.frame` | B Tape Frame Length | choice | no | a switch or a choice (a macro moves continuous controls) |
| `layerB.reimagined.toybox.motion` | B Toybox Motion | continuous | yes |  |
| `layerB.reimagined.toybox.digital` | B Toybox Digital | continuous | yes |  |
| `layerB.reimagined.toybox.play` | B Toybox Play | choice | no | a switch or a choice (a macro moves continuous controls) |
| `layerB.reimagined.mosaic.detail` | B Mosaic Detail | continuous | yes |  |
| `layerB.reimagined.mosaic.motion` | B Mosaic Motion | continuous | yes |  |
| `layerB.reimagined.mosaic.model` | B Mosaic Model | choice | no | a switch or a choice (a macro moves continuous controls) |
| `layerB.reimagined.mirage.clock` | B Mirage Clock | continuous | yes |  |
| `layerB.reimagined.mirage.filter` | B Mirage Filter | continuous | yes |  |
| `layerB.reimagined.mirage.tone` | B Mirage Tone | choice | no | a switch or a choice (a macro moves continuous controls) |
| `layerC.reimagined.mode` | C Reimagined Mode | choice | no | a switch or a choice (a macro moves continuous controls) |
| `layerC.reimagined.kaleidoscope.focus` | C Kaleidoscope Focus | continuous | yes |  |
| `layerC.reimagined.kaleidoscope.spread` | C Kaleidoscope Spread | continuous | yes |  |
| `layerC.reimagined.tapeFrame.age` | C Tape Frame Age | continuous | yes |  |
| `layerC.reimagined.tapeFrame.stability` | C Tape Frame Stability | continuous | yes |  |
| `layerC.reimagined.tapeFrame.frame` | C Tape Frame Length | choice | no | a switch or a choice (a macro moves continuous controls) |
| `layerC.reimagined.toybox.motion` | C Toybox Motion | continuous | yes |  |
| `layerC.reimagined.toybox.digital` | C Toybox Digital | continuous | yes |  |
| `layerC.reimagined.toybox.play` | C Toybox Play | choice | no | a switch or a choice (a macro moves continuous controls) |
| `layerC.reimagined.mosaic.detail` | C Mosaic Detail | continuous | yes |  |
| `layerC.reimagined.mosaic.motion` | C Mosaic Motion | continuous | yes |  |
| `layerC.reimagined.mosaic.model` | C Mosaic Model | choice | no | a switch or a choice (a macro moves continuous controls) |
| `layerC.reimagined.mirage.clock` | C Mirage Clock | continuous | yes |  |
| `layerC.reimagined.mirage.filter` | C Mirage Filter | continuous | yes |  |
| `layerC.reimagined.mirage.tone` | C Mirage Tone | choice | no | a switch or a choice (a macro moves continuous controls) |
| `reimaginedLink` | Reimagined Link | switch | no | a switch or a choice (a macro moves continuous controls) |
| `life.character` | Life Character | choice | no | a switch or a choice (a macro moves continuous controls) |
| `life.takes` | Life Takes | choice | no | a switch or a choice (a macro moves continuous controls) |
| `life.takeOrder` | Life Take Order | choice | no | a switch or a choice (a macro moves continuous controls) |
| `life.takesSeed` | Life Takes Variation | choice | no | a switch or a choice (a macro moves continuous controls) |
| `mix.x` | Mix X | continuous | yes |  |
| `mix.y` | Mix Y | continuous | yes |  |
| `decay` | Decay | continuous | yes |  |
| `sustainLevel` | Sustain Level | continuous | yes |  |
| `voiceMode` | Voice Mode | choice | no | a switch or a choice (a macro moves continuous controls) |
| `glide` | Glide | continuous | yes |  |
| `layerA.mute` | Layer A Mute | switch | no | a switch or a choice (a macro moves continuous controls) |
| `layerA.solo` | Layer A Solo | switch | no | a switch or a choice (a macro moves continuous controls) |
| `layerB.mute` | Layer B Mute | switch | no | a switch or a choice (a macro moves continuous controls) |
| `layerB.solo` | Layer B Solo | switch | no | a switch or a choice (a macro moves continuous controls) |
| `layerC.mute` | Layer C Mute | switch | no | a switch or a choice (a macro moves continuous controls) |
| `layerC.solo` | Layer C Solo | switch | no | a switch or a choice (a macro moves continuous controls) |
| `space.preDelay` | Space Pre-Delay | continuous | yes |  |
| `space.size` | Space Size | continuous | yes |  |
| `space.damping` | Space Damping | continuous | yes |  |
| `space.modulation` | Space Modulation | continuous | yes |  |
| `space.width` | Space Width | continuous | yes |  |
| `space.lowCut` | Space Low Cut | continuous | yes |  |
| `space.highCut` | Space High Cut | continuous | yes |  |
| `echo` | Echo | continuous | yes |  |
| `echo.type` | Echo Type | choice | no | a switch or a choice (a macro moves continuous controls) |
| `echo.sync` | Echo Sync | switch | no | a switch or a choice (a macro moves continuous controls) |
| `echo.division` | Echo Division | choice | no | a switch or a choice (a macro moves continuous controls) |
| `echo.time` | Echo Time | continuous | yes |  |
| `echo.feedback` | Echo Feedback | continuous | yes |  |
| `echo.tone` | Echo Tone | continuous | yes |  |
| `echo.age` | Echo Age | continuous | yes |  |
| `echo.stereo` | Echo Stereo | choice | no | a switch or a choice (a macro moves continuous controls) |
| `movement.shaper.custom` | Shaper Custom | switch | no | a switch or a choice (a macro moves continuous controls) |
| `drive` | Drive | continuous | yes |  |
| `drive.mode` | Drive Mode | choice | no | a switch or a choice (a macro moves continuous controls) |
| `drive.tone` | Drive Tone | continuous | yes |  |
| `drive.body` | Drive Body | continuous | yes |  |
| `arp.enabled` | Arp | switch | no | a switch or a choice (a macro moves continuous controls) |
| `arp.pattern` | Arp Pattern | choice | no | a switch or a choice (a macro moves continuous controls) |
| `arp.rate` | Arp Rate | choice | no | a switch or a choice (a macro moves continuous controls) |
| `arp.gate` | Arp Gate | continuous | yes |  |
| `arp.octaves` | Arp Octaves | choice | no | a switch or a choice (a macro moves continuous controls) |
| `arp.swing` | Arp Swing | continuous | yes |  |
| `arp.velocity` | Arp Velocity | continuous | yes |  |
| `layerA.dampen.enabled` | Layer A Dampen On | switch | no | a switch or a choice (a macro moves continuous controls) |
| `layerA.dampen.mode` | Layer A Dampen Mode | choice | no | a switch or a choice (a macro moves continuous controls) |
| `layerA.dampen.amount` | Layer A Dampen Amount | continuous | yes |  |
| `layerA.dampen.frequency` | Layer A Dampen Frequency | continuous | yes |  |
| `layerA.dampen.cutoff` | Layer A Dampen Min Cutoff | continuous | yes |  |
| `layerB.dampen.enabled` | Layer B Dampen On | switch | no | a switch or a choice (a macro moves continuous controls) |
| `layerB.dampen.mode` | Layer B Dampen Mode | choice | no | a switch or a choice (a macro moves continuous controls) |
| `layerB.dampen.amount` | Layer B Dampen Amount | continuous | yes |  |
| `layerB.dampen.frequency` | Layer B Dampen Frequency | continuous | yes |  |
| `layerB.dampen.cutoff` | Layer B Dampen Min Cutoff | continuous | yes |  |
| `layerC.dampen.enabled` | Layer C Dampen On | switch | no | a switch or a choice (a macro moves continuous controls) |
| `layerC.dampen.mode` | Layer C Dampen Mode | choice | no | a switch or a choice (a macro moves continuous controls) |
| `layerC.dampen.amount` | Layer C Dampen Amount | continuous | yes |  |
| `layerC.dampen.frequency` | Layer C Dampen Frequency | continuous | yes |  |
| `layerC.dampen.cutoff` | Layer C Dampen Min Cutoff | continuous | yes |  |
| `mod.lfo1.shape` | LFO 1 Shape | choice | no | a switch or a choice (a macro moves continuous controls) |
| `mod.lfo1.rate` | LFO 1 Rate | continuous | yes |  |
| `mod.lfo1.sync` | LFO 1 Sync | switch | no | a switch or a choice (a macro moves continuous controls) |
| `mod.lfo1.division` | LFO 1 Division | choice | no | a switch or a choice (a macro moves continuous controls) |
| `mod.lfo1.phase` | LFO 1 Phase | continuous | yes |  |
| `mod.lfo1.polarity` | LFO 1 Polarity | choice | no | a switch or a choice (a macro moves continuous controls) |
| `mod.lfo1.mode` | LFO 1 Mode | choice | no | a switch or a choice (a macro moves continuous controls) |
| `mod.lfo1.scope` | LFO 1 Scope | choice | no | a switch or a choice (a macro moves continuous controls) |
| `mod.lfo2.shape` | LFO 2 Shape | choice | no | a switch or a choice (a macro moves continuous controls) |
| `mod.lfo2.rate` | LFO 2 Rate | continuous | yes |  |
| `mod.lfo2.sync` | LFO 2 Sync | switch | no | a switch or a choice (a macro moves continuous controls) |
| `mod.lfo2.division` | LFO 2 Division | choice | no | a switch or a choice (a macro moves continuous controls) |
| `mod.lfo2.phase` | LFO 2 Phase | continuous | yes |  |
| `mod.lfo2.polarity` | LFO 2 Polarity | choice | no | a switch or a choice (a macro moves continuous controls) |
| `mod.lfo2.mode` | LFO 2 Mode | choice | no | a switch or a choice (a macro moves continuous controls) |
| `mod.lfo2.scope` | LFO 2 Scope | choice | no | a switch or a choice (a macro moves continuous controls) |
| `mod.env1.mode` | ENV 1 Mode | choice | no | a switch or a choice (a macro moves continuous controls) |
| `mod.env1.attack` | ENV 1 Attack | continuous | yes |  |
| `mod.env1.decay` | ENV 1 Decay | continuous | yes |  |
| `mod.env1.sustain` | ENV 1 Sustain | continuous | yes |  |
| `mod.env1.release` | ENV 1 Release | continuous | yes |  |
| `mod.env1.curve` | ENV 1 Curve | continuous | yes |  |
| `mod.env1.length` | ENV 1 Length | continuous | yes |  |
| `mod.env2.mode` | ENV 2 Mode | choice | no | a switch or a choice (a macro moves continuous controls) |
| `mod.env2.attack` | ENV 2 Attack | continuous | yes |  |
| `mod.env2.decay` | ENV 2 Decay | continuous | yes |  |
| `mod.env2.sustain` | ENV 2 Sustain | continuous | yes |  |
| `mod.env2.release` | ENV 2 Release | continuous | yes |  |
| `mod.env2.curve` | ENV 2 Curve | continuous | yes |  |
| `mod.env2.length` | ENV 2 Length | continuous | yes |  |
| `mod.route1.source` | Mod 1 Source | choice | no | a modulation route (its own depth editing) |
| `mod.route1.dest` | Mod 1 Destination | choice | no | a modulation route (its own depth editing) |
| `mod.route1.depth` | Mod 1 Depth | continuous | no | a modulation route (its own depth editing) |
| `mod.route1.enabled` | Mod 1 On | switch | no | a modulation route (its own depth editing) |
| `mod.route2.source` | Mod 2 Source | choice | no | a modulation route (its own depth editing) |
| `mod.route2.dest` | Mod 2 Destination | choice | no | a modulation route (its own depth editing) |
| `mod.route2.depth` | Mod 2 Depth | continuous | no | a modulation route (its own depth editing) |
| `mod.route2.enabled` | Mod 2 On | switch | no | a modulation route (its own depth editing) |
| `mod.route3.source` | Mod 3 Source | choice | no | a modulation route (its own depth editing) |
| `mod.route3.dest` | Mod 3 Destination | choice | no | a modulation route (its own depth editing) |
| `mod.route3.depth` | Mod 3 Depth | continuous | no | a modulation route (its own depth editing) |
| `mod.route3.enabled` | Mod 3 On | switch | no | a modulation route (its own depth editing) |
| `mod.route4.source` | Mod 4 Source | choice | no | a modulation route (its own depth editing) |
| `mod.route4.dest` | Mod 4 Destination | choice | no | a modulation route (its own depth editing) |
| `mod.route4.depth` | Mod 4 Depth | continuous | no | a modulation route (its own depth editing) |
| `mod.route4.enabled` | Mod 4 On | switch | no | a modulation route (its own depth editing) |
| `mod.route5.source` | Mod 5 Source | choice | no | a modulation route (its own depth editing) |
| `mod.route5.dest` | Mod 5 Destination | choice | no | a modulation route (its own depth editing) |
| `mod.route5.depth` | Mod 5 Depth | continuous | no | a modulation route (its own depth editing) |
| `mod.route5.enabled` | Mod 5 On | switch | no | a modulation route (its own depth editing) |
| `mod.route6.source` | Mod 6 Source | choice | no | a modulation route (its own depth editing) |
| `mod.route6.dest` | Mod 6 Destination | choice | no | a modulation route (its own depth editing) |
| `mod.route6.depth` | Mod 6 Depth | continuous | no | a modulation route (its own depth editing) |
| `mod.route6.enabled` | Mod 6 On | switch | no | a modulation route (its own depth editing) |
| `mod.route7.source` | Mod 7 Source | choice | no | a modulation route (its own depth editing) |
| `mod.route7.dest` | Mod 7 Destination | choice | no | a modulation route (its own depth editing) |
| `mod.route7.depth` | Mod 7 Depth | continuous | no | a modulation route (its own depth editing) |
| `mod.route7.enabled` | Mod 7 On | switch | no | a modulation route (its own depth editing) |
| `mod.route8.source` | Mod 8 Source | choice | no | a modulation route (its own depth editing) |
| `mod.route8.dest` | Mod 8 Destination | choice | no | a modulation route (its own depth editing) |
| `mod.route8.depth` | Mod 8 Depth | continuous | no | a modulation route (its own depth editing) |
| `mod.route8.enabled` | Mod 8 On | switch | no | a modulation route (its own depth editing) |
| `mod.route9.source` | Mod 9 Source | choice | no | a modulation route (its own depth editing) |
| `mod.route9.dest` | Mod 9 Destination | choice | no | a modulation route (its own depth editing) |
| `mod.route9.depth` | Mod 9 Depth | continuous | no | a modulation route (its own depth editing) |
| `mod.route9.enabled` | Mod 9 On | switch | no | a modulation route (its own depth editing) |
| `mod.route10.source` | Mod 10 Source | choice | no | a modulation route (its own depth editing) |
| `mod.route10.dest` | Mod 10 Destination | choice | no | a modulation route (its own depth editing) |
| `mod.route10.depth` | Mod 10 Depth | continuous | no | a modulation route (its own depth editing) |
| `mod.route10.enabled` | Mod 10 On | switch | no | a modulation route (its own depth editing) |
| `mod.route11.source` | Mod 11 Source | choice | no | a modulation route (its own depth editing) |
| `mod.route11.dest` | Mod 11 Destination | choice | no | a modulation route (its own depth editing) |
| `mod.route11.depth` | Mod 11 Depth | continuous | no | a modulation route (its own depth editing) |
| `mod.route11.enabled` | Mod 11 On | switch | no | a modulation route (its own depth editing) |
| `mod.route12.source` | Mod 12 Source | choice | no | a modulation route (its own depth editing) |
| `mod.route12.dest` | Mod 12 Destination | choice | no | a modulation route (its own depth editing) |
| `mod.route12.depth` | Mod 12 Depth | continuous | no | a modulation route (its own depth editing) |
| `mod.route12.enabled` | Mod 12 On | switch | no | a modulation route (its own depth editing) |
| `mod.route13.source` | Mod 13 Source | choice | no | a modulation route (its own depth editing) |
| `mod.route13.dest` | Mod 13 Destination | choice | no | a modulation route (its own depth editing) |
| `mod.route13.depth` | Mod 13 Depth | continuous | no | a modulation route (its own depth editing) |
| `mod.route13.enabled` | Mod 13 On | switch | no | a modulation route (its own depth editing) |
| `mod.route14.source` | Mod 14 Source | choice | no | a modulation route (its own depth editing) |
| `mod.route14.dest` | Mod 14 Destination | choice | no | a modulation route (its own depth editing) |
| `mod.route14.depth` | Mod 14 Depth | continuous | no | a modulation route (its own depth editing) |
| `mod.route14.enabled` | Mod 14 On | switch | no | a modulation route (its own depth editing) |
| `mod.route15.source` | Mod 15 Source | choice | no | a modulation route (its own depth editing) |
| `mod.route15.dest` | Mod 15 Destination | choice | no | a modulation route (its own depth editing) |
| `mod.route15.depth` | Mod 15 Depth | continuous | no | a modulation route (its own depth editing) |
| `mod.route15.enabled` | Mod 15 On | switch | no | a modulation route (its own depth editing) |
| `mod.route16.source` | Mod 16 Source | choice | no | a modulation route (its own depth editing) |
| `mod.route16.dest` | Mod 16 Destination | choice | no | a modulation route (its own depth editing) |
| `mod.route16.depth` | Mod 16 Depth | continuous | no | a modulation route (its own depth editing) |
| `mod.route16.enabled` | Mod 16 On | switch | no | a modulation route (its own depth editing) |
| `layerA.eq.enabled` | Layer A EQ On | switch | no | a switch or a choice (a macro moves continuous controls) |
| `layerA.eq.hp.enabled` | Layer A EQ hp On | switch | no | a switch or a choice (a macro moves continuous controls) |
| `layerA.eq.hp.frequency` | Layer A EQ hp Frequency | continuous | yes |  |
| `layerA.eq.hp.slope` | Layer A EQ hp Slope | choice | no | a switch or a choice (a macro moves continuous controls) |
| `layerA.eq.lowShelf.enabled` | Layer A EQ low shelf On | switch | no | a switch or a choice (a macro moves continuous controls) |
| `layerA.eq.lowShelf.frequency` | Layer A EQ low shelf Frequency | continuous | yes |  |
| `layerA.eq.lowShelf.gain` | Layer A EQ low shelf Gain | continuous | yes |  |
| `layerA.eq.lowShelf.q` | Layer A EQ low shelf Q | continuous | yes |  |
| `layerA.eq.bell.enabled` | Layer A EQ bell On | switch | no | a switch or a choice (a macro moves continuous controls) |
| `layerA.eq.bell.frequency` | Layer A EQ bell Frequency | continuous | yes |  |
| `layerA.eq.bell.gain` | Layer A EQ bell Gain | continuous | yes |  |
| `layerA.eq.bell.q` | Layer A EQ bell Q | continuous | yes |  |
| `layerA.eq.highShelf.enabled` | Layer A EQ high shelf On | switch | no | a switch or a choice (a macro moves continuous controls) |
| `layerA.eq.highShelf.frequency` | Layer A EQ high shelf Frequency | continuous | yes |  |
| `layerA.eq.highShelf.gain` | Layer A EQ high shelf Gain | continuous | yes |  |
| `layerA.eq.highShelf.q` | Layer A EQ high shelf Q | continuous | yes |  |
| `layerA.eq.lp.enabled` | Layer A EQ lp On | switch | no | a switch or a choice (a macro moves continuous controls) |
| `layerA.eq.lp.frequency` | Layer A EQ lp Frequency | continuous | yes |  |
| `layerA.eq.lp.slope` | Layer A EQ lp Slope | choice | no | a switch or a choice (a macro moves continuous controls) |
| `layerB.eq.enabled` | Layer B EQ On | switch | no | a switch or a choice (a macro moves continuous controls) |
| `layerB.eq.hp.enabled` | Layer B EQ hp On | switch | no | a switch or a choice (a macro moves continuous controls) |
| `layerB.eq.hp.frequency` | Layer B EQ hp Frequency | continuous | yes |  |
| `layerB.eq.hp.slope` | Layer B EQ hp Slope | choice | no | a switch or a choice (a macro moves continuous controls) |
| `layerB.eq.lowShelf.enabled` | Layer B EQ low shelf On | switch | no | a switch or a choice (a macro moves continuous controls) |
| `layerB.eq.lowShelf.frequency` | Layer B EQ low shelf Frequency | continuous | yes |  |
| `layerB.eq.lowShelf.gain` | Layer B EQ low shelf Gain | continuous | yes |  |
| `layerB.eq.lowShelf.q` | Layer B EQ low shelf Q | continuous | yes |  |
| `layerB.eq.bell.enabled` | Layer B EQ bell On | switch | no | a switch or a choice (a macro moves continuous controls) |
| `layerB.eq.bell.frequency` | Layer B EQ bell Frequency | continuous | yes |  |
| `layerB.eq.bell.gain` | Layer B EQ bell Gain | continuous | yes |  |
| `layerB.eq.bell.q` | Layer B EQ bell Q | continuous | yes |  |
| `layerB.eq.highShelf.enabled` | Layer B EQ high shelf On | switch | no | a switch or a choice (a macro moves continuous controls) |
| `layerB.eq.highShelf.frequency` | Layer B EQ high shelf Frequency | continuous | yes |  |
| `layerB.eq.highShelf.gain` | Layer B EQ high shelf Gain | continuous | yes |  |
| `layerB.eq.highShelf.q` | Layer B EQ high shelf Q | continuous | yes |  |
| `layerB.eq.lp.enabled` | Layer B EQ lp On | switch | no | a switch or a choice (a macro moves continuous controls) |
| `layerB.eq.lp.frequency` | Layer B EQ lp Frequency | continuous | yes |  |
| `layerB.eq.lp.slope` | Layer B EQ lp Slope | choice | no | a switch or a choice (a macro moves continuous controls) |
| `layerC.eq.enabled` | Layer C EQ On | switch | no | a switch or a choice (a macro moves continuous controls) |
| `layerC.eq.hp.enabled` | Layer C EQ hp On | switch | no | a switch or a choice (a macro moves continuous controls) |
| `layerC.eq.hp.frequency` | Layer C EQ hp Frequency | continuous | yes |  |
| `layerC.eq.hp.slope` | Layer C EQ hp Slope | choice | no | a switch or a choice (a macro moves continuous controls) |
| `layerC.eq.lowShelf.enabled` | Layer C EQ low shelf On | switch | no | a switch or a choice (a macro moves continuous controls) |
| `layerC.eq.lowShelf.frequency` | Layer C EQ low shelf Frequency | continuous | yes |  |
| `layerC.eq.lowShelf.gain` | Layer C EQ low shelf Gain | continuous | yes |  |
| `layerC.eq.lowShelf.q` | Layer C EQ low shelf Q | continuous | yes |  |
| `layerC.eq.bell.enabled` | Layer C EQ bell On | switch | no | a switch or a choice (a macro moves continuous controls) |
| `layerC.eq.bell.frequency` | Layer C EQ bell Frequency | continuous | yes |  |
| `layerC.eq.bell.gain` | Layer C EQ bell Gain | continuous | yes |  |
| `layerC.eq.bell.q` | Layer C EQ bell Q | continuous | yes |  |
| `layerC.eq.highShelf.enabled` | Layer C EQ high shelf On | switch | no | a switch or a choice (a macro moves continuous controls) |
| `layerC.eq.highShelf.frequency` | Layer C EQ high shelf Frequency | continuous | yes |  |
| `layerC.eq.highShelf.gain` | Layer C EQ high shelf Gain | continuous | yes |  |
| `layerC.eq.highShelf.q` | Layer C EQ high shelf Q | continuous | yes |  |
| `layerC.eq.lp.enabled` | Layer C EQ lp On | switch | no | a switch or a choice (a macro moves continuous controls) |
| `layerC.eq.lp.frequency` | Layer C EQ lp Frequency | continuous | yes |  |
| `layerC.eq.lp.slope` | Layer C EQ lp Slope | choice | no | a switch or a choice (a macro moves continuous controls) |
| `layerA.granular.scatter` | Layer A Grain Scatter | continuous | yes |  |
| `layerA.granular.pan` | Layer A Grain Pan | continuous | yes |  |
| `layerA.granular.window` | Layer A Grain Window | choice | no | a switch or a choice (a macro moves continuous controls) |
| `layerA.keyDna.amount` | Layer A KEY DNA Amount | continuous | yes |  |
| `layerB.granular.scatter` | Layer B Grain Scatter | continuous | yes |  |
| `layerB.granular.pan` | Layer B Grain Pan | continuous | yes |  |
| `layerB.granular.window` | Layer B Grain Window | choice | no | a switch or a choice (a macro moves continuous controls) |
| `layerB.keyDna.amount` | Layer B KEY DNA Amount | continuous | yes |  |
| `layerC.granular.scatter` | Layer C Grain Scatter | continuous | yes |  |
| `layerC.granular.pan` | Layer C Grain Pan | continuous | yes |  |
| `layerC.granular.window` | Layer C Grain Window | choice | no | a switch or a choice (a macro moves continuous controls) |
| `layerC.keyDna.amount` | Layer C KEY DNA Amount | continuous | yes |  |
| `grainBudget` | Grain Budget | choice | no | structural (the performance seed / CPU budget) |
| `mod.route1.destExt` | Mod 1 Destination+ | choice | no | a modulation route (its own depth editing) |
| `mod.route2.destExt` | Mod 2 Destination+ | choice | no | a modulation route (its own depth editing) |
| `mod.route3.destExt` | Mod 3 Destination+ | choice | no | a modulation route (its own depth editing) |
| `mod.route4.destExt` | Mod 4 Destination+ | choice | no | a modulation route (its own depth editing) |
| `mod.route5.destExt` | Mod 5 Destination+ | choice | no | a modulation route (its own depth editing) |
| `mod.route6.destExt` | Mod 6 Destination+ | choice | no | a modulation route (its own depth editing) |
| `mod.route7.destExt` | Mod 7 Destination+ | choice | no | a modulation route (its own depth editing) |
| `mod.route8.destExt` | Mod 8 Destination+ | choice | no | a modulation route (its own depth editing) |
| `mod.route9.destExt` | Mod 9 Destination+ | choice | no | a modulation route (its own depth editing) |
| `mod.route10.destExt` | Mod 10 Destination+ | choice | no | a modulation route (its own depth editing) |
| `mod.route11.destExt` | Mod 11 Destination+ | choice | no | a modulation route (its own depth editing) |
| `mod.route12.destExt` | Mod 12 Destination+ | choice | no | a modulation route (its own depth editing) |
| `mod.route13.destExt` | Mod 13 Destination+ | choice | no | a modulation route (its own depth editing) |
| `mod.route14.destExt` | Mod 14 Destination+ | choice | no | a modulation route (its own depth editing) |
| `mod.route15.destExt` | Mod 15 Destination+ | choice | no | a modulation route (its own depth editing) |
| `mod.route16.destExt` | Mod 16 Destination+ | choice | no | a modulation route (its own depth editing) |
| `layerA.synthesize` | Layer A Synthesize | continuous | yes |  |
| `layerB.synthesize` | Layer B Synthesize | continuous | yes |  |
| `layerC.synthesize` | Layer C Synthesize | continuous | yes |  |
| `layerA.synth.wave` | Layer A Synth Wave | choice | no | a switch or a choice (a macro moves continuous controls) |
| `layerA.synth.octave` | Layer A Synth Octave | choice | no | a switch or a choice (a macro moves continuous controls) |
| `layerA.synth.detune` | Layer A Synth Detune | continuous | yes |  |
| `layerA.synth.tone` | Layer A Synth Tone | continuous | yes |  |
| `layerA.synth.shape` | Layer A Synth Shape | choice | no | a switch or a choice (a macro moves continuous controls) |
| `layerA.synth.pitch` | Layer A Synth Pitch | choice | no | a switch or a choice (a macro moves continuous controls) |
| `layerB.synth.wave` | Layer B Synth Wave | choice | no | a switch or a choice (a macro moves continuous controls) |
| `layerB.synth.octave` | Layer B Synth Octave | choice | no | a switch or a choice (a macro moves continuous controls) |
| `layerB.synth.detune` | Layer B Synth Detune | continuous | yes |  |
| `layerB.synth.tone` | Layer B Synth Tone | continuous | yes |  |
| `layerB.synth.shape` | Layer B Synth Shape | choice | no | a switch or a choice (a macro moves continuous controls) |
| `layerB.synth.pitch` | Layer B Synth Pitch | choice | no | a switch or a choice (a macro moves continuous controls) |
| `layerC.synth.wave` | Layer C Synth Wave | choice | no | a switch or a choice (a macro moves continuous controls) |
| `layerC.synth.octave` | Layer C Synth Octave | choice | no | a switch or a choice (a macro moves continuous controls) |
| `layerC.synth.detune` | Layer C Synth Detune | continuous | yes |  |
| `layerC.synth.tone` | Layer C Synth Tone | continuous | yes |  |
| `layerC.synth.shape` | Layer C Synth Shape | choice | no | a switch or a choice (a macro moves continuous controls) |
| `layerC.synth.pitch` | Layer C Synth Pitch | choice | no | a switch or a choice (a macro moves continuous controls) |
| `space.engine` | Space Engine | choice | no | a switch or a choice (a macro moves continuous controls) |
| `space.abyss.gravity` | Abyss Gravity | continuous | yes |  |
| `space.abyss.freeze` | Abyss Freeze | switch | no | a switch or a choice (a macro moves continuous controls) |
| `space.abyss.resonance` | Abyss Resonance | continuous | yes |  |
| `space.vintage.variant` | Vintage Variant | choice | no | a switch or a choice (a macro moves continuous controls) |
| `space.vintage.era` | Vintage Era | choice | no | a switch or a choice (a macro moves continuous controls) |
| `space.vintage.diffusion` | Vintage Diffusion | continuous | yes |  |
| `space.bloom.variant` | Bloom Variant | choice | no | a switch or a choice (a macro moves continuous controls) |
| `space.bloom.sync` | Bloom Sync | switch | no | a switch or a choice (a macro moves continuous controls) |
| `space.bloom.division` | Bloom Division | choice | no | a switch or a choice (a macro moves continuous controls) |
| `space.bloom.time` | Bloom Time | continuous | yes |  |
| `space.bloom.diffusion` | Bloom Diffusion | continuous | yes |  |
| `space.abyss.variant` | Abyss Variant | choice | no | a switch or a choice (a macro moves continuous controls) |
| `echo.ascend.interval` | Echo Ascend Interval | choice | no | a switch or a choice (a macro moves continuous controls) |
| `echo.ascend.motion` | Echo Ascend Motion | choice | no | a switch or a choice (a macro moves continuous controls) |
| `arp.link` | Arp Link | switch | no | a switch or a choice (a macro moves continuous controls) |
| `arp.cardA` | Arp Card A | switch | no | a switch or a choice (a macro moves continuous controls) |
| `arp.cardB` | Arp Card B | switch | no | a switch or a choice (a macro moves continuous controls) |
| `arp.cardC` | Arp Card C | switch | no | a switch or a choice (a macro moves continuous controls) |
| `arp.cardB.pattern` | Arp Card B Pattern | choice | no | a switch or a choice (a macro moves continuous controls) |
| `arp.cardB.rate` | Arp Card B Rate | choice | no | a switch or a choice (a macro moves continuous controls) |
| `arp.cardB.octaves` | Arp Card B Octaves | choice | no | a switch or a choice (a macro moves continuous controls) |
| `arp.cardC.pattern` | Arp Card C Pattern | choice | no | a switch or a choice (a macro moves continuous controls) |
| `arp.cardC.rate` | Arp Card C Rate | choice | no | a switch or a choice (a macro moves continuous controls) |
| `arp.cardC.octaves` | Arp Card C Octaves | choice | no | a switch or a choice (a macro moves continuous controls) |
| `layerA.synth.mode` | Layer A Synth Mode | choice | no | a switch or a choice (a macro moves continuous controls) |
| `layerA.synth.imprint` | Layer A Synth Imprint | continuous | yes |  |
| `layerA.synth.motion` | Layer A Synth Motion | continuous | yes |  |
| `layerB.synth.mode` | Layer B Synth Mode | choice | no | a switch or a choice (a macro moves continuous controls) |
| `layerB.synth.imprint` | Layer B Synth Imprint | continuous | yes |  |
| `layerB.synth.motion` | Layer B Synth Motion | continuous | yes |  |
| `layerC.synth.mode` | Layer C Synth Mode | choice | no | a switch or a choice (a macro moves continuous controls) |
| `layerC.synth.imprint` | Layer C Synth Imprint | continuous | yes |  |
| `layerC.synth.motion` | Layer C Synth Motion | continuous | yes |  |
| `fx.spread.1.on` | FX STEREO SPREAD 1 On | switch | no | a switch or a choice (a macro moves continuous controls) |
| `fx.spread.1.mix` | FX STEREO SPREAD 1 Mix | continuous | yes |  |
| `fx.spread.1.output` | FX STEREO SPREAD 1 Output | continuous | yes |  |
| `fx.spread.1.mode` | FX STEREO SPREAD 1 Mode | choice | no | a switch or a choice (a macro moves continuous controls) |
| `fx.spread.1.width` | FX STEREO SPREAD 1 Width | continuous | yes |  |
| `fx.spread.1.voices` | FX STEREO SPREAD 1 Hyper Voices | choice | no | a switch or a choice (a macro moves continuous controls) |
| `fx.spread.1.detune` | FX STEREO SPREAD 1 Detune | continuous | yes |  |
| `fx.spread.1.hyperRate` | FX STEREO SPREAD 1 Hyper Rate | continuous | yes |  |
| `fx.spread.1.dimSize` | FX STEREO SPREAD 1 Dimension Size | continuous | yes |  |
| `fx.spread.1.dimRate` | FX STEREO SPREAD 1 Dimension Rate | continuous | yes |  |
| `fx.spread.1.dimAmount` | FX STEREO SPREAD 1 Dimension Amount | continuous | yes |  |
| `fx.spread.1.lowMono` | FX STEREO SPREAD 1 Low Mono | switch | no | a switch or a choice (a macro moves continuous controls) |
| `fx.spread.1.lowMonoHz` | FX STEREO SPREAD 1 Low Mono Frequency | continuous | yes |  |
| `fx.spread.2.on` | FX STEREO SPREAD 2 On | switch | no | a switch or a choice (a macro moves continuous controls) |
| `fx.spread.2.mix` | FX STEREO SPREAD 2 Mix | continuous | yes |  |
| `fx.spread.2.output` | FX STEREO SPREAD 2 Output | continuous | yes |  |
| `fx.spread.2.mode` | FX STEREO SPREAD 2 Mode | choice | no | a switch or a choice (a macro moves continuous controls) |
| `fx.spread.2.width` | FX STEREO SPREAD 2 Width | continuous | yes |  |
| `fx.spread.2.voices` | FX STEREO SPREAD 2 Hyper Voices | choice | no | a switch or a choice (a macro moves continuous controls) |
| `fx.spread.2.detune` | FX STEREO SPREAD 2 Detune | continuous | yes |  |
| `fx.spread.2.hyperRate` | FX STEREO SPREAD 2 Hyper Rate | continuous | yes |  |
| `fx.spread.2.dimSize` | FX STEREO SPREAD 2 Dimension Size | continuous | yes |  |
| `fx.spread.2.dimRate` | FX STEREO SPREAD 2 Dimension Rate | continuous | yes |  |
| `fx.spread.2.dimAmount` | FX STEREO SPREAD 2 Dimension Amount | continuous | yes |  |
| `fx.spread.2.lowMono` | FX STEREO SPREAD 2 Low Mono | switch | no | a switch or a choice (a macro moves continuous controls) |
| `fx.spread.2.lowMonoHz` | FX STEREO SPREAD 2 Low Mono Frequency | continuous | yes |  |
| `fx.comp.1.on` | FX COMPRESSOR 1 On | switch | no | a switch or a choice (a macro moves continuous controls) |
| `fx.comp.1.mix` | FX COMPRESSOR 1 Mix | continuous | yes |  |
| `fx.comp.1.output` | FX COMPRESSOR 1 Output | continuous | yes |  |
| `fx.comp.1.mode` | FX COMPRESSOR 1 Mode | choice | no | a switch or a choice (a macro moves continuous controls) |
| `fx.comp.1.threshold` | FX COMPRESSOR 1 Threshold | continuous | yes |  |
| `fx.comp.1.ratio` | FX COMPRESSOR 1 Ratio | continuous | yes |  |
| `fx.comp.1.attack` | FX COMPRESSOR 1 Attack | continuous | yes |  |
| `fx.comp.1.release` | FX COMPRESSOR 1 Release | continuous | yes |  |
| `fx.comp.1.knee` | FX COMPRESSOR 1 Knee | continuous | yes |  |
| `fx.comp.1.makeup` | FX COMPRESSOR 1 Makeup | continuous | yes |  |
| `fx.comp.1.detector` | FX COMPRESSOR 1 Detector | choice | no | a switch or a choice (a macro moves continuous controls) |
| `fx.comp.1.link` | FX COMPRESSOR 1 Stereo Link | continuous | yes |  |
| `fx.comp.1.sidechain` | FX COMPRESSOR 1 Sidechain HP | continuous | yes |  |
| `fx.comp.1.autoMakeup` | FX COMPRESSOR 1 Auto Makeup | switch | no | a switch or a choice (a macro moves continuous controls) |
| `fx.comp.1.fet.input` | FX COMPRESSOR 1 FET Input | continuous | yes |  |
| `fx.comp.1.fet.output` | FX COMPRESSOR 1 FET Output | continuous | yes |  |
| `fx.comp.1.fet.attack` | FX COMPRESSOR 1 FET Attack | continuous | yes |  |
| `fx.comp.1.fet.release` | FX COMPRESSOR 1 FET Release | continuous | yes |  |
| `fx.comp.1.fet.ratio` | FX COMPRESSOR 1 FET Ratio | choice | no | a switch or a choice (a macro moves continuous controls) |
| `fx.comp.1.fet.color` | FX COMPRESSOR 1 FET Color | continuous | yes |  |
| `fx.comp.1.fet.link` | FX COMPRESSOR 1 FET Stereo Link | switch | no | a switch or a choice (a macro moves continuous controls) |
| `fx.comp.1.fet.colorOnly` | FX COMPRESSOR 1 FET Color Only | switch | no | a switch or a choice (a macro moves continuous controls) |
| `fx.comp.1.multi.depth` | FX COMPRESSOR 1 Depth | continuous | yes |  |
| `fx.comp.1.multi.time` | FX COMPRESSOR 1 Time | continuous | yes |  |
| `fx.comp.1.multi.tone` | FX COMPRESSOR 1 Tone | continuous | yes |  |
| `fx.comp.1.multi.threshold` | FX COMPRESSOR 1 Band Threshold | continuous | yes |  |
| `fx.comp.1.multi.down` | FX COMPRESSOR 1 Down Ratio | continuous | yes |  |
| `fx.comp.1.multi.up` | FX COMPRESSOR 1 Up Ratio | continuous | yes |  |
| `fx.comp.1.multi.attack` | FX COMPRESSOR 1 Band Attack | continuous | yes |  |
| `fx.comp.1.multi.release` | FX COMPRESSOR 1 Band Release | continuous | yes |  |
| `fx.comp.1.multi.low` | FX COMPRESSOR 1 Low Crossover | continuous | yes |  |
| `fx.comp.1.multi.high` | FX COMPRESSOR 1 High Crossover | continuous | yes |  |
| `fx.comp.1.multi.lowGain` | FX COMPRESSOR 1 Low Gain | continuous | yes |  |
| `fx.comp.1.multi.lowOffset` | FX COMPRESSOR 1 Low Threshold Offset | continuous | yes |  |
| `fx.comp.1.multi.midGain` | FX COMPRESSOR 1 Mid Gain | continuous | yes |  |
| `fx.comp.1.multi.midOffset` | FX COMPRESSOR 1 Mid Threshold Offset | continuous | yes |  |
| `fx.comp.1.multi.highGain` | FX COMPRESSOR 1 High Gain | continuous | yes |  |
| `fx.comp.1.multi.highOffset` | FX COMPRESSOR 1 High Threshold Offset | continuous | yes |  |
| `fx.comp.1.multi.deadband` | FX COMPRESSOR 1 Deadband | continuous | yes |  |
| `fx.comp.1.multi.match` | FX COMPRESSOR 1 Loudness Match | switch | no | a switch or a choice (a macro moves continuous controls) |
| `fx.comp.2.on` | FX COMPRESSOR 2 On | switch | no | a switch or a choice (a macro moves continuous controls) |
| `fx.comp.2.mix` | FX COMPRESSOR 2 Mix | continuous | yes |  |
| `fx.comp.2.output` | FX COMPRESSOR 2 Output | continuous | yes |  |
| `fx.comp.2.mode` | FX COMPRESSOR 2 Mode | choice | no | a switch or a choice (a macro moves continuous controls) |
| `fx.comp.2.threshold` | FX COMPRESSOR 2 Threshold | continuous | yes |  |
| `fx.comp.2.ratio` | FX COMPRESSOR 2 Ratio | continuous | yes |  |
| `fx.comp.2.attack` | FX COMPRESSOR 2 Attack | continuous | yes |  |
| `fx.comp.2.release` | FX COMPRESSOR 2 Release | continuous | yes |  |
| `fx.comp.2.knee` | FX COMPRESSOR 2 Knee | continuous | yes |  |
| `fx.comp.2.makeup` | FX COMPRESSOR 2 Makeup | continuous | yes |  |
| `fx.comp.2.detector` | FX COMPRESSOR 2 Detector | choice | no | a switch or a choice (a macro moves continuous controls) |
| `fx.comp.2.link` | FX COMPRESSOR 2 Stereo Link | continuous | yes |  |
| `fx.comp.2.sidechain` | FX COMPRESSOR 2 Sidechain HP | continuous | yes |  |
| `fx.comp.2.autoMakeup` | FX COMPRESSOR 2 Auto Makeup | switch | no | a switch or a choice (a macro moves continuous controls) |
| `fx.comp.2.fet.input` | FX COMPRESSOR 2 FET Input | continuous | yes |  |
| `fx.comp.2.fet.output` | FX COMPRESSOR 2 FET Output | continuous | yes |  |
| `fx.comp.2.fet.attack` | FX COMPRESSOR 2 FET Attack | continuous | yes |  |
| `fx.comp.2.fet.release` | FX COMPRESSOR 2 FET Release | continuous | yes |  |
| `fx.comp.2.fet.ratio` | FX COMPRESSOR 2 FET Ratio | choice | no | a switch or a choice (a macro moves continuous controls) |
| `fx.comp.2.fet.color` | FX COMPRESSOR 2 FET Color | continuous | yes |  |
| `fx.comp.2.fet.link` | FX COMPRESSOR 2 FET Stereo Link | switch | no | a switch or a choice (a macro moves continuous controls) |
| `fx.comp.2.fet.colorOnly` | FX COMPRESSOR 2 FET Color Only | switch | no | a switch or a choice (a macro moves continuous controls) |
| `fx.comp.2.multi.depth` | FX COMPRESSOR 2 Depth | continuous | yes |  |
| `fx.comp.2.multi.time` | FX COMPRESSOR 2 Time | continuous | yes |  |
| `fx.comp.2.multi.tone` | FX COMPRESSOR 2 Tone | continuous | yes |  |
| `fx.comp.2.multi.threshold` | FX COMPRESSOR 2 Band Threshold | continuous | yes |  |
| `fx.comp.2.multi.down` | FX COMPRESSOR 2 Down Ratio | continuous | yes |  |
| `fx.comp.2.multi.up` | FX COMPRESSOR 2 Up Ratio | continuous | yes |  |
| `fx.comp.2.multi.attack` | FX COMPRESSOR 2 Band Attack | continuous | yes |  |
| `fx.comp.2.multi.release` | FX COMPRESSOR 2 Band Release | continuous | yes |  |
| `fx.comp.2.multi.low` | FX COMPRESSOR 2 Low Crossover | continuous | yes |  |
| `fx.comp.2.multi.high` | FX COMPRESSOR 2 High Crossover | continuous | yes |  |
| `fx.comp.2.multi.lowGain` | FX COMPRESSOR 2 Low Gain | continuous | yes |  |
| `fx.comp.2.multi.lowOffset` | FX COMPRESSOR 2 Low Threshold Offset | continuous | yes |  |
| `fx.comp.2.multi.midGain` | FX COMPRESSOR 2 Mid Gain | continuous | yes |  |
| `fx.comp.2.multi.midOffset` | FX COMPRESSOR 2 Mid Threshold Offset | continuous | yes |  |
| `fx.comp.2.multi.highGain` | FX COMPRESSOR 2 High Gain | continuous | yes |  |
| `fx.comp.2.multi.highOffset` | FX COMPRESSOR 2 High Threshold Offset | continuous | yes |  |
| `fx.comp.2.multi.deadband` | FX COMPRESSOR 2 Deadband | continuous | yes |  |
| `fx.comp.2.multi.match` | FX COMPRESSOR 2 Loudness Match | switch | no | a switch or a choice (a macro moves continuous controls) |
| `fx.eq.1.on` | FX EQUALIZER 1 On | switch | no | a switch or a choice (a macro moves continuous controls) |
| `fx.eq.1.mix` | FX EQUALIZER 1 Mix | continuous | yes |  |
| `fx.eq.1.output` | FX EQUALIZER 1 Output | continuous | yes |  |
| `fx.eq.1.band1.on` | FX EQUALIZER 1 Band 1 On | switch | no | a switch or a choice (a macro moves continuous controls) |
| `fx.eq.1.band1.type` | FX EQUALIZER 1 Band 1 Type | choice | no | a switch or a choice (a macro moves continuous controls) |
| `fx.eq.1.band1.freq` | FX EQUALIZER 1 Band 1 Frequency | continuous | yes |  |
| `fx.eq.1.band1.gain` | FX EQUALIZER 1 Band 1 Gain | continuous | yes |  |
| `fx.eq.1.band1.q` | FX EQUALIZER 1 Band 1 Q | continuous | yes |  |
| `fx.eq.1.band2.on` | FX EQUALIZER 1 Band 2 On | switch | no | a switch or a choice (a macro moves continuous controls) |
| `fx.eq.1.band2.type` | FX EQUALIZER 1 Band 2 Type | choice | no | a switch or a choice (a macro moves continuous controls) |
| `fx.eq.1.band2.freq` | FX EQUALIZER 1 Band 2 Frequency | continuous | yes |  |
| `fx.eq.1.band2.gain` | FX EQUALIZER 1 Band 2 Gain | continuous | yes |  |
| `fx.eq.1.band2.q` | FX EQUALIZER 1 Band 2 Q | continuous | yes |  |
| `fx.eq.1.band3.on` | FX EQUALIZER 1 Band 3 On | switch | no | a switch or a choice (a macro moves continuous controls) |
| `fx.eq.1.band3.type` | FX EQUALIZER 1 Band 3 Type | choice | no | a switch or a choice (a macro moves continuous controls) |
| `fx.eq.1.band3.freq` | FX EQUALIZER 1 Band 3 Frequency | continuous | yes |  |
| `fx.eq.1.band3.gain` | FX EQUALIZER 1 Band 3 Gain | continuous | yes |  |
| `fx.eq.1.band3.q` | FX EQUALIZER 1 Band 3 Q | continuous | yes |  |
| `fx.eq.1.band4.on` | FX EQUALIZER 1 Band 4 On | switch | no | a switch or a choice (a macro moves continuous controls) |
| `fx.eq.1.band4.type` | FX EQUALIZER 1 Band 4 Type | choice | no | a switch or a choice (a macro moves continuous controls) |
| `fx.eq.1.band4.freq` | FX EQUALIZER 1 Band 4 Frequency | continuous | yes |  |
| `fx.eq.1.band4.gain` | FX EQUALIZER 1 Band 4 Gain | continuous | yes |  |
| `fx.eq.1.band4.q` | FX EQUALIZER 1 Band 4 Q | continuous | yes |  |
| `fx.eq.1.hp.on` | FX EQUALIZER 1 High Pass On | switch | no | a switch or a choice (a macro moves continuous controls) |
| `fx.eq.1.hp.freq` | FX EQUALIZER 1 High Pass Frequency | continuous | yes |  |
| `fx.eq.1.hp.steep` | FX EQUALIZER 1 High Pass 24 dB | switch | no | a switch or a choice (a macro moves continuous controls) |
| `fx.eq.1.lp.on` | FX EQUALIZER 1 Low Pass On | switch | no | a switch or a choice (a macro moves continuous controls) |
| `fx.eq.1.lp.freq` | FX EQUALIZER 1 Low Pass Frequency | continuous | yes |  |
| `fx.eq.1.lp.steep` | FX EQUALIZER 1 Low Pass 24 dB | switch | no | a switch or a choice (a macro moves continuous controls) |
| `fx.eq.2.on` | FX EQUALIZER 2 On | switch | no | a switch or a choice (a macro moves continuous controls) |
| `fx.eq.2.mix` | FX EQUALIZER 2 Mix | continuous | yes |  |
| `fx.eq.2.output` | FX EQUALIZER 2 Output | continuous | yes |  |
| `fx.eq.2.band1.on` | FX EQUALIZER 2 Band 1 On | switch | no | a switch or a choice (a macro moves continuous controls) |
| `fx.eq.2.band1.type` | FX EQUALIZER 2 Band 1 Type | choice | no | a switch or a choice (a macro moves continuous controls) |
| `fx.eq.2.band1.freq` | FX EQUALIZER 2 Band 1 Frequency | continuous | yes |  |
| `fx.eq.2.band1.gain` | FX EQUALIZER 2 Band 1 Gain | continuous | yes |  |
| `fx.eq.2.band1.q` | FX EQUALIZER 2 Band 1 Q | continuous | yes |  |
| `fx.eq.2.band2.on` | FX EQUALIZER 2 Band 2 On | switch | no | a switch or a choice (a macro moves continuous controls) |
| `fx.eq.2.band2.type` | FX EQUALIZER 2 Band 2 Type | choice | no | a switch or a choice (a macro moves continuous controls) |
| `fx.eq.2.band2.freq` | FX EQUALIZER 2 Band 2 Frequency | continuous | yes |  |
| `fx.eq.2.band2.gain` | FX EQUALIZER 2 Band 2 Gain | continuous | yes |  |
| `fx.eq.2.band2.q` | FX EQUALIZER 2 Band 2 Q | continuous | yes |  |
| `fx.eq.2.band3.on` | FX EQUALIZER 2 Band 3 On | switch | no | a switch or a choice (a macro moves continuous controls) |
| `fx.eq.2.band3.type` | FX EQUALIZER 2 Band 3 Type | choice | no | a switch or a choice (a macro moves continuous controls) |
| `fx.eq.2.band3.freq` | FX EQUALIZER 2 Band 3 Frequency | continuous | yes |  |
| `fx.eq.2.band3.gain` | FX EQUALIZER 2 Band 3 Gain | continuous | yes |  |
| `fx.eq.2.band3.q` | FX EQUALIZER 2 Band 3 Q | continuous | yes |  |
| `fx.eq.2.band4.on` | FX EQUALIZER 2 Band 4 On | switch | no | a switch or a choice (a macro moves continuous controls) |
| `fx.eq.2.band4.type` | FX EQUALIZER 2 Band 4 Type | choice | no | a switch or a choice (a macro moves continuous controls) |
| `fx.eq.2.band4.freq` | FX EQUALIZER 2 Band 4 Frequency | continuous | yes |  |
| `fx.eq.2.band4.gain` | FX EQUALIZER 2 Band 4 Gain | continuous | yes |  |
| `fx.eq.2.band4.q` | FX EQUALIZER 2 Band 4 Q | continuous | yes |  |
| `fx.eq.2.hp.on` | FX EQUALIZER 2 High Pass On | switch | no | a switch or a choice (a macro moves continuous controls) |
| `fx.eq.2.hp.freq` | FX EQUALIZER 2 High Pass Frequency | continuous | yes |  |
| `fx.eq.2.hp.steep` | FX EQUALIZER 2 High Pass 24 dB | switch | no | a switch or a choice (a macro moves continuous controls) |
| `fx.eq.2.lp.on` | FX EQUALIZER 2 Low Pass On | switch | no | a switch or a choice (a macro moves continuous controls) |
| `fx.eq.2.lp.freq` | FX EQUALIZER 2 Low Pass Frequency | continuous | yes |  |
| `fx.eq.2.lp.steep` | FX EQUALIZER 2 Low Pass 24 dB | switch | no | a switch or a choice (a macro moves continuous controls) |
| `fx.filter.1.on` | FX FX FILTER 1 On | switch | no | a switch or a choice (a macro moves continuous controls) |
| `fx.filter.1.mix` | FX FX FILTER 1 Mix | continuous | yes |  |
| `fx.filter.1.output` | FX FX FILTER 1 Output | continuous | yes |  |
| `fx.filter.1.type` | FX FX FILTER 1 Type | choice | no | a switch or a choice (a macro moves continuous controls) |
| `fx.filter.1.freq` | FX FX FILTER 1 Frequency | continuous | yes |  |
| `fx.filter.1.reso` | FX FX FILTER 1 Resonance | continuous | yes |  |
| `fx.filter.1.drive` | FX FX FILTER 1 Drive | continuous | yes |  |
| `fx.filter.1.pan` | FX FX FILTER 1 Pan | continuous | yes |  |
| `fx.filter.1.combDamp` | FX FX FILTER 1 Comb Damping | continuous | yes |  |
| `fx.filter.1.formantShift` | FX FX FILTER 1 Formant Shift | continuous | yes |  |
| `fx.filter.2.on` | FX FX FILTER 2 On | switch | no | a switch or a choice (a macro moves continuous controls) |
| `fx.filter.2.mix` | FX FX FILTER 2 Mix | continuous | yes |  |
| `fx.filter.2.output` | FX FX FILTER 2 Output | continuous | yes |  |
| `fx.filter.2.type` | FX FX FILTER 2 Type | choice | no | a switch or a choice (a macro moves continuous controls) |
| `fx.filter.2.freq` | FX FX FILTER 2 Frequency | continuous | yes |  |
| `fx.filter.2.reso` | FX FX FILTER 2 Resonance | continuous | yes |  |
| `fx.filter.2.drive` | FX FX FILTER 2 Drive | continuous | yes |  |
| `fx.filter.2.pan` | FX FX FILTER 2 Pan | continuous | yes |  |
| `fx.filter.2.combDamp` | FX FX FILTER 2 Comb Damping | continuous | yes |  |
| `fx.filter.2.formantShift` | FX FX FILTER 2 Formant Shift | continuous | yes |  |
| `fx.phaser.1.on` | FX PHASER 1 On | switch | no | a switch or a choice (a macro moves continuous controls) |
| `fx.phaser.1.mix` | FX PHASER 1 Mix | continuous | yes |  |
| `fx.phaser.1.output` | FX PHASER 1 Output | continuous | yes |  |
| `fx.phaser.1.mode` | FX PHASER 1 Mode | choice | no | a switch or a choice (a macro moves continuous controls) |
| `fx.phaser.1.rate` | FX PHASER 1 Rate | continuous | yes |  |
| `fx.phaser.1.sync` | FX PHASER 1 Sync | switch | no | a switch or a choice (a macro moves continuous controls) |
| `fx.phaser.1.division` | FX PHASER 1 Division | choice | no | a switch or a choice (a macro moves continuous controls) |
| `fx.phaser.1.depth` | FX PHASER 1 Depth | continuous | yes |  |
| `fx.phaser.1.center` | FX PHASER 1 Center | continuous | yes |  |
| `fx.phaser.1.feedback` | FX PHASER 1 Feedback | continuous | yes |  |
| `fx.phaser.1.stages` | FX PHASER 1 Stages | choice | no | a switch or a choice (a macro moves continuous controls) |
| `fx.phaser.1.stereo` | FX PHASER 1 Stereo | continuous | yes |  |
| `fx.phaser.2.on` | FX PHASER 2 On | switch | no | a switch or a choice (a macro moves continuous controls) |
| `fx.phaser.2.mix` | FX PHASER 2 Mix | continuous | yes |  |
| `fx.phaser.2.output` | FX PHASER 2 Output | continuous | yes |  |
| `fx.phaser.2.mode` | FX PHASER 2 Mode | choice | no | a switch or a choice (a macro moves continuous controls) |
| `fx.phaser.2.rate` | FX PHASER 2 Rate | continuous | yes |  |
| `fx.phaser.2.sync` | FX PHASER 2 Sync | switch | no | a switch or a choice (a macro moves continuous controls) |
| `fx.phaser.2.division` | FX PHASER 2 Division | choice | no | a switch or a choice (a macro moves continuous controls) |
| `fx.phaser.2.depth` | FX PHASER 2 Depth | continuous | yes |  |
| `fx.phaser.2.center` | FX PHASER 2 Center | continuous | yes |  |
| `fx.phaser.2.feedback` | FX PHASER 2 Feedback | continuous | yes |  |
| `fx.phaser.2.stages` | FX PHASER 2 Stages | choice | no | a switch or a choice (a macro moves continuous controls) |
| `fx.phaser.2.stereo` | FX PHASER 2 Stereo | continuous | yes |  |
| `fx.flanger.1.on` | FX FLANGER 1 On | switch | no | a switch or a choice (a macro moves continuous controls) |
| `fx.flanger.1.mix` | FX FLANGER 1 Mix | continuous | yes |  |
| `fx.flanger.1.output` | FX FLANGER 1 Output | continuous | yes |  |
| `fx.flanger.1.mode` | FX FLANGER 1 Mode | choice | no | a switch or a choice (a macro moves continuous controls) |
| `fx.flanger.1.rate` | FX FLANGER 1 Rate | continuous | yes |  |
| `fx.flanger.1.sync` | FX FLANGER 1 Sync | switch | no | a switch or a choice (a macro moves continuous controls) |
| `fx.flanger.1.division` | FX FLANGER 1 Division | choice | no | a switch or a choice (a macro moves continuous controls) |
| `fx.flanger.1.depth` | FX FLANGER 1 Depth | continuous | yes |  |
| `fx.flanger.1.delay` | FX FLANGER 1 Delay | continuous | yes |  |
| `fx.flanger.1.feedback` | FX FLANGER 1 Feedback | continuous | yes |  |
| `fx.flanger.1.stereo` | FX FLANGER 1 Stereo | continuous | yes |  |
| `fx.flanger.1.width` | FX FLANGER 1 Width | continuous | yes |  |
| `fx.flanger.2.on` | FX FLANGER 2 On | switch | no | a switch or a choice (a macro moves continuous controls) |
| `fx.flanger.2.mix` | FX FLANGER 2 Mix | continuous | yes |  |
| `fx.flanger.2.output` | FX FLANGER 2 Output | continuous | yes |  |
| `fx.flanger.2.mode` | FX FLANGER 2 Mode | choice | no | a switch or a choice (a macro moves continuous controls) |
| `fx.flanger.2.rate` | FX FLANGER 2 Rate | continuous | yes |  |
| `fx.flanger.2.sync` | FX FLANGER 2 Sync | switch | no | a switch or a choice (a macro moves continuous controls) |
| `fx.flanger.2.division` | FX FLANGER 2 Division | choice | no | a switch or a choice (a macro moves continuous controls) |
| `fx.flanger.2.depth` | FX FLANGER 2 Depth | continuous | yes |  |
| `fx.flanger.2.delay` | FX FLANGER 2 Delay | continuous | yes |  |
| `fx.flanger.2.feedback` | FX FLANGER 2 Feedback | continuous | yes |  |
| `fx.flanger.2.stereo` | FX FLANGER 2 Stereo | continuous | yes |  |
| `fx.flanger.2.width` | FX FLANGER 2 Width | continuous | yes |  |
| `fx.shift.1.on` | FX FREQ SHIFTER On | switch | no | a switch or a choice (a macro moves continuous controls) |
| `fx.shift.1.mix` | FX FREQ SHIFTER Mix | continuous | yes |  |
| `fx.shift.1.output` | FX FREQ SHIFTER Output | continuous | yes |  |
| `fx.shift.1.mode` | FX FREQ SHIFTER Mode | choice | no | a switch or a choice (a macro moves continuous controls) |
| `fx.shift.1.hz` | FX FREQ SHIFTER Shift | continuous | yes |  |
| `fx.shift.1.direction` | FX FREQ SHIFTER Direction | choice | no | a switch or a choice (a macro moves continuous controls) |
| `fx.shift.1.width` | FX FREQ SHIFTER Width | continuous | yes |  |
| `fx.shift.1.feedback` | FX FREQ SHIFTER Feedback | continuous | yes |  |
| `fx.shift.1.blurAmount` | FX FREQ SHIFTER Blur Amount | continuous | yes |  |
| `fx.shift.1.blurTime` | FX FREQ SHIFTER Blur Time | continuous | yes |  |
| `fx.conv.1.on` | FX CONVOLUTION On | switch | no | a switch or a choice (a macro moves continuous controls) |
| `fx.conv.1.mix` | FX CONVOLUTION Mix | continuous | yes |  |
| `fx.conv.1.output` | FX CONVOLUTION Output | continuous | yes |  |
| `fx.conv.1.mode` | FX CONVOLUTION Mode | choice | no | a switch or a choice (a macro moves continuous controls) |
| `fx.conv.1.impulse` | FX CONVOLUTION Impulse | choice | no | a switch or a choice (a macro moves continuous controls) |
| `fx.conv.1.predelay` | FX CONVOLUTION Pre-delay | continuous | yes |  |
| `fx.conv.1.decay` | FX CONVOLUTION Decay | continuous | yes |  |
| `fx.conv.1.size` | FX CONVOLUTION Size | continuous | yes |  |
| `fx.conv.1.lowCut` | FX CONVOLUTION Low Cut | continuous | yes |  |
| `fx.conv.1.highCut` | FX CONVOLUTION High Cut | continuous | yes |  |
| `fx.conv.1.width` | FX CONVOLUTION Width | continuous | yes |  |
| `fx.conv.1.normalize` | FX CONVOLUTION Normalize | switch | no | a switch or a choice (a macro moves continuous controls) |
| `fx.conv.1.reverse` | FX CONVOLUTION Reverse | switch | no | a switch or a choice (a macro moves continuous controls) |
| `fx.utility.1.on` | FX UTILITY 1 On | switch | no | a switch or a choice (a macro moves continuous controls) |
| `fx.utility.1.mix` | FX UTILITY 1 Mix | continuous | yes |  |
| `fx.utility.1.output` | FX UTILITY 1 Output | continuous | yes |  |
| `fx.utility.1.gain` | FX UTILITY 1 Gain | continuous | yes |  |
| `fx.utility.1.pan` | FX UTILITY 1 Balance | continuous | yes |  |
| `fx.utility.1.width` | FX UTILITY 1 Width | continuous | yes |  |
| `fx.utility.1.invertL` | FX UTILITY 1 Invert Left | switch | no | a switch or a choice (a macro moves continuous controls) |
| `fx.utility.1.invertR` | FX UTILITY 1 Invert Right | switch | no | a switch or a choice (a macro moves continuous controls) |
| `fx.utility.1.monoBass` | FX UTILITY 1 Mono Bass | switch | no | a switch or a choice (a macro moves continuous controls) |
| `fx.utility.1.monoBassHz` | FX UTILITY 1 Mono Bass Frequency | continuous | yes |  |
| `fx.utility.1.hp.on` | FX UTILITY 1 High Pass On | switch | no | a switch or a choice (a macro moves continuous controls) |
| `fx.utility.1.hp.freq` | FX UTILITY 1 High Pass Frequency | continuous | yes |  |
| `fx.utility.1.lp.on` | FX UTILITY 1 Low Pass On | switch | no | a switch or a choice (a macro moves continuous controls) |
| `fx.utility.1.lp.freq` | FX UTILITY 1 Low Pass Frequency | continuous | yes |  |
| `fx.utility.2.on` | FX UTILITY 2 On | switch | no | a switch or a choice (a macro moves continuous controls) |
| `fx.utility.2.mix` | FX UTILITY 2 Mix | continuous | yes |  |
| `fx.utility.2.output` | FX UTILITY 2 Output | continuous | yes |  |
| `fx.utility.2.gain` | FX UTILITY 2 Gain | continuous | yes |  |
| `fx.utility.2.pan` | FX UTILITY 2 Balance | continuous | yes |  |
| `fx.utility.2.width` | FX UTILITY 2 Width | continuous | yes |  |
| `fx.utility.2.invertL` | FX UTILITY 2 Invert Left | switch | no | a switch or a choice (a macro moves continuous controls) |
| `fx.utility.2.invertR` | FX UTILITY 2 Invert Right | switch | no | a switch or a choice (a macro moves continuous controls) |
| `fx.utility.2.monoBass` | FX UTILITY 2 Mono Bass | switch | no | a switch or a choice (a macro moves continuous controls) |
| `fx.utility.2.monoBassHz` | FX UTILITY 2 Mono Bass Frequency | continuous | yes |  |
| `fx.utility.2.hp.on` | FX UTILITY 2 High Pass On | switch | no | a switch or a choice (a macro moves continuous controls) |
| `fx.utility.2.hp.freq` | FX UTILITY 2 High Pass Frequency | continuous | yes |  |
| `fx.utility.2.lp.on` | FX UTILITY 2 Low Pass On | switch | no | a switch or a choice (a macro moves continuous controls) |
| `fx.utility.2.lp.freq` | FX UTILITY 2 Low Pass Frequency | continuous | yes |  |
| `fx.retro.1.on` | FX RETRO 1 On | switch | no | a switch or a choice (a macro moves continuous controls) |
| `fx.retro.1.mix` | FX RETRO 1 Mix | continuous | yes |  |
| `fx.retro.1.output` | FX RETRO 1 Output | continuous | yes |  |
| `fx.retro.1.mode` | FX RETRO 1 Mode | choice | no | a switch or a choice (a macro moves continuous controls) |
| `fx.retro.1.tape.model` | FX RETRO 1 Tape Model | choice | no | a switch or a choice (a macro moves continuous controls) |
| `fx.retro.1.vinyl.rpm` | FX RETRO 1 Vinyl Speed | choice | no | a switch or a choice (a macro moves continuous controls) |
| `fx.retro.1.digital.era` | FX RETRO 1 Digital Type | choice | no | a switch or a choice (a macro moves continuous controls) |
| `fx.retro.1.character` | FX RETRO 1 Character | continuous | yes |  |
| `fx.retro.1.wear` | FX RETRO 1 Wear | continuous | yes |  |
| `fx.retro.1.input` | FX RETRO 1 Input | continuous | yes |  |
| `fx.retro.1.tone` | FX RETRO 1 Tone | continuous | yes |  |
| `fx.retro.1.saturation` | FX RETRO 1 Saturation | continuous | yes |  |
| `fx.retro.1.preserve` | FX RETRO 1 Preserve | choice | no | a switch or a choice (a macro moves continuous controls) |
| `fx.retro.1.wow.depth` | FX RETRO 1 Wow Depth | continuous | yes |  |
| `fx.retro.1.wow.rate` | FX RETRO 1 Wow Rate | continuous | yes |  |
| `fx.retro.1.flutter.depth` | FX RETRO 1 Flutter Depth | continuous | yes |  |
| `fx.retro.1.flutter.rate` | FX RETRO 1 Flutter Rate | continuous | yes |  |
| `fx.retro.1.transport.random` | FX RETRO 1 Transport Random | continuous | yes |  |
| `fx.retro.1.stereo.phase` | FX RETRO 1 Stereo Phase | continuous | yes |  |
| `fx.retro.1.noise.type` | FX RETRO 1 Noise Type | choice | no | a switch or a choice (a macro moves continuous controls) |
| `fx.retro.1.noise.level` | FX RETRO 1 Noise Level | continuous | yes |  |
| `fx.retro.1.noise.follow` | FX RETRO 1 Noise Follow | continuous | yes |  |
| `fx.retro.1.noise.automute` | FX RETRO 1 Noise Auto Mute | switch | no | a switch or a choice (a macro moves continuous controls) |
| `fx.retro.1.crackle` | FX RETRO 1 Crackle | continuous | yes |  |
| `fx.retro.1.pops` | FX RETRO 1 Pops | continuous | yes |  |
| `fx.retro.1.dropout.rate` | FX RETRO 1 Dropout Rate | continuous | yes |  |
| `fx.retro.1.dropout.depth` | FX RETRO 1 Dropout Depth | continuous | yes |  |
| `fx.retro.1.dropout.length` | FX RETRO 1 Dropout Length | continuous | yes |  |
| `fx.retro.1.bandwidth.low` | FX RETRO 1 Low Edge | continuous | yes |  |
| `fx.retro.1.bandwidth.high` | FX RETRO 1 High Edge | continuous | yes |  |
| `fx.retro.1.head.bump` | FX RETRO 1 Head Bump | continuous | yes |  |
| `fx.retro.1.compress.amount` | FX RETRO 1 Compress | continuous | yes |  |
| `fx.retro.1.compress.attack` | FX RETRO 1 Compress Attack | continuous | yes |  |
| `fx.retro.1.compress.release` | FX RETRO 1 Compress Release | continuous | yes |  |
| `fx.retro.1.compress.link` | FX RETRO 1 Compress Link | switch | no | a switch or a choice (a macro moves continuous controls) |
| `fx.retro.1.compress.tilt` | FX RETRO 1 Sidechain Tilt | continuous | yes |  |
| `fx.retro.1.digital.bits` | FX RETRO 1 Bits | continuous | yes |  |
| `fx.retro.1.digital.rate` | FX RETRO 1 Rate | continuous | yes |  |
| `fx.retro.1.digital.alias` | FX RETRO 1 Alias | choice | no | a switch or a choice (a macro moves continuous controls) |
| `fx.retro.1.digital.mod` | FX RETRO 1 Motion | continuous | yes |  |
| `fx.retro.1.codec.loss` | FX RETRO 1 Loss | continuous | yes |  |
| `fx.retro.1.codec.speed` | FX RETRO 1 Speed | continuous | yes |  |
| `fx.retro.1.codec.mode` | FX RETRO 1 Codec Mode | choice | no | a switch or a choice (a macro moves continuous controls) |
| `fx.retro.1.codec.artifactOnly` | FX RETRO 1 Artifact Only | switch | no | a switch or a choice (a macro moves continuous controls) |
| `fx.retro.1.codec.stereo` | FX RETRO 1 Codec Stereo | choice | no | a switch or a choice (a macro moves continuous controls) |
| `fx.retro.1.codec.artifact` | FX RETRO 1 Artifact | continuous | yes |  |
| `fx.retro.1.codec.ambience` | FX RETRO 1 Artifact Ambience | continuous | yes |  |
| `fx.retro.1.seed` | FX RETRO 1 Seed | continuous | yes |  |
| `fx.retro.2.on` | FX RETRO 2 On | switch | no | a switch or a choice (a macro moves continuous controls) |
| `fx.retro.2.mix` | FX RETRO 2 Mix | continuous | yes |  |
| `fx.retro.2.output` | FX RETRO 2 Output | continuous | yes |  |
| `fx.retro.2.mode` | FX RETRO 2 Mode | choice | no | a switch or a choice (a macro moves continuous controls) |
| `fx.retro.2.tape.model` | FX RETRO 2 Tape Model | choice | no | a switch or a choice (a macro moves continuous controls) |
| `fx.retro.2.vinyl.rpm` | FX RETRO 2 Vinyl Speed | choice | no | a switch or a choice (a macro moves continuous controls) |
| `fx.retro.2.digital.era` | FX RETRO 2 Digital Type | choice | no | a switch or a choice (a macro moves continuous controls) |
| `fx.retro.2.character` | FX RETRO 2 Character | continuous | yes |  |
| `fx.retro.2.wear` | FX RETRO 2 Wear | continuous | yes |  |
| `fx.retro.2.input` | FX RETRO 2 Input | continuous | yes |  |
| `fx.retro.2.tone` | FX RETRO 2 Tone | continuous | yes |  |
| `fx.retro.2.saturation` | FX RETRO 2 Saturation | continuous | yes |  |
| `fx.retro.2.preserve` | FX RETRO 2 Preserve | choice | no | a switch or a choice (a macro moves continuous controls) |
| `fx.retro.2.wow.depth` | FX RETRO 2 Wow Depth | continuous | yes |  |
| `fx.retro.2.wow.rate` | FX RETRO 2 Wow Rate | continuous | yes |  |
| `fx.retro.2.flutter.depth` | FX RETRO 2 Flutter Depth | continuous | yes |  |
| `fx.retro.2.flutter.rate` | FX RETRO 2 Flutter Rate | continuous | yes |  |
| `fx.retro.2.transport.random` | FX RETRO 2 Transport Random | continuous | yes |  |
| `fx.retro.2.stereo.phase` | FX RETRO 2 Stereo Phase | continuous | yes |  |
| `fx.retro.2.noise.type` | FX RETRO 2 Noise Type | choice | no | a switch or a choice (a macro moves continuous controls) |
| `fx.retro.2.noise.level` | FX RETRO 2 Noise Level | continuous | yes |  |
| `fx.retro.2.noise.follow` | FX RETRO 2 Noise Follow | continuous | yes |  |
| `fx.retro.2.noise.automute` | FX RETRO 2 Noise Auto Mute | switch | no | a switch or a choice (a macro moves continuous controls) |
| `fx.retro.2.crackle` | FX RETRO 2 Crackle | continuous | yes |  |
| `fx.retro.2.pops` | FX RETRO 2 Pops | continuous | yes |  |
| `fx.retro.2.dropout.rate` | FX RETRO 2 Dropout Rate | continuous | yes |  |
| `fx.retro.2.dropout.depth` | FX RETRO 2 Dropout Depth | continuous | yes |  |
| `fx.retro.2.dropout.length` | FX RETRO 2 Dropout Length | continuous | yes |  |
| `fx.retro.2.bandwidth.low` | FX RETRO 2 Low Edge | continuous | yes |  |
| `fx.retro.2.bandwidth.high` | FX RETRO 2 High Edge | continuous | yes |  |
| `fx.retro.2.head.bump` | FX RETRO 2 Head Bump | continuous | yes |  |
| `fx.retro.2.compress.amount` | FX RETRO 2 Compress | continuous | yes |  |
| `fx.retro.2.compress.attack` | FX RETRO 2 Compress Attack | continuous | yes |  |
| `fx.retro.2.compress.release` | FX RETRO 2 Compress Release | continuous | yes |  |
| `fx.retro.2.compress.link` | FX RETRO 2 Compress Link | switch | no | a switch or a choice (a macro moves continuous controls) |
| `fx.retro.2.compress.tilt` | FX RETRO 2 Sidechain Tilt | continuous | yes |  |
| `fx.retro.2.digital.bits` | FX RETRO 2 Bits | continuous | yes |  |
| `fx.retro.2.digital.rate` | FX RETRO 2 Rate | continuous | yes |  |
| `fx.retro.2.digital.alias` | FX RETRO 2 Alias | choice | no | a switch or a choice (a macro moves continuous controls) |
| `fx.retro.2.digital.mod` | FX RETRO 2 Motion | continuous | yes |  |
| `fx.retro.2.codec.loss` | FX RETRO 2 Loss | continuous | yes |  |
| `fx.retro.2.codec.speed` | FX RETRO 2 Speed | continuous | yes |  |
| `fx.retro.2.codec.mode` | FX RETRO 2 Codec Mode | choice | no | a switch or a choice (a macro moves continuous controls) |
| `fx.retro.2.codec.artifactOnly` | FX RETRO 2 Artifact Only | switch | no | a switch or a choice (a macro moves continuous controls) |
| `fx.retro.2.codec.stereo` | FX RETRO 2 Codec Stereo | choice | no | a switch or a choice (a macro moves continuous controls) |
| `fx.retro.2.codec.artifact` | FX RETRO 2 Artifact | continuous | yes |  |
| `fx.retro.2.codec.ambience` | FX RETRO 2 Artifact Ambience | continuous | yes |  |
| `fx.retro.2.seed` | FX RETRO 2 Seed | continuous | yes |  |
| `layerA.pedalLoop` | Layer A Pedal Loop | choice | no | a switch or a choice (a macro moves continuous controls) |
| `layerB.pedalLoop` | Layer B Pedal Loop | choice | no | a switch or a choice (a macro moves continuous controls) |
| `layerC.pedalLoop` | Layer C Pedal Loop | choice | no | a switch or a choice (a macro moves continuous controls) |
| `layerA.satellite.enabled` | Layer A Satellite On | switch | no | a switch or a choice (a macro moves continuous controls) |
| `layerA.satellite.mode` | Layer A Satellite Mode | choice | no | a switch or a choice (a macro moves continuous controls) |
| `layerA.satellite.sync` | Layer A Satellite Timing | choice | no | a switch or a choice (a macro moves continuous controls) |
| `layerA.satellite.division` | Layer A Satellite Division | choice | no | a switch or a choice (a macro moves continuous controls) |
| `layerA.satellite.rate` | Layer A Satellite Rate | continuous | yes |  |
| `layerA.satellite.depth` | Layer A Satellite Depth | continuous | yes |  |
| `layerA.satellite.width` | Layer A Satellite Width | continuous | yes |  |
| `layerA.satellite.phase` | Layer A Satellite Phase | continuous | yes |  |
| `layerA.satellite.offset` | Layer A Satellite Offset | continuous | yes |  |
| `layerA.satellite.retrigger` | Layer A Satellite Bounce | choice | no | a switch or a choice (a macro moves continuous controls) |
| `layerB.satellite.enabled` | Layer B Satellite On | switch | no | a switch or a choice (a macro moves continuous controls) |
| `layerB.satellite.mode` | Layer B Satellite Mode | choice | no | a switch or a choice (a macro moves continuous controls) |
| `layerB.satellite.sync` | Layer B Satellite Timing | choice | no | a switch or a choice (a macro moves continuous controls) |
| `layerB.satellite.division` | Layer B Satellite Division | choice | no | a switch or a choice (a macro moves continuous controls) |
| `layerB.satellite.rate` | Layer B Satellite Rate | continuous | yes |  |
| `layerB.satellite.depth` | Layer B Satellite Depth | continuous | yes |  |
| `layerB.satellite.width` | Layer B Satellite Width | continuous | yes |  |
| `layerB.satellite.phase` | Layer B Satellite Phase | continuous | yes |  |
| `layerB.satellite.offset` | Layer B Satellite Offset | continuous | yes |  |
| `layerB.satellite.retrigger` | Layer B Satellite Bounce | choice | no | a switch or a choice (a macro moves continuous controls) |
| `layerC.satellite.enabled` | Layer C Satellite On | switch | no | a switch or a choice (a macro moves continuous controls) |
| `layerC.satellite.mode` | Layer C Satellite Mode | choice | no | a switch or a choice (a macro moves continuous controls) |
| `layerC.satellite.sync` | Layer C Satellite Timing | choice | no | a switch or a choice (a macro moves continuous controls) |
| `layerC.satellite.division` | Layer C Satellite Division | choice | no | a switch or a choice (a macro moves continuous controls) |
| `layerC.satellite.rate` | Layer C Satellite Rate | continuous | yes |  |
| `layerC.satellite.depth` | Layer C Satellite Depth | continuous | yes |  |
| `layerC.satellite.width` | Layer C Satellite Width | continuous | yes |  |
| `layerC.satellite.phase` | Layer C Satellite Phase | continuous | yes |  |
| `layerC.satellite.offset` | Layer C Satellite Offset | continuous | yes |  |
| `layerC.satellite.retrigger` | Layer C Satellite Bounce | choice | no | a switch or a choice (a macro moves continuous controls) |
| `sub.enabled` | Sub On | switch | no | a switch or a choice (a macro moves continuous controls) |
| `sub.wave` | Sub Wave | choice | no | a switch or a choice (a macro moves continuous controls) |
| `sub.octave` | Sub Octave | choice | no | a switch or a choice (a macro moves continuous controls) |
| `sub.grit` | Sub Grit | continuous | yes |  |
| `sub.level` | Sub Level | continuous | yes |  |
| `sub.lowKey` | Sub Low Key | choice | no | a switch or a choice (a macro moves continuous controls) |
| `sub.highKey` | Sub High Key | choice | no | a switch or a choice (a macro moves continuous controls) |
| `sub.response` | Sub Response | choice | no | a switch or a choice (a macro moves continuous controls) |
| `sub.taperStart` | Sub Taper Start | choice | no | a switch or a choice (a macro moves continuous controls) |
| `sub.taperSlope` | Sub Taper Slope | continuous | yes |  |
| `vibrato.enabled` | Vibrato | switch | no | a switch or a choice (a macro moves continuous controls) |
| `vibrato.depth` | Vibrato Depth | continuous | yes |  |
| `vibrato.rate` | Vibrato Rate | continuous | yes |  |
| `vibrato.delay` | Vibrato Delay | continuous | yes |  |
| `vibrato.rise` | Vibrato Rise | continuous | yes |  |
| `vibrato.control` | Vibrato Control | choice | no | a switch or a choice (a macro moves continuous controls) |
| `vibrato.amount` | Vibrato Amount | continuous | yes |  |
| `vibrato.sourceA` | Vibrato Source A | switch | no | a switch or a choice (a macro moves continuous controls) |
| `vibrato.sourceB` | Vibrato Source B | switch | no | a switch or a choice (a macro moves continuous controls) |
| `vibrato.sourceC` | Vibrato Source C | switch | no | a switch or a choice (a macro moves continuous controls) |
| `userMacro.1` | User Macro 1 | continuous | no | a USER macro itself (a macro never moves a macro) |
| `userMacro.2` | User Macro 2 | continuous | no | a USER macro itself (a macro never moves a macro) |
| `userMacro.3` | User Macro 3 | continuous | no | a USER macro itself (a macro never moves a macro) |
| `userMacro.4` | User Macro 4 | continuous | no | a USER macro itself (a macro never moves a macro) |
| `userMacro.5` | User Macro 5 | continuous | no | a USER macro itself (a macro never moves a macro) |
| `userMacro.6` | User Macro 6 | continuous | no | a USER macro itself (a macro never moves a macro) |
