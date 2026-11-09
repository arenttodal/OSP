# Decisions and checkpoints

## Checkpoints

| Gate | State | Evidence |
|---|---|---|
| G0 Foundation | **Done, with one blocker** (no recordings) | `docs/PHASE0_AUDIT.md` |
| G1 Visual direction | **Done**: Gallery C, no serif, B's header, three layers | `docs/HERO_CONCEPTS.md` rounds 2–3 |
| H Hero approval | **Approved: E (open bubbles)**, 9 Nov 2026 | `g5.html`, `prototype/osp/Hero.tsx` |
| Landing page | **Built, awaiting review** | `osp.html`, `docs/PAGE.md` |

## Decisions made autonomously (routine)

| ID | Decision | Why |
|---|---|---|
| D-01 | Website code lives in `andor-web/` in the OSP repo on `claude/friendly-pascal-0y48r0` | This session can't create repos; isolated from DSP code; move later (B7) |
| D-02 | Captures come from a new hidden test `[web-capture]`, not from older repo screenshots | Older images predate SYNTH/IMPRINT; the test makes every asset reproducible at a known commit |
| D-03 | Masters not committed; optimized AVIF/WebP are committed | Keeps the repo small; masters regenerate from commit + script |
| D-05 | Token refinements: `--text-2`, `--text-muted` darkened; `--accent-strong`, `--text-quiet`, dark muted/quiet/accent added | Measured AA failures in the starting tokens |
| D-06 | Fonts self-hosted via @fontsource; Geist Mono added for technical labels | Owner references; no third-party font requests; free licences (OFL) |
| D-07 | Prototypes built in React + Vite as Framer-portable code; Framer layout is the later implementation | No Framer access from this environment (see Q-02) |
| D-08 | Audio player built on Web Audio with all variants started on one clock; gain crossfade on switch | Guarantees sample-aligned switching with position preserved; memory ≈ 20 MB for three 18 s stereo files, acceptable on mobile, re-measure with final files |
| D-09 | Hero player shows an honest pending state instead of any placeholder audio | Plan: audio controls must play real recordings; none exist |

## Decisions that need the owner

| ID | Question | Recommendation |
|---|---|---|
| **A-01** | Provide 1–3 owned source recordings (nyckelharpa note first) | Required for any audio, and to replace stand-in waveforms in captures |
| **D-04** | Accept **studio renders** (`object-*`) as product imagery? The face is the exact capture; the sides use the capture's own teak; nothing else is added. It does present the software as a physical slab. | Yes for the hero, labelled internally as "presentation render", never as hardware. If you say no, H1 falls back to the straight capture with perspective. |
| **Q-02** | Framer: this cloud session cannot open framer.com (and editing needs your signed-in browser). Options: (a) after hero approval, I produce the Framer build guide + code components and you or the Chrome extension apply them; (b) allow framer.com hosts here and grant a Framer API/plugin route. | (a) — keeps your account credentials out of this environment |
| Q-03 | Söhne licence instead of Inter Tight? | Not needed; Inter Tight passes rendering tests. Revisit only if you want the premium cut |
| Q-04 | Waitlist provider (MailerLite is connected to this workspace; no account changes made) | Decide at Phase 7 |
