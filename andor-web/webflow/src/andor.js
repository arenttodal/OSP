/* ANDOR OSP: the Webflow landing page's behaviour. Needs GSAP 3 + ScrollTrigger (+ SplitText) and Lenis, loaded
   before this file. Everything degrades: without JS, without the libraries, or under prefers-reduced-motion the
   page is complete and readable; only the motion is missing. */
(() => {
  const root = document.querySelector(".andor");
  if (!root) return;
  const M = root.dataset.media || "";
  const reduce = matchMedia("(prefers-reduced-motion: reduce)").matches;
  const fine = matchMedia("(hover: hover) and (pointer: fine)").matches;
  const desktop = matchMedia("(min-width: 960px)");
  const conn = navigator.connection || {};
  const frugal = () => reduce || conn.saveData === true || /(^|-)(2g|3g)$/.test(conn.effectiveType || "");
  const $ = (s, el = root) => el.querySelector(s);
  const $$ = (s, el = root) => [...el.querySelectorAll(s)];
  const G = window.gsap, ST = window.ScrollTrigger;
  const motion = !!(G && ST) && !reduce;

  /* ---------------- smooth scroll ---------------- */
  let lenis = null;
  if (motion && window.Lenis) {
    lenis = new window.Lenis({ lerp: 0.09, wheelMultiplier: 0.95, smoothWheel: true });
    lenis.on("scroll", ST.update);
    G.ticker.add((t) => lenis.raf(t * 1000));
    G.ticker.lagSmoothing(0);
  }
  if (G && ST) G.registerPlugin(ST);
  // in-page links: eased scroll that clears the fixed header
  $$('a[href^="#"]').forEach((a) => a.addEventListener("click", (e) => {
    const id = a.getAttribute("href");
    const t = id === "#andor-top" ? root : document.querySelector(id);
    if (!t) return;
    e.preventDefault();
    if (lenis) lenis.scrollTo(t, { offset: id === "#andor-top" ? 0 : -8, duration: 1.6, easing: (x) => 1 - Math.pow(1 - x, 4) });
    else t.scrollIntoView({ behavior: reduce ? "auto" : "smooth" });
    history.replaceState(null, "", id);
  }));

  /* ---------------- films (lazy, sized, AV1 first, reduced-motion aware) ---------------- */
  const WIDTHS = [854, 1280, 1920], AV1W = [1280, 1920];
  const probe = document.createElement("video");
  const av1 = probe.canPlayType && probe.canPlayType('video/mp4; codecs="av01.0.08M.08"') === "probably";
  const films = new Map();
  function sources(el) {
    const need = el.clientWidth * Math.min(devicePixelRatio || 1, 2);
    const w = WIDTHS.find((x) => x >= need * 0.9) || 1920;
    const base = `${M}/film/${el.dataset.film}`;
    const s = [];
    if (av1) s.push([`${base}_${AV1W.find((x) => x >= w) || 1920}_av1.mp4`, 'video/mp4; codecs="av01.0.08M.08"']);
    s.push([`${base}_${w}.mp4`, "video/mp4"]);
    return s;
  }
  function attach(el, v) {
    for (const [src, type] of sources(el)) { const s = document.createElement("source"); s.src = src; s.type = type; v.appendChild(s); }
    v.load();
  }
  function buildFilm(el) {
    const v = document.createElement("video");
    v.muted = true; v.playsInline = true; v.setAttribute("muted", ""); v.setAttribute("playsinline", "");
    v.preload = frugal() ? "none" : "metadata";
    v.loop = el.dataset.mode !== "once";
    v.poster = `${M}/film/${el.dataset.film}_${frugal() ? "still" : "poster"}.jpg`;
    v.setAttribute("aria-label", el.querySelector("img")?.alt || "");
    el.appendChild(v);
    const f = { el, v, user: false, wantPlay: false };
    films.set(el, f);
    if (frugal()) {
      const img = el.querySelector("img"); if (img) { img.srcset = ""; img.src = `${M}/film/${el.dataset.film}_still.jpg`; }
      const b = document.createElement("button");
      b.className = "a-film__play"; b.type = "button"; b.setAttribute("aria-label", "Play film");
      b.addEventListener("click", () => { b.remove(); if (!v.querySelector("source")) attach(el, v); f.user = true; play(f); addToggle(f); });
      el.appendChild(b);
    } else {
      attach(el, v); addToggle(f); view.observe(el);
    }
    v.addEventListener("playing", () => el.classList.add("is-playing"));
  }
  function play(f) { f.v.play().catch(() => {}); }
  function addToggle(f) {
    const b = document.createElement("button");
    b.type = "button"; b.className = "a-film__toggle"; b.textContent = "Pause";
    b.addEventListener("click", () => {
      if (f.v.paused) { f.held = false; play(f); b.textContent = "Pause"; } else { f.held = true; f.v.pause(); b.textContent = "Play"; }
    });
    f.el.appendChild(b); f.toggle = b;
  }
  const near = new IntersectionObserver((es) => es.forEach((e) => {
    if (e.isIntersecting && !films.has(e.target)) { near.unobserve(e.target); buildFilm(e.target); }
  }), { rootMargin: "120% 0px" });
  const view = new IntersectionObserver((es) => es.forEach((e) => {
    const f = films.get(e.target); if (!f || f.held) return;
    if (e.intersectionRatio >= 0.35) { if (!(f.v.ended && !f.v.loop)) play(f); } else f.v.pause();
  }), { threshold: [0, 0.35] });
  $$(".a-film[data-film]").forEach((el) => near.observe(el));
  // a "loop-soft" film loops, but fades through black at the seam instead of jumping
  $$('.a-film[data-mode="loop-soft"]').forEach((el) => {
    const tick = () => { const f = films.get(el); if (f && f.v.duration) { const r = f.v.duration - f.v.currentTime; f.v.style.opacity = r < 0.6 ? Math.max(0, r / 0.6) : f.v.currentTime < 0.6 ? Math.min(1, f.v.currentTime / 0.6) : ""; } requestAnimationFrame(tick); };
    if (!reduce) requestAnimationFrame(tick);
  });

  /* ---------------- tabs: REIMAGINED and layers ---------------- */
  function tabs(list, buttons, onPick) {
    const pick = (i, focus) => {
      buttons.forEach((b, k) => { b.setAttribute("aria-selected", String(k === i)); b.tabIndex = k === i ? 0 : -1; });
      if (focus) buttons[i].focus();
      onPick(i);
    };
    buttons.forEach((b, i) => {
      b.addEventListener("click", () => pick(i));
      b.addEventListener("keydown", (e) => {
        const n = buttons.length, k = { ArrowDown: 1, ArrowRight: 1, ArrowUp: -1, ArrowLeft: -1 }[e.key];
        if (k) { e.preventDefault(); pick((i + k + n) % n, true); }
        if (e.key === "Home") { e.preventDefault(); pick(0, true); }
        if (e.key === "End") { e.preventDefault(); pick(n - 1, true); }
      });
    });
    return pick;
  }
  const showStep = (scope, i) => $$("[data-step]", scope).forEach((p) => p.classList.toggle("is-on", +p.dataset.step === i));
  const modes = $("[data-tabs]");
  if (modes) tabs($('[role="tablist"]', modes), $$('[role="tab"]', modes), (i) => showStep($(".a-modes__view", modes), i));
  const layers = $("[data-layers]");
  if (layers) {
    const notes = ["One recording is enough to begin.", "Add a second sound. Here the pluck on layer B plays as grains.",
      "Three sounds play as one instrument: a sung vowel, a granular pluck and a low vowel with IMPRINT."];
    const btns = $$("[data-layer]", layers), pill = $(".a-seg__pill", layers), note = $("[data-layer-note]", layers);
    const movePill = (i) => { const b = btns[i]; pill.style.width = b.offsetWidth + "px"; pill.style.transform = `translateX(${b.offsetLeft - 4}px)`; };
    tabs(null, btns, (i) => { movePill(i); showStep($(".a-layers__view", layers), i); note.textContent = notes[i]; });
    movePill(0); addEventListener("resize", () => movePill(btns.findIndex((b) => b.getAttribute("aria-selected") === "true")));
  }

  /* ---------------- FAQ: animated open and close ---------------- */
  $$(".a-qa").forEach((d) => {
    const s = $("summary", d), body = $(".a-qa__body", d);
    s.addEventListener("click", (e) => {
      if (!G || reduce) return;
      e.preventDefault();
      if (d.open) {
        G.to(body, { height: 0, duration: 0.55, ease: "power3.inOut", onComplete: () => { d.open = false; body.style.height = ""; } });
      } else {
        d.open = true; const h = body.scrollHeight;
        G.fromTo(body, { height: 0 }, { height: h, duration: 0.7, ease: "expo.out", onComplete: () => (body.style.height = "") });
        G.fromTo(body.firstElementChild, { y: 14, opacity: 0 }, { y: 0, opacity: 1, duration: 0.7, ease: "expo.out", delay: 0.05 });
      }
    });
  });

  /* ---------------- waitlist (MailerLite embedded-form endpoint) ---------------- */
  const form = $("[data-waitlist]");
  if (form) {
    const msg = $("[data-msg]", form), btn = $('button[type="submit"]', form), email = $('input[type="email"]', form);
    const say = (t, err) => { msg.textContent = t; msg.classList.toggle("is-error", !!err); };
    form.addEventListener("submit", async (e) => {
      e.preventDefault();
      if ($(".a-hp", form).value) return;                        // a bot filled the hidden field
      if (!email.validity.valid) { say("Please enter a valid email address.", true); email.focus(); return; }
      const endpoint = root.dataset.waitlistEndpoint;
      if (!endpoint) { say("The waitlist is not connected yet. Please try again soon.", true); return; }
      btn.disabled = true; say("Joining…");
      try {
        const body = new FormData(); body.append("fields[email]", email.value.trim()); body.append("ml-submit", "1"); body.append("anticsrf", "true");
        const r = await fetch(endpoint, { method: "POST", body });
        const j = await r.json().catch(() => ({}));
        if (!r.ok || j.success === false) throw new Error("rejected");
        form.classList.add("is-done");
        say("Thank you. Check your inbox to confirm, and we will write when ANDOR OSP is ready to play.");
      } catch (err) {
        say("That did not go through. Please check the address and try again.", true);
      } finally { btn.disabled = false; }
    });
  }

  /* ---------------- navigation ---------------- */
  const nav = $("[data-nav]");
  if (nav) {
    let last = scrollY;
    const darkZones = $$(".a-dark, .a-close, .a-foot");
    const update = () => {
      const y = scrollY;
      nav.classList.toggle("is-solid", y > innerHeight * 0.6);
      nav.classList.toggle("is-hidden", y > innerHeight && y > last + 2);
      if (y < last - 2) nav.classList.remove("is-hidden");
      last = y;
      const probeY = 36;
      nav.classList.toggle("is-dark", darkZones.some((z) => { const r = z.getBoundingClientRect(); return r.top <= probeY && r.bottom >= probeY; }));
    };
    addEventListener("scroll", update, { passive: true }); update();
    const links = $$(".a-nav__links a");
    const io = new IntersectionObserver((es) => es.forEach((e) => {
      if (e.isIntersecting) links.forEach((a) => a.classList.toggle("is-active", a.getAttribute("href") === "#" + e.target.id));
    }), { rootMargin: "-45% 0px -50% 0px" });
    links.forEach((a) => { const t = document.querySelector(a.getAttribute("href")); if (t) io.observe(t); });
  }

  /* ---------------- magnetic buttons and card glow (fine pointers only) ---------------- */
  if (fine && !reduce) {
    $$("[data-magnetic]").forEach((b) => {
      b.addEventListener("pointermove", (e) => {
        const r = b.getBoundingClientRect(), x = e.clientX - r.left - r.width / 2, y = e.clientY - r.top - r.height / 2;
        b.style.transform = `translate(${x * 0.18}px, ${y * 0.28}px)`;
      });
      b.addEventListener("pointerleave", () => (b.style.transform = ""));
    });
    $$(".a-card").forEach((c) => c.addEventListener("pointermove", (e) => {
      const r = c.getBoundingClientRect();
      c.style.setProperty("--mx", `${e.clientX - r.left}px`); c.style.setProperty("--my", `${e.clientY - r.top}px`);
    }));
  }

  /* ---------------- chapters: the sticky stage follows the chapter in view ---------------- */
  const ch = $("[data-chapters]");
  if (ch) {
    const stage = $(".a-chapters__stagewrap .a-stage", ch), bars = $$(".a-progress b", ch), items = $$("[data-chapter]", ch);
    const set = (i) => { items.forEach((it, k) => it.classList.toggle("is-active", k === i)); if (stage) showStep(stage, i); };
    set(0);
    if (G && ST) items.forEach((it, i) => {
      ST.create({ trigger: it, start: "top 55%", end: "bottom 45%", onToggle: (s) => s.isActive && set(i),
        onUpdate: (s) => bars[i] && G.set(bars[i], { scaleX: s.progress }) });
    });
    else items.forEach((it) => it.classList.add("is-active"));
  }

  if (!motion) { root.classList.add("is-static"); return; }

  /* ================= scroll choreography (GSAP) ================= */
  root.classList.add("is-ready");
  const E = "expo.out";

  // split headings into masked lines (SplitText when present; a small fallback otherwise)
  function lines(el) {
    if (window.SplitText) {
      const s = new window.SplitText(el, { type: "lines", linesClass: "a-line__in", mask: "lines" });
      return s.lines;
    }
    const words = el.innerHTML.split(/(\s+|<br\s*\/?>)/);
    el.innerHTML = words.map((w) => (/^\s+$/.test(w) || /^<br/.test(w) ? w : `<span class="a-w" style="display:inline-block">${w}</span>`)).join("");
    const ws = $$(".a-w", el); const rows = [];
    ws.forEach((w) => { const t = w.offsetTop; let row = rows.find((r) => Math.abs(r.t - t) < 4); if (!row) rows.push(row = { t, w: [] }); row.w.push(w); });
    el.innerHTML = rows.map((r) => `<span class="a-line"><span class="a-line__in">${r.w.map((w) => w.outerHTML).join(" ")}</span></span>`).join("");
    return $$(".a-line__in", el);
  }

  // hero: the plate settles, the headline rises line by line, then the rest
  const heroTitle = $("[data-hero-title]");
  const heroLines = heroTitle ? lines(heroTitle) : [];
  const heroImg = $("[data-hero-media] img");
  const intro = G.timeline({ defaults: { ease: E } });
  intro.fromTo("[data-hero-media]", { scale: 1.09, filter: "brightness(1.08) saturate(.9)" }, { scale: 1, filter: "brightness(1) saturate(1)", duration: 2.4, ease: "expo.out" }, 0)
    .from(heroLines, { yPercent: 110, duration: 1.4, stagger: 0.12 }, 0.25)
    .from("[data-hero-in]", { y: 24, opacity: 0, duration: 1.2, stagger: 0.1 }, 0.7);
  if (heroImg && !heroImg.complete) { intro.pause(); const go = () => intro.play(); heroImg.addEventListener("load", go, { once: true }); setTimeout(go, 1800); }
  // and drifts as you leave it
  G.to("[data-hero-media]", { yPercent: 14, ease: "none", scrollTrigger: { trigger: ".a-hero", start: "top top", end: "bottom top", scrub: true } });
  G.to(".a-hero__copy", { yPercent: -30, opacity: 0, ease: "none", scrollTrigger: { trigger: ".a-hero", start: "top top", end: "70% top", scrub: true } });

  // headings: masked line reveal
  $$("[data-lines]").forEach((el) => {
    const ls = lines(el);
    G.from(ls, { yPercent: 110, duration: 1.25, ease: E, stagger: 0.09, scrollTrigger: { trigger: el, start: "top 86%" } });
  });
  // everything else: a soft rise
  ST.batch("[data-fade]", {
    start: "top 90%", once: true,
    onEnter: (b) => G.to(b, { opacity: 1, y: 0, duration: 1.1, ease: E, stagger: 0.08, overwrite: true }),
  });

  // statement: words light up as you read down
  const st = $("[data-words]");
  if (st) {
    st.innerHTML = st.innerHTML.split(/(\s+)/).map((w) => (/^\s+$/.test(w) ? w : `<span class="a-word">${w}</span>`)).join("");
    G.to($$(".a-word", st), { opacity: 1, ease: "none", stagger: 0.05, scrollTrigger: { trigger: st, start: "top 78%", end: "bottom 42%", scrub: 0.6 } });
  }

  // the reveal: the film opens from an inset card to full bleed, pinned while it does
  const stageR = $("[data-reveal-stage]");
  if (stageR) ST.matchMedia({ "(min-width: 960px)": () => {
    const film = $(".a-film", stageR);
    // the frame is CSS-sticky inside a tall stage (no GSAP pin: a pin reparents the film and counts as a layout shift)
    const tl = G.timeline({ scrollTrigger: { trigger: stageR, start: "top top", end: "bottom bottom", scrub: 0.8 } });
    tl.fromTo(film, { clipPath: "inset(14% 9% 14% 9% round 22px)" }, { clipPath: "inset(0% 0% 0% 0% round 0px)", ease: "power2.inOut" }, 0)
      .fromTo($("img", film), { scale: 1.12 }, { scale: 1, ease: "power2.out" }, 0)
      .to("[data-reveal-caption]", { opacity: 1, duration: 0.25 }, 0.75);
  } });

  // atmosphere: the film slowly settles while the lines arrive one by one
  const atm = $(".a-atmos");
  if (atm) {
    const tl = G.timeline({ scrollTrigger: { trigger: atm, start: "top top", end: "bottom bottom", scrub: 0.8 } });
    tl.fromTo($(".a-film", atm), { scale: 1.14 }, { scale: 1, ease: "none" }, 0)
      .from(lines($("[data-atmos-title]", atm)), { yPercent: 110, stagger: 0.08, duration: 0.3, ease: "power3.out" }, 0.12)
      .from($$("[data-atmos-in]", atm), { y: 30, opacity: 0, stagger: 0.08, duration: 0.25 }, 0.2)
      .from($$("[data-atmos-line]", atm), { x: 30, opacity: 0, stagger: 0.12, duration: 0.25 }, 0.3);
  }

  // films in frames drift a little inside their frame
  $$(".a-macros__film, .a-layers__film").forEach((f) => G.fromTo($("img", f) || f, { scale: 1.08 }, { scale: 1, ease: "none", scrollTrigger: { trigger: f, start: "top bottom", end: "bottom top", scrub: true } }));
  $$("[data-parallax] img").forEach((img) => G.fromTo(img, { yPercent: -4 }, { yPercent: 4, ease: "none", scrollTrigger: { trigger: img.closest("[data-parallax]"), start: "top bottom", end: "bottom top", scrub: true } }));

  // the real interface: it tilts up into place
  const real = $("[data-real]");
  if (real) G.fromTo(real, { rotateX: 14, y: 80, scale: 0.94, transformPerspective: 1400 }, { rotateX: 0, y: 0, scale: 1, ease: "power2.out", scrollTrigger: { trigger: real, start: "top 95%", end: "top 30%", scrub: 0.8 } });

  // details gallery: pinned horizontal travel on desktop, native swipe on touch
  const gal = $("[data-gallery]");
  if (gal) {
    ST.matchMedia({
      "(min-width: 960px)": () => {
        gal.classList.add("is-pinned");
        const track = $(".a-gallery__track", gal);
        const dist = () => track.scrollWidth - innerWidth;
        const tw = G.to(track, { x: () => -dist(), ease: "none", scrollTrigger: { trigger: gal, start: "center center", end: () => "+=" + dist(), pin: true, scrub: 0.8, invalidateOnRefresh: true } });
        $$(".a-tile__img img", gal).forEach((img) => G.fromTo(img, { xPercent: 6 }, { xPercent: -6, ease: "none", scrollTrigger: { trigger: img, containerAnimation: tw, start: "left right", end: "right left", scrub: true } }));
        return () => gal.classList.remove("is-pinned");
      },
    });
  }

  // closing image: a slow push-in
  const cm = $("[data-close-media]");
  if (cm) G.fromTo(cm, { scale: 1.12 }, { scale: 1, ease: "none", scrollTrigger: { trigger: ".a-close", start: "top bottom", end: "bottom bottom", scrub: true } });

  // footer wordmark: letters spread into place
  const fb = $("[data-foot-big]");
  if (fb) G.fromTo(fb, { letterSpacing: "0.02em", opacity: 0.2 }, { letterSpacing: "0.12em", opacity: 0.92, ease: "none", scrollTrigger: { trigger: fb, start: "top bottom", end: "bottom 80%", scrub: true } });

  // triggers made before the pins above them (the chapters) must be measured after those pins: refresh in page order
  ST.sort();
  // fonts and late images change layout: refresh once everything has settled
  const refresh = () => { ST.sort(); ST.refresh(); };
  if (document.fonts && document.fonts.ready) document.fonts.ready.then(refresh);
  addEventListener("load", refresh);
})();
