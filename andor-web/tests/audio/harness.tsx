// Test-only page: two AudioComparison players fed with generated sine tones (NOT site content).
import React from 'react';
import { createRoot } from 'react-dom/client';
import '../../shared/tokens/base.css';
import { AudioComparison } from '../../components/audio/AudioComparison';
import type { AudioDemo } from '../../shared/types/content';

const tone = (f: number) => [{ src: `./fixtures/tone-${f}.webm`, type: 'audio/webm; codecs=opus' }];
const demo = (id: string): AudioDemo => ({
  id, title: `Fixture ${id}`, sourceInstrument: 'Test tones', description: '', approved: false,
  variants: [{ id: 'original', label: 'Original', sources: tone(220) }, { id: 'osp', label: 'OSP', sources: tone(330) }, { id: 'reimagined', label: 'Reimagined', sources: tone(440) }],
});
createRoot(document.getElementById('root')!).render(
  <div style={{ padding: 40, display: 'grid', gap: 40, maxWidth: 520 }}>
    <div data-player="a"><AudioComparison demo={demo('a')} /></div>
    <div data-player="b"><AudioComparison demo={demo('b')} /></div>
    <div data-player="broken"><AudioComparison demo={{ ...demo('broken'), variants: demo('x').variants.map((v) => ({ ...v, sources: [{ src: './fixtures/missing.webm', type: 'audio/webm; codecs=opus' }] })) }} /></div>
  </div>,
);
