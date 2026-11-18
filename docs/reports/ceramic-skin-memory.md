# Ceramic skin · resize memory investigation (18 Nov 2026)

Branch `claude/ceramic-skin-memory`, from the default branch `56bab47`.

**Code changes (behaviour unchanged by default):**
- `skin::Pieces` gains instrumentation: hits, misses, evictions, peak bytes, compose time, and the overlay pack's bytes.
- A settable budget: `setBudget`, plus `OSP_SKIN_CACHE_MB` for measuring inside a host. The default stays 192 MB / 160 images.
- An opt-in stale-variant eviction: `beginGeneration` / `evictUnusedSince`.

**Tests:**
- `[plugin][ceramic]`: the new test for budget and eviction passes; all 10 ceramic cases pass, 2091 assertions.
- The measurement is the hidden `[skin-memory]` test. Its raw CSV is `ceramic-skin-memory.csv`.

## Method

- **The real editor:** three layers, painted at 2× through `createComponentSnapshot`.
- **For each budget (192, 128, 96, 64 MB), with and without eviction on settle:**
  - cold open at 1448;
  - 5 warm repaints;
  - a sustained resize: three rounds from 869 to 1810 px in 47 px steps, 63 paints;
  - settle at 1448;
  - revisit 1086 and 1448;
  - 60 repaints while notes play ("normal use");
  - a second editor instance;
  - editor destruction.
- **Memory:** `/proc/self/statm` resident memory, both raw and after `malloc_trim(0)`. The trimmed figure separates live data from glibc's retention of freed heap.
- **Timing caveat:** the machine was shared with renders and builds, so absolute milliseconds are inflated. The relative comparisons hold, since every configuration ran under the same load.

## Results (cache MB, trimmed process MB relative to before the editor opened)

| Budget | Eviction on settle | Cache after sweep | Process after sweep | Cache settled at 1448 | Process settled | Process during normal use | Revisit 1086 recomposed? |
|---|---|---|---|---|---|---|---|
| 192 (current) | no | 182.7 | 279.5 | 169.0 | 260.9 | 281.9 | **yes** (27 misses) |
| 192 | yes | 182.7 | 249.5 | **36.7** | **98.6** | 119.4 | yes |
| 128 | no | 127.2 | 180.6 | 115.8 | 164.3 | 177.2 | yes |
| 128 | yes | 127.2 | 174.4 | 36.7 | 79.0 | 100.3 | yes |
| 96 | no | 79.1 | 129.6 | 94.0 | 139.6 | 126.5 | yes |
| 96 | yes | 79.1 | 125.9 | 36.7 | 78.6 | 99.4 | yes |
| 64 | no | 57.3 | 107.4 | 59.7 | 104.9 | 102.8 | yes |
| 64 | yes | 57.3 | 103.7 | 36.7 | 78.2 | 99.0 | yes |

**The same in every configuration:**
- **Cold open:** 27 composed pieces, 36.7 MB at 2× for one window size; about 380–470 ms composing.
- **Sweep:** 1728 misses and about 24 s of composing over 63 paints, so about 700 ms per paint at a new size.
- **Warm repaint:** about 140 ms. (This is a whole-editor 2× snapshot; the real editor repaints only what changed.)
- **Second instance:** it shares the pieces. One `Pieces` object per process, 2 extra misses.
- **Editor destroyed:** the pieces and their caches are **released** (`Pieces::current() == nullptr`), leaving 30–80 MB of resident memory (the allocator and the rest of the plugin).
- **Overlay pack:** 0 MB in ANDOR OSP. ANALOG's walnut pack keeps a second cache under the same budget, so its worst case is twice this. Not measured here.

## Findings

1. **No leak.** Memory is bounded by the budget and fully released when the last editor closes.
2. **The 192 MB budget buys nothing.**
   - During sustained resizing each size is new, so every piece is composed again at any budget. LRU keeps about 5 sizes at 192 MB and thrashes through the rest.
   - Even a size seen a moment earlier (1086 after the sweep) was composed again at 192 MB.
   - The large budget only adds retained memory: up to +180 MB, and about 280 MB of process memory after resizing.
3. **Dropping stale variants once the resize settles** brings the settled cache to exactly the working set (36.7 MB) at every budget, and process memory to about 80–100 MB.
   - No measured cost: the warm repaint, the revisit and the normal-use paints are unchanged.
   - The only extra work is composing a previous size again if the window returns to it, which already happens today.
4. **What does cost time is composing a new size: about 700 ms per paint at 2×** (inflated here by load). That is a resize-smoothness issue independent of the budget. The skin already has a "moving" path, which stretches the nine-slices without composing; using it during live resize would remove those stalls.

## Recommendation (separate PR, after review)

1. **Budget:** default to 96 MB, about 2.5 window sizes at 2×.
2. **Settle eviction:** in the editor, ~500 ms after the last resize, call `beginGeneration()`, repaint, then `evictUnusedSince(gen)`.
3. **Live resize:** draw ceramic pieces with `moving = true` while the window is being dragged, and compose exact sizes once on settle. This removes the ~700 ms per-size stalls and most of the transient peak.
4. **Re-measure:**
   - in a host on macOS (Logic, Live), with `OSP_SKIN_CACHE_MB`;
   - ANALOG with its overlay pack.

Visual output is unchanged (`[design-capture]` and the visual regressions are unaffected: same pieces, same sizes).
