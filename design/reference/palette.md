# Palette sampled from the references

Median of 5 x 5 px at each point of `main-2-layer.png` / `popup-space.png` (1448 x 1086).
These are the starting values of `apps/plugin/Source/Design.h`; tune them only against
an overlay, never by eye.

| Role | Sampled | Where |
|---|---|---|
| Backdrop (around the housing) | `#d6ccc2` top-left -> `#e3dacd` bottom-right | corners |
| Housing | `#e5ded0` top -> `#d8cfc2` bottom; `#e9e3d5` lit left edge | header, gaps, footer |
| Card face (sources) | `#f3ebe2` top -> `#f0e9de` middle -> `#e6dfd3` bottom | card A / B |
| Panel face (blend, macros) | `#f0e9de` -> `#e7e2d6`; macros `#eee7dc` -> `#dbd1c5` | bands |
| Card top highlight | `#fffff4` (1 px) | top edge of every card |
| Card edge shadow | `#c7c0b2` - `#cac3b5` (1 px under the bottom edge) | |
| Waveform well A | `#1d2223` top, `#242625` body | |
| Waveform well B | `#1b2020` top, `#20292a` body | |
| Envelope display | `#212526` | |
| Popup visual well | `#1c2323` -> `#1f272a` | SPACE |
| Popup face | `#efe8dd` -> `#e5dccf` | SPACE popup |
| Primary text | `#262421` | titles, C4, values |
| Secondary text | `#5f5a53` (labels ~60 %) | ORIGINAL, captions |
| Micro text | `#8c867d` (~45 %) | Ready - 7 notes, time ticks |
| Layer A badge | `#e47a40` top -> `#d1672d` -> `#9d3a07` bottom | |
| Layer B badge | `#7d93a6` top -> `#63798b` -> `#2f4358` bottom | |
| LED A / B | `#feaf61` / `#9fcdf1` (glowing centres) | header dots |
| Accent coral (global live state) | `#f15c22` top -> `#d14213` bottom (active tab), arcs `#e8732a` | |
| Blend track | `#f78031` (A) -> `#6da7cc` (B), through a dusky middle | |
| Reimagined track | `#9e9990` (2 px) | |
| Thumb centres | A `#ef8640`, B `#69a9d5` | |
| Knob cap | `#efe8db` top -> `#d0c7b8` bottom; rim `#bcaf9d` | |
| Macro LED | lit `#ffbb75`, dim `#db6828` | under the knobs |
| White key / black key | `#f3eee4` / `#2b2b2b` | |
| Keyboard bed | `#ece5d7` | |
| Pitch / mod wheel | `#f99354` (lit amber) / `#222c33` with blue `#5b9bd0` light | |
| Button face | `#f1ede5`; active fill `#efab93`, active border `#e05a2a` | modifiers |
| Envelope fill | `#533c2f` (under the orange line `#e8732a`) | |
