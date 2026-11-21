# Learn ANDOR: the public visual guides

Astro 7 + MDX + Pagefind. Deployed as a preview to Cloudflare Pages (`andor-learn.pages.dev`, see
`DEPLOY.md`), served at the root of its own address; every page sends `noindex` until publishing is
approved.

## Layout

- **Gallery** (`/osp/`): a featured clip (Pedal Loop), then one horizontal row of guide cards per topic.
- **Guide** (`/osp/<group>/<guide>/`): a full-window player. Top bar: brand, breadcrumb, search (⌘K),
  previous / position / next guide, and a progress bar per guide in the topic. Left: the curriculum
  (every topic; the current one open; guides watched in this browser are ticked). Middle: title,
  purpose, the clip with one timeline segment per step. Right: the steps (the active one shows its
  result) and "Good to know" (the guide's `about`) with its reference link. At 1180 px and wider it
  fits one window with no page scroll; narrower screens stack clip, steps, curriculum.
- The clip plays straight through; the step it is on is highlighted. Clicking a step or a timeline
  segment (or ← / →) jumps to it. Reduced motion: nothing plays by itself.

```sh
cd learn/site
npm install
npm run build      # content checks → copy the captured clips → astro build → Pagefind index
npm test           # link check + browser tests (search, step URLs, one-window fit, reduced motion, 390 px, a11y)
npm run preview    # http://localhost:4321/osp/
```

## Where things come from

| What | Source |
|---|---|
| Guides (title, purpose, steps, synonyms, links) | `src/content/guides/*.mdx` (front matter; schema in `src/content.config.ts`) |
| Reference articles | `src/content/reference/*.mdx` |
| Videos, posters, step stills, step time ranges | `learn/assets/osp/manifest.json`, written by the capture pipeline (`learn/capture/`) |
| Look | ANDOR web tokens (`src/styles/global.css`), Inter Tight and Geist Mono self-hosted |

A guide names its scene (`scene: osp.scene.…`); its step IDs must be exactly that scene's marks,
so each step plays its own segment of the clip. `scripts/check-content.mjs` fails the build if a
guide's scene is missing, failed validation, has different steps, or was verified at another commit.
Scenes that failed validation are never copied into the site.

## URLs

- `/osp/` — gallery: featured clip, a row per topic
- `/osp/<group>/<guide>/` — a guide; `#<step>` selects a step
- `/osp/<group>/<guide>/<step>/` — a step's own page (what search results open)
- `/osp/reference/<article>/` — reference
- `/osp/search/?q=…` — search

## Editorial rules

Use the control names as drawn in OSP. One instruction and one result per step. State only what the
capture shows or the code confirms. Host-specific steps that have not been verified carry
`editorial.hostVerification`, which the guide shows as a note.
