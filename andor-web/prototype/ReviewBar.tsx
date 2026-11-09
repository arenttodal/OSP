import React from 'react';

/** Prototype-only navigation between concepts. Hidden with ?clean (for screenshots). */
export function ReviewBar({ current }: { current: string }) {
  if (typeof location !== 'undefined' && new URLSearchParams(location.search).has('clean')) return null;
  const items = [['index.html', 'Board'], ['h1.html', 'H1'], ['h2.html', 'H2'], ['h3.html', 'H3']];
  return (
    <nav aria-label="Concept review" style={{ position: 'fixed', left: 12, bottom: 12, zIndex: 100, display: 'flex', gap: 2, padding: 3, borderRadius: 999, background: 'rgba(36,39,35,0.86)', backdropFilter: 'blur(8px)', fontSize: 12, fontFamily: 'var(--font-mono)' }}>
      {items.map(([href, label]) => (
        <a key={href} href={href} aria-current={current === label ? 'page' : undefined}
           style={{ padding: '6px 11px', borderRadius: 999, textDecoration: 'none', color: current === label ? '#242723' : '#f4f1e9', background: current === label ? '#f4f1e9' : 'transparent' }}>{label}</a>
      ))}
    </nav>
  );
}
