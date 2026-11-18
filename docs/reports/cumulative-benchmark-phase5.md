# Cumulative performance benchmark, corrected (Phase 5)

**Why it was redone:** the Phase 4 benchmark ran every scenario in one process, one after another. Two of its figures were therefore unreliable:
- its `editors_open` column was a yes/no flag, not a count;
- its resident-memory (RSS) figures mixed in memory left over from earlier scenarios.

This run fixes both.

## Method

| | |
|---|---|
| Build | The trial tree with everything integrated: D2 macros, layer-ivory B, mini-ivory P5, mini-graphite G1, the K2 keys, and the ceramic cache PR (`claude/ceramic-cache-settle`) applied |
| Isolation | **One process per scenario and repetition** (`bench_iso.sh` → `[bench-iso]`), each case run twice. Figures are the mean of the two runs |
| Machine | 4 cores, Linux, Release build. **No competing renders:** the two Blender jobs were suspended (SIGSTOP) for the whole run, and the script refused to start otherwise. Load average 1.1 at the end |
| Skins | `vector` (`OSP_KNOB_SKIN=original`: no rendered caps) and `rendered` (all rendered families). The ceramic surfaces are on in both |
| Instance | `OspAudioProcessor` with three loaded sources (vowel, pluck, vowel), prepared at 48 kHz |
| Editor | 1448 px wide (or as listed), painted at 2× through `createComponentSnapshot` |
| Memory | `VmRSS` and `VmHWM` (peak) from `/proc/self/status`. `malloc_trim(0)` runs before every baseline and after every close |

**Definitions:**

| Term | Meaning |
|---|---|
| *absolute* | RSS |
| *incremental* | Change from the process before any instance existed (about 11 MB: the test binary and JUCE) |
| *editors* | Change from the loaded instances to the same instances with their editors open |
| *cache* | The asset caches' own byte counts |
| *peak* | The process's high-water mark |
| *retained* | What stays after closing, against the state before |

The raw files are `cumulative-benchmark-phase5.csv` here, plus the 40 JSON files in Package C.

**Repeatability:**
- **Memory:** within 2 MB between the two runs in every case except `rendered closed:16` (17.9 MB, below 2 % of 850 MB).
- **Retained after closing 4–32 instances:** varies by up to 16 MB (allocator arenas).
- **First-paint times:** vary by up to 109 ms. The table gives means; the CSV has the spread.

## Results

### Instances with editors closed

| Instances | Vector: RSS / incremental / per instance | Rendered: RSS / incremental / per instance | Retained after closing all (vector / rendered) |
|---|---|---|---|
| 1 | 70.7 / 59.9 / 59.9 MB | 71.0 / 60.1 / 60.1 MB | 6.6 / 7.0 MB |
| 4 | 230.8 / 219.7 / 54.9 | 230.6 / 219.5 / 54.9 | 27.5 / 33.8 |
| 16 | 850.6 / 839.2 / 52.4 | 859.8 / 848.8 / 53.1 | 108.9 / 109.3 |
| 32 | 1665.6 / 1654.5 / 51.7 | 1686.2 / 1674.6 / 52.3 | 79.6 / 122.4 |

- **The rendered skin costs nothing while editors are closed.** Assets are decoded when an editor first draws them, so the per-instance cost (52–60 MB) is the audio engine and its sources.
- **Peak = RSS** in every closed case: loading has no transient overshoot.
- **Retained after closing many instances** (80–122 MB at 32) is glibc arena retention from the instances' threads, not GUI. It is the same with both skins, within the spread.

### Editors open (all at 1448 px)

| Editors | Skin | Editors' RSS | Per editor | Peak | First open / first paint / warm paint | Cache: ceramic / macro / layer / mini | Retained after editors close |
|---|---|---|---|---|---|---|---|
| 1 | vector | 88.8 MB | 88.8 | 235.8 | 41 / 506 / 119 ms | 36.7 / – / – / – MB | 17.0 |
| 1 | rendered | 93.0 | 93.0 | 235.5 | 36 / 547 / 113 | 36.7 / 1.4 / 0.3 / 0.2 | 19.6 |
| 4 | vector | 117.0 | 29.3 | 395.3 | 42 / 504 / 125 | 36.7 | 5.2 |
| 4 | rendered | 121.2 | 30.3 | 395.9 | 55 / 517 / 106 | 36.7 / 1.4 / 0.3 / 0.2 | 7.6 |
| 8 | vector | 144.3 | 18.0 | 608.0 | 57 / 531 / 118 | 36.7 | ≈ 0 (−11, allocator) |
| 8 | rendered | 149.1 | 18.6 | 607.6 | 64 / 596 / 119 | 36.7 / 1.4 / 0.3 / 0.2 | ≈ 0 (−8) |

- **The first editor costs about 89 MB, mostly the 2× paint buffers and the ceramic surfaces** (36.7 MB for one window size). The rendered caps add **4–5 MB** to that.
- **Each further editor at the same size adds about 9 MB.** The ceramic pieces and the knob atlases are shared per process.
- **The first paint takes about 0.5 s:** the ceramic surfaces are composed once per size. Further editors' first paints average about 230–250 ms, because the pieces are already cached.
- **Warm repaints take 105–125 ms** for a full 2× editor snapshot, with no difference between skins.

### Different sizes in one process (three editors)

| Widths | Skin | Editors' RSS | Peak | First paint each | Warm paint | Ceramic cache |
|---|---|---|---|---|---|---|
| 1448, 1448, 1448 | vector | 117.2 MB | 340.9 | 566 / 175 / 185 ms | 118 ms | 36.7 MB |
| 1448, 1448, 1448 | rendered | 121.0 | 342.2 | 540 / 179 / 176 | 122 | 36.7 |
| 869, 1448, 1810 | vector | 280.9 | 533.6 | 351 / 480 / 862 | **467** | **96.0** (budget) |
| 869, 1448, 1810 | rendered | 290.6 | 543.2 | 294 / 505 / 1001 | **566** | **96.0** (budget) |

**The finding that matters:** three editors at three sizes need about 110 MB of ceramic pieces at 2×, more than the 96 MB budget.

**In this benchmark the pieces are evicted, and every repaint composes again.** The editors here are offscreen (no window), so the PR's per-editor claims do not apply and the plain LRU budget evicts. The warm paint rises to 470–570 ms.

**On screen, the cache PR's rule applies:** no piece another open editor still uses is evicted. The cache then holds the ~110 MB, over the soft budget, and repaints stay warm. The PR's test "two open editors of different sizes keep their pieces" covers this.

**Trade-off for review:** the budget is soft while several editors at different sizes are open. It is hard again as soon as editors close or settle to shared sizes.

### Repeated open / resize / close (one instance, 10 cycles)

Each cycle: open at 1448, paint, resize to 1086, 1810 and 1448 (painting each), close.

| Skin | Open + first paint | Paint after each resize | Retained after each close (cycles 1 → 10) | Peak |
|---|---|---|---|---|
| vector | 572 ms | 623 ms | 17.6 → 18.1 MB | 407.7 MB |
| rendered | 572 ms | 623 ms | 21.9 → 22.5 MB | 413.8 MB |

- **No growth:** +0.5–0.6 MB over ten cycles is allocator noise. Everything an editor creates is released when it closes.
- **About 620 ms per resize paint** is composing a new size. On screen, the cache PR stretches during a live drag and composes once after settling (see the PR's settle measurement).

### Mini-graphite G1

- **In the build:** packs at 48, 96 and 180 px; decoded at most 1.1 / 4.5 / 15.9 MB per size, frames decoded on demand.
- **Not drawn in these scenarios:** its knobs show only in a granular well under the pointer, and no state here has one open, so it adds 0 MB.
- **Where its cost shows:** while a granular well is hovered, at the level of the mini-ivory atlas (0.2–0.5 MB measured at these sizes).

## What changed from the Phase 4 figures

The Phase 4 run attributed accumulated memory to later scenarios. With one process per scenario:
- the GUI's own cost is small and bounded: about 89–93 MB for the first editor, about 9 MB per further editor at the same size;
- the rendered caps cost 4–5 MB;
- instance memory is the engine's: 52–60 MB each.

**Still open:** the same measurements inside a host on macOS (Logic, Live), with real windows. That is part of the manual QA and has not been run.
