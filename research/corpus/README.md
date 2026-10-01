# research/corpus

Put the research corpus here (WAV/AIFF, any file names, any sub-folders). Audio files
in this folder are **git-ignored** — the corpus stays local. See `docs/corpus.md`.

```sh
./build/apps/research-renderer/research-renderer --index research/corpus
./build/apps/research-renderer/research-renderer --corpus research/corpus --profile standard
```
