import React from 'react';
import { createRoot } from 'react-dom/client';
import '../../shared/tokens/base.css';
import '../gallery/panel-header.css';
import '../osp/osp.css';
import './pages.css';
import { SiteHeader } from '../../components/interactive/SiteHeader';
import { Footer } from '../osp/sections';
import { ReviewBar } from '../ReviewBar';
import { GROUPS, loadPage } from './doc';

const HOME = 'osp.html';
/** The section list starts open beside the text, folded above it on phones. */
const WIDE = typeof matchMedia === 'undefined' || matchMedia('(min-width: 901px)').matches;

/** One support or legal page: the landing page's header and footer around a quiet reading column. */
function DocPageView({ slug }: { slug: string }) {
  const page = loadPage(slug);
  const group = GROUPS.find((g) => (g.pages as readonly string[]).includes(slug))!;
  const titles = Object.fromEntries(GROUPS.flatMap((g) => g.pages.map((p) => [p, loadPage(p).title])));
  const nav = [
    { label: 'Instrument', href: HOME },
    { label: 'Sounds', href: `${HOME}#sounds` },
    { label: 'Support', href: 'installation.html', current: group.label === 'Support' },
  ];
  return (
    <div className="docs">
      <a className="skip" href="#main">Skip to content</a>
      <SiteHeader className="panel-header" wordmark="brand" homeHref={HOME} nav={nav} cta={{ label: 'Join the waitlist', href: `${HOME}#waitlist` }} />
      <main id="main">
        <header className="container doc-hero">
          <p className="t-mono doc-kicker">{page.kicker}</p>
          <h1 className="doc-title">{page.title}</h1>
          <p className="t-lede doc-summary">{page.summary}</p>
          {(page.draft || page.placeholders > 0) && (
            <p className="doc-note" role="note">
              <span className="t-mono">{page.draft ? 'Draft' : 'To fill in'}</span>
              {page.draft ?? 'Highlighted items need the owner’s details before launch.'}
              {page.placeholders > 0 && <> <mark className="ph">{page.placeholders} highlighted</mark></>}
            </p>
          )}
        </header>
        <div className="container doc-grid">
          <aside className="doc-side">
            <nav aria-label={group.label} className="doc-pages">
              <p className="t-mono">{group.label}</p>
              {group.pages.map((p) => (
                <a key={p} href={`${p}.html`} aria-current={p === slug ? 'page' : undefined}>{titles[p]}</a>
              ))}
            </nav>
            {page.toc.length > 2 && (
              <details className="doc-toc" open={WIDE}>
                <summary className="t-mono">On this page</summary>
                <nav aria-label="On this page">
                  {page.toc.map((t) => <a key={t.id} href={`#${t.id}`}>{t.label}</a>)}
                </nav>
              </details>
            )}
          </aside>
          <article className="doc-body" dangerouslySetInnerHTML={{ __html: page.html }} />
        </div>
      </main>
      <Footer home={HOME} />
      <ReviewBar current={group.label} />
    </div>
  );
}

const slug = document.body.dataset.page!;
document.title = `${loadPage(slug).title} · ANDOR OSP`;
// Text is the whole page here, so wait (briefly) for the two typefaces rather than reflow
// every paragraph when they arrive.
const fonts = Promise.all(['500 1rem "Inter Tight Variable"', '500 1rem "Geist Mono"'].map((f) => document.fonts?.load(f)));
Promise.race([fonts, new Promise((r) => setTimeout(r, 700))]).finally(() => {
  createRoot(document.getElementById('root')!).render(<DocPageView slug={slug} />);
});
