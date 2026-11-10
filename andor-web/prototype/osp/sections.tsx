import React, { FormEvent, useEffect, useRef, useState } from 'react';
import { ProductMedia } from '../../components/media/ProductMedia';
import { AudioComparison } from '../../components/audio/AudioComparison';
import { Segmented } from '../../components/interactive/Segmented';
import { Accordion } from '../../components/interactive/Accordion';
import { Wordmark } from '../../components/interactive/SiteHeader';
import type { AudioDemo } from '../../shared/types/content';
import { useMedia } from '../../shared/utilities/motion';
import { CHAPTERS, DEEPER, DEMOS, DEMO_START, DETAILS, ESSENTIALS, FAQ, LAYERS, MODES, SPECS, SYNTH_STATES } from './content';

/** A section heading with a mono kicker; the second phrase takes the quiet tone. */
function Heading({ id, kicker, lead, quiet, as: Tag = 'h2', className = '' }: { id: string; kicker?: string; lead: string; quiet?: string; as?: 'h2' | 'h3'; className?: string }) {
  return (
    <header className={`sec-head ${className}`}>
      {kicker && <p className="t-mono sec-kicker">{kicker}</p>}
      <Tag id={id} className="sec-title">{lead}{quiet && <> <span className="quiet">{quiet}</span></>}</Tag>
    </header>
  );
}

/** Pending sound: a quiet, honest line where a listening example will go. */
const Pending = ({ children = 'Sound example in preparation' }: { children?: React.ReactNode }) => (
  <p className="t-mono pending"><span className="pending__dot" aria-hidden="true" />{children}</p>
);

/** Crossfading views of one subject (all mounted, one visible), so switching never shifts layout. */
function Views({ items, current, variant, sizes, className = '' }: { items: { id: string; asset: string }[]; current: string; variant: 'capture' | 'studio' | 'detail'; sizes: string; className?: string }) {
  return (
    <div className={`views ${className}`}>
      {items.map((v) => (
        <ProductMedia key={v.id} asset={v.asset} variant={variant} sizes={sizes} className={`views__item ${v.id === current ? 'is-current' : ''}`} />
      ))}
    </div>
  );
}

export function Statement() {
  return (
    <section className="sec statement" aria-labelledby="statement-title">
      <div className="container">
        <h2 id="statement-title" className="statement__text">Every recording contains something worth discovering. <span className="quiet">A sung vowel, a bowed note, an old synth, a phone memo. OSP turns it into an instrument you want to play, and it still sounds like your sound.</span></h2>
      </div>
    </section>
  );
}

export function FirstNote() {
  const [active, setActive] = useState(CHAPTERS[0].id);
  const refs = useRef<(HTMLElement | null)[]>([]);
  useEffect(() => {
    // The chapter crossing the middle of the screen is the one shown.
    const io = new IntersectionObserver((entries) => {
      for (const e of entries) if (e.isIntersecting) setActive((e.target as HTMLElement).dataset.id!);
    }, { rootMargin: '-45% 0px -45% 0px' });
    refs.current.forEach((r) => r && io.observe(r));
    return () => io.disconnect();
  }, []);
  return (
    <section id="first-note" className="sec first" aria-labelledby="first-title">
      <div className="container">
        <Heading id="first-title" kicker="The first note" lead="From one sound" quiet="to an instrument." />
        <div className="first__grid">
          <div className="first__chapters">
            {CHAPTERS.map((c, i) => (
              <article key={c.id} ref={(el) => (refs.current[i] = el)} data-id={c.id} className={`chapter ${active === c.id ? 'is-active' : ''}`} aria-labelledby={`ch-${c.id}`}>
                <p className="t-mono chapter__n">{c.n} · {c.kicker}</p>
                <h3 id={`ch-${c.id}`} className="chapter__title">{c.title}</h3>
                <p className="chapter__body">{c.body}</p>
                <Pending />
                {/* Phones: each chapter carries its own picture (no sticky stage). */}
                <ProductMedia asset={c.asset} variant="capture" className="chapter__media" sizes="92vw" />
              </article>
            ))}
          </div>
          <div className="first__stage" aria-hidden="true">
            <div className="first__sticky">
              <Views items={CHAPTERS} current={active} variant="studio" sizes="(max-width: 1023px) 0px, 56vw" />
              <ol className="first__steps">
                {CHAPTERS.map((c) => <li key={c.id} className={active === c.id ? 'is-active' : ''}><span className="t-mono">{c.n}</span> {c.kicker}</li>)}
              </ol>
            </div>
          </div>
        </div>
      </div>
    </section>
  );
}

const demoFor = (d: (typeof DEMOS)[number]): AudioDemo => ({
  id: d.id, title: d.name, sourceInstrument: `Source / ${d.name}`, description: d.line, approved: false,
  variants: [{ id: 'original', label: 'Original', sources: [] }, { id: 'osp', label: 'OSP', sources: [] }, { id: 'reimagined', label: 'Reimagined', sources: [] }],
});

export function Sounds() {
  const [sel, setSel] = useState(DEMO_START);
  const n = DEMOS.length;
  const row = useMedia('(max-width: 640px)'); // phones: a plain swipeable row, every card visible
  return (
    <section id="sounds" className="sec sounds theme-dark" aria-labelledby="sounds-title">
      <div className="container">
        <Heading id="sounds-title" kicker="Sounds" lead="Hear it." quiet="One recording, played three ways: the original, OSP and REIMAGINED." />
        <p className="sounds__note">These examples arrive with the first approved recordings. Each player is ready and will play the moment they do.</p>
      </div>
      <div className="deck" role="group" aria-roledescription="carousel" aria-label="Sound examples">
        <div className="deck__track">
          {DEMOS.map((d, i) => {
            const off = i - sel;
            const current = off === 0;
            return (
              <article key={d.id} className={`deck__card ${current ? 'is-current' : ''}`} style={{ ['--off' as any]: off, ['--abs' as any]: Math.abs(off) }}
                aria-roledescription="slide" aria-label={`${i + 1} of ${n}: ${d.name}`} aria-hidden={!row && Math.abs(off) > 1 ? true : undefined}>
                <div className="deck__face">
                  <p className="t-mono deck__n">{String(i + 1).padStart(2, '0')} / {d.name}</p>
                  <h3 className="deck__name">{d.name}</h3>
                  <p className="deck__line">{d.line}</p>
                  {current ? (
                    <div className="deck__player"><AudioComparison demo={demoFor(d)} theme="dark" /></div>
                  ) : (
                    <button type="button" className="deck__pick" onClick={() => setSel(i)} tabIndex={row || Math.abs(off) === 1 ? 0 : -1}>{row ? `Listen to ${d.name}` : `Show ${d.name}`}</button>
                  )}
                </div>
              </article>
            );
          })}
        </div>
        <div className="deck__nav container">
          <button type="button" className="deck__arrow" onClick={() => setSel((sel - 1 + n) % n)} aria-label="Previous example">←</button>
          <span className="t-mono deck__count" aria-live="polite">{sel + 1} / {n}</span>
          <button type="button" className="deck__arrow" onClick={() => setSel((sel + 1) % n)} aria-label="Next example">→</button>
        </div>
      </div>
    </section>
  );
}

export function Reimagined() {
  const [mode, setMode] = useState(MODES[3].id);
  const m = MODES.find((x) => x.id === mode)!;
  return (
    <section id="reimagined" className="sec reim" aria-labelledby="reim-title">
      <div className="container reim__grid">
        <div className="reim__copy">
          <Heading id="reim-title" kicker="REIMAGINED" lead="Five instruments" quiet="hidden in every sample." />
          <p className="sec-lede">One knob takes each layer from the recording itself toward its mode’s far end, and the sound stays recognisably yours.</p>
          <Segmented options={MODES} value={mode} onChange={setMode} label="REIMAGINED mode" variant="list" panelId="reim-panel" />
        </div>
        <div id="reim-panel" role="tabpanel" aria-labelledby={`reim-panel-tab-${mode}`} className="reim__stage">
          <div className="plate plate--large">
            <Views items={MODES} current={mode} variant="detail" sizes="(max-width: 900px) 84vw, 520px" className="reim__views" />
          </div>
          <p className="reim__text"><strong>{m.label}.</strong> {m.text}</p>
          <Pending />
        </div>
      </div>
    </section>
  );
}

export function Synth() {
  const [state, setState] = useState(SYNTH_STATES[2].id);
  const s = SYNTH_STATES.find((x) => x.id === state)!;
  return (
    <section id="synth" className="sec synth theme-dark" aria-labelledby="synth-title">
      <div className="container synth__grid">
        <div className="synth__copy">
          <Heading id="synth-title" kicker="SYNTH + IMPRINT" lead="Give a synthesizer" quiet="the soul of your recording." />
          <p className="sec-lede">Every layer has an analog oscillator of its own. In IMPRINT mode it takes on the body of your recording, so you can add weight and sustain under an acoustic sound and keep its identity.</p>
          <Segmented options={SYNTH_STATES} value={state} onChange={setState} label="SYNTH state" panelId="synth-panel" />
          <p className="synth__text">{s.text}</p>
          <Pending>IMPRINT is new. Listening examples arrive with the first recordings.</Pending>
        </div>
        <div id="synth-panel" role="tabpanel" aria-labelledby={`synth-panel-tab-${state}`} className="synth__stage">
          <Views items={SYNTH_STATES} current={state} variant="detail" sizes="(max-width: 1023px) 92vw, 58vw" className="synth__views" />
          <p className="t-mono synth__caption">The SYNTH page of layer A, as it looks in OSP</p>
        </div>
      </div>
    </section>
  );
}

export function Layers() {
  const [n, setN] = useState('3');
  const l = LAYERS.find((x) => x.id === n)!;
  return (
    <section id="layers" className="sec layers" aria-labelledby="layers-title">
      <div className="container">
        <div className="layers__head">
          <Heading id="layers-title" kicker="Layers" lead="One, two, three sounds." quiet="One instrument." />
          <div className="layers__ctl">
            <Segmented options={LAYERS} value={n} onChange={setN} label="Number of layers" panelId="layers-panel" />
            <p className="layers__text">{l.text} Each layer has its own mode, REIMAGINED, EQ, SYNTH and arpeggiator lane.</p>
          </div>
        </div>
        <div id="layers-panel" role="tabpanel" aria-labelledby={`layers-panel-tab-${n}`} className="layers__stage">
          <Views items={LAYERS} current={n} variant="studio" sizes="(max-width: 900px) 94vw, 72vw" />
        </div>
        <div className="granular">
          <div className="plate granular__plate"><ProductMedia asset="gran-card" variant="detail" sizes="(max-width: 900px) 90vw, 620px" /></div>
          <div className="granular__copy">
            <p className="t-mono sec-kicker">Granular</p>
            <h3 className="granular__title">Or turn any layer into a cloud.</h3>
            <p>Any layer can play as grains: position, size, density, tune, spread, scatter and pan, with three window shapes. With KEY DNA, each key gets its own grain behaviour.</p>
            <Pending />
          </div>
        </div>
      </div>
    </section>
  );
}

export function Details() {
  return (
    <section id="details" className="sec details" aria-labelledby="details-title">
      <div className="container">
        <Heading id="details-title" kicker="The instrument" lead="Made like an instrument," quiet="down to the knobs." />
        <div className="bento">
          {DETAILS.map((d) => (
            <figure key={d.id} className={`bento__item bento__item--${d.id}`}>
              <ProductMedia asset={d.asset} variant="detail" sizes="(max-width: 900px) 92vw, 50vw" />
              <figcaption><span className="t-mono">{d.title}</span> {d.note}</figcaption>
            </figure>
          ))}
        </div>
      </div>
    </section>
  );
}

export function Features() {
  const [lead, ...rest] = ESSENTIALS;
  return (
    <section id="features" className="sec features" aria-labelledby="features-title">
      <div className="container">
        <Heading id="features-title" kicker="Deeper" lead="Simple to start." quiet="Deep when you want it." />
        <article className="ess ess--lead">
          <div className="plate ess__plate"><ProductMedia asset={lead.asset} variant="detail" sizes="(max-width: 900px) 92vw, 60vw" /></div>
          <div className="ess__copy"><p className="t-mono sec-kicker">{lead.kicker}</p><h3 className="ess__title">{lead.title}</h3><p>{lead.body}</p></div>
        </article>
        <div className="ess__pair">
          {rest.map((e) => (
            <article key={e.id} className="ess">
              <div className="plate ess__plate"><ProductMedia asset={e.asset} variant="detail" sizes="(max-width: 900px) 92vw, 44vw" /></div>
              <div className="ess__copy"><p className="t-mono sec-kicker">{e.kicker}</p><h3 className="ess__title">{e.title}</h3><p>{e.body}</p></div>
            </article>
          ))}
        </div>
        <dl className="deeper">
          {DEEPER.map(([t, d]) => (
            <div key={t} className="deeper__row"><dt>{t}</dt><dd>{d}</dd></div>
          ))}
        </dl>
      </div>
    </section>
  );
}

export function Tech() {
  return (
    <section id="tech" className="sec tech" aria-labelledby="tech-title">
      <div className="container tech__grid">
        <Heading id="tech-title" kicker="Technical" lead="The details." />
        <dl className="specs">
          {SPECS.map(([k, v]) => <div key={k} className="specs__row"><dt className="t-mono">{k}</dt><dd>{v}</dd></div>)}
        </dl>
      </div>
    </section>
  );
}

export function Waitlist() {
  const [sent, setSent] = useState(false);
  const submit = (e: FormEvent) => { e.preventDefault(); setSent(true); };
  return (
    <section id="waitlist" className="sec wait" aria-labelledby="wait-title">
      <div className="container">
        <div className="wait__panel theme-dark">
          <Heading id="wait-title" kicker="Waitlist" lead="Be first to play it." />
          <p className="sec-lede">Leave your email and we will write when ANDOR OSP is ready, with the first sound examples. Pricing and the release date will be announced together.</p>
          {sent ? (
            <p className="wait__done" role="status">Thank you. This preview isn’t connected to a mailing list yet, so nothing was sent or stored.</p>
          ) : (
            <form className="wait__form" onSubmit={submit}>
              <label htmlFor="wait-email" className="sr-only">Email address</label>
              <input id="wait-email" name="email" type="email" required autoComplete="email" placeholder="you@example.com" />
              <button type="submit" className="btn btn-primary">Join the waitlist</button>
            </form>
          )}
        </div>
      </div>
    </section>
  );
}

export function Faq() {
  return (
    <section id="faq" className="sec faq" aria-labelledby="faq-title">
      <div className="container faq__grid">
        <Heading id="faq-title" kicker="Questions" lead="Asked often." />
        <Accordion items={FAQ} />
      </div>
    </section>
  );
}

/** Site footer. `home` prefixes in-page links when the footer sits on another page. */
export function Footer({ home = '' }: { home?: string }) {
  return (
    <footer className="foot theme-dark">
      <div className="container foot__grid">
        <div className="foot__brand"><Wordmark variant="brand" href={home ? home : '#top'} /><p>Instruments made from the sounds you love.</p></div>
        <nav aria-label="Footer" className="foot__cols">
          <div><p className="t-mono">Instruments</p><a href={home ? home : '#top'}>ANDOR OSP</a><span>ANDOR ANALOG <em>soon</em></span></div>
          <div><p className="t-mono">{home ? 'ANDOR OSP' : 'On this page'}</p><a href={`${home}#sounds`}>Sounds</a><a href={`${home}#reimagined`}>REIMAGINED</a><a href={`${home}#synth`}>SYNTH + IMPRINT</a><a href={`${home}#faq`}>Questions</a></div>
          <div><p className="t-mono">Support</p><a href="installation.html">Installation</a><a href="documentation.html">Documentation</a><a href="contact.html">Contact</a></div>
          <div><p className="t-mono">Legal</p><a href="privacy.html">Privacy</a><a href="terms.html">Terms</a><a href="licence.html">Licence</a></div>
        </nav>
        <p className="foot__fine">© 2026 ANDOR. ANDOR OSP is in development; pictures show the current development build.</p>
      </div>
    </footer>
  );
}
