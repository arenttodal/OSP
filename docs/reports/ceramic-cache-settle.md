# Ceramic skin cache · settle eviction, per-editor claims and live-resize stretching (Phase 5)

**Branch:** `claude/ceramic-cache-settle`, on `claude/ceramic-skin-memory`.

**What it implements:** the recommendation of `ceramic-skin-memory.md`, approved in review:

| | What it does |
|---|---|
| **Budget** | 96 MB (was 192 MB / 160 images) |
| **Live resize** | While an editor's window is being resized, its ceramic pieces are stretched from the composition at its last settled scale, through the nine-slice "moving" path. No piece is composed for a size in passing |
| **Settle** | 500 ms after the editor's last resize (`EditorCacheScope`, a `juce::Timer`), the editor starts a generation, repaints at the exact size, then drops its claim on every piece it did not draw again |
| **Several editors** | Every piece records which open editors drew it and in which generation (`Entry::users`). A piece is evicted only when **no** open editor still claims it. The LRU budget never evicts a claimed piece, so several editors at different sizes keep their pieces |
| **Editor closed** | Its claims are released, and pieces no other editor uses are evicted |

**Only on-screen editors** (with a peer) use stretching and claims. Snapshots, headless tests and the visual regressions draw exactly as before, so `[design-capture]` and every product's visual regression are unchanged.

**Wiring:**
- `OspAudioProcessorEditor` and `ShellEditor` own an `EditorCacheScope`.
- `paint()` calls `beginPaint()`; `paintOverChildren()` calls `endPaint()`; `resized()` calls `resized()`.

## Tests

- **`[plugin][ceramic]`:** 11 cases, 2103 assertions. They include:
  - the budget at 96 MB;
  - "two open editors of different sizes keep their pieces when one resizes and settles".
- **Full plugin ctest (Linux, Release):**

| Group | Result | Time |
|---|---|---|
| plugin | passed | 1823 s |
| osp_visual_reference | passed | 14 s |
| andor_example_tests | passed | 3 s |
| andor_example_tests_visual | passed | 25 s |

## Measurement (`[skin-settle]`, `ceramic-cache-settle.csv`)

**Setup:**
- One editor, three layers, painted at 2×.
- Open at 1448 px, then a live-resize sweep: three rounds from 869 to 1810 px in 47 px steps, 63 paints. Then settle at 1448.
- **Run twice,** with no other jobs running: the machine's renders were suspended for the measurement.

**Three modes:**
- **`lru`:** the cache before this change. No claims, the plain budget, every passing size composed.
- **`compose-every-size`:** a stress case. Claims on, but the editor never settles, so nothing may be evicted.
- **`settle`:** this change.

| | lru (before) | settle (this PR) |
|---|---|---|
| Sweep: average / worst paint | 461–471 / 740–850 ms | **174–187 / 394–437 ms** |
| Sweep: pieces composed, compose time | 1197, 18.6–18.8 s | **0, 0 ms** |
| Sweep: cache, peak cache | 64 MB, 96 MB | **32.4 MB, 32.4 MB** |
| Sweep: process (trimmed, relative to before the editor opened) | +133–135 MB | **+69 MB** |
| Settled at 1448: paint, pieces composed | 387–518 ms, 19 (1448 had been evicted) | **138–150 ms, 0** |
| Settled: cache / process | 93.5 MB / +184–187 MB | **32.4 MB / +92 MB** |
| After the editor closed (process) | +84–87 MB | +52 MB |
| Stretched frame vs exact frame at 1448 | – | mean 0.09 levels, max 192 at the edges of stretched pieces (shown only while dragging, replaced on settle) |

**The stress case:**
- `compose-every-size` with claims and no settle grows to 641 MB.
- That path does not occur in the editor: every `resized()` puts the editor in the stretching state, and the settle step releases the sizes passed through.
- It documents why claims must be released on settle.

**Several open editors:** see the cumulative benchmark (`docs/reports/cumulative-benchmark-phase5.md` on the marketing branch).
- Three editors at 869 / 1448 / 1810 need about 110 MB of pieces at 2×, more than the 96 MB budget.
- On screen, claims keep them, so the budget is soft while such editors are open.
- Offscreen (no claims), the LRU evicts, and each repaint composes again.

## Known limits

- **A display-scale change without a resize** (dragging the window to a 1× display): the pieces of the previous scale stay claimed until the next resize or close. That is at most one extra window's set (about 37 MB at 2×), not an accumulation. Starting a generation on a scale change would remove it.
- **Not yet measured:**
  - inside a host on macOS with real windows (Logic, Live);
  - ANALOG with its overlay pack.
  Both are part of the manual QA.
