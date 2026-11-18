# ANDOR — Blender Motion Library
Version 1.0 · 18 November 2026 · Production brief for Gate D
Companion: [Launch creative playbook](andor-launch-playbook.md).

## Objective
Create eight exceptional, reusable product-cinematography clips from the approved ANDOR OSP Blender master. They are building blocks for One Sound, Made From This, From Sound to Instrument, paid ads and the Framer website. Do not produce complete all-CGI ads. No baked typography or music. The library must remain editable and reusable for future ANDOR products.

## Locked foundation
Use the approved C3 physical model and campaign configurations. Preserve G macro knobs (0.30 H/D), 14.4 mm timber cheeks, refined ivory body, individual key fronts, compact chassis, full-height stationary PITCH/MOD touch strips (2% crown, 0.4 mm bezel), approved materials and genuine current UI. Do not reopen geometry or global materials. Verify current editor capture before final output; do not advertise non-shipping features. M06 lighting correction is local to its shot. Preserve approved A1-R1/W, A2 and B1/B2 R4 looks.

## Shot library
All timings and lens suggestions below are initial art-direction targets, to be judged in motion. Aim for 5–10 seconds per clip with usable handles before and after the essential movement. Keep movement purposeful, smooth, restrained and cinematic.

| ID | Shot | Starting camera direction | Motion and focus | Use |
|---|---|---|---|---|
| D01 | Gallery reveal | Light A1/A2 three-quarter, 65–100 mm equivalent | Slow push or shallow lateral arc; entire instrument resolves into sharp hero composition | Website hero, flagship reveal |
| D02 | Dark orbit | B1 R4 architectural scene, 65–85 mm | Shallow orbit, approximately 8–15 degrees; consistent floating shadow; no spin | Paid ads, closing shot |
| D03 | Macro glide | Low oblique macro view, 85–120 mm | Track along 2–3 fluted macro knobs; controlled focus and real highlights | Material montage, feature intro |
| D04 | Inside the sound | Close to actual waveform display, 85–120 mm | Slow lateral movement across glass/bezel; preserve real waveform content | Sound transformation transitions |
| D05 | Keybed | Low front three-quarter, 85–120 mm | Track along key fronts; individual keys remain readable; stationary unless driven by genuine captured MIDI | Performance match cut |
| D06 | Material | Timber/ceramic corner, 90–120 mm | Short slider move and subtle changing highlight; reveal grain and precise junction | Design story |
| D07 | Expression | Corrected M06 framing, 85–120 mm | Gentle move across PITCH/MOD; orange indication remains accurate, both full-height touch surfaces visible | Expressive controls |
| D08 | Interface | Near-frontal perspective to matched orthographic view | Slow controlled alignment into real editor capture; match landmarks at transition | Product walkthrough |

Lens figures are starting equivalents, not mandatory Blender camera values; verify actual sensor size/framing. Reject wide-angle distortions, dramatic full rotations, artificial particles, disassembly or fictional interface movement.

## Gate D1 — Do only three previews first
1. D01 Gallery reveal: 6–8s. Compose for the light editorial site; prepare a static opening/end poster and test a portrait reframe.
2. D02 Dark orbit: 6–8s. Use B1 R4 with B2 +15% as the vertical composition reference. Render physically correct dynamic shadows. Do not move the product independently of an existing shadow pass containing its silhouette.
3. D03 Macro glide: 5–7s. Use M01 look. Fine fluting, bevel, pointer and cap must hold sharp at intended viewing distance. Avoid focus blur that conceals important physical detail.

Deliver an editable .blend or reproducible scene setup, camera curves/settings, opening/middle/closing frames, and actual playable MP4 previews. Draft 960–1280 px wide, 30 fps, with a representative high-quality still for each shot. Provide contact sheet and short rationale. Stop for creative review before completing the other five shots or rendering full-resolution batches.

## Camera, lighting and motion rules
- Work from the approved product master; link or instance shared product collection rather than creating divergent copies.
- Separate product, cameras, environments, lights, shadow catchers and compositing collections. Use named shots and versioned configs.
- Camera moves should have gentle acceleration/deceleration; preserve intentional parallax. Avoid large lens shifts or sudden speed ramps.
- Use fixed or smoothly animated focus; keep the relevant detail sharp. Render sharp feature-oriented alternatives only when justified.
- Preserve approved material response, colour management, controlled white-on-white definition and wood appearance.
- Use physical lighting/shadows. Avoid artificial glow and heavy postprocessed shadow bands. Check for flicker, denoising instability, texture crawling and highlight clipping across frames.
- Where the product is floating, motion must keep shadow/reflection physically consistent. Use frame-specific rendering rather than sliding static shadow assets.
- No fictitious animated controls, notes or waveforms. Static states are fine. For authentic animation, use recorded parameter/MIDI/screen data and document sync; otherwise cut to real screen capture.

## Gate D2 — Complete approved library
After D1 review, finish D04–D08 and refine the chosen first three. Use shot IDs consistently across formats. Create separate compositions for desktop and vertical (not automatic crops), with deliberate action-safe areas and typography-safe areas. Preserve room for live copy, especially on mobile. Review actual footage at phone size and full resolution.

## Gate D3 — Final production and export
After shot approval, render frame sequences for archival masters (lossless PNG or EXR according to compositing needs), including handles; keep frame sequences resumable. Suggested final 16:9 3840×2160 and dedicated vertical 2160×3840 masters for selected shots; choose 30 fps as initial common rate, or use the approved music/film editing timeline rate consistently. Deliver high-quality editing intermediates (e.g. ProRes 422 HQ; ProRes 4444 when alpha is required) and H.264 MP4 web/social derivatives. Provide appropriate modern web codecs where tested with the website. Do not require every shot in every ratio: prioritize the actual editor and website needs. No audio or titles baked in. Short silent website loops need an intentional seam or designed playback behavior, poster and reduced-motion fallback. Web exports should be optimized only after inspecting real output quality and playback performance.

## Gate D4 — Repeatable workflow
Build a versioned manifest for each shot: ID, purpose, scene/version, product build, camera, focal length, frame range, fps, focus curve, light rig, output dimensions, colour space, alpha status, render settings, file locations, approval state. Save scene/config, render scripts, stills and export settings. Keep bulky media in designated asset storage, not ordinary Git. Commit only docs, configs, scripts and manageable review assets. Never overwrite approved masters.

Suggested structure:
```
marketing/motion/
  configs/
  manifests/
  review/
  scripts/
  # scene and large render paths recorded in manifests
```
Use existing repo conventions if different. Separate work from GUI/runtime branches and do not modify DSP or actual editor controls.

## Acceptance criteria
- Product model remains faithful to actual ANDOR.
- Every shot has a clear photographic purpose and can be reused in different edits.
- Motion feels smooth, quiet and expensive, not generic CGI.
- No detail flicker, unstable denoising, texture crawling, alpha fringes or inconsistent shadows.
- Actual UI graphics are sharp where relevant; no invented interactions.
- Desktop and mobile assets work in real compositions and maintain readable typography space.
- All outputs are reproducible and documented; no publishing or full production batch without approval.

## Immediate agent instruction
Read the launch playbook and this brief. Inspect the current C3 scenes and asset manifest, confirm the approved master, initialize the motion library using the existing Blender pipeline, and complete Gate D1 previews D01–D03 only. Report scene versions, camera/lighting decisions, known limitations and playable previews. Preserve all approved stills and the physical master. Continue independent GUI asset work separately.
