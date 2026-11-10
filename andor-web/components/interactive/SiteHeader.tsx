import React, { useEffect, useRef, useState } from 'react';

export interface NavItem { label: string; href: string; current?: boolean }

export interface SiteHeaderProps {
  nav?: NavItem[];
  cta?: { label: string; href: string };
  /** Visual style of the wordmark; 'product' sets ANDOR/OSP as in the instrument's own logotype. */
  wordmark?: 'brand' | 'product';
  className?: string;
  /** Extra class names to stage the header in an entrance sequence. */
  revealClass?: string;
  /** Where the wordmark leads: the top of the page by default, the home page from other pages. */
  homeHref?: string;
}

const DEFAULT_NAV: NavItem[] = [
  { label: 'Instrument', href: '#explore', current: true },
  { label: 'Sounds', href: '#sounds' },
  { label: 'Support', href: '#support' },
];

export function Wordmark({ variant = 'product', href = '#top' }: { variant?: 'brand' | 'product'; href?: string }) {
  return (
    <a className="wordmark" href={href} aria-label="ANDOR OSP, home">
      <b>ANDOR</b>{variant === 'product' && <><i>/</i><span>OSP</span></>}
    </a>
  );
}

export function SiteHeader({ nav = DEFAULT_NAV, cta = { label: 'Join the waitlist', href: '#waitlist' }, wordmark = 'product', className = '', revealClass = '', homeHref = '#top' }: SiteHeaderProps) {
  const [open, setOpen] = useState(false);
  const closeRef = useRef<HTMLButtonElement>(null);
  const openRef = useRef<HTMLButtonElement>(null);

  useEffect(() => {
    if (!open) return;
    closeRef.current?.focus();
    const onKey = (e: KeyboardEvent) => e.key === 'Escape' && setOpen(false);
    document.addEventListener('keydown', onKey);
    document.documentElement.style.overflow = 'hidden';
    return () => { document.removeEventListener('keydown', onKey); document.documentElement.style.overflow = ''; openRef.current?.focus(); };
  }, [open]);

  return (
    <header className={`site-header ${className}`}>
      <div className="container site-header__bar">
        <span className={revealClass} style={{ ['--i' as any]: 0 }}><Wordmark variant={wordmark} href={homeHref} /></span>
        <nav className={`site-nav ${revealClass}`} aria-label="Main" style={{ ['--i' as any]: 1 }}>
          {nav.map((n) => <a key={n.href} href={n.href} aria-current={n.current ? 'page' : undefined}>{n.label}</a>)}
          <a className="btn btn-secondary btn-small" href={cta.href}>{cta.label}</a>
        </nav>
        <button ref={openRef} type="button" className={`menu-button ${revealClass}`} aria-label="Open menu" aria-expanded={open} aria-controls="menu-sheet" onClick={() => setOpen(true)}>
          <svg width="18" height="18" viewBox="0 0 18 18" aria-hidden="true"><path d="M2 6h14M2 12h14" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" /></svg>
        </button>
      </div>
      {open && (
        <div id="menu-sheet" className="menu-sheet" role="dialog" aria-modal="true" aria-label="Menu">
          <div className="site-header__bar">
            <Wordmark variant={wordmark} href={homeHref} />
            <button ref={closeRef} type="button" className="menu-button" style={{ display: 'inline-flex' }} aria-label="Close menu" onClick={() => setOpen(false)}>
              <svg width="18" height="18" viewBox="0 0 18 18" aria-hidden="true"><path d="M4 4l10 10M14 4L4 14" stroke="currentColor" strokeWidth="1.5" strokeLinecap="round" /></svg>
            </button>
          </div>
          <nav aria-label="Main">
            {nav.map((n) => <a key={n.href} href={n.href} aria-current={n.current ? 'page' : undefined} onClick={() => setOpen(false)}>{n.label}</a>)}
          </nav>
          <a className="btn btn-primary" style={{ marginTop: 32, alignSelf: 'flex-start' }} href={cta.href} onClick={() => setOpen(false)}>{cta.label}</a>
        </div>
      )}
    </header>
  );
}
