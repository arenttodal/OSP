# Unified-engine scenes (linux-x86_64-gcc)

| Scene | Seconds | RMS dB | Peak dB | Brightness | Width | Hash | Mean block % | Worst block % | Peak voices | Peak grains |
|---|---|---|---|---|---|---|---|---|---|---|
| oneshot-chord4 | 2.58 | -28.81 | -15.55 | 0.259 | 0.934 | `34b39c356d611f5` | 1.85 | 4.65 | 4 | 0 |
| oneshot-scale-pluck | 2.94 | -42.68 | -27.13 | 0.66 | 0.013 | `6a89aed0cca5285d` | 1.37 | 3.4 | 3 | 0 |
| granular-single | 2.54 | -35.14 | -24.64 | 0.142 | 0.687 | `feaaff4e7c1dd03e` | 0.22 | 0.61 | 1 | 3 |
| granular-scale | 2.43 | -37.93 | -25.23 | 0.127 | 0.784 | `2423153b47d2003d` | 1.12 | 3.34 | 2 | 5 |
| granular-repeated | 2.51 | -36.71 | -23.57 | 0.142 | 0.796 | `b5ec75dd8e6fd072` | 1.09 | 1.87 | 2 | 4 |
| granular-chord4 | 2.14 | -28.94 | -14.99 | 0.259 | 0.991 | `51b40d391c403e68` | 1.49 | 3.8 | 4 | 11 |
| granular-chord8 | 2.29 | -25.52 | -11.64 | 0.172 | 0.74 | `67defac374b9bcd8` | 2.14 | 7.68 | 8 | 26 |
| layers-abc-mixed | 2.58 | -32.27 | -18.63 | 0.255 | 0.796 | `62ed6d7c00e14240` | 2.83 | 9.73 | 4 | 25 |
| layers-abc-granular | 2.68 | -40.68 | -26.57 | 0.127 | 0.735 | `fd186dba06c0cb8c` | 1.47 | 4.76 | 2 | 12 |
| layers-ab-oneshot-granular | 2.58 | -31.33 | -18.15 | 0.303 | 0.764 | `b401f5ddf18ba8ef` | 2.37 | 7.15 | 4 | 15 |
| granular-modulated | 2.06 | -29.08 | -13.22 | 0.259 | 1.026 | `b650bcd8aa1fa11b` | 2.07 | 9.04 | 4 | 57 |
| patch-granular-textured | 2.47 | -29.18 | -16.08 | 0.369 | 1.161 | `cbcedc5da5b5ff6f` | 2.9 | 7.86 | 4 | 51 |
| patch-granular-glitch | 2.44 | -39.03 | -25.34 | 0.063 | 0.916 | `ac29ff37b398c2e3` | 1.11 | 3.37 | 2 | 4 |
| granular-mono-glide | 2.02 | -34.92 | -22.52 | 0.178 | 0.662 | `f8a5a73380506b8e` | 1.08 | 1.75 | 1 | 3 |
| stress-abc-granular-chord8 | 2.58 | -27.41 | -13.64 | 0.161 | 0.76 | `4e200c8246570948` | 7.26 | 24.89 | 8 | 294 |

Peak resident memory of the renderer: 26.7 MB. InstrumentEngine object: 58744 bytes (InstrumentVoice 19392 bytes each).
