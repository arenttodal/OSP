# Note Atlas: memory, preparation, lookup (linux-x86_64-gcc)

Every member owns a copy of its note (attackStart..sourceEnd) as OSP's playback buffers: memory is the members' approximateBytes (playback copies, transients, anchors, REIMAGINED data). Build: region copy, analysis, model and continuation per member, on one thread.

| Atlas | Notes | Region audio s | Member MB | MB per region second | Build ms | ms per note |
|---|---|---|---|---|---|---|
| boundary-probe | 2 | 1 | 0.39 | 0.386 | 129 | 64.5 |
| bowed-phrase | 3 | 1.65 | 1.21 | 0.734 | 436.3 | 145.4 |
| legato | 4 | 1.6 | 1.25 | 0.781 | 330.7 | 82.7 |
| missing-notes | 3 | 2.1 | 1.38 | 0.656 | 358.5 | 119.5 |
| missing-notes:fill-4 | 3 | 2.1 | 1.38 | 0.656 | 351.4 | 117.1 |
| missing-notes:no-fill | 3 | 2.1 | 1.38 | 0.656 | 347.3 | 115.8 |
| out-of-tune | 3 | 2.1 | 1.38 | 0.656 | 366.3 | 122.1 |
| out-of-tune:as-recorded | 3 | 2.1 | 1.38 | 0.656 | 335.3 | 111.8 |
| regions-10 | 10 | 10 | 5.69 | 0.569 | 2677.9 | 267.8 |
| regions-50 | 50 | 50 | 28.46 | 0.569 | 12171.1 | 243.4 |
| repeated-note | 3 | 2.1 | 1.38 | 0.656 | 371.3 | 123.8 |
| silence-gaps | 3 | 2.1 | 0.69 | 0.33 | 262 | 87.3 |
| stereo-wide | 2 | 1.6 | 0.99 | 0.621 | 435.9 | 218 |
| sung-four | 4 | 3.2 | 1.98 | 0.62 | 635.9 | 159 |
| velocity-layers | 4 | 2.4 | 1.69 | 0.704 | 464.3 | 116.1 |

## Scaling with the number of regions

| Regions | Member MB | Build ms | Peak RSS after, MB | Lookup ns (groupFor, per key) |
|---|---|---|---|---|
| 10 | 5.69 | 2677.9 | 100.3 | 10.6 |
| 50 | 28.46 | 12171.1 | 100.3 | 12.5 |
| 200 | 113.84 | 49973.4 | 326.7 | 12.5 |

## Baselines A and B (the melody from the recorded A3 alone)

| Render | RMS dB | Hash |
|---|---|---|
| atlas-melody-baseline-a | -26.37 | `6e8e1c2656c2c512` |
| atlas-melody-baseline-b | -26.56 | `606bd4a045c5bc2c` |
