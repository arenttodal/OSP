import React from 'react';
import { createRoot } from 'react-dom/client';
import '../shared/tokens/base.css';
import './h3.css';
import { SiteHeader } from '../components/interactive/SiteHeader';
import { ProductMedia } from '../components/media/ProductMedia';
import { AudioComparison } from '../components/audio/AudioComparison';
import { heroDemo } from '../shared/content/demos';
import { imageReady, useEntrance } from '../shared/utilities/motion';
import { ReviewBar } from './ReviewBar';
import { NextSectionStub } from './Stub';

const DETAILS = [
  { asset: 'crop-knob-life', n: 'I', title: 'LIFE', note: 'Each note performed a little differently, the way a player would.' },
  { asset: 'crop-teak', n: 'II', title: 'Ceramic and teak', note: 'The interface, finished like an object you keep on the desk.' },
  { asset: 'crop-mosaic-popover', n: 'III', title: 'MOSAIC', note: 'One of five ways to reimagine the same recording.' },
];

/**
 * H3 — The Gallery. Museum restraint: one object, centred and lit, a serif title, two placards.
 * Below the fold, three genuine details hang like prints.
 */
function HeroGallery() {
  const [scope, entered] = useEntrance<HTMLDivElement>(imageReady('.h3-object img'));
  return (
    <div ref={scope} className={`h3 ${entered ? 'is-in' : ''}`}>
      <SiteHeader revealClass="reveal" wordmark="brand" />
      <main id="top">
        <section className="h3-hero" aria-labelledby="h3-title">
          <div className="container h3-head">
            <h1 id="h3-title" className="h3-title t-serif">
              <span className="line-mask" style={{ ['--i' as any]: 1 }}><span>A single sound.</span></span>
              <span className="line-mask" style={{ ['--i' as any]: 2 }}><span><em>Somewhere entirely new.</em></span></span>
            </h1>
          </div>
          <div className="container h3-room">
            <aside className="h3-placard h3-placard--left reveal" style={{ ['--i' as any]: 6 }}>
              <p className="h3-lede">Turn the smallest musical moment into an instrument that feels alive.</p>
              <a className="btn-pair" href="#explore">
                <span className="pill">Explore OSP</span>
                <span className="orb" aria-hidden="true"><svg viewBox="0 0 18 18"><path d="M5 13L13 5M6.5 5H13v6.5" fill="none" stroke="currentColor" strokeWidth="1.6" /></svg></span>
              </a>
            </aside>
            <div className="h3-wall">
              <div className="h3-spot" aria-hidden="true" />
              <ProductMedia asset="osp-hero-1l" variant="studio" className="h3-object" priority
                sizes="(max-width: 860px) 92vw, 52vw" />
            </div>
            <aside className="h3-placard h3-placard--right reveal" style={{ ['--i' as any]: 7 }} aria-label="About the object">
              <dl className="h3-label">
                <dt className="t-mono">ANDOR OSP</dt>
                <dd>One Shot Performer</dd>
                <dd className="h3-label__meta">Software instrument, in development, 2026</dd>
                <dd className="h3-label__meta">Material: one recording</dd>
              </dl>
              <div id="listen" className="h3-listen"><AudioComparison demo={heroDemo} layout="stack" /></div>
            </aside>
          </div>
        </section>
        <section className="container h3-prints" aria-label="Details">
          {DETAILS.map((d, i) => (
            <figure key={d.asset} className={`h3-print h3-print--${i + 1}`}>
              <ProductMedia asset={d.asset} variant="detail" sizes="(max-width: 860px) 92vw, 30vw" />
              <figcaption><span className="t-serif h3-num">{d.n}.</span> <strong>{d.title}</strong> — {d.note}</figcaption>
            </figure>
          ))}
        </section>
      </main>
      <NextSectionStub />
      <ReviewBar current="H3" />
    </div>
  );
}

createRoot(document.getElementById('root')!).render(<HeroGallery />);
