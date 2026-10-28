# Unified-engine scenes (linux-x86_64-gcc)

| Scene | Seconds | RMS dB | Peak dB | Brightness | Width | Hash | Mean block % | Worst block % | Worst seen µs | Peak voices | Peak grains | Live grains |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| atlas-oneshot-melody | 4.38 | -37.21 | -23.61 | 0.129 | 1.231 | `142b352c08cef698` | 1.4 | 2.62 | 2981 | 3 | 0 | 0 |
| atlas-oneshot-chord | 2.58 | -32.92 | -15.95 | 0.126 | 1.149 | `2c36a971f80e172d` | 1.24 | 2.66 | 508 | 4 | 0 | 0 |
| atlas-granular-chord | 2.23 | -31.23 | -17.76 | 0.13 | 0.674 | `828de3b6b42b14c9` | 1.41 | 2.93 | 2387 | 4 | 9 | 9 |
| atlas-granular-keydna-scale | 2.47 | -37.22 | -24.09 | 0.113 | 0.827 | `b2917aa57d603041` | 1.15 | 3.34 | 718 | 2 | 6 | 6 |
| atlas-granular-keydna-chord8 | 2.27 | -26.09 | -13.3 | 0.125 | 0.749 | `fbf6a24c839d68a7` | 1.73 | 3.48 | 358 | 6 | 25 | 25 |
| atlas-three-abc-chord | 2.58 | -31.28 | -16.47 | 0.15 | 0.59 | `7153ac3c3f10eeb1` | 2.29 | 7.31 | 581 | 4 | 12 | 12 |
| atlas-polyphony-24 | 3.46 | -28.88 | -11.85 | 0.118 | 1.018 | `3b7acc90fa348ec6` | 2.51 | 10.26 | 4329 | 24 | 0 | 0 |
| atlas-polyphony-24-granular | 3.14 | -25.61 | -11.85 | 0.121 | 0.777 | `3553f014d72502b5` | 4.15 | 12.67 | 4636 | 24 | 63 | 63 |
| atlas-rapid-repeats | 2.48 | -42.15 | -28.58 | 0.154 | 1.151 | `32ed9b5d802610ac` | 1.24 | 2.06 | 358 | 2 | 0 | 0 |
| atlas-velocity-layers | 3.1 | -33.16 | -20.34 | 0.138 | 2.265 | `aaa0d54dfbe9b51` | 1.22 | 2.42 | 1978 | 2 | 0 | 0 |
| atlas-out-of-tune-corrected | 3.4 | -34.19 | -21.69 | 0.127 | 0.764 | `f8416f0b5d70d9ba` | 1.17 | 2.41 | 238 | 3 | 0 | 0 |
| atlas-out-of-tune-as-recorded | 3.4 | -34.12 | -20.62 | 0.127 | 1.011 | `2c617321f7a7016f` | 1.16 | 2.13 | 286 | 3 | 0 | 0 |
| atlas-fill-12 | 4.01 | -36.56 | -23.9 | 0.17 | 0.242 | `f88d515c222dc7e2` | 1.66 | 3.86 | 2250 | 5 | 0 | 0 |
| atlas-fill-4 | 3.79 | -38.81 | -25.35 | 0.146 | 0.263 | `4a79cdb7317b2446` | 1.36 | 3.29 | 213 | 5 | 0 | 0 |
| atlas-no-fill | 3.79 | -46.23 | -25.62 | 0.139 | 0.248 | `e33a53a81face5d` | 0.98 | 2.01 | 234 | 1 | 0 | 0 |
| atlas-legato-line | 1.94 | -32.04 | -21.99 | 0.128 | 1.984 | `a47c9b1b31999b7e` | 1.09 | 2.35 | 1002 | 2 | 0 | 0 |
| atlas-silence-gaps-scale | 2.68 | -42.89 | -30.96 | 0.26 | 0.014 | `4d8bf355ddfb082b` | 1.23 | 2.81 | 925 | 3 | 0 | 0 |
| atlas-stereo-wide-granular | 2.14 | -32.03 | -20.15 | 0.151 | 1.244 | `435f9f4198d44e1c` | 1.19 | 1.94 | 698 | 2 | 6 | 6 |
| atlas-boundary-granular | 2.33 | -34.26 | -26.44 | 0.057 | 0 | `22be61c93f75effb` | 0.29 | 0.93 | 112 | 1 | 5 | 5 |
| atlas-regions-10-chord8 | 2.58 | -23.99 | -10.58 | 0.134 | 1.285 | `3c70f3396d7b6282` | 2.42 | 6.85 | 1079 | 8 | 0 | 0 |
| atlas-regions-50-chord8 | 2.58 | -24.94 | -10.51 | 0.145 | 1.221 | `b66488f7965bc028` | 2.38 | 6.64 | 401 | 8 | 0 | 0 |
| atlas-publication-while-rendering | 3.82 | -33.89 | -17.84 | 0.139 | 1.149 | `2c49b0ebd8e2655e` | 1.4 | 3.68 | 1157 | 6 | 0 | 0 |

Peak resident memory of the renderer: 326.7 MB. InstrumentEngine object: 127584 bytes (InstrumentVoice 20880 bytes each).
