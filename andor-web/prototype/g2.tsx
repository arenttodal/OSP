import React from 'react';
import { createRoot } from 'react-dom/client';
import '../shared/tokens/base.css';
import '@fontsource-variable/geist';
import './gallery/gallery.css';
import './gallery/panel-header.css';
import './g2.css';
import { SiteHeader } from '../components/interactive/SiteHeader';
import { ProductMedia } from '../components/media/ProductMedia';
import { AudioComparison } from '../components/audio/AudioComparison';
import { heroDemo } from '../shared/content/demos';
import { imageReady, useEntrance } from '../shared/utilities/motion';
import { ExploreButton } from './gallery/ArrowPair';
import { ReviewBar } from './ReviewBar';
import { NextSectionStub } from './Stub';

/**
 * Gallery B — Panel. Geist (the closest free face to the reference's neo-grotesk). The hero is a
 * framed panel: pill navigation, a large centred two-tone title, the object centred in its light,
 * a mono statement to the lower left and the actions to the lower right.
 */
function GalleryPanel() {
  const [scope, entered] = useEntrance<HTMLDivElement>(imageReady('.g-object img'));
  return (
    <div ref={scope} className={`g g2 ${entered ? 'is-in' : ''}`}>
      <div className="g2-panel">
        <SiteHeader revealClass="reveal" wordmark="brand" className="panel-header g2-header" />
        <main id="top">
          <section className="g-hero g2-hero" aria-labelledby="g2-title">
            <h1 id="g2-title" className="g-title g2-title">
              <span className="line-mask" style={{ ['--i' as any]: 1 }}><span>A single sound.</span></span>
              <span className="line-mask" style={{ ['--i' as any]: 2 }}><span><span className="quiet">Somewhere</span> entirely new.</span></span>
            </h1>
            <div className="g2-room">
              <div className="g2-statement reveal" style={{ ['--i' as any]: 6 }}>
                <p className="t-mono g2-mono">One recording.<br />A whole instrument.</p>
                <p className="g2-lede">Turn the smallest musical moment into an instrument that feels alive.</p>
              </div>
              <div className="g-wall g2-wall">
                <div className="g-spot" aria-hidden="true" />
                <ProductMedia asset="osp-hero-1l" variant="studio" className="g-object" priority sizes="(max-width: 900px) 94vw, 54vw" />
              </div>
              <div className="g2-actions reveal" style={{ ['--i' as any]: 7 }}>
                <ExploreButton />
                <div id="listen" className="g2-listen"><AudioComparison demo={heroDemo} /></div>
              </div>
            </div>
          </section>
        </main>
      </div>
      <NextSectionStub />
      <ReviewBar current="B" />
    </div>
  );
}

createRoot(document.getElementById('root')!).render(<GalleryPanel />);
