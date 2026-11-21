/* ANDOR motion embed: one small script for every film on the page (no dependencies, about 2 KB).

   <figure class="andor-film" data-film="R04_WIN16" data-base="media/" data-mode="once">
     <picture><source type="image/webp" srcset="media/R04_WIN16_poster.webp">
       <img src="media/R04_WIN16_poster.jpg" alt="…" loading="lazy" decoding="async"></picture>
   </figure>

   What it does, and why:
   - Nothing downloads until the film is within one screen of the viewport (IntersectionObserver); the page
     loads with the poster only.
   - It plays while at least half of it is visible and pauses when it leaves, so a long page never decodes
     films nobody is looking at.
   - The size follows the element's width × devicePixelRatio (854, 1280, 1920), so phones fetch 1-2 MB, not
     the 1920 master. AV1 is offered first where the browser decodes it, H.264 otherwise.
   - prefers-reduced-motion, Save-Data or a 2G/3G connection: no autoplay. The representative still is shown
     with a play button; the visitor can choose to play.
   - data-mode="loop" for seamless loops (R02); "once" (default) plays through and holds the last frame,
     because the other films are camera moves whose end does not meet their start.
*/
(() => {
  const WIDTHS = [854, 1280, 1920];
  const AV1_WIDTHS = [1280, 1920];
  const probe = document.createElement("video");
  const av1 = !!probe.canPlayType && probe.canPlayType('video/mp4; codecs="av01.0.08M.08"') === "probably";
  const reduce = matchMedia("(prefers-reduced-motion: reduce)");
  const conn = navigator.connection || {};
  const frugal = () => reduce.matches || conn.saveData === true || /(^|-)(2g|3g)$/.test(conn.effectiveType || "");

  function pick(el) {
    const need = el.clientWidth * Math.min(window.devicePixelRatio || 1, 2);
    const w = WIDTHS.find((x) => x >= need * 0.9) || WIDTHS[WIDTHS.length - 1];
    const base = el.dataset.base + el.dataset.film;
    const sources = [];
    if (av1) {
      const a = AV1_WIDTHS.find((x) => x >= w) || 1920;
      sources.push({ src: `${base}_${a}_av1.mp4`, type: 'video/mp4; codecs="av01.0.08M.08"' });
    }
    sources.push({ src: `${base}_${w}.mp4`, type: "video/mp4" });
    return sources;
  }

  function addSources(el, v) {
    for (const s of pick(el)) {
      const src = document.createElement("source"); src.src = s.src; src.type = s.type; v.appendChild(src);
    }
    v.load();
  }

  function build(el, deferred) {
    const v = document.createElement("video");
    v.muted = true; v.playsInline = true; v.preload = deferred ? "none" : "metadata";
    v.setAttribute("muted", ""); v.setAttribute("playsinline", "");
    v.loop = el.dataset.mode === "loop";
    v.poster = el.dataset.base + el.dataset.film + "_poster.jpg";
    v.setAttribute("aria-label", el.querySelector("img")?.alt || "");
    // reduced motion / data saver: no sources until the visitor presses play, so nothing is downloaded unasked
    if (!deferred) addSources(el, v);
    el.classList.add("has-video");
    el.prepend(v);
    return v;
  }

  function playButton(el, v) {
    const b = document.createElement("button");
    b.className = "andor-film__play"; b.type = "button"; b.setAttribute("aria-label", "Play film");
    b.addEventListener("click", () => {
      b.remove();
      if (!v.querySelector("source")) addSources(el, v);
      el.classList.add("is-playing"); v.play();
    });
    el.appendChild(b);
  }

  const seen = new WeakMap();
  const near = new IntersectionObserver((entries) => {
    for (const e of entries) {
      if (!e.isIntersecting || seen.has(e.target)) continue;
      const el = e.target, v = build(el, frugal());
      seen.set(el, v);
      near.unobserve(el);
      if (frugal()) {
        // the still, not the first frame: a closer, more representative image for those who will not see it move
        const img = el.querySelector("img");
        if (img) img.src = el.dataset.base + el.dataset.film + "_still.jpg";
        v.poster = el.dataset.base + el.dataset.film + "_still.jpg";
        playButton(el, v);
      } else {
        view.observe(el);
      }
    }
  }, { rootMargin: "100% 0px" });

  const view = new IntersectionObserver((entries) => {
    for (const e of entries) {
      const v = seen.get(e.target);
      if (!v) continue;
      if (e.intersectionRatio >= 0.5) {
        if (v.ended && !v.loop) continue;        // played once: hold the last frame
        v.play().then(() => e.target.classList.add("is-playing")).catch(() => playButton(e.target, v));
      } else {
        v.pause();
      }
    }
  }, { threshold: [0, 0.5] });

  // a visitor who turns reduced motion on while the page is open: stop everything now
  reduce.addEventListener?.("change", () => {
    if (!reduce.matches) return;
    document.querySelectorAll(".andor-film video").forEach((v) => v.pause());
  });

  document.querySelectorAll(".andor-film[data-film]").forEach((el) => near.observe(el));
})();
