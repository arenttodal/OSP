import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';
import { resolve } from 'node:path';

// Prototype build: one static page per hero concept plus the concept board. `base: './'` keeps
// every URL relative so the build can be previewed from any static host or artifact.
export default defineConfig({
  base: './',
  publicDir: 'assets/optimized',
  plugins: [react()],
  build: {
    outDir: 'dist',
    assetsInlineLimit: 0,
    rollupOptions: {
      input: {
        index: resolve(__dirname, 'index.html'),
        h1: resolve(__dirname, 'h1.html'),
        h2: resolve(__dirname, 'h2.html'),
        h3: resolve(__dirname, 'h3.html'),
        g1: resolve(__dirname, 'g1.html'),
        g2: resolve(__dirname, 'g2.html'),
        g3: resolve(__dirname, 'g3.html'),
        g4: resolve(__dirname, 'g4.html'),
        g5: resolve(__dirname, 'g5.html'),
        osp: resolve(__dirname, 'osp.html'),
      },
    },
  },
});
