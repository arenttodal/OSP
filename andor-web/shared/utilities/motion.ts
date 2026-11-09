import { useEffect, useRef, useState } from 'react';

export const prefersReducedMotion = () =>
  typeof window !== 'undefined' && window.matchMedia('(prefers-reduced-motion: reduce)').matches;

/**
 * Starts an entrance sequence once fonts and the priority image are ready, so text never reflows
 * mid-animation. Returns a ref for the scope element; `.is-in` is added to it.
 */
export function useEntrance<T extends HTMLElement>(waitFor?: () => Promise<unknown>) {
  const ref = useRef<T>(null);
  const [inState, setIn] = useState(false);
  useEffect(() => {
    let cancelled = false;
    const fonts = (document as any).fonts?.ready ?? Promise.resolve();
    const timeout = new Promise((r) => setTimeout(r, 1200)); // never hold the page hostage
    Promise.race([Promise.all([fonts, waitFor?.() ?? Promise.resolve()]), timeout]).then(() => {
      if (cancelled) return;
      requestAnimationFrame(() => { ref.current?.classList.add('is-in'); setIn(true); });
    });
    return () => { cancelled = true; };
  }, []);
  return [ref, inState] as const;
}

/** Resolves when the given <img> has decoded (or immediately if it already has). */
export function imageReady(selector: string): () => Promise<unknown> {
  return () => {
    const img = document.querySelector<HTMLImageElement>(selector);
    if (!img) return Promise.resolve();
    if (img.complete) return img.decode?.().catch(() => undefined) ?? Promise.resolve();
    return new Promise((r) => { img.addEventListener('load', r, { once: true }); img.addEventListener('error', r, { once: true }); });
  };
}

/** 0..1 progress of an element scrolling through the viewport's sticky span (rAF-throttled). */
export function useScrollProgress<T extends HTMLElement>(enabled: boolean) {
  const ref = useRef<T>(null);
  const [p, setP] = useState(0);
  useEffect(() => {
    if (!enabled) return;
    let frame = 0;
    const update = () => {
      frame = 0;
      const el = ref.current;
      if (!el) return;
      const r = el.getBoundingClientRect();
      const span = r.height - window.innerHeight;
      setP(span > 0 ? Math.min(1, Math.max(0, -r.top / span)) : 0);
    };
    const onScroll = () => { if (!frame) frame = requestAnimationFrame(update); };
    update();
    window.addEventListener('scroll', onScroll, { passive: true });
    window.addEventListener('resize', onScroll);
    return () => { window.removeEventListener('scroll', onScroll); window.removeEventListener('resize', onScroll); cancelAnimationFrame(frame); };
  }, [enabled]);
  return [ref, p] as const;
}

export function useMedia(query: string) {
  const [match, setMatch] = useState(() => typeof window !== 'undefined' && window.matchMedia(query).matches);
  useEffect(() => {
    const m = window.matchMedia(query);
    const on = () => setMatch(m.matches);
    m.addEventListener('change', on);
    return () => m.removeEventListener('change', on);
  }, [query]);
  return match;
}
