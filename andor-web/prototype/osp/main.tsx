import React, { useCallback, useState } from 'react';
import { createRoot } from 'react-dom/client';
import '../../shared/tokens/base.css';
import '../gallery/gallery.css';
import '../gallery/panel-header.css';
import '../g3.css';
import '../g4.css';
import '../g5.css';
import './osp.css';
import { SiteHeader } from '../../components/interactive/SiteHeader';
import { FeatureModal } from '../../components/interactive/FeatureModal';
import { imageReady, useEntrance } from '../../shared/utilities/motion';
import { DEEP_DIVES } from '../gallery/content';
import { HeroE } from './Hero';
import { Details, Faq, Features, FirstNote, Footer, Layers, Reimagined, Sounds, Statement, Synth, Tech, Waitlist } from './sections';
import { ReviewBar } from '../ReviewBar';

const NAV = [
  { label: 'Instrument', href: '#first-note', current: true },
  { label: 'Sounds', href: '#sounds' },
  { label: 'Questions', href: '#faq' },
];

/** The ANDOR OSP landing page: hero E (approved at Gate H) and the sections of the site map. */
function OspPage() {
  const [scope, entered] = useEntrance<HTMLDivElement>(imageReady('.g-object img'));
  const [open, setOpen] = useState<string | null>(null);
  const close = useCallback(() => setOpen(null), []);
  return (
    <div ref={scope} className={`g g3 g4 g5 osp ${entered ? 'is-in' : ''}`}>
      <a className="skip" href="#first-note">Skip to content</a>
      <SiteHeader revealClass="reveal" wordmark="brand" className="panel-header" nav={NAV} cta={{ label: 'Join the waitlist', href: '#waitlist' }} />
      <main id="top">
        <HeroE onOpen={setOpen} exploreHref="#first-note" hearHref="#sounds" />
        <Statement />
        <FirstNote />
        <Sounds />
        <Reimagined />
        <Synth />
        <Layers />
        <Details />
        <Features />
        <Tech />
        <Waitlist />
        <Faq />
      </main>
      <Footer />
      <FeatureModal items={DEEP_DIVES} openId={open} onClose={close} onNavigate={setOpen} />
      <ReviewBar current="OSP" />
    </div>
  );
}

createRoot(document.getElementById('root')!).render(<OspPage />);
