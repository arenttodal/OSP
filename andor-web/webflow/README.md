# ANDOR OSP landing page for Webflow

**What this is.** The landing page as pure code, placed in a Webflow page.
- **Scoping.** Everything is scoped under `.andor`, so it doesn't touch the Webflow site's own styles, and isn't affected by them.
- **Hosting.** Media, fonts, the stylesheet, the script and the libraries are served from one base URL: a Cloudflare R2 bucket.
- **Sign-ups.** The waitlist posts to a MailerLite embedded-form endpoint.

## Files

| Path | What it is |
|---|---|
| `src/page.html` | The page. Uses `<x-img name=…>` and `<x-film id=…>` shorthands, which the build expands into responsive `<picture>` markup |
| `src/andor.css` | Styles: tokens, layout, components, motion defaults, reduced-motion overrides |
| `src/andor.js` | Behaviour: smooth scroll, scroll choreography, films, tabs, FAQ, navigation, waitlist |
| `media.json` | Media manifest (widths and ratios), written by `tools/stage_media.py` |
| `build.mjs` | Builds `dist/`: minified CSS and JS, the three Webflow snippets, a single-embed variant and `preview.html` |
| `tools/stage_media.py` | Collects the approved images, films and fonts into the upload layout: `img/`, `film/`, `font/` and `lib/` (about 57 MB) |
| `tools/deploy_r2.py` | Publishes the staged media to R2: bucket, CORS, public URL, upload |
| `tools/qa.mjs` | Browser QA: four sizes; screenshots, console errors, failed requests, overflow and layout shift |

## Services

**Waitlist: MailerLite** (the Evenant account, chosen by the owner).
- **Where sign-ups go:** the group *ANDOR OSP waitlist* (`204835922699618252`), through the embedded form *ANDOR OSP waitlist (website)* (`204835943155238722`).
- **How the page submits:** it posts to `https://assets.mailerlite.com/jsonp/161928/forms/204835943155238722/subscribe`.
- **Checked:** that endpoint allows any origin (CORS `*`) and rejects an invalid address with `success: false`.

**Before launch, in MailerLite** (the API can't set these):
- turn on **double opt-in** for this form. The page promises a confirmation email first.
- set the form's **sender name** to ANDOR.

**Media: Cloudflare R2** (the owner's personal account).
- `tools/deploy_r2.py <account id> andor-media <media dir>` does the publishing:
  - creates the bucket;
  - sets read-only CORS, which the fonts need;
  - enables the public `r2.dev` URL;
  - uploads everything under `osp/v1/`, with content types and cache lifetimes.
- **What it needs:**
  - R2 enabled in that account;
  - a `CLOUDFLARE_API_TOKEN` with *Workers R2 Storage: Edit* on that account.
- **For production traffic:** connect a custom domain to the bucket. Cloudflare rate-limits `r2.dev` URLs. Then rebuild with `--media https://<domain>/osp/v1`.

## Build

```bash
node webflow/build.mjs --media https://<bucket public URL>/osp/v1 --waitlist https://assets.mailerlite.com/jsonp/<account>/forms/<form id>/subscribe
```

**Output** (`webflow/dist/`). Each Webflow field holds at most 50,000 characters; the build checks every snippet against that limit.

| File | Where it goes in Webflow | Size |
|---|---|---|
| `webflow-head.html` | Page settings → Custom code → Inside `<head>` | ~0.8k characters |
| `webflow-embed.html` | A **Code Embed** element on the page (the page itself) | ~44k characters |
| `webflow-footer.html` | Page settings → Custom code → Before `</body>` | ~0.4k characters |
| `webflow-single-embed.html` | All three in one Code Embed, if preferred | ~45k characters |
| `andor.min.css`, `andor.min.js` | Upload next to the media, at `<base>/andor.min.css` and `<base>/andor.min.js` | 26 KB and 12 KB |

**In Webflow:**
1. Put the Code Embed directly in the page body, or in a full-width section with no padding. The page draws its own navigation and footer, so hide the site's navbar and footer on this page.

## Motion and quality

**Libraries:** GSAP 3.13 (ScrollTrigger and SplitText) and Lenis 1.3.4. Both are pinned and self-hosted under `<base>/lib/`.

**Motion:**
- The hero plate settles while the headline rises line by line.
- The statement lights up word by word as you read.
- The reveal film opens from an inset card to full bleed. It uses CSS `position: sticky`, not a GSAP pin, so there is no layout shift.
- The interface chapters follow the scroll.
- The macro and layer films drift inside their frames.
- The real-interface capture tilts into place.
- The detail gallery travels horizontally on desktop and swipes on touch.
- Buttons are magnetic, and cards glow under the cursor.

**Films:**
- **Loading:** nothing loads until a film is near the screen. The size follows the element's width × device pixel ratio, and AV1 is offered first where the browser decodes it.
- **Reduced motion, Save-Data, or a 2G/3G connection:** visitors see a still and a play button. Nothing downloads until they press it.
- **Pause:** every film has a Pause button.

**Without JavaScript, without the libraries, or under `prefers-reduced-motion`,** the page is complete and readable. Only the motion is missing.

**Checked** (`tools/qa.mjs`, Chromium, at 1440×900, 1280×720, 834×1112 and 390×844):
- no console errors, no failed requests and no horizontal overflow;
- layout shift 0, both at load and while wheel-scrolling;
- the hero copy clears the instrument at 1280×720 through 2560×1080, and on phones down to 375×667.

**Not yet checked:** Safari and Firefox on real devices, and the page inside Webflow itself.

## Content

**Copy and claims:** they follow `docs/marketing/features-and-benefits.md`, including §7.
- No prices, dates, testimonials or "first/only" claims.
- The formats wording: AU, VST3 and Standalone for macOS "at release"; Windows "not announced yet".
- Host validation is described as still to come.

**Interface captures:**
- The "real thing" image is the shipping editor (0.9.0-beta.1, `32e0bee`).
- The chapter, mode and feature captures come from the earlier site captures. They differ from the shipping build only in details not visible at these sizes (`product-renders/documentation/interface_release_check.md`). Re-capture them on the beta before launch.

**Not in the page yet:**
- Audio: no recordings exist yet, and nothing stands in for them.
- The support and legal pages: the footer links `/privacy`, `/terms` and `/contact`, which must exist on the Webflow site.
