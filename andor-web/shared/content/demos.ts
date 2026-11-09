import type { AudioDemo } from '../types/content';

/**
 * The hero's comparison. No approved recording exists yet (docs/ASSET_MANIFEST.md, A-01), so the
 * variants carry no sources and the player shows its honest pending state. When the recording is
 * rendered, fill `sources` (aligned, loudness-matched Opus + AAC) and `waveformPeaks`.
 */
export const heroDemo: AudioDemo = {
  id: 'hero-01',
  title: 'One recording, three ways',
  sourceInstrument: 'Source recording / 01',
  description: 'The recording on its own, played by OSP, then reimagined.',
  variants: [
    { id: 'original', label: 'Original', sources: [] },
    { id: 'osp', label: 'OSP', sources: [] },
    { id: 'reimagined', label: 'Reimagined', sources: [] },
  ],
  approved: false,
};
