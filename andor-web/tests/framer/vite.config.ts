import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';
import { resolve } from 'node:path';
// Harness for the Framer code components: "framer" resolves to the stand-in module.
export default defineConfig({
  root: resolve(__dirname, '../..'),
  plugins: [react()],
  resolve: { alias: { framer: resolve(__dirname, 'framer-shim.ts') } },
  server: { port: 4830, strictPort: true },
});
