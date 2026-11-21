# Deploying ANDOR Learn to Cloudflare Pages

The site is static (`dist/`). It is served at the root of its own address: a `….pages.dev` preview
now, later `learn.evenant.com`. Every page sends `noindex` until publishing is approved (remove the
`<meta name="robots">` in `src/layouts/Base.astro` and `X-Robots-Tag` in `public/_headers`).

## Option A — connect the repository (rebuilds on every push)

Cloudflare dashboard → Workers & Pages → Create → Pages → Connect to Git → `arenttodal/OSP`.

| Setting | Value |
|---|---|
| Production branch | `feature/learn-capture-studio` (later a dedicated `learn-site` branch) |
| Framework preset | None |
| Root directory | `learn/site` |
| Build command | `npm ci && npm run build` |
| Build output directory | `dist` |
| Environment variable | `NODE_VERSION` = `22` (also in `.nvmrc`) |

**Connected:** Pages project `andor-learn` → https://andor-learn.pages.dev. Automatic production
deployments on; preview branches: none; build watch paths: `learn/*`, so only changes under `learn/`
start a build. Check a build in the project's Deployments tab: its log ends with the content check
(`content: 30 guides, …`) and Pagefind (`Indexed 155 pages`).

The build copies the captured clips from `learn/assets/osp/` (committed) and fails if a guide does not
match a captured, validated scene.

## Option B — direct upload (no Git connection)

```sh
cd learn/site && npm ci && npm run build
```
Cloudflare dashboard → Workers & Pages → Create → Pages → Upload assets → name the project →
drag the `dist` folder (or a zip of its contents) → Deploy. Or with Wrangler:
`npx wrangler pages deploy dist --project-name andor-learn`.

## Keep the preview private (optional)

Cloudflare Zero Trust → Access → Applications → Add → Self-hosted → domain `<project>.pages.dev`
(and `*.<project>.pages.dev` for preview builds) → policy: Allow, emails you list.

## Later: learn.evenant.com

DNS stays at DreamHost. In the Pages project → Custom domains → add `learn.evenant.com`; then at
DreamHost add the CNAME record Cloudflare shows (`learn` → `<project>.pages.dev`).
