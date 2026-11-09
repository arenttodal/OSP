import React from 'react';

/** Where "Explore OSP" lands. The rest of the page is built only after hero approval (Gate H). */
export function NextSectionStub({ dark = false }: { dark?: boolean }) {
  return (
    <section id="explore" className={dark ? 'theme-dark' : ''} style={{ padding: 'var(--section-space) 0', borderTop: '1px solid var(--border)' }}>
      <div className="container">
        <p className="t-mono" style={{ color: 'var(--text-muted)', margin: 0 }}>Next: First Note Story</p>
        <p className="t-lede" style={{ maxWidth: '40ch', marginTop: 12 }}>The rest of the page is designed after a hero direction is approved (Gate H).</p>
      </div>
    </section>
  );
}
