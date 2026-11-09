import React, { useLayoutEffect, useRef, useState } from 'react';
import { createRoot } from 'react-dom/client';
import '../shared/tokens/base.css';
import './h2.css';
import { SiteHeader } from '../components/interactive/SiteHeader';
import { ProductMedia, getAsset } from '../components/media/ProductMedia';
import { AudioComparison } from '../components/audio/AudioComparison';
import { heroDemo } from '../shared/content/demos';
import { imageReady, prefersReducedMotion, useEntrance } from '../shared/utilities/motion';
import { ReviewBar } from './ReviewBar';
import { NextSectionStub } from './Stub';

// The waveform display's rectangle inside the capture (1x editor coordinates, see assets.json).
const EDITOR = { w: 1448, h: 1086 };
const DISPLAY = { x: 50, y: 189, w: 1349, h: 321 };

/**
 * H2 — The Sound. The page opens on the sound itself: the recording's waveform, alone in the dark.
 * Then the frame pulls back (a matched cut on the same genuine capture) and the instrument is
 * around it. Time-based, once; no scroll-jacking. Reduced motion shows the settled state.
 */
function HeroSound() {
  const stage = useRef<HTMLDivElement>(null);
  const [zoom, setZoom] = useState(2.4);
  const [shift, setShift] = useState({ x: 0, y: 0 });
  const [scope, entered] = useEntrance<HTMLDivElement>(imageReady('.h2-object img'));
  const [phase, setPhase] = useState<'sound' | 'instrument'>(prefersReducedMotion() ? 'instrument' : 'sound');

  // Start scale: the display fills the stage width with a little bleed.
  useLayoutEffect(() => {
    const fit = () => {
      const el = stage.current;
      const img = el?.querySelector<HTMLElement>('.h2-object');
      if (!el || !img) return;
      // Fill the frame's height with the display: the waveform alone, held close, in the dark.
      const box = el.querySelector<HTMLElement>('.h2-zoom')!;
      const displayH = img.offsetHeight * (DISPLAY.h / EDITOR.h);
      setZoom(Math.max(1.4, (box.offsetHeight * 1.14) / displayH));
      // ... and centre it in the frame (the origin is the display's centre, so only translate).
      const cx = img.offsetLeft + img.offsetWidth * ((DISPLAY.x + DISPLAY.w / 2) / EDITOR.w);
      const cy = img.offsetTop + img.offsetHeight * ((DISPLAY.y + DISPLAY.h / 2) / EDITOR.h);
      setShift({ x: box.clientWidth / 2 - cx, y: box.clientHeight / 2 - cy });
    };
    fit();
    window.addEventListener('resize', fit);
    return () => window.removeEventListener('resize', fit);
  }, []);

  React.useEffect(() => {
    if (!entered || phase === 'instrument') return;
    const t = setTimeout(() => setPhase('instrument'), 1500);
    return () => clearTimeout(t);
  }, [entered, phase]);

  const origin = `${((DISPLAY.x + DISPLAY.w / 2) / EDITOR.w) * 100}% ${((DISPLAY.y + DISPLAY.h / 2) / EDITOR.h) * 100}%`;
  const hero = getAsset('osp-hero-1l')!;

  return (
    <div ref={scope} className={`h2 theme-dark is-${phase} ${entered ? 'is-in' : ''}`}>
      <SiteHeader revealClass="reveal" />
      <main id="top">
        <section className="h2-hero container" aria-labelledby="h2-title">
          <div className="h2-copy">
            <p className="t-mono h2-eyebrow reveal" style={{ ['--i' as any]: 1 }}>ANDOR OSP — One Shot Performer</p>
            <h1 id="h2-title" className="t-hero h2-title">
              <span className="line-mask" style={{ ['--i' as any]: 2 }}><span>A single sound.</span></span>
              <span className="line-mask" style={{ ['--i' as any]: 3 }}><span className="t-serif h2-italic">Somewhere entirely new.</span></span>
            </h1>
            <p className="t-lede h2-lede reveal" style={{ ['--i' as any]: 5 }}>Turn the smallest musical moment into an instrument that feels alive.</p>
            <div className="h2-ctas reveal" style={{ ['--i' as any]: 6 }}>
              <a className="btn-pair" href="#explore">
                <span className="pill">Explore OSP</span>
                <span className="orb" aria-hidden="true"><svg viewBox="0 0 18 18"><path d="M5 13L13 5M6.5 5H13v6.5" fill="none" stroke="currentColor" strokeWidth="1.6" /></svg></span>
              </a>
              <a className="h2-hear" href="#listen">Hear it</a>
            </div>
          </div>
          <div ref={stage} className="h2-stage" style={{ ['--zoom' as any]: zoom, ['--origin' as any]: origin, ['--dx' as any]: `${shift.x}px`, ['--dy' as any]: `${shift.y}px` }}>
            <div className="h2-zoom">
              <ProductMedia asset="osp-hero-1l" variant="capture" className="h2-object" priority
                sizes="(max-width: 1023px) 260vw, 150vw" />
            </div>
            <p className="t-mono h2-caption" aria-hidden={phase !== 'instrument'}>One layer · Reimagined 24 % · Cmaj7 held</p>
          </div>
          <div id="listen" className="h2-listen">
            <AudioComparison demo={heroDemo} theme="dark" />
          </div>
        </section>
      </main>
      <NextSectionStub dark />
      <ReviewBar current="H2" />
      <span hidden>{hero.id}</span>
    </div>
  );
}

createRoot(document.getElementById('root')!).render(<HeroSound />);
