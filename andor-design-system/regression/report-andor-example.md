# Visual regression: ANDOR Example vs ANDOR OSP

Tolerance: 0 (exact), masks: identity, preset-name, status-line.

| State | Differing pixels outside the masks | Inside the masks (allowed) |
|---|---|---|
| 00-empty | 0 | 4147 |
| 01-one-layer | 0 | 4147 |
| 02-two-layers | 0 | 4147 |
| 03-three-layers | 0 | 4147 |
| 03-three-layers@2x | 0 | 13841 |
| 04-window-869 | 0 | 1826 |
| 04-window-1810 | 0 | 6072 |
| 05-mod-tab-amp | 0 | 4147 |
| 05-mod-tab-env | 0 | 4147 |
| 05-mod-tab-lfo1 | 0 | 4147 |
| 05-mod-tab-lfo2 | 0 | 4147 |
| 05-mod-tab-vel | 0 | 4147 |
| 05-mod-tab-note | 0 | 4147 |
| 07-card-eq | 0 | 4147 |
| 07-card-dampen | 0 | 4147 |
| 07-card-granular | 0 | 4147 |
| 08-keys-held | 0 | 4147 |
| 09-arp-open | 0 | 4147 |

Result: identical outside the masks in every state.
