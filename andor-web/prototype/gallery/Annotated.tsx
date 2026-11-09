import React, { useLayoutEffect, useRef } from 'react';
import { ProductMedia } from '../../components/media/ProductMedia';

/** A note pinned to a real point of the interface, in 1x editor coordinates (1448 x 1086). */
export interface Annotation { n: number; id: string; x: number; y: number; side: 'left' | 'right'; title: string; text?: string }

const EDITOR = { w: 1448, h: 1086 };

/**
 * The object with notes beside it. Each note sits at the height of the part it names, and a
 * hairline runs from it to that exact point on the genuine capture (measured from the component
 * bounds the capture test writes). With `onOpen`, notes and markers are buttons that open a deep
 * dive. Below 1000 px the notes become numbered markers on the image and a legend underneath.
 */
export function AnnotatedObject({ asset, notes, sizes, onOpen, bare = false }: { asset: string; notes: Annotation[]; sizes: string; onOpen?: (id: string, trigger: HTMLElement) => void; /** markers only: no side notes, no legend */ bare?: boolean }) {
  const room = useRef<HTMLDivElement>(null);
  const interactive = !!onOpen;

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

  const open = (id: string) => (e: React.MouseEvent<HTMLElement>) => onOpen?.(id, e.currentTarget);

  const side = (s: 'left' | 'right') => (
    <div className={`ga-side ga-side--${s}`} aria-hidden={interactive ? undefined : true}>
      {notes.filter((a) => a.side === s).map((a) => (
        <div key={a.n} className="ga-note" data-x={a.x} data-y={a.y} data-side={s}>
          {interactive ? (
            <button type="button" className="t-mono ga-note__title ga-note__btn" onClick={open(a.id)} aria-haspopup="dialog">
              <span className="ga-num" aria-hidden="true">{a.n}</span>{a.title}<span className="ga-open" aria-hidden="true">+</span>
            </button>
          ) : (
            <p className="t-mono ga-note__title"><span className="ga-num">{a.n}</span>{a.title}</p>
          )}
          {a.text && <p className="ga-note__text">{a.text}</p>}
        </div>
      ))}
    </div>
  );

  return (
    <div ref={room} className={`ga ${interactive ? 'ga--interactive' : ''} ${bare ? 'ga--bare' : ''}`}>
      {!bare && side('left')}
      <div className="g-wall ga-wall">
        <div className="g-spot" aria-hidden="true" />
        <div className="ga-frame">
          <ProductMedia asset={asset} variant="studio" className="g-object" priority sizes={sizes} />
          {notes.map((a) => interactive ? (
            <button key={a.n} type="button" className="ga-dot" style={{ left: `${(a.x / EDITOR.w) * 100}%`, top: `${(a.y / EDITOR.h) * 100}%` }}
              onClick={open(a.id)} aria-haspopup="dialog" aria-label={`${a.n}. ${a.title}: look closer`}>
              <span aria-hidden="true">{a.n}</span>
            </button>
          ) : (
            <span key={a.n} className="ga-dot" style={{ left: `${(a.x / EDITOR.w) * 100}%`, top: `${(a.y / EDITOR.h) * 100}%` }} aria-hidden="true">
              <span>{a.n}</span>
            </span>
          ))}
        </div>
      </div>
      {!bare && side('right')}
      {bare ? null : interactive ? (
        <ol className="ga-legend ga-legend--buttons" aria-label="Look closer">
          {notes.map((a) => (
            <li key={a.n}><button type="button" onClick={open(a.id)} aria-haspopup="dialog"><span className="ga-num" aria-hidden="true">{a.n}</span><span>{a.title}</span><span className="ga-open" aria-hidden="true">+</span></button></li>
          ))}
        </ol>
      ) : (
        <ol className="ga-legend" aria-label="What you are looking at">
          {notes.map((a) => (
            <li key={a.n}><span className="ga-num" aria-hidden="true">{a.n}</span><span><strong>{a.title}.</strong> {a.text}</span></li>
          ))}
        </ol>
      )}
    </div>
  );
}
