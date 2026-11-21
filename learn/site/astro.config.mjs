// ANDOR Learn: the public visual guides (local preview; not deployed).
// Served at the root of its own host by default (a ….pages.dev preview, later learn.evenant.com).
// LEARN_BASE=/learn builds it for a path on another site instead.
import { defineConfig } from 'astro/config';
const base = process.env.LEARN_BASE ?? '/';
import mdx from '@astrojs/mdx';

export default defineConfig({
  site: process.env.LEARN_SITE ?? 'https://learn.evenant.com',
  base,
  trailingSlash: 'always',
  output: 'static',
  integrations: [mdx()],
  build: { format: 'directory' },
});
