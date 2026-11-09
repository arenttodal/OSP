import React from 'react';

export interface AccordionItem { q: string; a: React.ReactNode }

/** FAQ on native <details>/<summary>: keyboard, screen readers and find-in-page work for free. */
export function Accordion({ items, className = '' }: { items: AccordionItem[]; className?: string }) {
  return (
    <div className={`acc ${className}`}>
      {items.map((it) => (
        <details key={it.q} className="acc__item">
          <summary className="acc__q"><span>{it.q}</span><span className="acc__icon" aria-hidden="true" /></summary>
          <div className="acc__a">{it.a}</div>
        </details>
      ))}
    </div>
  );
}
