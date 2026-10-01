# Research corpus

The initial corpus is ~80 recordings (79 WAV, 1 AIFF; 44.1/48 kHz; mostly stereo;
~0.7–28.8 s). It is the internal DSP benchmark and must remain available as a
regression corpus throughout development.

## Where it lives

Copy the files anywhere under `research/corpus/` (sub-folders are fine). Audio files
there are git-ignored: the corpus is local-only (rights and size). Nothing in the
pipeline depends on filenames or folder structure.

```sh
./build/apps/research-renderer/research-renderer --index research/corpus      # writes research/corpus/index.json
./build/apps/research-renderer/research-renderer --corpus research/corpus --profile standard
```

## Identity

A source's identity is the SHA-256 of its bytes (`"sha256:<hex>"`). Renaming or
moving a file does not change it; byte-identical copies are reported as duplicates
and processed once. Report/render folders are named `<sanitised-stem>__<first 8 hex>`
for readability; the id inside the JSON is authoritative.

## Source families (conceptual)

No file is required to carry labels. These families describe what the corpus
contains and what each is good for testing; analysis descriptors (not filenames)
will eventually place sources on these axes with probabilities, never hard classes.

| Family | Examples in the corpus | What it tests |
|---|---|---|
| Repeated performances / round robins | violin RR sets, "Tagel" set, repeated takes | Performance Engine ground truth: what varies between real takes |
| Multi-register plucks | pluck sets at several pitches | Register Engine ground truth; transient handling under transposition |
| Bowed sustains | bowed nyckelharpa, violin | continuation of expressive sustains, bow noise, vibrato |
| Vocal vowels / vocals | vowels, sung notes | formants vs pitch, vibrato, breath; the north-star demo |
| Organ-like sustained material | Trampeorgel | long stable sustains, unstable organ modulation, loopability |
| Synthetic sources | Prophet 6 and other synths | harmonically rich, stable or LFO-modulated material |
| Tremolo / pulsating material | tremolo/tremolando takes | modulation extraction; avoiding cycle-A-A-A loops |
| Bass, saxophone | bass notes, sax | low-frequency pitch tracking; breathy expressive tone |
| Strange / processed | frozen, processed recordings | graceful failure; ambiguous pitch; texture behaviour |

## Optional metadata (later)

If useful, a hand-written `research/corpus/labels.json` may map content ids to notes
such as `{ "family": "bowed", "expectedRoot": "A3", "rrGroup": "violin-A" }`. It is
optional and must never be required by the pipeline.
