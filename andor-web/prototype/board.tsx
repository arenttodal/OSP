import React from 'react';
import { createRoot } from 'react-dom/client';
import '../shared/tokens/base.css';
import './board.css';
import { Wordmark } from '../components/interactive/SiteHeader';

// Screenshots are real renders of each concept (tests/visual/shoot.mjs), copied into prototype/board/.
const shots = import.meta.glob('./board/*.webp', { eager: true, query: '?url', import: 'default' }) as Record<string, string>;
const shot = (name: string) => shots[`./board/${name}.webp`];

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
        <h1 className="t-section board-title">Three openings for <span className="quiet">ANDOR OSP.</span></h1>
        <p className="board-rec"><span className="t-mono">Recommendation</span> H1, The Instrument: the strongest product presentation and the most distinctive opening, and the direct answer to the depth in your references. H2 is the alternative if studio renders are not accepted (decision D-04).</p>
        <p className="t-lede board-lede">Every product image is a genuine capture of the current build (c49d77f), or a studio render with that capture as its face. Open each concept at full size and on a phone.</p>
        <div className="board-grid">
          {CONCEPTS.map((c) => (
            <article key={c.id} className="board-card">
              <a href={c.href} className="board-shots" aria-label={`Open ${c.id}, ${c.name}`}>
                {c.id === 'H2' && <img className="board-desk board-desk--frame" src={shot('h2-desktop-sound')} width={1200} height={750} alt="" loading="lazy" />}
                {shot(`${c.id.toLowerCase()}-desktop`) && <img className="board-desk" src={shot(`${c.id.toLowerCase()}-desktop`)} width={1200} height={750} alt="" loading="lazy" />}
                {shot(`${c.id.toLowerCase()}-mobile`) && <img className="board-mob" src={shot(`${c.id.toLowerCase()}-mobile`)} width={390} height={844} alt="" loading="lazy" />}
              </a>
              <div className="board-meta">
                <p className="t-mono">{c.id} · {c.tone}{(c as any).rec ? ' · recommended' : ''}</p>
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
