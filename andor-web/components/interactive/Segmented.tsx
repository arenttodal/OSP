import React, { KeyboardEvent, useRef } from 'react';

export interface SegmentOption { id: string; label: string; hint?: string }

/**
 * A choice between views of the same thing (modes, states, layer counts): an ARIA tablist with
 * arrow-key, Home and End navigation. `variant="list"` stacks the options with a hint line under
 * each (the REIMAGINED mode list); `pills` is a compact row.
 */
export function Segmented({ options, value, onChange, label, variant = 'pills', panelId, className = '' }: {
  options: SegmentOption[]; value: string; onChange: (id: string) => void; label: string; variant?: 'pills' | 'list'; panelId?: string; className?: string;
}) {
  const ref = useRef<HTMLDivElement>(null);
  const index = Math.max(0, options.findIndex((o) => o.id === value));
  const move = (i: number) => {
    const n = (i + options.length) % options.length;
    onChange(options[n].id);
    (ref.current?.children[n] as HTMLElement | undefined)?.focus();
  };
  const onKey = (e: KeyboardEvent) => {
    const keys: Record<string, number> = { ArrowRight: 1, ArrowDown: 1, ArrowLeft: -1, ArrowUp: -1 };
    if (e.key in keys) { e.preventDefault(); move(index + keys[e.key]); }
    if (e.key === 'Home') { e.preventDefault(); move(0); }
    if (e.key === 'End') { e.preventDefault(); move(options.length - 1); }
  };
  return (
    <div ref={ref} role="tablist" aria-label={label} className={`seg seg--${variant} ${className}`} onKeyDown={onKey}>
      {options.map((o, i) => (
        <button key={o.id} type="button" role="tab" id={`${panelId ?? label}-tab-${o.id}`} aria-selected={i === index} aria-controls={panelId}
          tabIndex={i === index ? 0 : -1} onClick={() => onChange(o.id)} className="seg__opt">
          <span className="seg__label">{o.label}</span>
          {o.hint && variant === 'list' && <span className="seg__hint">{o.hint}</span>}
        </button>
      ))}
    </div>
  );
}
