import type { Annotation } from './Annotated';
import type { DeepDive } from '../../components/interactive/FeatureModal';

// Points: centres of the real components in the three-layer capture, from the bounds the capture
// test writes (assets/source/captures/web-08-hero-3L-bounds.json), in 1x editor coordinates.
const P = {
  recording: { x: 215, y: 300 },  // card A's display (SourceDisplay 51,190 419x258)
  life: { x: 128, y: 815 },       // LIFE macro (MacroLabel 53..203, knob below)
  synth: { x: 1336, y: 206 },     // card C's SYNTH chip (1284,196 104x20): the IMPRINT layer
  reimagined: { x: 884, y: 515 }, // card B's REIMAGINED dial (846,478 75x74)
  space: { x: 858, y: 815 },      // SPACE macro (MacroLabel 783..933)
  keyboard: { x: 1100, y: 965 },  // the keybed (OspKeyboard 148,913 1150x101)
};

/** Version 1: notes with a sentence each (and the facts strip). */
export const NOTES: Annotation[] = [
  { n: 1, id: 'recording', ...P.recording, side: 'left', title: 'Your recording', text: 'Drop in one sound. Its root note is found for you, and it plays right away.' },
  { n: 2, id: 'life', ...P.life, side: 'left', title: 'LIFE', text: 'Every note is performed a little differently, the way a player would.' },
  { n: 3, id: 'synth', ...P.synth, side: 'right', title: 'SYNTH + IMPRINT', text: 'An analog oscillator, coloured by the body of your recording.' },
  { n: 4, id: 'reimagined', ...P.reimagined, side: 'right', title: 'REIMAGINED', text: 'One knob from faithful to far away, in five different modes.' },
  { n: 5, id: 'keyboard', ...P.keyboard, side: 'right', title: 'The keyboard', text: 'Play it in chords. Hold a note longer than the recording lasts.' },
];

/** Version 2: titles only; each opens its deep dive. */
export const DEEP_NOTES: Annotation[] = [
  { n: 1, id: 'recording', ...P.recording, side: 'left', title: 'Your recording' },
  { n: 2, id: 'life', ...P.life, side: 'left', title: 'LIFE' },
  { n: 3, id: 'synth', ...P.synth, side: 'right', title: 'SYNTH + IMPRINT' },
  { n: 4, id: 'reimagined', ...P.reimagined, side: 'right', title: 'REIMAGINED' },
  { n: 5, id: 'space', ...P.space, side: 'right', title: 'SPACE' },
  { n: 6, id: 'keyboard', ...P.keyboard, side: 'right', title: 'Keyboard + arpeggiator' },
];

// Copy follows docs/marketing/features-and-benefits.md. No listening claims ("designed to").
export const DEEP_DIVES: DeepDive[] = [
  {
    id: 'recording', n: 1, kicker: 'Getting sound in', title: 'Your recording', media: 'panel',
    body: 'Drop in one WAV, AIFF or FLAC file. OSP finds its root note and the sound is playable while the analysis finishes. Add a second and a third sound, and all three play as one instrument.',
    views: [{ label: 'Before', asset: 'dd-recording-empty' }, { label: 'Three layers', asset: 'dd-recording-loaded' }],
    facts: ['WAV · AIFF · FLAC', '44.1–96 kHz', 'Up to 3 layers', 'No mapping'],
  },
  {
    id: 'life', n: 2, kicker: 'The performance engine', title: 'LIFE', media: 'popover',
    body: 'Each note is a slightly different performance. Level, brightness, body, attack, micro-pitch and stereo move together, the way a player’s do. At 0 % every note is identical, around 50 % it plays like a believable player, at 100 % more freely.',
    views: [{ label: 'LIFE', asset: 'dd-life' }],
    facts: ['Natural · Loose · Fray', 'Takes 2–16', 'Pitch · Tone · Attack'],
  },
  {
    id: 'synth', n: 3, kicker: 'Synthesis from your sound', title: 'SYNTH + IMPRINT', media: 'panel',
    body: 'A warm analog oscillator blends under each layer and follows the keys, or the recording’s own tuning and vibrato.',
    views: [
      { label: 'Analog', asset: 'dd-synth-analog', text: 'ANALOG: saw, square, triangle or sine, with octave, detune and tone, blended under the recording from subtle to fully synthetic.' },
      { label: 'Imprint', asset: 'dd-synth-imprint', text: 'IMPRINT: the recording’s spectral body (its resonances, formants, bow or breath colour) shapes the oscillator, so the synth sounds cut from your sound.' },
      { label: 'Imprint + Motion', asset: 'dd-synth-motion', text: 'MOTION: the imprint follows how that colour changes through the note. Every note of a mapped phrase carries its own imprint.' },
    ],
    facts: ['Per layer', 'Keys or recording pitch', 'Costs nothing when off'],
  },
  {
    id: 'reimagined', n: 4, kicker: 'Five instruments in every sample', title: 'REIMAGINED', media: 'popover',
    body: 'One knob takes each layer from the recording itself toward its mode’s far end, and the sound stays recognisably yours.',
    views: [
      { label: 'Kaleidoscope', asset: 'dd-reimagined-kaleidoscope', text: 'KALEIDOSCOPE: sympathetic resonance tuned to the sound, a fluid sustain, moving doubling and wandering formants, with grains at the far end.' },
      { label: 'Tape Frame', asset: 'dd-reimagined-tape-frame', text: 'TAPE FRAME: the sound laid on a finite piece of tape. Low notes are slower and darker, and every pass is a little more worn.' },
      { label: 'Toybox', asset: 'dd-reimagined-toybox', text: 'TOYBOX: a tiny, clever digital memory whose head turns back and forth through the sound.' },
      { label: 'Mosaic', asset: 'dd-reimagined-mosaic', text: 'MOSAIC: the sound rebuilt from its harmonic fingerprint, at any pitch, travelling through the recording’s own evolution.' },
      { label: 'Mirage', asset: 'dd-reimagined-mirage', text: 'MIRAGE: early-digital weight. The sample clock follows the note into a driven, resonant 4-pole filter.' },
    ],
  },
  {
    id: 'space', n: 5, kicker: 'Four reverb engines', title: 'SPACE', media: 'popover',
    body: 'ROOMS gives you room, hall, plate and spring. ABYSS is huge, swelling and freezable. VINTAGE brings early-digital halls, plates and chambers, and BLOOM diffuses tempo-synced echoes into reverb.',
    views: [{ label: 'SPACE', asset: 'dd-space' }],
    facts: ['Rooms', 'Abyss', 'Vintage', 'Bloom'],
  },
  {
    id: 'keyboard', n: 6, kicker: 'Play it, or let it play', title: 'Keyboard + arpeggiator', media: 'panel',
    body: 'Play chords and hold notes longer than the recording lasts. The arpeggiator has 18 patterns with rate, gate, octaves and swing, synced to your host, and each layer can arpeggiate on its own or all together.',
    views: [{ label: 'Arpeggiator', asset: 'dd-arp' }],
    facts: ['18 patterns', 'Per layer or linked', 'MPE · aftertouch'],
  },
];
