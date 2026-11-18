"""Phase 5 button pilot (review only): the ceramic keys in the K2 direction, every role and state.

    <bpy python> tools/blender-assets/skin/k2_pilot.py render [--samples N]
    python3      tools/blender-assets/skin/k2_pilot.py pack <shipped ceramic-skin.zip> <out zip>

render: every "key-<role>-<state>" piece of pieces.py, at its real place, size and radius, with
        the skin's own studio, materials and calibrated exposure (render.py), in K2's form: a
        crisp 0.35 mm edge (6 segments), a restrained satin coat on top of the preset's, and a
        deeper press (0.30 x emphasis for pressed / active) so selection does not rest on a soft
        shadow alone (the active state keeps the skin's accent tint and the live accent label).
        Output: tools/blender-assets/output/ceramic-k2/raw/ (git-ignored).
pack:   a copy of the shipped pack with only the key pieces replaced (processed exactly as
        pack.py processes slabs: feathered straight alpha, a 4x and an exact 2x level, the face
        colour in the manifest). Every other piece, the manifest's format and the runtime are
        unchanged: the pilot is a resource swap.
"""
import io
import json
import os
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import pieces as P  # noqa: E402

REPO = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
RAW = os.path.join(REPO, "tools", "blender-assets", "output", "ceramic-k2", "raw")
K2_COAT = 0.18


def key_pieces():
    return [p for p in P.pieces() if p["name"].startswith("key-")]


def render(samples):
    import render as R  # noqa: E402  (bpy)
    from lab.materials import resolve  # noqa: E402
    os.makedirs(RAW, exist_ok=True)
    for p in key_pieces():
        path = os.path.join(RAW, p["name"] + ".png")
        if os.path.exists(path):
            continue
        role, state = p["role"], p["state"]
        e = P.EMPHASIS.get(role.split("-")[0], P.EMPHASIS["generic"])
        pressed = state in ("pressed", "active")
        h = (0.30 if pressed else 0.8) * e
        bevel = min(0.35, 0.9 * h)

        def build(p=p, h=h, bevel=bevel, state=state):
            R.catcher(p["rect"], p["base"])
            mat = R.ref("key", state)
            mat["coat"] = resolve(mat)[1].get("coat", 0.0) + K2_COAT
            mat["coatRoughness"] = 0.28
            R.forms.slab(p["name"], p["rect"], p["radius"], R.BASE_TOP[p["base"]], h, bevel, R.material(mat), segments=6)

        R.render(p, path, build, samples=samples)


def pack(shipped, out):
    import numpy as np
    from pack import encode, face_colour, feather, halve, load  # noqa: E402
    zin = zipfile.ZipFile(shipped)
    manifest = json.loads(zin.read("manifest.json"))
    by_name = {e["name"]: e for e in manifest["pieces"]}
    replaced = {}
    for p in key_pieces():
        path = os.path.join(RAW, p["name"] + ".png")
        if not os.path.exists(path) or p["name"] not in by_name:
            continue
        img = feather(load(path), p["margin"] * p["scale"])
        entry = by_name[p["name"]]
        entry["face"] = face_colour(img, p["margin"] * p["scale"])
        scales = [p["scale"]] + ([2] if p["scale"] == 4 else [])
        level, levels = img, []
        for s in scales:
            if s != scales[0]:
                level = halve(level)
            file = f"pieces/{p['name']}@{s}x.png"
            replaced[file] = encode(level)
            levels.append({"scale": s, "file": file, "width": int(level.shape[1]), "height": int(level.shape[0])})
        entry["levels"] = levels
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    with zipfile.ZipFile(out, "w", zipfile.ZIP_STORED) as z:
        z.writestr("manifest.json", json.dumps(manifest, indent=1))
        for info in zin.infolist():
            if info.filename == "manifest.json" or info.filename in replaced:
                continue
            z.writestr(info.filename, zin.read(info.filename))
        for file, data in sorted(replaced.items()):
            z.writestr(file, data)
    print(f"{len(replaced)} key images replaced -> {out}")


if __name__ == "__main__":
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:]
    if argv and argv[0] == "render":
        samples = int(argv[argv.index("--samples") + 1]) if "--samples" in argv else 64
        render(samples)
    elif argv and argv[0] == "pack":
        pack(argv[1], argv[2])
    else:
        raise SystemExit(__doc__)
