# ANDOR motion on the website

**What this is.** The website delivery versions of the approved final-quality films, and the small script that plays them. The 1920 masters (H.264 CRF 14, 4–8 Mbit/s) are for editing and archive. They are not for streaming.

| File | Purpose |
|---|---|
| `make_web_motion.sh <masters> <out>` | Builds the delivery set from the masters folder (`01_final-quality_1920` in the motion ZIP) |
| `motion.js` | The player: lazy loading, size and codec choice, reduced motion, play once or loop. No dependencies, about 2 KB |
| `motion_sections.html` | Reference mock: the films in the sections the visual review assigned. Expects the delivery set in `media/` |

## The delivery set (per film)

| File | Codec | Typical size, 12–16 s |
|---|---|---|
| `<film>_854.mp4` | H.264 High, yuv420p: phones | 0.4–0.9 MB |
| `<film>_1280.mp4` | H.264 | 0.9–1.8 MB |
| `<film>_1920.mp4` | H.264 | 2.4–4.2 MB |
| `<film>_1280_av1.mp4`, `<film>_1920_av1.mp4` | AV1, offered first where the browser decodes it | 40–70 % smaller than H.264 |
| `<film>_poster.{webp,jpg}` | The first frame, shown until playback starts, so there is no jump | |
| `<film>_still.{webp,jpg}` | A representative frame for visitors who will not see the film move | |

**Settings:**
- No audio.
- `faststart`, so playback can begin before the file has fully downloaded.
- A keyframe every 2 s.
- Bitrate capped at 5 / 2.5 / 1.2 Mbit/s.

**Quality against the master:** SSIM 0.992–0.995 for every film, at 1920 and at 1280, in H.264 and in AV1. The two are indistinguishable at normal viewing.

**Films:** R01_CUT-R4 (with and without T1), R04_WIN16, D2_TRACK, R03_GLIDE-edit-15s5, R02_DIM-R4-loop.

## Embedding

```html
<figure class="andor-film" data-film="R04_WIN16" data-base="/media/" data-mode="once">
  <picture>
    <source type="image/webp" srcset="/media/R04_WIN16_poster.webp">
    <img src="/media/R04_WIN16_poster.jpg" alt="Window light moving slowly across the ANDOR OSP instrument"
         loading="lazy" decoding="async">
  </picture>
</figure>
<script src="/js/motion.js" defer></script>
```

**What it does:**
- **Nothing loads at first.** No video is fetched until a film is within one screen of the viewport.
- **Plays only when seen.** A film plays while half of it is visible, and pauses when it leaves.
- **Size.** The file follows the element's width × device pixel ratio, capped at 2×. A phone fetches the 854 version.
- **Codec.** AV1 is offered where the browser decodes it, otherwise H.264.
- **Reduced motion, Save-Data, or a 2G/3G connection.** The visitor sees the still and a play button. Nothing is downloaded until they press it.
- **Play once or loop.** `data-mode="once"` (the default) plays through and holds the last frame, because the camera moves do not end where they start. `data-mode="loop"` is for R02, which loops seamlessly.

**Tested in Chromium** (Playwright) on desktop at 1280 px, with reduced motion, and on a 390 px phone at 3× with AV1 unavailable:
- the right files load;
- films play and pause with scrolling;
- reduced motion downloads nothing until play;
- no console errors.

**Not yet tested:** Safari and Firefox on real devices. Do that before launch.

## Framer

Framer's Video component can take the 1920 H.264 file directly: set *Autoplay*, *Muted*, *Loop* off for camera moves, and the poster image. For the size and codec choice and the reduced-motion behaviour above, use `motion.js` in a code component or embed.
