import React from 'react';
import { createRoot } from 'react-dom/client';
import '../shared/tokens/base.css';
import './gallery/gallery.css';
import './g3.css';
import { SiteHeader } from '../components/interactive/SiteHeader';
import { AudioComparison } from '../components/audio/AudioComparison';
import { heroDemo } from '../shared/content/demos';
import { imageReady, useEntrance } from '../shared/utilities/motion';
import { ExploreButton } from './gallery/ArrowPair';
import { AnnotatedObject, Annotation } from './gallery/Annotated';
import { ReviewBar } from './ReviewBar';
import { NextSectionStub } from './Stub';

// Points are real positions of these controls in the capture (1x editor coordinates).
const NOTES: Annotation[] = [
  { n: 1, x: 330, y: 300, side: 'left', title: 'Your recording', text: 'Drop in one sound. Its root note is found for you, and it plays right away.' },
  { n: 2, x: 128, y: 815, side: 'left', title: 'LIFE', text: 'Every note is performed a little differently, the way a player would.' },
  { n: 3, x: 1330, y: 205, side: 'right', title: 'SYNTH + IMPRINT', text: 'An analog oscillator, coloured by the body of your recording.' },
  { n: 4, x: 1100, y: 578, side: 'right', title: 'REIMAGINED', text: 'One knob from faithful to far away, in five different modes.' },
  { n: 5, x: 1150, y: 962, side: 'right', title: 'The keyboard', text: 'Play it in chords. Hold a note longer than the recording lasts.' },
];

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
 * Gallery C — Context. The museum object with its labels: notes pinned to real parts of the
 * interface and a strip of facts beneath, so the opening explains what OSP is as well as how it looks.
 */
function GalleryContext() {
  const [scope, entered] = useEntrance<HTMLDivElement>(imageReady('.g-object img'));
  return (
    <div ref={scope} className={`g g3 ${entered ? 'is-in' : ''}`}>
      <SiteHeader revealClass="reveal" />
      <main id="top">
        <section className="g-hero g3-hero" aria-labelledby="g3-title">
          <div className="container g3-head">
            <h1 id="g3-title" className="g-title g3-title">
              <span className="line-mask" style={{ ['--i' as any]: 1 }}><span>A single sound.</span></span>
              <span className="line-mask" style={{ ['--i' as any]: 2 }}><span className="quiet">Somewhere entirely new.</span></span>
            </h1>
            <div className="g3-intro reveal" style={{ ['--i' as any]: 4 }}>
              <p className="g3-lede">Turn the smallest musical moment into an instrument that feels alive.</p>
              <div className="g3-ctas"><ExploreButton /><a className="g-hear" href="#listen">Hear it</a></div>
            </div>
          </div>
          <div className="container g3-room reveal" style={{ ['--i' as any]: 6 }}>
            <AnnotatedObject asset="osp-hero-1l" notes={NOTES} sizes="(max-width: 1000px) 94vw, 50vw" />
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
