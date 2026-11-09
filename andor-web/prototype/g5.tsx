import React, { useCallback, useState } from 'react';
import { createRoot } from 'react-dom/client';
import '../shared/tokens/base.css';
import './gallery/gallery.css';
import './gallery/panel-header.css';
import './g3.css';
import './g4.css';
import './g5.css';
import { SiteHeader } from '../components/interactive/SiteHeader';
import { FeatureModal } from '../components/interactive/FeatureModal';
import { imageReady, useEntrance } from '../shared/utilities/motion';
import { DEEP_DIVES } from './gallery/content';
import { HeroE } from './osp/Hero';
import { ReviewBar } from './ReviewBar';
import { NextSectionStub } from './Stub';

/**
 * Gallery C — E, open bubbles (approved at Gate H). The hero itself lives in osp/Hero.tsx so the
 * concept page and the landing page share one source.
 */
function GalleryBubbles() {
  const [scope, entered] = useEntrance<HTMLDivElement>(imageReady('.g-object img'));
  const [open, setOpen] = useState<string | null>(null);
  const close = useCallback(() => setOpen(null), []);
  return (
    <div ref={scope} className={`g g3 g4 g5 ${entered ? 'is-in' : ''}`}>
      <SiteHeader revealClass="reveal" wordmark="brand" className="panel-header" />
      <main id="top">
        <HeroE onOpen={setOpen} titleId="g5-title" />
      </main>
      <FeatureModal items={DEEP_DIVES} openId={open} onClose={close} onNavigate={setOpen} />
      <NextSectionStub />
      <ReviewBar current="E" />
    </div>
  );
}

createRoot(document.getElementById('root')!).render(<GalleryBubbles />);
