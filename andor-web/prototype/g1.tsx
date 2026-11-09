import React from 'react';
import { createRoot } from 'react-dom/client';
import '../shared/tokens/base.css';
import '@fontsource-variable/inter/opsz.css';
import './gallery/gallery.css';
import './g1.css';
import { SiteHeader } from '../components/interactive/SiteHeader';
import { ProductMedia } from '../components/media/ProductMedia';
import { AudioComparison } from '../components/audio/AudioComparison';
import { heroDemo } from '../shared/content/demos';
import { imageReady, useEntrance } from '../shared/utilities/motion';
import { ExploreButton } from './gallery/ArrowPair';
import { ReviewBar } from './ReviewBar';
import { NextSectionStub } from './Stub';

/**
 * Gallery A — Quiet. The museum composition in Inter Display (optical sizing), two-tone title.
 * Both placards carry content: what it is (left), the object label and listening (right).
 */
function GalleryQuiet() {
  const [scope, entered] = useEntrance<HTMLDivElement>(imageReady('.g-object img'));
  return (
    <div ref={scope} className={`g g1 ${entered ? 'is-in' : ''}`}>
      <SiteHeader revealClass="reveal" wordmark="brand" />
      <main id="top">
        <section className="g-hero g1-hero" aria-labelledby="g1-title">
          <div className="container g1-head">
            <h1 id="g1-title" className="g-title g1-title">
              <span className="line-mask" style={{ ['--i' as any]: 1 }}><span>A single sound.</span></span>
              <span className="line-mask" style={{ ['--i' as any]: 2 }}><span className="quiet">Somewhere entirely new.</span></span>
            </h1>
          </div>
          <div className="container g1-room">
            <aside className="g1-placard g1-placard--left reveal" style={{ ['--i' as any]: 6 }}>
              <p className="t-mono g1-kicker">ANDOR OSP — One Shot Performer</p>
              <p className="g1-lede">Turn the smallest musical moment into an instrument that feels alive.</p>
              <ul className="g1-points">
                <li><strong>One recording</strong> is enough to begin.</li>
                <li><strong>Played</strong> across the keyboard, every note a little different.</li>
                <li><strong>Reimagined</strong>, layered and shaped into something new.</li>
              </ul>
              <div className="g1-ctas"><ExploreButton /></div>
            </aside>
            <div className="g-wall">
              <div className="g-spot" aria-hidden="true" />
              <ProductMedia asset="osp-hero-1l" variant="studio" className="g-object" priority sizes="(max-width: 900px) 92vw, 50vw" />
            </div>
            <aside className="g1-placard g1-placard--right reveal" style={{ ['--i' as any]: 7 }} aria-label="About the object">
              <dl className="g-label">
                <dt className="t-mono">On display</dt>
                <dd>ANDOR OSP</dd>
                <dd className="g-meta">One recording loaded, a chord held. REIMAGINED at 24 %.</dd>
                <dd className="g-meta">Software instrument, in development, 2026</dd>
              </dl>
              <div id="listen" className="g1-listen"><AudioComparison demo={heroDemo} layout="stack" /></div>
            </aside>
          </div>
        </section>
      </main>
      <NextSectionStub />
      <ReviewBar current="A" />
    </div>
  );
}

createRoot(document.getElementById('root')!).render(<GalleryQuiet />);
