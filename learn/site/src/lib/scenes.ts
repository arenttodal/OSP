// The capture manifest, read at build time: each guide's video, poster, step stills and the
// time range of each step come from its scene's entry (never from a pasted filename).
import manifest from '../data/manifest.json';

export interface Segment { id: string; start: number; end: number; still: string }
export interface Scene {
  id: string; webm: string; mp4: string; poster: string; width: number; height: number;
  duration: number; status: string; productCommit: string; segments: Segment[]; disclosures: string[];
  /** The clip carries the instrument's own sound (captured with the frames). */
  hasAudio: boolean;
}

const BASE = `${import.meta.env.BASE_URL.replace(/\/?$/, '/')}media/osp/`;
type Entry = (typeof manifest)['scenes'][number];

export function scene(id: string): Scene {
  const e = manifest.scenes.find((s: Entry) => s.scenario === id) as Entry | undefined;
  if (!e) throw new Error(`scene ${id} is not in the capture manifest`);
  const marks = Object.entries(e.marks as Record<string, number>).sort((a, b) => a[1] - b[1]);
  const segments = marks.map(([id, start], i) => ({
    id, start, end: i + 1 < marks.length ? marks[i + 1][1] : e.video.duration,
    still: BASE + (e.steps as Record<string, string>)[id],
  }));
  return {
    id, webm: BASE + e.video.files.webm.path, mp4: BASE + e.video.files.mp4.path, poster: BASE + e.poster.path,
    width: e.video.width, height: e.video.height, duration: e.video.duration, status: e.validation.status,
    productCommit: e.productCommit, segments, disclosures: e.disclosures ?? [], hasAudio: Boolean((e as { audio?: unknown }).audio),
  };
}

export const productCommit = manifest.productCommit;
