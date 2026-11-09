import React, { KeyboardEvent, useRef, useState } from 'react';
import { createRoot } from 'react-dom/client';
import '../shared/tokens/base.css';
import './h1.css';
import { SiteHeader } from '../components/interactive/SiteHeader';
import { ProductMedia, getAsset, srcSet } from '../components/media/ProductMedia';
import { AudioComparison } from '../components/audio/AudioComparison';
import { heroDemo } from '../shared/content/demos';
import { imageReady, useEntrance } from '../shared/utilities/motion';
import { ReviewBar } from './ReviewBar';
import { NextSectionStub } from './Stub';

// Three real states of the same instrument, each a studio render of a genuine capture.
const STATES = [
  { id: 'object-1l', label: 'One recording', short: '1 layer' },
  { id: 'object-3l', label: 'Three layers', short: '3 layers' },
  { id: 'object-imprint', label: 'IMPRINT synthesis', short: 'Imprint' },
];
const BASE = (import.meta as any).env?.BASE_URL ?? './';

/**
 * H1 — The Instrument. The object is the hero: a large studio render (genuine capture as its face)
 * standing to the right, two-tone type to the left, and a column of real states beside it.
 */
function HeroInstrument() {
  const [scope, entered] = useEntrance<HTMLDivElement>(imageReady('.h1-object--active img'));
  const [state, setState] = useState(0);
  const group = useRef<HTMLDivElement>(null);
  const onKey = (e: KeyboardEvent) => {
    const d = e.key === 'ArrowDown' || e.key === 'ArrowRight' ? 1 : e.key === 'ArrowUp' || e.key === 'ArrowLeft' ? -1 : 0;
    if (!d) return;
    e.preventDefault();
    const next = (state + d + STATES.length) % STATES.length;
    setState(next);
    (group.current?.querySelectorAll('button')[next] as HTMLButtonElement | undefined)?.focus();
  };

  return (
    <div ref={scope} className={`h1 ${entered ? 'is-in' : ''}`}>
      <SiteHeader revealClass="reveal" />
      <main id="top">
        <section className="h1-hero container" aria-labelledby="h1-title">
          <div className="h1-copy">
            <p className="h1-eyebrow reveal" style={{ ['--i' as any]: 1 }}>
              <span className="h1-mark" aria-hidden="true" />
              <span className="t-mono">One Shot Performer</span>
            </p>
            <h1 id="h1-title" className="t-hero h1-title">
              <span className="line-mask" style={{ ['--i' as any]: 2 }}><span>A single sound.</span></span>
              <span className="line-mask" style={{ ['--i' as any]: 3 }}><span className="quiet">Somewhere entirely new.</span></span>
            </h1>
            <p className="t-lede h1-lede reveal" style={{ ['--i' as any]: 5 }}>Turn the smallest musical moment into an instrument that feels alive.</p>
            <div className="h1-ctas reveal" style={{ ['--i' as any]: 6 }}>
              <a className="btn-pair" href="#explore">
                <span className="pill">Explore OSP</span>
                <span className="orb" aria-hidden="true"><svg viewBox="0 0 18 18"><path d="M5 13L13 5M6.5 5H13v6.5" fill="none" stroke="currentColor" strokeWidth="1.6" /></svg></span>
              </a>
            </div>
            <div id="listen" className="h1-listen reveal" style={{ ['--i' as any]: 8 }}>
              <AudioComparison demo={heroDemo} />
            </div>
          </div>

          <div className="h1-stage" aria-live="polite">
            {STATES.map((s, i) => (
              <ProductMedia key={s.id} asset={s.id} variant="object" priority={i === 0}
                className={`h1-object ${i === state ? 'h1-object--active' : ''}`}
                sizes="(max-width: 1023px) 118vw, 60vw" />
            ))}
          </div>

          <div ref={group} className="h1-states reveal" role="radiogroup" aria-label="Instrument state" onKeyDown={onKey} style={{ ['--i' as any]: 9 }}>
            {STATES.map((s, i) => {
              const a = getAsset(s.id);
              return (
                <button key={s.id} type="button" role="radio" aria-checked={i === state} tabIndex={i === state ? 0 : -1} onClick={() => setState(i)} className="h1-state">
                  <span className="h1-thumb">
                    {a && (
                      <picture>
                        <source type="image/avif" srcSet={srcSet(a, 'avif')} sizes="96px" />
                        <img src={`${BASE}${a.files.webp[0].file}`} srcSet={srcSet(a, 'webp')} sizes="96px" width={a.width} height={a.height} alt="" loading="lazy" />
                      </picture>
                    )}
                  </span>
                  <span className="t-mono h1-state__label">{s.short}</span>
                  <span className="sr-only">{s.label}</span>
                </button>
              );
            })}
          </div>
        </section>
      </main>
      <NextSectionStub />
      <ReviewBar current="H1" />
    </div>
  );
}

createRoot(document.getElementById('root')!).render(<HeroInstrument />);
