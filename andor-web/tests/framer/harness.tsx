// Renders every Framer code component with realistic props, as a Framer page would.
// Audio uses the generated test tones (tests/audio/fixtures), never site content.
import React, { useState } from 'react';
import { createRoot } from 'react-dom/client';
import AudioComparison from '../../framer/AudioComparison.tsx';
import SoundDeck from '../../framer/SoundDeck.tsx';
import ViewSwitcher from '../../framer/ViewSwitcher.tsx';
import DeepDive from '../../framer/DeepDive.tsx';
import AnnotatedInstrument from '../../framer/AnnotatedInstrument.tsx';
import FAQ from '../../framer/FAQ.tsx';

const tone = (f: number) => `/tests/audio/fixtures/tone-${f}.webm`;
const img = (id: string, w: number, alt: string) => ({ src: `/assets/optimized/${id}-${w}.webp`, srcSet: `/assets/optimized/${id}-${w}.webp ${w}w`, alt });
const modes = ['kaleidoscope', 'tape-frame', 'toybox', 'mosaic', 'mirage'];

function Page() {
  const [overlay, setOverlay] = useState<string | null>(null);
  (window as any).__overlay = overlay;
  return (
    <div style={{ background: '#F7F6F2', padding: 40, display: 'grid', gap: 64, maxWidth: 1200, margin: '0 auto' }}>
      <div data-c="ac-live" style={{ maxWidth: 420 }}>
        <AudioComparison title="Fixture" source="Test tones" file1={tone(220)} file2={tone(330)} file3={tone(440)} />
      </div>
      <div data-c="ac-pending" style={{ maxWidth: 420 }}><AudioComparison /></div>
      <div data-c="deck" style={{ background: '#222924', padding: '40px 0' }}>
        <SoundDeck start={1} items={[
          { name: 'Cello', line: 'One long, low tone.' },
          { name: 'Fixture', line: 'Test tones.', file1: tone(220), file2: tone(330), file3: tone(440) },
          { name: 'Voice', line: 'A sung vowel.' },
        ]} />
      </div>
      <div data-c="reim">
        <ViewSwitcher label="REIMAGINED mode" initial={3} pending="Sound example in preparation"
          intro={<h2 style={{ margin: 0, fontSize: 48 }}>Five instruments</h2>}
          items={modes.map((m) => ({ label: m.replace('-', ' '), hint: `Hint for ${m}`, image: img(`dd-reimagined-${m}`, 600, `REIMAGINED ${m}`), text: `Text for ${m}.` }))} />
      </div>
      <div data-c="synth" style={{ background: '#222924', padding: 32 }}>
        <ViewSwitcher label="SYNTH state" theme="dark" selector="pills" plate={false} textPlacement="selector" initial={2}
          items={['sample', 'analog', 'imprint', 'motion'].map((s) => ({ label: s, image: img(`syn-card-${s}`, 1080, `SYNTH ${s}`), text: `Text for ${s}.` }))} />
      </div>
      <div data-c="layers">
        <ViewSwitcher label="Layers" layout="stacked" selector="pills" plate={false} initial={2}
          items={['1l', '2l', '3l'].map((s, i) => ({ label: `${i + 1}`, image: img(`osp-hero-${s}`, 1080, `${i + 1} layers`) }))} />
      </div>
      <div data-c="hero" style={{ maxWidth: 660 }}>
        <AnnotatedInstrument image={img('osp-hero-3l', 1080, 'ANDOR OSP with three layers')} delay={0}
          onMarker2={() => setOverlay('life')} onMarker4={() => setOverlay('reimagined')} />
      </div>
      {overlay && (
        <div data-c="overlay" style={{ position: 'fixed', inset: 0, background: 'rgba(34,41,36,0.38)', display: 'grid', placeItems: 'center', zIndex: 10 }}>
          <div style={{ width: 'min(1040px, calc(100vw - 48px))' }}>
            <DeepDive startAt={overlay} onClose={() => setOverlay(null)} items={[
              { id: 'life', n: 2, kicker: 'The performance engine', title: 'LIFE', body: 'Body for LIFE.', facts: 'Natural · Loose · Fray', media: 'popover', v1Label: 'LIFE', v1Image: img('dd-life', 600, 'LIFE popover') },
              { id: 'reimagined', n: 4, kicker: 'Five instruments', title: 'REIMAGINED', body: 'Body for REIMAGINED.', media: 'popover',
                ...Object.fromEntries(modes.flatMap((m, i) => [[`v${i + 1}Label`, m], [`v${i + 1}Image`, img(`dd-reimagined-${m}`, 600, m)], [`v${i + 1}Text`, `${m} text`]])) },
            ]} />
          </div>
        </div>
      )}
      <div data-c="faq"><FAQ items={[{ q: 'Does it work with my DAW?', a: 'Answer one.' }, { q: 'Does it need the internet?', a: 'No.' }]} /></div>
    </div>
  );
}
createRoot(document.getElementById('root')!).render(<Page />);
