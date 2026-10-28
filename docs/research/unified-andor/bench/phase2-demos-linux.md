# Unified-engine scenes (linux-x86_64-gcc)

| Scene | Seconds | RMS dB | Peak dB | Brightness | Width | Hash | Mean block % | Worst block % | Worst seen µs | Peak voices | Peak grains | Live grains |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| p2-options-neutral-chord4 | 2.15 | -29.79 | -16.68 | 0.25 | 1.504 | `c0dfe824b6e051ff` | 5.16 | 10.82 | 2324 | 4 | 11 | 11 |
| p2-scatter-3-chord4 | 2.15 | -29.92 | -16.26 | 0.256 | 1.546 | `5a368c923c5dfa50` | 5.19 | 9.42 | 1508 | 4 | 11 | 11 |
| p2-scatter-12-single | 3.15 | -34.78 | -23.12 | 0.176 | 2.451 | `58d15cb7c8d151b2` | 4.41 | 9.46 | 777 | 1 | 3 | 3 |
| p2-pan-50-chord4 | 2.15 | -29.81 | -16.03 | 0.249 | 1.434 | `22ab27e605ad546b` | 5.11 | 9.57 | 3374 | 4 | 11 | 11 |
| p2-pan-100-single | 3.15 | -35.05 | -21.77 | 0.146 | 1.603 | `c53ed50f302ddf53` | 3.93 | 9.87 | 4589 | 1 | 3 | 3 |
| p2-pan-100-mono-single | 3.15 | -35.57 | -21.83 | 0.147 | 0.27 | `57af4b393ce896c1` | 3.86 | 9.4 | 3519 | 1 | 3 | 3 |
| p2-window-soft-chord4 | 2.15 | -29.79 | -17.05 | 0.249 | 1.525 | `aefa38f7ac015716` | 5.4 | 9.66 | 3248 | 4 | 11 | 11 |
| p2-window-percussive-chord4 | 2.15 | -29.89 | -15.11 | 0.25 | 2.04 | `9e9cbdbec5967c6` | 5.15 | 9.24 | 1362 | 4 | 11 | 11 |
| p2-window-percussive-sparse-scale | 2.56 | -37.91 | -21.34 | 0.128 | 1.157 | `3c909d1082afc5f2` | 4.24 | 9.47 | 3256 | 2 | 3 | 3 |
| p2-keydna-scatter-pan-scale | 2.45 | -37.69 | -24.92 | 0.128 | 1.285 | `d5e107c597a876d9` | 4.38 | 9.37 | 1571 | 2 | 4 | 4 |
| p2-keydna-scatter-pan-chord8 | 2.19 | -26.03 | -12.57 | 0.162 | 0.896 | `28c4966f67a70db3` | 5.55 | 8.54 | 2626 | 8 | 21 | 21 |
| p2-amount-automation-held-chord | 5.16 | -30.75 | -16.99 | 0.247 | 1.113 | `d811099f5aa295c4` | 4.7 | 9.21 | 1850 | 4 | 10 | 10 |
| p2-amount-step-held-chord | 5.15 | -30.59 | -16.04 | 0.248 | 1.047 | `38d6f41f99e985c2` | 4.61 | 9.23 | 4259 | 4 | 10 | 10 |
| p2-amount-lfo-held-chord | 5.17 | -30.71 | -17.19 | 0.243 | 1.197 | `1d9c1e70dc3d77e9` | 5.1 | 10.1 | 2737 | 4 | 9 | 9 |
| budget-stress-unlimited | 2.58 | -27.41 | -13.64 | 0.161 | 0.76 | `4e200c8246570948` | 7.27 | 25.77 | 2385 | 8 | 294 | 294 |
| budget-stress-128 | 2.5 | -27.09 | -11.33 | 0.16 | 0.717 | `96b7990f88f3ed10` | 5.49 | 20.24 | 1999 | 8 | 126 | 126 |

Peak resident memory of the renderer: 27.9 MB. InstrumentEngine object: 127568 bytes (InstrumentVoice 20848 bytes each).
