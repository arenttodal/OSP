import React from 'react';
import { createRoot } from 'react-dom/client';
import '../shared/tokens/base.css';
import './board.css';
import { Wordmark } from '../components/interactive/SiteHeader';

// Screenshots are real renders of each concept (tests/visual/shoot.mjs), copied into prototype/board/.
const shots = import.meta.glob('./board/*.webp', { eager: true, query: '?url', import: 'default' }) as Record<string, string>;
const shot = (name: string) => shots[`./board/${name}.webp`];

const PAGE = { id: 'osp', href: 'osp.html', name: 'ANDOR OSP · the landing page', tone: 'Hero E approved · every section', line: 'Hero E, then a statement pause, First Note (capture, play, transform), Sounds, REIMAGINED, SYNTH + IMPRINT, layers and granular, the instrument details, deeper features, technical details, waitlist, questions and footer. All product imagery is genuine.' };

const ROUND3 = [
  { id: 'g5', href: 'g5.html', name: 'E · Open bubbles', tone: 'Approved at Gate H', line: 'The deep dives are already open on the page: soft floating bubbles beside the instrument, each holding the real popover or panel (the drop area, LIFE, SYNTH + IMPRINT, REIMAGINED, SPACE, the arpeggiator). Click a bubble or a marker for the full modal. A swipeable row on phones.' },
  { id: 'g3', href: 'g3.html', name: 'C · Notes', tone: 'Chosen direction · B header · three layers', line: 'Gallery C with edition B’s header and no subheading. The real three-layer instrument, notes pinned to its controls and a strip of real numbers beneath.' },
  { id: 'g4', href: 'g4.html', name: 'D · Deep dive', tone: 'Chosen direction · rounded modals', line: 'Each numbered part opens a rounded modal with the real controls behind it: LIFE’s page, the five REIMAGINED modes, SYNTH and IMPRINT, SPACE and the arpeggiator. A bottom sheet on phones.' },
];

const EDITIONS = [
  { id: 'A', href: 'g1.html', name: 'Gallery A · Quiet', tone: 'Inter Display · centred · placards', line: 'The museum composition, now all sans with a two-tone title. Both placards carry content: what OSP is on the left, the object label and listening on the right.' },
  { id: 'B', href: 'g2.html', name: 'Gallery B · Panel', tone: 'Geist · framed panel · pill navigation', line: 'Closest to your references. A softly lit panel with pill navigation, a large centred title, a mono statement lower left and the actions lower right.' },
  { id: 'C', href: 'g3.html', name: 'Gallery C · Context', tone: 'Inter Tight · annotated object · facts', line: 'The object with its labels. Notes pinned to real controls (the recording, LIFE, SYNTH + IMPRINT, REIMAGINED, the keyboard) and a strip of real numbers beneath. Numbered markers and a legend on phones.' },
];

const CONCEPTS = [
  { id: 'H1', rec: true, href: 'h1.html', name: 'The Instrument', line: 'The object is the hero. A studio render of the real interface stands beside two-tone type, with its genuine states (one layer, three layers, IMPRINT) as a contact sheet.', tone: 'Light · asymmetric · tactile depth' },
  { id: 'H2', href: 'h2.html', name: 'The Sound', line: 'The page opens on the recording itself: the waveform alone in the dark (first image). The frame then pulls back on the same genuine capture, and the instrument is around it (second image).', tone: 'Dark · cinematic · one matched cut' },
  { id: 'H3', href: 'h3.html', name: 'The Gallery', line: 'Museum restraint. One object centred in its own light, a serif title, two placards, and three real details hung below like prints.', tone: 'Quiet · symmetrical · editorial serif' },
];

function Board() {
  return (
    <div className="board">
      <header className="container board-head">
        <Wordmark />
        <p className="t-mono">Gate H · hero directions · concept</p>
      </header>
      <main className="container">
        <h1 className="t-section board-title">ANDOR OSP, <span className="quiet">the whole page.</span></h1>
        <p className="board-rec"><span className="t-mono">Gate H passed · hero E</span> The full landing page is built around the approved hero. The hero rounds are kept below for reference.</p>
        <p className="t-lede board-lede">Every product image is a genuine capture of the current build (c49d77f), or a studio render with that capture as its face. Round-2 editions use a straight capture; H1 uses the render.</p>
        <div className="board-grid">
          <article className="board-card">
            <a href={PAGE.href} className="board-shots" aria-label={`Open ${PAGE.name}`}>
              <img className="board-desk" src={shot('osp-desktop')} width={1200} height={750} alt="" loading="lazy" />
              <img className="board-mob" src={shot('osp-mobile')} width={390} height={844} alt="" loading="lazy" />
            </a>
            <div className="board-meta">
              <p className="t-mono">{PAGE.tone}</p>
              <h2 className="t-feature">{PAGE.name}</h2>
              <p>{PAGE.line}</p>
              <a className="btn btn-primary btn-small" href={PAGE.href}>Open the landing page</a>
            </div>
          </article>
        </div>
        <div className="board-grid">
          <article className="board-card">
            <a href="installation.html" className="board-shots" aria-label="Open the support and legal pages">
              <img className="board-desk" src={shot('pages-desktop')} width={1200} height={750} alt="" loading="lazy" />
              <img className="board-mob" src={shot('pages-mobile')} width={390} height={844} alt="" loading="lazy" />
            </a>
            <div className="board-meta">
              <p className="t-mono">Drafts · highlighted items need the owner</p>
              <h2 className="t-feature">Support and legal pages</h2>
              <p>Installation, Documentation and Contact; Privacy, Terms and Licence (with the third-party notices). One template, the landing page’s header and footer. The legal pages are drafts for legal review.</p>
              <a className="btn btn-secondary btn-small" href="installation.html">Open Support</a> <a className="btn btn-secondary btn-small" href="privacy.html">Open Legal</a>
            </div>
          </article>
        </div>
        <h2 className="t-feature board-round">Round 3 · Gallery C, three versions</h2>
        <div className="board-grid">
          {ROUND3.map((c) => (
            <article key={c.id} className="board-card">
              <a href={c.href} className="board-shots" aria-label={`Open ${c.name}`}>
                <img className="board-desk" src={shot(`${c.id}-desktop`)} width={1200} height={750} alt="" loading="lazy" />
                <img className="board-mob" src={shot(`${c.id}-mobile`)} width={390} height={844} alt="" loading="lazy" />
              </a>
              <div className="board-meta">
                <p className="t-mono">{c.tone}</p>
                <h2 className="t-feature">{c.name}</h2>
                <p>{c.line}</p>
                <a className="btn btn-secondary btn-small" href={c.href}>Open {c.name.split(' · ')[0]}</a>
              </div>
            </article>
          ))}
          {shot('g4-modal') && (
            <article className="board-card">
              <a href="g4.html" className="board-shots" aria-label="Open the deep-dive version">
                <img className="board-desk" src={shot('g4-modal')} width={1200} height={750} alt="" loading="lazy" />
                {shot('g4-modal-mobile') && <img className="board-mob" src={shot('g4-modal-mobile')} width={390} height={844} alt="" loading="lazy" />}
              </a>
              <div className="board-meta">
                <p className="t-mono">D and E · a modal open</p>
                <h2 className="t-feature">REIMAGINED, up close</h2>
                <p>The real MOSAIC popover, with tabs for all five modes. On phones the same content arrives as a bottom sheet.</p>
              </div>
            </article>
          )}
        </div>
        <h2 className="t-feature board-round">Round 2 · three editions</h2>
        <div className="board-grid">
          {EDITIONS.map((c) => (
            <article key={c.id} className="board-card">
              <a href={c.href} className="board-shots" aria-label={`Open ${c.name}`}>
                <img className="board-desk" src={shot(`g${'ABC'.indexOf(c.id) + 1}-desktop${c.id === 'C' ? '-r2' : ''}`)} width={1200} height={750} alt="" loading="lazy" />
                <img className="board-mob" src={shot(`g${'ABC'.indexOf(c.id) + 1}-mobile${c.id === 'C' ? '-r2' : ''}`)} width={390} height={844} alt="" loading="lazy" />
              </a>
              <div className="board-meta">
                <p className="t-mono">{c.tone}</p>
                <h2 className="t-feature">{c.name}</h2>
                <p>{c.line}</p>
                <a className="btn btn-secondary btn-small" href={c.href}>Open edition {c.id}</a>
              </div>
            </article>
          ))}
        </div>
        <h2 className="t-feature board-round">Round 1 · three directions</h2>
        <div className="board-grid">
          {CONCEPTS.map((c) => (
            <article key={c.id} className="board-card">
              <a href={c.href} className="board-shots" aria-label={`Open ${c.id}, ${c.name}`}>
                {c.id === 'H2' && <img className="board-desk board-desk--frame" src={shot('h2-desktop-sound')} width={1200} height={750} alt="" loading="lazy" />}
                {shot(`${c.id.toLowerCase()}-desktop`) && <img className="board-desk" src={shot(`${c.id.toLowerCase()}-desktop`)} width={1200} height={750} alt="" loading="lazy" />}
                {shot(`${c.id.toLowerCase()}-mobile`) && <img className="board-mob" src={shot(`${c.id.toLowerCase()}-mobile`)} width={390} height={844} alt="" loading="lazy" />}
              </a>
              <div className="board-meta">
                <p className="t-mono">{c.id} · {c.tone}{c.id === 'H3' ? ' · chosen direction' : ''}</p>
                <h2 className="t-feature">{c.name}</h2>
                <p>{c.line}</p>
                <a className="btn btn-secondary btn-small" href={c.href}>Open {c.id}</a>
              </div>
            </article>
          ))}
        </div>
      </main>
    </div>
  );
}
createRoot(document.getElementById('root')!).render(<Board />);
