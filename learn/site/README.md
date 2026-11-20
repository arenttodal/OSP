# ANDOR Learn: the public visual guides (local preview)

Astro 7 + MDX + Pagefind. Served under `/learn` (a path on the main site or a `learn.` subdomain:
decided before publishing). Nothing here is deployed; there is no hosting, DNS or routing
configuration in this repository.

```sh
cd learn/site
npm install
npm run build      # content checks → copy the captured clips → astro build → Pagefind index
npm test           # link check + browser tests (search, step URLs, reduced motion, 390 px, a11y)
npm run preview    # http://localhost:4321/learn/osp/
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

- `/learn/osp/` — home: search, quick access, every guide by group
- `/learn/osp/<group>/<guide>/` — a guide; `#<step>` selects a step
- `/learn/osp/<group>/<guide>/<step>/` — a step's own page (what search results open)
- `/learn/osp/reference/<article>/` — reference
- `/learn/osp/search/?q=…` — search

## Editorial rules

Use the control names as drawn in OSP. One instruction and one result per step. State only what the
capture shows or the code confirms. Host-specific steps that have not been verified carry
`editorial.hostVerification`, which the guide shows as a note.
