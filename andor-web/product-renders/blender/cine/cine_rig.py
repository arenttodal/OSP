"""ANDOR cinematography rig (Cinematic Direction 2.0): directed shots from JSON shot specs.

  <bpy python> cine/cine_rig.py -- --spec cine/shots/R01_A.json --out <dir>
      [--width 768] [--samples 6] [--noise 0.05] [--pass beauty|matte|clean]
      [--frames A B] [--still SECONDS --still-width 1920 --still-samples 128]

The approved master (geometry, materials, environment, lights) is built by scripts/andor_scene.py
from the spec's `setup` and `base_shot`; this rig then replaces the generic orbit with directed,
independently animated channels:

  camera.position / camera.target   product-frame metres (x along the keys, y toward the back,
                                     z up; origin at the product's centre at rest). The camera
                                     looks at its target with world-up (no accidental roll);
                                     an explicit `roll` channel exists for intent only.
  camera.lens / camera.fstop / camera.focus   focal length (mm), aperture, focus point (product
                                     frame) - independent of the target.
  product.tilt / product.turn        degrees about the product's own x (back edge up: toward the
                                     viewer) and z axes, about `product.pivot`; product.translate
                                     (product frame, metres). The whole instrument moves as one
                                     body; floor shadows and reflections are rendered every frame.
  lights.<name>.energy               a multiplier of the setup's energy; .orbit (deg about the
                                     vertical through the product centre); .translate (metres).
  cards[]                            reflection cards: emissive planes seen only in reflections.
  exposure                           an offset (stops) on the setup's exposure.
  world / emission                   multipliers of the world light and of the instrument's
                                     self-lit surfaces (displays, LEDs).

Every channel is a list of keys [t_seconds, value, easing-to-next] (value: number or [x, y, z]).
Easing: linear, smooth, in_out (cubic), out (cubic), settle (quintic out), in (cubic), hold, or
a CSS-style cubic bezier [x1, y1, x2, y2]. Keys are evaluated per frame here (not Blender
f-curves), so each shot has its own velocity curves and handles hold the end poses.

Passes (same animation, deterministic): beauty (the master), matte (product alpha: environment
held out, film transparent) and clean (the environment without the product: shadow ratio).
"""
import argparse
import json
import math
import os
import runpy
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPTS = os.path.join(HERE, "..", "scripts")
argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:]
ap = argparse.ArgumentParser()
ap.add_argument("--spec", required=True)
ap.add_argument("--out", required=True)
ap.add_argument("--width", type=int, default=768)
ap.add_argument("--samples", type=int, default=6)
ap.add_argument("--noise", type=float, default=0.05)
ap.add_argument("--pass", dest="pass_", default="beauty", choices=["beauty", "matte", "clean"])
ap.add_argument("--frames", type=int, nargs=2, default=None)
ap.add_argument("--keyframes", action="store_true", help="render the opening, 1/4, 1/2, 3/4 and closing frames of the move")
ap.add_argument("--still", type=float, default=None, help="render one frame at this time (seconds into the move)")
ap.add_argument("--still-width", type=int, default=1920)
ap.add_argument("--still-samples", type=int, default=128)
ap.add_argument("--still-noise", type=float, default=0.01)
ap.add_argument("--textures", default=None, help="the interface texture directory (default: andor_scene.py's, the release interface)")
ap.add_argument("--device", default="CPU", choices=["CPU", "CUDA", "OPTIX", "HIP", "METAL", "ONEAPI"],
                help="Cycles device (GPU benchmark: the machine's backend; falls back to CPU, and says so, if none is found)")
ap.add_argument("--final", action="store_true", help="a final-quality render: refuse unless the textures match their FROZEN.json")
ap.add_argument("--depth", default="auto", choices=["auto", "8", "16"], help="PNG bit depth; auto: 8 for draft sizes (< 1920 px), else 16")
args = ap.parse_args(argv)
spec = json.load(open(args.spec))

FPS = spec.get("fps", 24)
DUR = float(spec["seconds"])
HANDLE = float(spec.get("handles", 1.0))
N = int(round((DUR + 2 * HANDLE) * FPS))
aspect = spec["aspect"]   # [w, h]
width = args.still_width if args.still is not None else args.width
res = [width, 2 * round(width * aspect[1] / aspect[0] / 2)]
samples = args.still_samples if args.still is not None else (4 if args.pass_ == "matte" else args.samples)
noise = args.still_noise if args.still is not None else args.noise

sys.argv = ["andor_scene.py", "--", "--setup", spec["setup"], "--shot", spec["base_shot"], "--res", str(res[0]), str(res[1]),
            "--samples", str(samples), "--noise", str(noise)] + (["--textures", args.textures] if args.textures else [])
g = runpy.run_path(os.path.join(SCRIPTS, "andor_scene.py"), run_name="__main__")


def frozen_check(tex):
    """True when tex/FROZEN.json exists and every listed file matches its checksum."""
    import hashlib
    mp = os.path.join(tex, "FROZEN.json")
    if not os.path.exists(mp):
        return False, "no FROZEN.json"
    man = json.load(open(mp))
    for f, h in man["files"].items():
        if hashlib.sha256(open(os.path.join(tex, f), "rb").read()).hexdigest() != h:
            return False, f + " does not match FROZEN.json"
    return True, man.get("approved_commit", "")


FROZEN_OK, FROZEN_NOTE = frozen_check(g["TEX"])
# Final-quality work (asked for, or still-size and still-quality) is only ever rendered from the frozen,
# approved interface: old interface graphics must never reach an expensive final asset.
if (args.final or (res[0] >= 1920 and samples >= 96)) and not FROZEN_OK:
    raise SystemExit(f"cine: refusing a final-quality render: interface textures not frozen ({FROZEN_NOTE}): {g['TEX']}")
bpy, Vector, Euler = g["bpy"], g["Vector"], g["Euler"]
from mathutils import Matrix, Quaternion  # noqa: E402
scene = bpy.context.scene
scene.render.fps, scene.render.fps_base = FPS, 1.0
scene.cycles.seed, scene.cycles.use_animated_seed = 0, False
if args.device != "CPU":
    prefs = bpy.context.preferences.addons["cycles"].preferences
    prefs.compute_device_type = args.device
    prefs.get_devices()
    used = [d for d in prefs.devices if d.type == args.device]
    for d in prefs.devices:
        d.use = d.type == args.device
    scene.cycles.device = "GPU" if used else "CPU"
    DEVICE_NAMES = [d.name for d in used] or ["(no %s device found: rendered on CPU)" % args.device]
else:
    import platform
    DEVICE_NAMES = [platform.processor() or platform.machine()]
scene.render.image_settings.color_depth = args.depth if args.depth != "auto" else ("8" if res[0] < 1920 else "16")
ROT = math.radians(g["ROT"])
Q_ROT = Quaternion((0, 0, 1), ROT)

# ---- easing and keys -------------------------------------------------------------------------
def bezier(x1, y1, x2, y2):
    def f(u):
        lo, hi = 0.0, 1.0   # solve x(s) = u by bisection, return y(s)
        for _ in range(40):
            s = 0.5 * (lo + hi)
            x = 3 * (1 - s) ** 2 * s * x1 + 3 * (1 - s) * s * s * x2 + s ** 3
            lo, hi = (s, hi) if x < u else (lo, s)
        s = 0.5 * (lo + hi)
        return 3 * (1 - s) ** 2 * s * y1 + 3 * (1 - s) * s * s * y2 + s ** 3
    return f


EASE = {"linear": lambda u: u, "smooth": lambda u: u * u * (3 - 2 * u), "in": lambda u: u ** 3,
        "out": lambda u: 1 - (1 - u) ** 3, "in_out": lambda u: 4 * u ** 3 if u < 0.5 else 1 - (-2 * u + 2) ** 3 / 2,
        "settle": lambda u: 1 - (1 - u) ** 5, "hold": lambda u: 0.0}


def ev(keys, t, default=None):
    if keys is None:
        return default
    if not isinstance(keys, list) or not keys or not isinstance(keys[0], list):
        return keys   # a constant
    if t <= keys[0][0]:
        return keys[0][1]
    for k0, k1 in zip(keys, keys[1:]):
        if k0[0] <= t <= k1[0]:
            e = k0[2] if len(k0) > 2 else "smooth"
            fn = bezier(*e) if isinstance(e, list) else EASE[e]
            u = fn((t - k0[0]) / max(1e-9, k1[0] - k0[0]))
            a, b = k0[1], k1[1]
            return [x + (y - x) * u for x, y in zip(a, b)] if isinstance(a, list) else a + (b - a) * u
    return keys[-1][1]


# ---- the product frame --------------------------------------------------------------------------
root = bpy.data.objects["OSP"]
bpy.context.view_layer.update()
mins, maxs = [1e9] * 3, [-1e9] * 3
def _under_root(o):
    while o.parent is not None:
        if o.parent == root:
            return True
        o = o.parent
    return False


# Only the instrument (descendants of the OSP root): the backdrop parents its own planks.
product_objs = [o for o in bpy.data.objects if o.type == "MESH" and _under_root(o)]
for o in product_objs:
    for c in o.bound_box:
        w = o.matrix_world @ Vector(c)
        for i in range(3):
            mins[i], maxs[i] = min(mins[i], w[i]), max(maxs[i], w[i])
CENTER = Vector([(a + b) / 2 for a, b in zip(mins, maxs)])


def P(v):
    """product-frame point (metres, from the centre at rest) -> world."""
    return CENTER + Q_ROT @ Vector(v)


pc = spec.get("product", {})
pivot_local = Vector(pc.get("pivot", [0, 0, 0]))
pivot = bpy.data.objects.new("Cine product pivot", None)
scene.collection.objects.link(pivot)
pivot.location = P(pivot_local)
bpy.context.view_layer.update()
root.parent = pivot
root.matrix_parent_inverse = pivot.matrix_world.inverted()
pivot.rotation_mode = "QUATERNION"

# ---- stage additions (Art Direction Gate R2): cyclorama, extra lights, flags -----------------------
# All optional and additive: a spec without a `stage` block renders exactly as before.
PRODUCT_C, STAGE_C = g["PRODUCT"], g["STAGE"]
stg = spec.get("stage", {})
FLOOR_NAMES = ("Gallery floor", "Studio floor", "Plank", "Grooves")
if stg.get("hide_floor"):
    for o in bpy.data.objects:
        if o.type == "MESH" and not _under_root(o) and o.name.startswith(FLOOR_NAMES):
            o.hide_render = True


def _plane(name, w, h, coll):
    me = bpy.data.meshes.new(name)
    me.from_pydata([(-w / 2, -h / 2, 0), (w / 2, -h / 2, 0), (w / 2, h / 2, 0), (-w / 2, h / 2, 0)], [], [(0, 1, 2, 3)])
    o = bpy.data.objects.new(name, me)
    coll.objects.link(o)
    return o


def _matte_mat(name, rgb, rough, spec_level=0.25):
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    b = mat.node_tree.nodes["Principled BSDF"]
    b.inputs["Base Color"].default_value = (*rgb, 1)
    b.inputs["Roughness"].default_value = rough
    b.inputs["Specular IOR Level"].default_value = spec_level
    return mat


cyc = stg.get("cyc")
if cyc:
    # A seamless cyclorama: floor, a large-radius cove and the wall in one smooth surface, so there
    # is no horizon line. It stands on the real floor plane (world z = 0), `wall` metres behind the
    # product centre in the direction `yaw_deg` (0 = the product's back), its cove of `radius`.
    R, wall, hgt, wid = cyc.get("radius", 1.2), cyc.get("wall", 1.6), cyc.get("height", 4.0), cyc.get("width", 12.0)
    prof = [(-6.0, 0.0)]
    for i in range(33):
        a = math.pi / 2 * i / 32
        prof.append((wall - R + R * math.sin(a), R - R * math.cos(a)))
    prof.append((wall, hgt))
    qy = Q_ROT @ Quaternion((0, 0, 1), math.radians(cyc.get("yaw_deg", 0.0)))
    base = Vector((CENTER.x, CENTER.y, 0.0))
    verts, faces = [], []
    xs = [-wid / 2, wid / 2]
    for y, z in prof:
        for x in xs:
            verts.append(tuple(base + qy @ Vector((x, y, z))))
    for i in range(len(prof) - 1):
        a = 2 * i
        faces.append((a, a + 1, a + 3, a + 2))
    me = bpy.data.meshes.new("Cyclorama")
    me.from_pydata(verts, [], faces)
    for p in me.polygons:
        p.use_smooth = True
    co = bpy.data.objects.new("Cyclorama", me)
    STAGE_C.objects.link(co)
    me.materials.append(_matte_mat("Cyclorama", cyc.get("rgb", [0.62, 0.59, 0.55]), cyc.get("roughness", 0.85), cyc.get("specular", 0.2)))


def _link(name):
    return {"product": PRODUCT_C, "stage": STAGE_C}.get(name)


for L in stg.get("add_lights", []):
    d = bpy.data.lights.new(L["name"], "AREA")
    d.shape, d.size, d.size_y = "RECTANGLE", L["size"][0], L["size"][1]
    d.energy, d.color, d.spread = L["power"], tuple(L.get("rgb", [1, 1, 1])), math.radians(L.get("spread", 180))
    o = bpy.data.objects.new(L["name"], d)
    scene.collection.objects.link(o)
    o.location = P(L["position"])
    o.rotation_euler = (P(L["target"]) - o.location).to_track_quat("-Z", "Y").to_euler()
    o.visible_camera = False
    if L.get("glossy_only"):
        o.visible_diffuse = False
    if _link(L.get("link")) is not None:
        o.light_linking.receiver_collection = _link(L.get("link"))
for name in stg.get("remove_lights", []):
    o = bpy.data.objects.get(name)
    if o is not None and o.type == "LIGHT":
        o.data.energy = 0.0
        o.hide_render = True

# Black flags. mode "reflection": seen only in reflections (a card just off frame, as on set);
# "negative": a real flag, it absorbs bounce and blocks light; neither ever appears in frame.
# mirror: true places the flag where the display glass reflects the camera (recomputed per
# frame), so the waveforms read clean instead of mirroring the studio.
flags = []
for fl in stg.get("flags", []):
    o = _plane(fl["name"], fl["size"][0], fl["size"][1], STAGE_C)
    o.data.materials.append(_matte_mat(fl["name"], [0.004, 0.004, 0.004], 0.95, 0.0))
    o.visible_camera = False
    neg = fl.get("mode", "reflection") == "negative"
    o.visible_shadow = neg
    o.visible_diffuse = neg
    o.visible_transmission = False
    flags.append((o, fl))
for name in stg.get("hide_objects", []):
    o = bpy.data.objects.get(name)
    if o is not None:
        o.hide_render = True

# ---- lights, cards, camera ------------------------------------------------------------------------
bpy.context.view_layer.update()   # added lights' matrix_world must be current before it is captured below
lights = {}
for name, lc in spec.get("lights", {}).items():
    o = bpy.data.objects.get(name)
    if o is None:
        print("no light", name)
        continue
    lights[name] = (o, lc, o.matrix_world.copy(), o.data.energy)
cards = []
for cd in spec.get("cards", []):
    me = bpy.data.meshes.new(cd["name"])
    w, h = cd["size"]
    me.from_pydata([(-w / 2, -h / 2, 0), (w / 2, -h / 2, 0), (w / 2, h / 2, 0), (-w / 2, h / 2, 0)], [], [(0, 1, 2, 3)])
    o = bpy.data.objects.new(cd["name"], me)
    scene.collection.objects.link(o)
    mat = bpy.data.materials.new(cd["name"])
    mat.use_nodes = True
    nt = mat.node_tree
    nt.nodes.clear()
    em, out = nt.nodes.new("ShaderNodeEmission"), nt.nodes.new("ShaderNodeOutputMaterial")
    em.inputs["Color"].default_value = (*cd.get("color", [1, 1, 1]), 1)
    nt.links.new(em.outputs[0], out.inputs[0])
    me.materials.append(mat)
    o.visible_camera = False
    o.visible_diffuse = cd.get("diffuse", False)
    o.visible_shadow = False
    if cd.get("link") == "product":   # seen only in the instrument (not in a glossy floor)
        o.light_linking.receiver_collection = g["PRODUCT"]
    cards.append((o, cd, em))
cam_data = bpy.data.cameras.new("Cine camera")
cam = bpy.data.objects.new("Cine camera", cam_data)
scene.collection.objects.link(cam)
cam_data.sensor_fit, cam_data.sensor_width = "HORIZONTAL", 36.0
cam_data.clip_start = 0.005
cs = spec["camera"]
if cs.get("fstop"):
    cam_data.dof.use_dof = True
    cam_data.dof.aperture_blades = 9
scene.camera = cam
base_exposure = scene.view_settings.exposure
world_bg = scene.world.node_tree.nodes["Background"].inputs["Strength"] if scene.world else None
world_base = world_bg.default_value if world_bg else 0.0
# The instrument's self-lit surfaces (displays, LEDs): one multiplier, so a shot emerging from
# darkness is not led by glowing screens.
emitters, _seen = [], set()
for o in product_objs:
    for slot in o.material_slots:
        m = slot.material
        if m is None or not m.use_nodes or m.name in _seen:
            continue
        _seen.add(m.name)
        for n in m.node_tree.nodes:
            sock = n.inputs.get("Emission Strength") if n.type == "BSDF_PRINCIPLED" else (n.inputs.get("Strength") if n.type == "EMISSION" else None)
            if sock is None:
                continue
            if sock.is_linked:   # displays: strength = mask x constant (a Math multiply)
                src = sock.links[0].from_node
                if src.type == "MATH" and src.operation == "MULTIPLY" and not src.inputs[1].is_linked:
                    sock = src.inputs[1]
                else:
                    continue
            if sock.default_value > 0:
                emitters.append((sock, sock.default_value))
print("cine: emitters", len(emitters), "world", world_base)
print("cine: lights", [(o.name, round(o.data.energy, 2)) for o in bpy.data.objects if o.type == "LIGHT" and not o.hide_render])


def look(o, target, roll_deg=0.0):
    z = (o.location - target).normalized()
    up = Vector((0, 0, 1))
    if abs(z.dot(up)) > 0.999:
        up = Q_ROT @ Vector((0, 1, 0))
    x = up.cross(z).normalized()
    y = z.cross(x)
    m = Matrix((x, y, z)).transposed()
    if roll_deg:
        m = m @ Matrix.Rotation(math.radians(roll_deg), 3, "Z")
    o.rotation_euler = m.to_euler()


# ---- passes -----------------------------------------------------------------------------------------
env_objs = [o for o in bpy.data.objects if o.type == "MESH" and o not in product_objs and o not in [c[0] for c in cards]]
if args.pass_ == "matte":
    scene.render.film_transparent = True
    scene.cycles.use_denoising = False
    for o in env_objs:
        o.is_holdout = True
    scene.render.image_settings.color_mode = "RGBA"
elif args.pass_ == "clean":
    for o in product_objs:
        o.hide_render = True


def apply(t):
    """Sets every animated channel for time t (seconds into the move; handles hold the ends)."""
    tt = min(DUR, max(0.0, t))
    tilt = math.radians(ev(pc.get("tilt"), tt, 0.0))
    turn = math.radians(ev(pc.get("turn"), tt, 0.0))
    q_local = Quaternion((0, 0, 1), turn) @ Quaternion((1, 0, 0), tilt)
    pivot.rotation_quaternion = Q_ROT @ q_local @ Q_ROT.inverted()
    pivot.location = P(pivot_local + Vector(ev(pc.get("translate"), tt, [0, 0, 0])))
    for name, (o, lc, m0, e0) in lights.items():
        o.data.energy = e0 * ev(lc.get("energy"), tt, 1.0)
        a = math.radians(ev(lc.get("orbit"), tt, 0.0))
        tr = Q_ROT @ Vector(ev(lc.get("translate"), tt, [0, 0, 0]))
        o.matrix_world = Matrix.Translation(tr) @ Matrix.Translation(CENTER) @ Matrix.Rotation(a, 4, "Z") @ Matrix.Translation(-CENTER) @ m0
    for o, cd, em in cards:
        if cd.get("mirror") is None:
            o.location = P(ev(cd["position"], tt))
            look(o, P(ev(cd["target"], tt)))
        em.inputs["Strength"].default_value = ev(cd.get("strength"), tt, 1.0)
    cam_data.lens = ev(cs["lens"], tt)
    cam.location = P(ev(cs["position"], tt))
    target = P(ev(cs["target"], tt))
    look(cam, target, ev(cs.get("roll"), tt, 0.0))
    q_world = pivot.rotation_quaternion

    def mirrored(aim, distance, offset=(0.0, 0.0)):
        # where the instrument's top surface reflects the camera at `aim` (a product-frame point on
        # it): start there, follow the mirrored view ray `distance` metres; offset tilts the ray
        # (radians, about the surface's x and y) so a highlight lands beside the aim, not on it
        p0 = pivot.location + q_world @ (P(aim) - P(pivot_local))
        n = q_world @ (Q_ROT @ Vector((0, 0, 1)))
        d = (p0 - cam.location).normalized()
        r = d - 2 * d.dot(n) * n
        if offset[0] or offset[1]:
            r = (Quaternion(q_world @ (Q_ROT @ Vector((1, 0, 0))), offset[0]) @ Quaternion(q_world @ (Q_ROT @ Vector((0, 1, 0))), offset[1])) @ r
        return p0, p0 + r * distance

    for o, cd, em in cards:
        mc = cd.get("mirror")
        if mc is not None:
            p0, o.location = mirrored(mc["aim"], mc.get("distance", 1.0), mc.get("offset", (0.0, 0.0)))
            look(o, p0)
    for o, fl in flags:
        if fl.get("mirror"):
            # the display glass reflects black instead of the studio
            p0, o.location = mirrored(fl.get("aim", [0.0, 0.03, 0.02]), fl.get("distance", 0.9))
            o.rotation_euler = (p0 - o.location).to_track_quat("Z", "Y").to_euler()
        else:
            o.location = P(ev(fl["position"], tt))
            o.rotation_euler = (P(ev(fl["target"], tt)) - o.location).to_track_quat("Z", "Y").to_euler()
    if cam_data.dof.use_dof:
        cam_data.dof.aperture_fstop = ev(cs["fstop"], tt)
        cam_data.dof.focus_distance = (P(ev(cs.get("focus", cs["target"]), tt)) - cam.location).length
    shift = ev(cs.get("shift"), tt, [0.0, 0.0])
    cam_data.shift_x, cam_data.shift_y = shift
    scene.view_settings.exposure = base_exposure + ev(spec.get("exposure"), tt, 0.0)
    if world_bg is not None:
        world_bg.default_value = world_base * ev(spec.get("world"), tt, 1.0)
    em_k = ev(spec.get("emission"), tt, 1.0)
    for sock, v0 in emitters:
        sock.default_value = v0 * em_k
    return {"lights_w": {o.name: round(o.data.energy, 3) for o in bpy.data.objects if o.type == "LIGHT" and not o.hide_render},
            "t": round(t, 4), "lens": round(cam_data.lens, 2), "cam": [round(x, 4) for x in cam.location],
            "target": [round(x, 4) for x in target], "tilt_deg": round(math.degrees(tilt), 2)}


def complete_png(path):
    try:
        with open(path, "rb") as f:
            f.seek(0, 2)
            if f.tell() < 64:
                return False
            f.seek(-12, 2)
            return f.read()[4:8] == b"IEND"
    except OSError:
        return False


os.makedirs(args.out, exist_ok=True)
log = []
if args.keyframes:
    kdir = os.path.join(args.out, "keyframes_" + args.pass_)
    os.makedirs(kdir, exist_ok=True)
    for lab, u in (("0_open", 0.0), ("1_quarter", 0.25), ("2_mid", 0.5), ("3_three_quarter", 0.75), ("4_close", 1.0)):
        rec = apply(u * DUR)
        rec["label"] = lab
        path = os.path.join(kdir, f"{spec['id']}_{lab}.png")
        if not complete_png(path):
            scene.render.filepath = path
            t0 = time.time()
            bpy.ops.render.render(write_still=True)
            rec["render_s"] = round(time.time() - t0, 1)
        log.append(rec)
elif args.still is not None:
    log.append(apply(args.still))
    path = os.path.join(args.out, f"{spec['id']}_still_t{args.still:.2f}.png")
    scene.render.filepath = path
    t0 = time.time()
    bpy.ops.render.render(write_still=True)
    log[-1]["render_s"] = round(time.time() - t0, 1)
else:
    fdir = os.path.join(args.out, args.pass_)
    os.makedirs(fdir, exist_ok=True)
    f0, f1 = args.frames if args.frames else (1, N)
    for f in range(f0, f1 + 1):
        t = (f - 1) / FPS - HANDLE
        rec = apply(t)
        path = os.path.join(fdir, f"{f:04d}.png")
        if not complete_png(path):
            scene.frame_set(f)
            scene.render.filepath = path
            t0 = time.time()
            bpy.ops.render.render(write_still=True)
            rec["render_s"] = round(time.time() - t0, 1)
        log.append(rec)
json.dump({"interface_textures": os.path.basename(os.path.normpath(g["TEX"])), "device": args.device, "devices": DEVICE_NAMES, "interface_frozen": FROZEN_OK, "interface_commit": FROZEN_NOTE if FROZEN_OK else None, "spec": spec, "pass": args.pass_, "res": res, "samples": samples, "noise": noise, "fps": FPS,
           "frames_total": N, "handles_s": HANDLE, "blender": bpy.app.version_string, "frames": log},
          open(os.path.join(args.out, f"{spec['id']}_{'keyframes' if args.keyframes else (args.pass_ if args.still is None else 'still')}_meta.json"), "w"), indent=1)
print("done", spec["id"], args.pass_ if args.still is None else "still")
