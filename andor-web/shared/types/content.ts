// Content model (docs/CONTENT_MODEL.md). These shapes mirror the Framer CMS collections so a
// non-developer can edit titles, copy, images and audio without touching component code.

export type ApprovalStatus = 'concept' | 'review' | 'approved' | 'retired';

export interface ImageFile { width: number; height: number; file: string }

/** One entry of assets/manifests/generated.json (built from genuine captures). */
export interface ProductAsset {
  id: string;
  alt: string;
  use: string;
  width: number;
  height: number;
  aspect: number;
  priority: boolean;
  approval: ApprovalStatus;
  master: string;
  crop: { x: number; y: number; w: number; h: number } | null;
  files: { avif: ImageFile[]; webp: ImageFile[] };
}

export interface AudioSource { src: string; type: string }

export interface AudioVariant {
  id: 'original' | 'osp' | 'reimagined' | string;
  label: string;
  /** Alternative encodings of the same aligned, loudness-matched file. Empty = not yet produced. */
  sources: AudioSource[];
  /** Integrated loudness after matching (LUFS), recorded by the audio pipeline. */
  loudnessLufs?: number;
}

export interface AudioDemo {
  id: string;
  title: string;
  sourceInstrument: string;
  description: string;
  artwork?: string;
  /** Precomputed peaks (0..1), one per column; computed once in asset preparation. */
  waveformPeaks?: number[];
  durationSeconds?: number;
  bpm?: number;
  variants: AudioVariant[];
  sourceBuild?: string;
  approved: boolean;
}

export interface ReimaginedMode {
  id: 'kaleidoscope' | 'tape-frame' | 'toybox' | 'mosaic' | 'mirage';
  name: string;
  subtitle: string;
  description: string;
  screenshot: string;   // ProductAsset id
  video?: string;
  audio?: AudioDemo;
  fallbackImage: string; // ProductAsset id
  approved: boolean;
}
