/* ANDOR OSP site kit: behaviour shared by the five site directions.
   Native scrolling everywhere: nothing here holds, smooths or hijacks the scroll. */
(() => {
  const $ = (s, r = document) => r.querySelector(s), $$ = (s, r = document) => [...r.querySelectorAll(s)];
  const reduce = matchMedia("(prefers-reduced-motion: reduce)").matches;
  const conn = navigator.connection || {};
  const frugal = reduce || conn.saveData || /(^|-)2g|3g/.test(conn.effectiveType || "");
  let soundOn = false;
  const allVideos = () => $$("video");

  /* clips: play while on screen, pause when not; frugal visitors get the poster and a play button */
  const clipIO = new IntersectionObserver((es) => es.forEach((e) => {
    const v = e.target; if (v.closest("[data-seq], [data-cycle]")) return;   // sequences drive their own
    if (e.isIntersecting && !frugal) v.play().catch(() => {}); else v.pause();
  }), { threshold: 0.35 });
  $$("video[data-clip]").forEach((v) => {
    v.muted = true; v.playsInline = true; if (!v.closest("[data-seq]")) v.loop = true;
    if (frugal) { const c = v.closest(".clip"); if (c) { c.classList.add("frugal"); const b = document.createElement("button"); b.className = "play"; b.textContent = "Play"; b.onclick = () => { c.classList.remove("frugal"); v.play(); }; c.appendChild(b); } }
    clipIO.observe(v);
  });

  /* sound: one toggle for the page; turning it on unmutes the clip on screen */
  $$("[data-sound]").forEach((b) => b.addEventListener("click", () => {
    soundOn = !soundOn; $$("[data-sound]").forEach((x) => x.setAttribute("aria-pressed", soundOn));
    allVideos().forEach((v) => (v.muted = !soundOn));
    const playing = allVideos().find((v) => !v.paused); if (playing && soundOn) playing.play().catch(() => {});
  }));

  /* sequences: steps + clips; a step ends when its clip ends; the bar follows the clip */
  $$("[data-seq]").forEach((sec) => {
    const vids = $$("video", sec), steps = $$("[data-step]", sec); let i = 0, vis = false, raf = 0;
    const bar = () => { const v = vids[i]; steps.forEach((s, j) => { const b = $(".bar", s); if (b) b.style.width = j === i && v.duration ? (100 * v.currentTime / v.duration) + "%" : "0"; }); raf = requestAnimationFrame(bar); };
    const show = (k, play) => { vids[i].pause(); i = (k + vids.length) % vids.length;
      vids.forEach((v, j) => v.classList.toggle("on", j === i)); steps.forEach((s, j) => s.classList.toggle("on", j === i));
      const v = vids[i]; v.preload = "auto"; v.currentTime = 0; if (vids[i + 1]) vids[i + 1].preload = "auto"; v.muted = !soundOn; if (play) v.play().catch(() => {}); };
    vids.forEach((v, j) => { v.loop = false; v.addEventListener("ended", () => { if (j === i) show(i + 1, vis && !frugal); }); });
    steps.forEach((s, j) => s.addEventListener("click", () => show(j, true)));
    new IntersectionObserver(([e]) => { vis = e.isIntersecting; if (vis && !frugal) { vids[i].play().catch(() => {}); cancelAnimationFrame(raf); raf = requestAnimationFrame(bar); } else { vids[i].pause(); cancelAnimationFrame(raf); } }, { threshold: 0.4 }).observe(sec);
    show(0, false);
  });

  /* cycles: tabs/items with panes; each item holds for data-dur ms (or its clip's length) */
  $$("[data-cycle]").forEach((sec) => {
    const items = $$("[data-item]", sec), panes = $$("[data-pane]", sec), base = +(sec.dataset.cycle || 5000);
    let i = 0, t = null, vis = false;
    const dur = (k) => { const v = panes[k] && $("video", panes[k]); return +(items[k].dataset.dur || (v && v.duration ? v.duration * 1000 : base)); };
    const go = (k) => { i = (k + items.length) % items.length;
      items.forEach((el, j) => { el.classList.toggle("on", j === i); el.setAttribute("aria-selected", j === i); const b = $(".bar", el); if (b) { b.style.transition = "none"; b.style.width = "0"; } });
      panes.forEach((p, j) => { p.classList.toggle("on", j === i); const v = $("video", p); if (v) { if (j === i) { v.currentTime = 0; v.muted = !soundOn; if (vis && !frugal) v.play().catch(() => {}); } else v.pause(); } });
      requestAnimationFrame(() => { const b = $(".bar", items[i]); if (b && vis && !reduce) { b.offsetWidth; b.style.transition = `width ${dur(i)}ms linear`; b.style.width = "100%"; } }); };
    const run = () => { clearTimeout(t); if (vis && !reduce) t = setTimeout(() => { go(i + 1); run(); }, dur(i)); };
    items.forEach((el, j) => el.addEventListener("click", () => { go(j); run(); }));
    new IntersectionObserver(([e]) => { vis = e.isIntersecting; if (vis) { go(i); run(); } else { clearTimeout(t); panes.forEach((p) => { const v = $("video", p); if (v) v.pause(); }); } }, { threshold: 0.35 }).observe(sec);
    go(0);
  });

  /* loops: crossfade a set of images on a fixed beat while on screen (no controls) */
  $$("[data-loop]").forEach((box) => {
    const imgs = $$("img", box), chips = $$("[data-chip]", box.closest("section") || document); let i = 0, t = null;
    const tick = () => { i = (i + 1) % imgs.length; imgs.forEach((im, j) => im.classList.toggle("on", j === i)); chips.forEach((c, j) => c.classList.toggle("on", j === i)); };
    new IntersectionObserver(([e]) => { clearInterval(t); if (e.isIntersecting && !reduce) t = setInterval(tick, +(box.dataset.loop || 2000)); }, { threshold: 0.35 }).observe(box);
  });

  /* rise: the instrument's frames follow the scroll (the page is never held); backdrop parallax; copy fades */
  $$("[data-rise]").forEach((box) => {
    if (matchMedia("(max-aspect-ratio: 4/5)").matches) return;
    const sec = box.closest("section"), N = +box.dataset.frames, base = box.dataset.base, cv = $("canvas", box), ctx = cv.getContext("2d"), bg = $(".rise__bg", box);
    const copy = box.dataset.copy ? $(box.dataset.copy, sec) : null, range = +(box.dataset.range || 0.55), imgs = []; let shown = -1, want = 0;
    const draw = (k) => { const im = imgs[k]; if (!im || !im.complete || !im.naturalWidth) return false;
      const r = cv.getBoundingClientRect(), dpr = Math.min(devicePixelRatio || 1, 2), W = Math.round(r.width * dpr), H = Math.round(r.height * dpr);
      if (cv.width !== W || cv.height !== H) { cv.width = W; cv.height = H; }
      const s = Math.max(W / im.naturalWidth, H / im.naturalHeight), w = im.naturalWidth * s, h = im.naturalHeight * s;
      ctx.clearRect(0, 0, W, H); ctx.drawImage(im, (W - w) * 0.5, (H - h) * 0.62, w, h); shown = k; return true; };
    const tick = () => { const y = scrollY - sec.offsetTop, p = Math.min(1, Math.max(0, y / (innerHeight * range)));
      want = reduce ? 0 : Math.round((1 - Math.pow(1 - p, 2.2)) * (N - 1));
      if (want !== shown && !draw(want)) for (let k = want; k >= 0; k--) if (draw(k)) break;
      if (bg) bg.style.transform = `translateY(${Math.max(0, Math.min(y, innerHeight)) * 0.18}px)`;
      if (copy) { const q = Math.min(1, Math.max(0, y / (innerHeight * 0.32))); copy.style.opacity = String(1 - q); copy.style.transform = `translateY(${-q * 24}px)`; } };
    for (let k = 0; k < N; k++) { const im = new Image(); im.decoding = "async"; im.src = base + String(k).padStart(2, "0") + ".webp"; im.onload = () => { if (k === want || shown < 0) tick(); }; imgs.push(im); }
    addEventListener("scroll", () => requestAnimationFrame(tick), { passive: true }); addEventListener("resize", () => { shown = -1; tick(); }); tick();
  });

  /* nav: solid after the hero; dark over dark sections */
  $$("[data-nav]").forEach((nav) => {
    const darks = $$(".on-night");
    const up = () => { nav.classList.toggle("solid", scrollY > innerHeight * 0.5); nav.classList.toggle("dark", darks.some((z) => { const r = z.getBoundingClientRect(); return r.top <= 30 && r.bottom >= 30; })); };
    addEventListener("scroll", up, { passive: true }); up();
  });

  /* hotspots: points on the real editor, placed from its layout (1448 x 986) */
  $$("[data-hotspots]").forEach((st) => {
    const H = JSON.parse(st.dataset.hotspots), W = +(st.dataset.w || 1448), Hh = +(st.dataset.h || 986);
    H.forEach(([x, y, t, d]) => { const b = document.createElement("button"); b.className = "hot"; b.style.left = (x / W * 100) + "%"; b.style.top = (y / Hh * 100) + "%";
      b.setAttribute("aria-label", t); b.innerHTML = `<span class="tip"><b>${t}</b>${d}</span>`; b.addEventListener("click", () => b.classList.toggle("open")); st.appendChild(b); });
  });

  /* waitlist: the MailerLite embedded-form endpoint (double opt-in) */
  $$("form[data-waitlist]").forEach((form) => {
    const msg = $(".msg", form), btn = $("button", form), email = $("input[type=email]", form);
    const say = (t, err) => { msg.textContent = t; msg.classList.toggle("err", !!err); };
    form.addEventListener("submit", async (e) => {
      e.preventDefault(); if ($(".hp", form).value) return;
      if (!email.validity.valid) { say("Please enter a valid email address.", true); email.focus(); return; }
      btn.disabled = true; say("Joining…");
      try {
        const body = new FormData(); body.append("fields[email]", email.value.trim()); body.append("ml-submit", "1"); body.append("anticsrf", "true");
        const r = await fetch(form.dataset.waitlist, { method: "POST", body }); const j = await r.json().catch(() => ({}));
        if (!r.ok || j.success === false) throw new Error("rejected");
        form.classList.add("done"); say("Thank you. Check your inbox to confirm, and we will write when ANDOR OSP is ready to play.");
      } catch (err) { say("That did not go through. Please check the address and try again.", true); } finally { btn.disabled = false; }
    });
  });
})();
