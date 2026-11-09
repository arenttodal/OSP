import React from 'react';
import { createRoot } from 'react-dom/client';
import '../shared/tokens/base.css';
import './gallery/gallery.css';
import './gallery/panel-header.css';
import './g3.css';
import { SiteHeader } from '../components/interactive/SiteHeader';
import { AudioComparison } from '../components/audio/AudioComparison';
import { heroDemo } from '../shared/content/demos';
import { imageReady, useEntrance } from '../shared/utilities/motion';
import { ExploreButton } from './gallery/ArrowPair';
import { AnnotatedObject } from './gallery/Annotated';
import { NOTES } from './gallery/content';
import { ReviewBar } from './ReviewBar';
import { NextSectionStub } from './Stub';

// Real numbers from the current build (docs/marketing/features-and-benefits.md).
const FACTS = [
  ['1', 'recording to begin'],
  ['3', 'layers, played as one'],
  ['5', 'REIMAGINED modes'],
  ['6', 'macros, each with its own page'],
  ['4', 'SPACE engines'],
  ['16', 'modulation routes'],
];

/**
 * Gallery C — Context (chosen direction). Edition B's header, the title without a subheading,
 * the three-layer instrument with notes pinned to real controls, and a strip of facts beneath.
 */
function GalleryContext() {
  const [scope, entered] = useEntrance<HTMLDivElement>(imageReady('.g-object img'));
  return (
    <div ref={scope} className={`g g3 ${entered ? 'is-in' : ''}`}>
      <SiteHeader revealClass="reveal" wordmark="brand" className="panel-header" />
      <main id="top">
        <section className="g-hero g3-hero" aria-labelledby="g3-title">
          <div className="container g3-head">
            <h1 id="g3-title" className="g-title g3-title">
              <span className="line-mask" style={{ ['--i' as any]: 1 }}><span>A single sound.</span></span>
              <span className="line-mask" style={{ ['--i' as any]: 2 }}><span className="quiet">Somewhere entirely new.</span></span>
            </h1>
            <div className="g3-intro reveal" style={{ ['--i' as any]: 4 }}>
              <ExploreButton /><a className="g-hear" href="#listen">Hear it</a>
            </div>
          </div>
          <div className="container g3-room reveal" style={{ ['--i' as any]: 6 }}>
            <AnnotatedObject asset="osp-hero-3l" notes={NOTES} sizes="(max-width: 1000px) 94vw, 50vw" />
          </div>
          <div className="container">
            <div className="g3-strip">
              <dl className="g3-facts">
                {FACTS.map(([n, label]) => (
                  <div key={label} className="g3-fact"><dt>{n}</dt><dd>{label}</dd></div>
                ))}
              </dl>
              <div id="listen" className="g3-listen"><AudioComparison demo={heroDemo} /></div>
            </div>
          </div>
        </section>
      </main>
      <NextSectionStub />
      <ReviewBar current="C" />
    </div>
  );
}

createRoot(document.getElementById('root')!).render(<GalleryContext />);
