import React, { useCallback, useState } from 'react';
import { createRoot } from 'react-dom/client';
import '../shared/tokens/base.css';
import './gallery/gallery.css';
import './gallery/panel-header.css';
import './g3.css';
import './g4.css';
import { SiteHeader } from '../components/interactive/SiteHeader';
import { FeatureModal } from '../components/interactive/FeatureModal';
import { AudioComparison } from '../components/audio/AudioComparison';
import { heroDemo } from '../shared/content/demos';
import { imageReady, useEntrance } from '../shared/utilities/motion';
import { ExploreButton } from './gallery/ArrowPair';
import { AnnotatedObject } from './gallery/Annotated';
import { DEEP_DIVES, DEEP_NOTES } from './gallery/content';
import { ReviewBar } from './ReviewBar';
import { NextSectionStub } from './Stub';

/**
 * Gallery C — Deep dive. The same hero, but each numbered part opens a rounded modal with the
 * real controls behind it: LIFE's page, the five REIMAGINED modes, SYNTH and IMPRINT, SPACE and the
 * arpeggiator, all genuine captures.
 */
function GalleryDeepDive() {
  const [scope, entered] = useEntrance<HTMLDivElement>(imageReady('.g-object img'));
  const [open, setOpen] = useState<string | null>(null);
  const close = useCallback(() => setOpen(null), []);
  return (
    <div ref={scope} className={`g g3 g4 ${entered ? 'is-in' : ''}`}>
      <SiteHeader revealClass="reveal" wordmark="brand" className="panel-header" />
      <main id="top">
        <section className="g-hero g3-hero" aria-labelledby="g4-title">
          <div className="container g3-head">
            <h1 id="g4-title" className="g-title g3-title">
              <span className="line-mask" style={{ ['--i' as any]: 1 }}><span>A single sound.</span></span>
              <span className="line-mask" style={{ ['--i' as any]: 2 }}><span className="quiet">Somewhere entirely new.</span></span>
            </h1>
            <div className="g3-intro reveal" style={{ ['--i' as any]: 4 }}>
              <ExploreButton /><a className="g-hear" href="#listen">Hear it</a>
            </div>
          </div>
          <div className="container g3-room reveal" style={{ ['--i' as any]: 6 }}>
            <AnnotatedObject asset="osp-hero-3l" notes={DEEP_NOTES} sizes="(max-width: 1000px) 94vw, 50vw" onOpen={(id) => setOpen(id)} />
          </div>
          <div className="container g4-foot">
            <p className="t-mono g4-hint">Select a number to look inside the instrument</p>
            <div id="listen" className="g4-listen"><AudioComparison demo={heroDemo} /></div>
          </div>
        </section>
      </main>
      <FeatureModal items={DEEP_DIVES} openId={open} onClose={close} onNavigate={setOpen} />
      <NextSectionStub />
      <ReviewBar current="D" />
    </div>
  );
}

createRoot(document.getElementById('root')!).render(<GalleryDeepDive />);
