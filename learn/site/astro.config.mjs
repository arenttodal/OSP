// ANDOR Learn: the public visual guides (local preview; not deployed).
// Served under /learn (a path on the main site or a learn. subdomain: decided at Gate 3).
import { defineConfig } from 'astro/config';
import mdx from '@astrojs/mdx';

export default defineConfig({
  site: 'https://learn.preview.invalid',
  base: '/learn',
  trailingSlash: 'always',
  output: 'static',
  integrations: [mdx()],
  build: { format: 'directory' },
});
