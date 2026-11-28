# Five site directions (for review)

Five complete landing pages for ANDOR OSP, each a different direction, for the owner to compare
and pick from. They are design explorations, built on one shared kit. The Webflow page in
`../src` is unchanged.

| Site | Direction | Reference |
|---|---|---|
| `v1/` Signal | Dark and cinematic after the approved ivory hero; sound first; a Listen list | Teletone Audio |
| `v2/` Studio | Light, friendly-pro; the software as the hero; numbered steps; playing tabs | XLN Audio Life |
| `v3/` Monument | Editorial, one idea per screen; the three pillars; close-ups; tech specs | Apple product pages |
| `v4/` Workbench | Graphite, technical; hotspot editor; anatomy; comparison; engine room | Native Instruments Kontakt |
| `v5/` Story | Narrative; the five use-case stories beside their recordings | Claims doc §4 |

**House rules, all five.**
- **Type and motion:** Inter Tight Bold. Native scrolling, nothing pinned, no smoothing, no bounces.
- **Media:** real GUI and real recordings only, with no DAW behind the main images.
- **Claims:** checked against the beta source and `docs/marketing/features-and-benefits.md`, which
  predates the rename of CHARACTER to FILTER.

**Kit (`kit/`).**
- **Base:** type and tokens.
- **Hero:** the scroll-scrubbed hero (the H01_FACE frames over a parallax backdrop with drifting light).
- **Clips:** they play only on screen; with reduced motion or Save-Data they show a poster and a Play button.
- **Sequences and tabs:**
  - step sequences advance when each clip ends;
  - tabs advance by themselves and hold for their clip;
  - crossfade loops.
- **Interface:** a page-wide sound toggle, hotspots, FAQ.
- **Waitlist:** posts to the real MailerLite form.

**Media is not in git.**
- **Where it lives:** in the R2 bucket under `osp/sites/m/`, with the fonts under `osp/sites/font/`.
- **What it is:**
  - the 30 ANDOR Learn clips with posters;
  - the three-card granular film (`[web-capture]` with `OSP_WEB_FILM`);
  - the isolated panels and editor captures (`[web-capture]`);
  - the editor-only display renders (`product-renders/blender/mockup`);
  - the H01_FACE hero frames (`compose_rgba.py`).

**Publish.** Copy `kit/`, `vN/` and `index.html` next to the media folder, then run:

```bash
CLOUDFLARE_API_TOKEN=… python3 webflow/tools/deploy_r2.py <account> andor-media <dir> --prefix osp/sites --cache 300
```

**Live.** https://pub-8db6c5f98f17416f953fc3d0e0354144.r2.dev/osp/sites/index.html. R2 has no folder
index, so links name `index.html`.

**Room hero (`v2/room.html`).** Studio with a different opening: left-aligned copy, the real editor
on the right, and a warm room far out of focus behind (`product-renders/blender/mockup/room_bokeh.py`).
A review switch in the corner compares the room and a soft plate (the same render blurred to light
only), wide type (Montserrat) and tight type (Inter Tight), and a flat or angled editor; `?bg=`,
`?type=` and `?plug=` set them by link. The waitlist sits in the hero as one field with the action
inside it. The rest of the page is a copy of `v2/index.html` at the time it was made.
