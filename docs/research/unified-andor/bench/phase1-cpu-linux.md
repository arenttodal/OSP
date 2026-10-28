# KEY DNA: CPU and memory (linux-x86_64-gcc, 4 x Xeon 2.1 GHz, Release)

The 15 baseline scenes, each the fastest of 5 runs per block, as % of a 256-sample block (5.3 ms).
Phase 0: the build before KEY DNA. Phase 1 off: this build, KEY DNA off (bit-identical output).
Phase 1 DNA: this build, the expressive profile on every layer (One Shot layers ignore it).

| Scene | Phase 0 mean | Phase 1 off | Phase 1 DNA | Phase 0 worst | Phase 1 off | Phase 1 DNA | Peak grains P0 / off / DNA |
|---|---|---|---|---|---|---|---|
| oneshot-chord4 | 1.85 | 1.87 | 1.87 | 4.66 | 4.80 | 4.97 | 0 / 0 / 0 |
| oneshot-scale-pluck | 1.37 | 1.40 | 1.38 | 3.40 | 3.37 | 3.30 | 0 / 0 / 0 |
| granular-single | 0.22 | 0.22 | 0.23 | 0.61 | 0.66 | 0.69 | 3 / 3 / 3 |
| granular-scale | 1.12 | 1.12 | 1.17 | 3.34 | 3.35 | 3.50 | 5 / 5 / 6 |
| granular-repeated | 1.09 | 1.09 | 1.13 | 1.87 | 1.77 | 1.83 | 4 / 4 / 4 |
| granular-chord4 | 1.49 | 1.51 | 1.53 | 3.80 | 3.94 | 3.98 | 11 / 11 / 10 |
| granular-chord8 | 2.14 | 2.14 | 2.20 | 7.68 | 7.45 | 7.20 | 26 / 26 / 29 |
| layers-abc-mixed | 2.83 | 3.09 | 2.87 | 9.73 | 11.24 | 10.12 | 25 / 25 / 23 |
| layers-abc-granular | 1.47 | 1.50 | 1.50 | 4.76 | 4.92 | 4.90 | 12 / 12 / 12 |
| layers-ab-oneshot-granular | 2.37 | 2.40 | 2.37 | 7.15 | 8.24 | 7.11 | 15 / 15 / 14 |
| granular-modulated | 2.07 | 2.11 | 2.15 | 9.04 | 9.11 | 8.78 | 57 / 57 / 53 |
| patch-granular-textured | 2.90 | 2.98 | 2.93 | 7.86 | 7.84 | 7.54 | 51 / 51 / 49 |
| patch-granular-glitch | 1.11 | 1.11 | 1.14 | 3.37 | 3.34 | 3.46 | 4 / 4 / 5 |
| granular-mono-glide | 1.08 | 1.12 | 1.11 | 1.75 | 1.93 | 1.75 | 3 / 3 / 3 |
| stress-abc-granular-chord8 | 7.26 | 7.55 | 7.54 | 24.89 | 25.81 | 26.69 | 294 / 294 / 294 |

Memory: InstrumentEngine object 58744 -> 108256 bytes (+49512: three 128-key profiles and their settings); InstrumentVoice 19392 -> 19440 bytes (+48, the key's offsets in NoteShape), x 208 voices; renderer peak RSS 26.7 / 26.9 / 26.6 MB.
