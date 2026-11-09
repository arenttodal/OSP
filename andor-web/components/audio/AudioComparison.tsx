import React, { KeyboardEvent, PointerEvent, useCallback, useEffect, useId, useMemo, useRef, useState } from 'react';
import type { AudioDemo } from '../../shared/types/content';
import { GlobalAudioController } from './GlobalAudioController';
import './AudioComparison.css';

type Status = 'pending' | 'idle' | 'loading' | 'ready' | 'playing' | 'error';

export interface AudioComparisonProps {
  demo: AudioDemo;
  theme?: 'light' | 'dark';
  /** compact: one row (hero); full: title and description above. */
  layout?: 'compact' | 'full' | 'stack';
  className?: string;
}

const FADE = 0.03; // seconds: a short equal-gain crossfade between aligned variants (no clicks)

const fmt = (s: number) => {
  if (!isFinite(s) || s < 0) s = 0;
  const m = Math.floor(s / 60);
  return `${m}:${String(Math.floor(s % 60)).padStart(2, '0')}`;
};

async function decodeFirst(ctx: AudioContext, sources: { src: string; type: string }[], signal: AbortSignal) {
  // Try each encoding in order (e.g. Opus, then AAC, then MP3); unsupported ones fail to decode.
  let last: unknown = null;
  for (const s of sources) {
    try {
      const probe = document.createElement('audio');
      if (s.type && probe.canPlayType(s.type) === '') continue;
      const res = await fetch(s.src, { signal });
      if (!res.ok) throw new Error(`${res.status} ${s.src}`);
      return await ctx.decodeAudioData(await res.arrayBuffer());
    } catch (e) {
      if ((e as Error).name === 'AbortError') throw e;
      last = e;
    }
  }
  throw last ?? new Error('No playable format.');
}

/**
 * Synchronised A/B/C listening: every variant is decoded and started together on one clock, and
 * switching only crossfades gains, so the playhead never jumps and the variants never overlap
 * audibly. Audio loads on the first press of play, never on page load.
 */
export function AudioComparison({ demo, theme = 'light', layout = 'compact', className = '' }: AudioComparisonProps) {
  const uid = useId();
  const playable = demo.variants.length > 0 && demo.variants.every((v) => v.sources.length > 0);
  const [status, setStatus] = useState<Status>(playable ? 'idle' : 'pending');
  const [variant, setVariant] = useState(0);
  const [position, setPosition] = useState(0);
  const [duration, setDuration] = useState(demo.durationSeconds ?? 0);
  const [error, setError] = useState<string | null>(null);

  const buffers = useRef<AudioBuffer[] | null>(null);
  const graph = useRef<{ sources: AudioBufferSourceNode[]; gains: GainNode[]; startedAt: number; offset: number } | null>(null);
  const abort = useRef<AbortController | null>(null);
  const raf = useRef(0);
  const offsetRef = useRef(0);
  const variantRef = useRef(0);
  variantRef.current = variant;

  const stopGraph = useCallback(() => {
    const g = graph.current;
    if (!g) return;
    g.sources.forEach((s) => { s.onended = null; try { s.stop(); } catch { /* already stopped */ } s.disconnect(); });
    g.gains.forEach((n) => n.disconnect());
    graph.current = null;
    cancelAnimationFrame(raf.current);
  }, []);

  const currentTime = useCallback(() => {
    const g = graph.current;
    if (!g) return offsetRef.current;
    const ctx = g.sources[0].context;
    return Math.min(g.offset + (ctx.currentTime - g.startedAt), duration || Infinity);
  }, [duration]);

  const pause = useCallback(() => {
    offsetRef.current = currentTime();
    stopGraph();
    GlobalAudioController.release(uid);
    setStatus((s) => (s === 'playing' ? 'ready' : s));
  }, [currentTime, stopGraph, uid]);

  // Register once, with a stable pointer to the latest pause: re-registering whenever `pause`
  // changes would clear the controller's active player mid-playback and break exclusivity.
  const pauseRef = useRef(pause);
  pauseRef.current = pause;
  useEffect(() => GlobalAudioController.register(uid, () => pauseRef.current()), [uid]);
  useEffect(() => () => { abort.current?.abort(); stopGraph(); GlobalAudioController.release(uid); }, [stopGraph, uid]);

  const tick = useCallback(() => {
    setPosition(currentTime());
    raf.current = requestAnimationFrame(tick);
  }, [currentTime]);

  const startAt = useCallback(async (offset: number) => {
    const ctx = await GlobalAudioController.context();
    const bufs = buffers.current!;
    stopGraph();
    const t0 = ctx.currentTime + 0.04; // schedule slightly ahead so all variants start together
    const sources: AudioBufferSourceNode[] = [];
    const gains: GainNode[] = [];
    bufs.forEach((b, i) => {
      const s = ctx.createBufferSource();
      s.buffer = b;
      const g = ctx.createGain();
      g.gain.value = i === variantRef.current ? 1 : 0;
      s.connect(g).connect(ctx.destination);
      s.start(t0, Math.min(offset, b.duration - 0.01));
      sources.push(s);
      gains.push(g);
    });
    sources[0].onended = () => { offsetRef.current = 0; stopGraph(); setPosition(0); setStatus('ready'); GlobalAudioController.release(uid); };
    graph.current = { sources, gains, startedAt: t0, offset };
    GlobalAudioController.claim(uid);
    setStatus('playing');
    raf.current = requestAnimationFrame(tick);
  }, [stopGraph, tick, uid]);

  const load = useCallback(async () => {
    setStatus('loading');
    setError(null);
    abort.current = new AbortController();
    try {
      const ctx = await GlobalAudioController.context();
      const bufs = await Promise.all(demo.variants.map((v) => decodeFirst(ctx, v.sources, abort.current!.signal)));
      buffers.current = bufs;
      setDuration(Math.min(...bufs.map((b) => b.duration)));
      return true;
    } catch (e) {
      if ((e as Error).name === 'AbortError') return false;
      setError('This example could not be loaded.');
      setStatus('error');
      return false;
    }
  }, [demo.variants]);

  const toggle = useCallback(async () => {
    if (status === 'pending' || status === 'loading') return;
    if (status === 'playing') return pause();
    if (!buffers.current && !(await load())) return;
    await startAt(offsetRef.current);
  }, [status, pause, load, startAt]);

  const choose = useCallback((i: number) => {
    setVariant(i);
    const g = graph.current;
    if (!g) return;
    const ctx = g.sources[0].context;
    const now = ctx.currentTime;
    g.gains.forEach((n, k) => {
      n.gain.cancelScheduledValues(now);
      n.gain.setValueAtTime(n.gain.value, now);
      n.gain.linearRampToValueAtTime(k === i ? 1 : 0, now + FADE);
    });
  }, []);

  const seek = useCallback((t: number) => {
    const clamped = Math.max(0, Math.min(t, duration || 0));
    offsetRef.current = clamped;
    setPosition(clamped);
    if (graph.current) startAt(clamped);
  }, [duration, startAt]);

  // Seek by pointer on the timeline (touch and mouse alike)
  const track = useRef<HTMLDivElement>(null);
  const seekFromPointer = (e: PointerEvent<HTMLDivElement>) => {
    const r = track.current!.getBoundingClientRect();
    seek(((e.clientX - r.left) / r.width) * duration);
  };
  const onTrackKey = (e: KeyboardEvent) => {
    const step = { ArrowRight: 5, ArrowUp: 5, ArrowLeft: -5, ArrowDown: -5, PageUp: 15, PageDown: -15 } as Record<string, number>;
    if (e.key in step) { e.preventDefault(); seek(position + step[e.key]); }
    if (e.key === 'Home') { e.preventDefault(); seek(0); }
    if (e.key === 'End') { e.preventDefault(); seek(duration - 0.5); }
  };
  const onVariantKey = (e: KeyboardEvent) => {
    const n = demo.variants.length;
    if (e.key === 'ArrowRight' || e.key === 'ArrowDown') { e.preventDefault(); choose((variant + 1) % n); focusVariant((variant + 1) % n); }
    if (e.key === 'ArrowLeft' || e.key === 'ArrowUp') { e.preventDefault(); choose((variant + n - 1) % n); focusVariant((variant + n - 1) % n); }
  };
  const group = useRef<HTMLDivElement>(null);
  const focusVariant = (i: number) => (group.current?.querySelectorAll('button')[i] as HTMLButtonElement | undefined)?.focus();

  const peaks = useMemo(() => demo.waveformPeaks ?? [], [demo.waveformPeaks]);
  const progress = duration ? position / duration : 0;
  const pending = status === 'pending';

  return (
    <section className={`ac ac--${theme} ac--${layout} ${className}`} aria-label={`Listen: ${demo.title}`} data-status={status}>
      {layout === 'full' && (
        <header className="ac__head">
          <p className="ac__eyebrow">{demo.sourceInstrument}</p>
          <h3 className="ac__title">{demo.title}</h3>
          {demo.description && <p className="ac__desc">{demo.description}</p>}
        </header>
      )}
      <div className="ac__row">
        <button
          type="button"
          className="ac__play"
          onClick={toggle}
          disabled={pending || status === 'loading'}
          aria-label={status === 'playing' ? `Pause ${demo.title}` : `Play ${demo.title}, ${demo.variants[variant]?.label ?? ''}`}
          aria-describedby={pending ? `${uid}-pending` : undefined}
        >
          {status === 'loading' ? <span className="ac__spinner" aria-hidden="true" /> : status === 'playing' ? (
            <svg viewBox="0 0 16 16" aria-hidden="true"><rect x="3.5" y="2.5" width="3" height="11" rx="1" /><rect x="9.5" y="2.5" width="3" height="11" rx="1" /></svg>
          ) : (
            <svg viewBox="0 0 16 16" aria-hidden="true"><path d="M4.5 2.6v10.8c0 .5.5.8.9.5l8.1-5.4c.4-.3.4-.8 0-1.1L5.4 2.1c-.4-.3-.9 0-.9.5z" /></svg>
          )}
        </button>
        <div className="ac__body">
          <div className="ac__meta">
            {layout !== 'full' && <span className="ac__name">{demo.sourceInstrument}</span>}
            <span className="ac__time" aria-hidden={pending}>{pending ? '' : `${fmt(position)} / ${fmt(duration)}`}</span>
          </div>
          <div
            ref={track}
            className="ac__track"
            role="slider"
            tabIndex={pending ? -1 : 0}
            aria-label="Playback position"
            aria-valuemin={0}
            aria-valuemax={Math.round(duration)}
            aria-valuenow={Math.round(position)}
            aria-valuetext={`${fmt(position)} of ${fmt(duration)}`}
            aria-disabled={pending || undefined}
            onKeyDown={onTrackKey}
            onPointerDown={(e) => { if (pending) return; (e.target as HTMLElement).setPointerCapture?.(e.pointerId); seekFromPointer(e); }}
          >
            {peaks.length ? (
              <svg className="ac__wave" viewBox={`0 0 ${peaks.length} 100`} preserveAspectRatio="none" aria-hidden="true">
                {peaks.map((p, i) => <rect key={i} x={i + 0.15} width={0.7} y={50 - p * 48} height={Math.max(1.5, p * 96)} className={i / peaks.length < progress ? 'on' : ''} />)}
              </svg>
            ) : (
              <div className="ac__line" aria-hidden="true"><div className="ac__fill" style={{ transform: `scaleX(${progress})` }} /></div>
            )}
          </div>
        </div>
      </div>
      <div ref={group} className="ac__variants" role="radiogroup" aria-label="Version" onKeyDown={onVariantKey}>
        {demo.variants.map((v, i) => (
          <button key={v.id} type="button" role="radio" aria-checked={i === variant} tabIndex={i === variant ? 0 : -1} disabled={pending} onClick={() => choose(i)}>
            {v.label}
          </button>
        ))}
      </div>
      {pending && <p id={`${uid}-pending`} className="ac__note">Recordings in preparation — this player activates when the approved demo is rendered.</p>}
      {error && <p className="ac__note ac__note--error" role="alert">{error} <button type="button" className="link" onClick={() => { buffers.current = null; setStatus('idle'); }}>Try again</button></p>}
    </section>
  );
}
