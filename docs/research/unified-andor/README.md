# Unified ANDOR source engine — research, then implementation

Feasibility study for bringing ANDOR Grain (KEY DNA) and ANDOR Phrase (Note Atlas) into
ANDOR OSP's A/B/C sources. Start with **[11-decision-brief.md](11-decision-brief.md)**.

| Doc | Contents |
|---|---|
| [01-current-osp-audit.md](01-current-osp-audit.md) | what exists now, with file:line; current DSP flow |
| [02-grain-comparison.md](02-grain-comparison.md) | ANDOR Grain vs OSP's Granulator; reuse verdicts |
| [03-phrase-comparison.md](03-phrase-comparison.md) | ANDOR Phrase vs OSP; overlap; reuse verdicts |
| [04-unified-architecture-options.md](04-unified-architecture-options.md) | comparison table, architectures A/B/C, recommendation, module and ownership diagrams |
| [05-granulator-evolution.md](05-granulator-evolution.md) | extend vs replace vs share; static vs KEY DNA; legacy rules; Natural ↔ Grain; budget |
| [06-note-atlas-integration.md](06-note-atlas-integration.md) | atlas → `InstrumentSet`; region selection; lifecycle |
| [07-modulation-and-key-dna.md](07-modulation-and-key-dna.md) | NOTE vs KEY DNA; evaluation sequence; curve editor |
| [08-gui-proposals.md](08-gui-proposals.md) | space budget, six annotated mockups on real screenshots |
| [09-performance-and-risks.md](09-performance-and-risks.md) | benchmarks (OSP scratch harness + lab harness, same VM), memory, risks |
| [10-implementation-roadmap.md](10-implementation-roadmap.md) | phases 0-8 with goals, files, tests, acceptance |
| [phase-0-baseline.md](phase-0-baseline.md) | **Phase 0 (done):** the baseline harness, the PRNG streams, the baseline scenes and their costs |
| [phase-1-key-dna.md](phase-1-key-dna.md) | **Phase 1 (done, approved):** KEY DNA in OSP's granulator, evaluation order, compatibility, tests, measurements, limitations, the updated roadmap |
| [phase-2-report.md](phase-2-report.md) | **Phase 2 (done, approved):** live KEY DNA AMOUNT, SCATTER / PAN / WINDOW, the grain budget, KEY DNA in the plugin (state 13) |
| [phase-3-report.md](phase-3-report.md) | **Phase 3 (done, for review):** the Note Atlas as an OSP instrument: model, conversion, FILL, memory, publication, state 14 and the asset policy, the bounded grain budget, tests, costs |
| `bench/` | `bench_osp.cpp` (scratch harness, not built by CMake) and raw outputs |
| `mockups/` | annotated screenshots and the script that draws them |

Reproduce the OSP benchmark:
`g++ -std=c++20 -O3 -DNDEBUG -Isrc docs/research/unified-andor/bench/bench_osp.cpp build/libosp_dsp.a -o bench_osp && ./bench_osp 256 6`

Prototype code: `git fetch origin andor-lab && git archive origin/andor-lab | tar -x -C <dir>`
(read it without switching branches).
