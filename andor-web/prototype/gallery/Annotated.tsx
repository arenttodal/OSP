import React, { useLayoutEffect, useRef } from 'react';
import { ProductMedia } from '../../components/media/ProductMedia';

/** A note pinned to a real point of the interface, in 1x editor coordinates (1448 x 1086). */
export interface Annotation { n: number; x: number; y: number; side: 'left' | 'right'; title: string; text: string }

const EDITOR = { w: 1448, h: 1086 };

/**
 * The object with notes beside it. Each note sits at the height of the part it names, and a
 * hairline runs from it to that exact point on the genuine capture. Below 1000 px the notes become
 * numbered markers on the image and a legend underneath (nothing depends on hover).
 */
export function AnnotatedObject({ asset, notes, sizes }: { asset: string; notes: Annotation[]; sizes: string }) {
  const room = useRef<HTMLDivElement>(null);

  useLayoutEffect(() => {
    const el = room.current!;
    const measure = () => {
      // The frame, not the image: the image is mid-entrance (transformed) when this first runs.
      const wall = el.querySelector<HTMLElement>('.ga-frame');
      if (!wall) return;
      const w = wall.getBoundingClientRect();
      el.querySelectorAll<HTMLElement>('.ga-note').forEach((note) => {
        const x = Number(note.dataset.x), y = Number(note.dataset.y);
        const px = w.left + (x / EDITOR.w) * w.width;
        const py = w.top + (y / EDITOR.h) * w.height;
        const r = note.getBoundingClientRect();
        const len = note.dataset.side === 'left' ? px - r.right : r.left - px;
        note.style.setProperty('--len', `${Math.max(0, len)}px`);
        note.style.setProperty('--top', `${py - note.parentElement!.getBoundingClientRect().top}px`);
      });
    };
    measure();
    const ro = new ResizeObserver(measure);
    ro.observe(el);
    el.querySelector('img')?.addEventListener('load', measure);
    return () => ro.disconnect();
  }, []);

  const side = (s: 'left' | 'right') => (
    <div className={`ga-side ga-side--${s}`} aria-hidden="true">
      {notes.filter((a) => a.side === s).map((a) => (
        <div key={a.n} className="ga-note" data-x={a.x} data-y={a.y} data-side={s}>
          <p className="t-mono ga-note__title"><span className="ga-num">{a.n}</span>{a.title}</p>
          <p className="ga-note__text">{a.text}</p>
        </div>
      ))}
    </div>
  );

  return (
    <div ref={room} className="ga">
      {side('left')}
      <div className="g-wall ga-wall">
        <div className="g-spot" aria-hidden="true" />
        <div className="ga-frame">
          <ProductMedia asset={asset} variant="studio" className="g-object" priority sizes={sizes} />
          {notes.map((a) => (
            <span key={a.n} className="ga-dot" style={{ left: `${(a.x / EDITOR.w) * 100}%`, top: `${(a.y / EDITOR.h) * 100}%` }} aria-hidden="true">
              <span>{a.n}</span>
            </span>
          ))}
        </div>
      </div>
      {side('right')}
      {/* One accessible list for every width; shown as the legend on small screens. */}
      <ol className="ga-legend" aria-label="What you are looking at">
        {notes.map((a) => (
          <li key={a.n}><span className="ga-num" aria-hidden="true">{a.n}</span><span><strong>{a.title}.</strong> {a.text}</span></li>
        ))}
      </ol>
    </div>
  );
}
