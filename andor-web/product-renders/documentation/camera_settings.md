# Cameras

All cameras are defined in `andor_scene.py` (`cams = {...}`). Angles are measured from the face
normal: `pitch` tilts toward the viewer (the instrument's front edge), `yaw` turns around the normal.
Sensor 36 mm, horizontal fit.

| Camera | Status | Lens | Pitch / yaw | Distance | f-stop | Notes |
|---|---|---|---|---|---|---|
| 01 Hero (`hero_9x16`) | **rendered, for approval** | 75 mm | 27° / −24° | 1.12 m | f/9, focus on the face centre | shift y 0.07 places the product low for type above; product turned −9° in its plane |
| 02 Gallery (`gallery`) | defined, not yet tuned | 135 mm | 3° / 0° | 2.6 m | none | near-frontal, minimal distortion |
| 03 Close-up (`closeup`) | defined, not yet tuned | 90 mm | 38° / −24° | 0.42 m | f/4 | aimed at the DRIVE macro knob |

The instrument floats 28 cm above the floor. Depth of field: at f/9 the face (±7 cm in depth) stays
sharp; the floor is softened.
