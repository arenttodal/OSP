// Landing-page copy. Every claim follows docs/marketing/features-and-benefits.md (and its §7:
// release status, listening verdicts, platform claims). No invented prices, dates or testimonials.

export const CHAPTERS = [
  {
    id: 'capture', n: '01', kicker: 'Capture', title: 'Start with one sound.', asset: 'osp-empty',
    body: 'A sung vowel, a bowed note, an old synth, a memo from your phone. Drop it in. There is no mapping, no zones and nothing to prepare.',
  },
  {
    id: 'play', n: '02', kicker: 'Play', title: 'Play it across the keys.', asset: 'osp-hero-1l',
    body: 'The root note is found for you and the sound spreads across the keyboard. Hold a chord, and hold it longer than the recording lasts: the sustain carries on without an obvious loop.',
  },
  {
    id: 'transform', n: '03', kicker: 'Transform', title: 'Then take it somewhere new.', asset: 'osp-reimagined-mosaic',
    body: 'One REIMAGINED knob moves the sound from the recording itself toward something else: a worn tape, a resonant kaleidoscope, a mosaic rebuilt from its own harmonics.',
  },
];

// The plan's initial examples. No recording exists yet, so each card says so.
export const DEMOS = [
  { id: 'cello', name: 'Cello', line: 'One long, low tone.' },
  { id: 'voice', name: 'Voice', line: 'A sung vowel.' },
  { id: 'nyckelharpa', name: 'Nyckelharpa', line: 'A single bowed note.' },
  { id: 'synth', name: 'Analog synth', line: 'An old synth, one key.' },
  { id: 'pluck', name: 'Plucked string', line: 'A short pluck, held for ever.' },
];
export const DEMO_START = 2; // the nyckelharpa, the plan's lead example, in the middle

export const MODES = [
  { id: 'kaleidoscope', label: 'Kaleidoscope', hint: 'Resonance, doubling, wandering formants', asset: 'dd-reimagined-kaleidoscope',
    text: 'Sympathetic resonance tuned to the sound, a fluid sustain, moving doubling and wandering formants, with grains at the far end.' },
  { id: 'tape-frame', label: 'Tape Frame', hint: 'A finite piece of tape, worn a little more each pass', asset: 'dd-reimagined-tape-frame',
    text: 'The sound laid on a finite piece of tape. Low notes are slower and darker, and every pass is a little more worn.' },
  { id: 'toybox', label: 'Toybox', hint: 'A tiny digital memory turning back and forth', asset: 'dd-reimagined-toybox',
    text: 'A tiny, clever digital memory whose head turns back and forth through the sound.' },
  { id: 'mosaic', label: 'Mosaic', hint: 'Rebuilt from its harmonic fingerprint', asset: 'dd-reimagined-mosaic',
    text: 'The sound rebuilt from its harmonic fingerprint, at any pitch, travelling through the recording’s own evolution.' },
  { id: 'mirage', label: 'Mirage', hint: 'Early-digital weight into a driven filter', asset: 'dd-reimagined-mirage',
    text: 'Early-digital weight. The sample clock follows the note into a driven, resonant 4-pole filter.' },
];

export const SYNTH_STATES = [
  { id: 'sample', label: 'Sample only', asset: 'syn-card-sample', text: 'The recording on its own, played across the keys. SYNTH at 0 %.' },
  { id: 'analog', label: 'Analog', asset: 'syn-card-analog', text: 'A warm, band-limited oscillator blends in under the recording: saw, square, triangle or sine, with octave, detune and tone. It follows the keys, or the recording’s own tuning and vibrato.' },
  { id: 'imprint', label: 'Imprint', asset: 'syn-card-imprint', text: 'The recording’s spectral body (its resonances, formants, bow or breath colour) shapes the oscillator, so the synth sounds cut from your sound.' },
  { id: 'motion', label: 'Imprint + Motion', asset: 'syn-card-motion', text: 'MOTION lets that colour change through the note, the way the recording does. Every note of a mapped phrase carries its own imprint.' },
];

export const LAYERS = [
  { id: '1', label: 'One sound', asset: 'osp-hero-1l', text: 'One recording is enough to begin.' },
  { id: '2', label: 'Two', asset: 'osp-hero-2l', text: 'Add a second sound. Here the pluck on layer B plays as grains.' },
  { id: '3', label: 'Three', asset: 'osp-hero-3l', text: 'Three sounds play as one instrument: a sung vowel, a granular pluck and a low vowel with IMPRINT.' },
];

export const DETAILS = [
  { id: 'knob', asset: 'crop-knob-life', title: 'The macro knob', note: 'LIFE, one of six ceramic macros. The arc shows its range.' },
  { id: 'macros', asset: 'crop-macros', title: 'Six macros', note: 'LIFE, DRIVE, CHARACTER, MOVEMENT, ECHO and SPACE. Each opens its own page.' },
  { id: 'teak', asset: 'crop-teak', title: 'Teak and ceramic', note: 'The frame of the instrument.' },
  { id: 'display', asset: 'crop-display', title: 'The display', note: 'Your recording, with a playhead for every voice.' },
  { id: 'mod', asset: 'det-mod', title: 'Modulation', note: 'Two LFOs, two envelopes, velocity and note: drag any of them onto a knob.' },
  { id: 'keybed', asset: 'crop-keybed', title: 'The keybed', note: 'Held keys glow; the arpeggiator sits beside them.' },
];

export const ESSENTIALS = [
  { id: 'notemap', asset: 'feat-notemap', kicker: 'Note Atlas', title: 'One phrase becomes many samples.',
    body: 'Record a melody once. MAP NOTES finds each note, places it on its own key and keeps the real note-to-note character of the performance. Edit, split, merge, retune and fill the keyboard.' },
  { id: 'capture', asset: 'feat-capture', kicker: 'Live Capture', title: 'Grab what you just played.',
    body: 'Every card has a REC key. Record a take of up to a minute, or grab the last 10, 20, 30 or 60 seconds after the fact, from a microphone, an interface or a DAW track. It is chopped into notes and mapped for you.' },
  { id: 'keydna', asset: 'feat-keydna', kicker: 'KEY DNA', title: 'Every key its own grain.',
    body: 'In Granular mode, KEY DNA gives each key its own grain behaviour: curves you draw across the keyboard, variation and individuality, so a texture changes with where you play.' },
];

export const DEEPER = [
  ['Modulation', 'Two LFOs with custom shapes, two envelopes and the mod wheel. Sixteen routes, dragged onto any knob, and modulation of a modulation’s depth.'],
  ['Arpeggiator', 'Eighteen patterns with rate, gate, octaves and swing, synced to your host. Each layer can run its own, or all linked.'],
  ['ECHO', 'Tape, BBD and ASCEND, which puts a pitch shifter in the delay loop so the repeats climb an interval.'],
  ['SPACE', 'Four reverb engines: ROOMS, ABYSS (huge and freezable), VINTAGE and BLOOM.'],
  ['DRIVE and CHARACTER', 'Tube, tape and crunch circuits, level-matched; a filter that moves the source’s own body resonances.'],
  ['The Library', 'Every sound you load is kept, searchable and auditionable, with collections, tags, packs and backups. An Inbox watches your Voice Memos, Dropbox or iCloud folder.'],
  ['Presets and recall', 'Projects reopen exactly, and bounces render the same every time. An .ospinstrument carries the recordings, their analysis and settings to another computer.'],
  ['Expression', 'MPE with per-note bend of ±48 semitones, pressure and slide, aftertouch, the mod wheel and CC macros. Mono mode with legato and glide.'],
];

export const SPECS = [
  ['Status', 'In development. Not released yet.'],
  ['Formats', 'AU, VST3 and Standalone for macOS (Apple Silicon and Intel), at release. Windows is not announced yet.'],
  ['Hosts', 'To be validated in Logic Pro, Ableton Live and Reaper before release.'],
  ['Audio files', 'WAV, AIFF and FLAC. Mono or stereo, 44.1 to 96 kHz.'],
  ['Layers', 'Up to three sounds, played as one instrument.'],
  ['Expression', 'MPE, aftertouch, mod wheel and CC macros.'],
  ['Interface', 'Scales from 80 to 200 %. Undo and redo for sound changes too.'],
  ['Privacy', 'Works offline. No account, no cloud: your audio never leaves your machine.'],
];

export const FAQ = [
  { q: 'Does it work with my DAW?', a: 'ANDOR OSP is being built as AU, VST3 and Standalone for macOS. Before release it will be validated in Logic Pro, Ableton Live and Reaper, and we will list every host it has been tested in.' },
  { q: 'Can I use my own sounds?', a: 'Yes, that is the point. Drop in any WAV, AIFF or FLAC recording, record from an input, or grab the last seconds you played. A phone memo works as well as a studio recording.' },
  { q: 'What kind of recording works best?', a: 'Anything with a pitch: a voice, a bowed or plucked string, an organ, an old synth, a sustained texture. Unusual sounds still play; if a sound has no clear pitch, OSP asks you for the key instead of refusing it.' },
  { q: 'How is it different from my DAW’s sampler?', a: 'A sampler maps and loops what you give it. OSP starts from one recording and makes it an instrument for you: it finds the root, sustains past the end of the recording, performs each note a little differently, and can transform it while it stays recognisably your sound.' },
  { q: 'Does it need the internet?', a: 'No. Everything works offline, there is no account, and your audio never leaves your computer.' },
  { q: 'What does it cost, and when is it out?', a: 'Pricing and the release date will be announced together. Join the waitlist and we will write when it is ready to play.' },
  { q: 'Windows?', a: 'The first release is for macOS. Windows has not been announced yet.' },
];
