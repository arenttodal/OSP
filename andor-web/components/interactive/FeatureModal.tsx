import React, { useEffect, useLayoutEffect, useRef, useState } from 'react';
import { ProductMedia } from '../media/ProductMedia';
import './FeatureModal.css';

export interface DeepDiveView { label: string; asset: string; text?: string }
export interface DeepDive {
  id: string;
  n: number;
  kicker: string;
  title: string;
  body: string;
  /** One view, or several shown as tabs (e.g. the five REIMAGINED modes). Genuine captures only. */
  views: DeepDiveView[];
  /** `popover` media is a small real popover shown near its own size; `panel` fills the width. */
  media: 'popover' | 'panel';
  facts?: string[];
}

/**
 * A rounded deep-dive modal on the native <dialog>: focus is trapped, Escape and the backdrop
 * close it, focus returns to the button that opened it. Prev/next walk the features in order.
 */
export function FeatureModal({ items, openId, onClose, onNavigate }: { items: DeepDive[]; openId: string | null; onClose: () => void; onNavigate: (id: string) => void }) {
  const ref = useRef<HTMLDialogElement>(null);
  const trigger = useRef<Element | null>(null);
  const [view, setView] = useState(0);
  const index = items.findIndex((i) => i.id === openId);
  const item = index >= 0 ? items[index] : null;

  useLayoutEffect(() => setView(0), [openId]);

  useEffect(() => {
    const d = ref.current!;
    if (item && !d.open) {
      trigger.current = document.activeElement;
      d.showModal();
      document.documentElement.style.overflow = 'hidden';
    } else if (!item && d.open) {
      d.close();
    }
  }, [item]);

  useEffect(() => {
    const d = ref.current!;
    const closed = () => {
      document.documentElement.style.overflow = '';
      (trigger.current as HTMLElement | null)?.focus?.();
      onClose();
    };
    d.addEventListener('close', closed);
    return () => d.removeEventListener('close', closed);
  }, [onClose]);

  const go = (d: number) => onNavigate(items[(index + d + items.length) % items.length].id);
  const prev = item ? items[(index - 1 + items.length) % items.length] : null;
  const next = item ? items[(index + 1) % items.length] : null;
  const current = item?.views[view];

  return (
    <dialog ref={ref} className="fm" aria-labelledby="fm-title" onClick={(e) => { if (e.target === ref.current) ref.current?.close(); }}>
      {item && current && (
        <div className="fm__sheet" key={item.id}>
          <header className="fm__head">
            <div>
              <p className="t-mono fm__kicker"><span className="fm__num" aria-hidden="true">{item.n}</span>{item.kicker}</p>
              <h2 id="fm-title" className="fm__title">{item.title}</h2>
            </div>
            <button type="button" className="fm__close" onClick={() => ref.current?.close()} aria-label="Close">
              <svg viewBox="0 0 18 18" aria-hidden="true"><path d="M4 4l10 10M14 4L4 14" stroke="currentColor" strokeWidth="1.6" strokeLinecap="round" /></svg>
            </button>
          </header>

          {item.views.length > 1 && (
            <div className="fm__tabs" role="tablist" aria-label={`${item.title} views`}>
              {item.views.map((v, i) => (
                <button key={v.asset} type="button" role="tab" id={`fm-tab-${i}`} aria-selected={i === view} aria-controls="fm-panel" tabIndex={i === view ? 0 : -1}
                  onClick={() => setView(i)}
                  onKeyDown={(e) => {
                    const d = e.key === 'ArrowRight' ? 1 : e.key === 'ArrowLeft' ? -1 : 0;
                    if (!d) return;
                    e.preventDefault();
                    const n = (view + d + item.views.length) % item.views.length;
                    setView(n);
                    (e.currentTarget.parentElement?.children[n] as HTMLElement | undefined)?.focus();
                  }}>{v.label}</button>
              ))}
            </div>
          )}

          <div className={`fm__main fm__main--${item.media}`}>
          <div id="fm-panel" role={item.views.length > 1 ? 'tabpanel' : undefined} aria-labelledby={item.views.length > 1 ? `fm-tab-${view}` : undefined} className={`fm__stage fm__stage--${item.media}`}>
            {item.views.map((v, i) => (
              <ProductMedia key={v.asset} asset={v.asset} variant="detail" className={`fm__media ${i === view ? 'is-current' : ''}`}
                sizes={item.media === 'popover' ? '(max-width: 640px) 86vw, 460px' : '(max-width: 640px) 92vw, 960px'} />
            ))}
          </div>

          <div className="fm__copy">
            <p className="fm__body">{current.text ?? item.body}</p>
            {current.text && <p className="fm__note">{item.body}</p>}
            {item.facts && (
              <ul className="fm__facts">{item.facts.map((f) => <li key={f} className="t-mono">{f}</li>)}</ul>
            )}
          </div>
          </div>

          <footer className="fm__foot">
            <button type="button" className="fm__step" onClick={() => go(-1)}><span aria-hidden="true">←</span> {prev?.title}</button>
            <span className="t-mono fm__count">{index + 1} / {items.length}</span>
            <button type="button" className="fm__step" onClick={() => go(1)}>{next?.title} <span aria-hidden="true">→</span></button>
          </footer>
        </div>
      )}
    </dialog>
  );
}
