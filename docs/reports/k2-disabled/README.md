# K2 disabled state: design and visual review (Phase 6 C)

**Status:**
- A local pilot, captured headless from the real editor.
- Not committed to any product branch.
- No manual Retina review done yet: these are headless captures at 1× and 2×, not a QA pass.

**Rule:** the disabled state keeps the same construction (body, size, radius, shadow). It changes only:
- **Ink:** lower contrast. The key's own drawing (icon, label, lamp) is drawn at 45 % alpha (`disabledInkAlpha = 0.45`).
- **Illumination:** none. No accent fill and no lit lamp. A set (latent on) lamp shows as an unlit lens with a small ink centre.
- **Interaction:** none. Hover and press are not drawn, and the key face stays in the normal state.

**Patch:** `k2_disabled_pilot.patch` holds the pilot's hunks only:
- `ArpSection.cpp`: the lamp;
- `KeyboardUtilityActions.cpp`: utility keys;
- `CeramicSkin.cpp` / `CeramicSkin.h`: `drawKeys`, which dims a disabled key's ink and holds it at rest.

`pilotForcedKeyState` is capture scaffolding and must not ship.

## Review: the smallest size (869 px) at 1× and 2×, and 1448 at 2×

**Method:** the maximum per-pixel luminance difference between each pair of states, over the key area.

| Pair | 869@1x | 869@2x | 1448@2x | Reading |
|---|---|---|---|---|
| normal → disabled (headers / housing) | 97 / 85 | 97 / 94 | 97 / 94 | clearly distinct |
| real-on → disabled | 173 / 110 | 173 / 119 | 185 / 119 | clearly distinct |
| disabled → disabled-while-on, before the lamp fix | 79 / 34 | 78 / 37 | 83 / 38 | distinct, but the lamp kept a faint orange glow, which breaks "no illumination" |
| disabled → disabled-while-on, after the lamp fix | – / 31 | – / 31 | – / 32 | distinct by the ink centre only. No warm pixels (max R−B = 15–16, the ceramic's own warmth) |
| normal → hover | 5 | 5 | 5 | **hard to see, but this is not K2.** It is the shipped hover cue (a slight lift) and is unchanged here. Flagged for the owner: at 1× hover is nearly invisible |
| pressed → active | 11 | 11 | 11 | subtle. As in Phase 5, the deeper press must never be the only selection cue |

**Images:**
- `k2v2_housing_869@1x.png` (shown at 3×) and `k2v2_housing_869@2x.png`: every state of the ARP / SUB housing keys, after the lamp fix.
- `k2-disabled-keys-headers-*-869@1x.png`: the layer-header keys.

**Open for the owner:**
1. Accept the latent-on ink centre, or show a disabled lamp identically whether set or not.
2. Whether the hover cue (5 levels) should be strengthened. That is a separate ticket, not part of K2.
