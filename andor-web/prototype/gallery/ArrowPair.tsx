import React from 'react';

/** The pill + orb CTA (design system: ButtonPair). */
export function ExploreButton({ label = 'Explore OSP', href = '#explore' }: { label?: string; href?: string }) {
  return (
    <a className="btn-pair" href={href}>
      <span className="pill">{label}</span>
      <span className="orb" aria-hidden="true"><svg viewBox="0 0 18 18"><path d="M5 13L13 5M6.5 5H13v6.5" fill="none" stroke="currentColor" strokeWidth="1.6" /></svg></span>
    </a>
  );
}
