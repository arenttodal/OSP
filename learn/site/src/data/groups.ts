export const groups = [
  { slug: 'make-an-instrument', title: 'Make an instrument', blurb: 'Load a sound, map a phrase, record or grab from an input.' },
  { slug: 'play-and-layer', title: 'Play and layer', blurb: 'One Shot and Granular, the sustain pedal, layers and presets.' },
  { slug: 'synth-imprint', title: 'Synth & Imprint', blurb: 'Blend an oscillator into a recording and colour it with the recording.' },
  { slug: 'arpeggiator', title: 'Arpeggiator', blurb: 'Patterns per layer, Strum, Satellite, custom patterns and Sub.' },
  { slug: 'granular-key-dna', title: 'Granular & Key DNA', blurb: 'Shape the grains and give each key its own character.' },
  { slug: 'shape-and-finish', title: 'Shape and finish', blurb: 'Reimagined, Movement, modulation, macros and effects.' },
] as const;
export type GroupSlug = (typeof groups)[number]['slug'];
export const groupTitle = (slug: string) => groups.find((g) => g.slug === slug)?.title ?? slug;
