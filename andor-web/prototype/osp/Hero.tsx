import React from 'react';
import { ProductMedia } from '../../components/media/ProductMedia';
import { AudioComparison } from '../../components/audio/AudioComparison';
import { heroDemo } from '../../shared/content/demos';
import { ExploreButton } from '../gallery/ArrowPair';
import { AnnotatedObject } from '../gallery/Annotated';
import { DEEP_NOTES } from '../gallery/content';

interface Bubble { id: string; n: number; title: string; line: string; asset: string; shape: 'popover' | 'wide'; area: string }

// The approved hero (Gate H: E). Each bubble shows the genuine popover or panel, already open.
// Numbers match the markers on the instrument and the deep-dive modal.
const BUBBLES: Bubble[] = [
  { id: 'recording', n: 1, title: 'Your recording', line: 'Drop in one sound. It plays right away.', asset: 'bub-recording', shape: 'wide', area: 'b1' },
  { id: 'life', n: 2, title: 'LIFE', line: 'Every note performed a little differently.', asset: 'dd-life', shape: 'popover', area: 'b2' },
  { id: 'synth', n: 3, title: 'SYNTH + IMPRINT', line: 'An oscillator coloured by your recording.', asset: 'bub-synth', shape: 'wide', area: 'b3' },
  { id: 'reimagined', n: 4, title: 'REIMAGINED', line: 'Five modes, from faithful to far away.', asset: 'dd-reimagined-mosaic', shape: 'popover', area: 'b4' },
  { id: 'space', n: 5, title: 'SPACE', line: 'Four reverb engines.', asset: 'dd-space', shape: 'popover', area: 'b5' },
  { id: 'keyboard', n: 6, title: 'Keyboard + arpeggiator', line: 'Chords, held notes and an arpeggiator for each layer.', asset: 'dd-arp', shape: 'wide', area: 'b6' },
];

function FloatingBubble({ b, index, onOpen }: { b: Bubble; index: number; onOpen: (id: string) => void }) {
  return (
    <article className={`bub bub--${b.shape}`} style={{ gridArea: b.area, ['--k' as any]: index }}>
      <header className="bub__head">
        <span className="ga-num" aria-hidden="true">{b.n}</span>
        <h2 className="t-mono bub__title">
          {/* One button per bubble; its hit area covers the whole card (stretched). */}
          <button type="button" className="bub__btn" onClick={() => onOpen(b.id)} aria-haspopup="dialog">{b.title}<span className="sr-only">: look closer</span></button>
        </h2>
        <span className="ga-open" aria-hidden="true">+</span>
      </header>
      <p className="bub__line">{b.line}</p>
      <div className="bub__plate">
        <ProductMedia asset={b.asset} variant="detail" className="bub__media" sizes={b.shape === 'wide' && b.id === 'keyboard' ? '(max-width: 1100px) 90vw, 46vw' : '(max-width: 640px) 76vw, 300px'} />
      </div>
    </article>
  );
}

/** Hero E: the three-layer instrument with its features open around it as floating bubbles. */
export function HeroE({ onOpen, exploreHref = '#explore', hearHref = '#listen', titleId = 'hero-title' }: { onOpen: (id: string) => void; exploreHref?: string; hearHref?: string; titleId?: string }) {
  return (
    <section className="g-hero g3-hero" aria-labelledby={titleId}>
      <div className="container g3-head">
        <h1 id={titleId} className="g-title g3-title">
          <span className="line-mask" style={{ ['--i' as any]: 1 }}><span>A single sound.</span></span>
          <span className="line-mask" style={{ ['--i' as any]: 2 }}><span className="quiet">Somewhere entirely new.</span></span>
        </h1>
        <div className="g3-intro reveal" style={{ ['--i' as any]: 4 }}>
          <ExploreButton href={exploreHref} /><a className="g-hear" href={hearHref}>Hear it</a>
        </div>
      </div>
      <div className="container">
        <div className="e-stage">
          <div className="e-object reveal" style={{ ['--i' as any]: 6 }}>
            <AnnotatedObject asset="osp-hero-3l" notes={DEEP_NOTES} bare sizes="(max-width: 1100px) 94vw, 48vw" onOpen={(id) => onOpen(id)} />
          </div>
          <div className="e-bubbles" aria-label="Inside the instrument">
            {BUBBLES.map((b, i) => (
              <div key={b.id} className="e-cell" style={{ gridArea: b.area }}>
                <FloatingBubble b={b} index={i} onOpen={onOpen} />
              </div>
            ))}
          </div>
          <div id="listen" className="e-listen"><AudioComparison demo={heroDemo} /></div>
        </div>
      </div>
    </section>
  );
}
