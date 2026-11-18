"""Phase 5 controlled studies (review only): ceramic card surface, recessed display bezel and
enclosure finish, each in three variants, through the skin's own renderer (render.py: studio,
materials, calibrated exposure; nine-slice pieces at their real places and sizes).

    <bpy python> tools/blender-assets/skin/ceramic_study.py [--samples N]

V0 is the shipped piece's own definition (pieces.py); V1 / V2 change one physical property each,
never the layout, sizes, colours or the approved materials' identity:
  card-451 (the three-layer source card)   V1 a rounder 0.8 mm shoulder   V2 a little more lift (1.2 mm)
  bezel-r9 (the source display's lip)      V1 a rounder lip (0.5 mm)       V2 a deeper recess (0.9 mm)
  housing (the enclosure)                  V1 a finer, slightly stronger powder grain   V2 a touch more satin (lower roughness)
Output: tools/blender-assets/output/ceramic-studies/<V>/raw/ (git-ignored); patch_pack.py turns
each into a pilot pack for real-editor comparisons.
"""
import copy
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import render as R  # noqa: E402
import pieces as P  # noqa: E402

OUT = os.path.join(R.REPO, "tools", "blender-assets", "output", "ceramic-studies")
NAMES = ("card-451", "bezel-r9", "housing")


def variants():
    base = {p["name"]: p for p in P.pieces() if p["name"] in NAMES}
    out = {"V0": {}, "V1": {}, "V2": {}}
    for v in out:
        for n, p in base.items():
            q = copy.deepcopy(p)
            if v == "V1":
                if n == "card-451":
                    q["bevel"] = 0.8
                elif n == "bezel-r9":
                    q["lip"] = 0.5
                else:
                    q["grain"] = {"heightScale": 1.5, "sizeScale": 0.7}
            elif v == "V2":
                if n == "card-451":
                    q["height"] = 1.2
                elif n == "bezel-r9":
                    q["depth"] = 0.9
                else:
                    q["satin"] = 0.85   # roughness x 0.85
            out[v][n] = q
    return out


def housing_scene(p):
    trim = R.CTRL["trim"][R.SPEC["trim"]]
    hg = R.CTRL["geometry"]["housing"]
    R.geometry.plane("backdrop", (-200, -200, 1848, 1486), R.material(R.CTRL["roles"]["backdrop"]))
    ref = R.ref("housing")
    from lab.materials import resolve
    base = resolve(ref)[1]
    if "grain" in p:
        g = dict(base.get("grain", {}))
        if "height" in g:
            g["height"] = g["height"] * p["grain"]["heightScale"]
        if "size" in g:
            g["size"] = g["size"] * p["grain"]["sizeScale"]
        ref["grain"] = g
    if "satin" in p:
        ref["roughness"] = base.get("roughness", 0.5) * p["satin"]
    R.forms.slab("housing", P.HOUSING, trim["housingRadius"], 0.0, hg["height"], hg["bevel"], R.material(ref))
    for side in ("left", "right"):
        R.forms.slab("trim", tuple(trim[side]), trim["radius"], 0.0, trim["height"], trim["bevel"], R.material(R.ref("teak")))


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:]
    samples = int(argv[argv.index("--samples") + 1]) if "--samples" in argv else None
    for v, pieces in variants().items():
        raw = os.path.join(OUT, v, "raw")
        os.makedirs(raw, exist_ok=True)
        for n, p in pieces.items():
            if p["kind"] == "pocket":
                for cut, suffix in ((True, "with"), (False, "without")):
                    path = os.path.join(raw, f"{n}.{suffix}.png")
                    if not os.path.exists(path):
                        R.render(p, path, lambda cut=cut, p=p: R.pocket_scene(p, cut), transparent=False, samples=samples)
                continue
            path = os.path.join(raw, n + ".png")
            if os.path.exists(path):
                continue
            if p["kind"] == "opaque":
                R.render(p, path, lambda p=p: housing_scene(p), transparent=False, samples=samples)
            else:
                R.render(p, path, lambda p=p: R.slab_scene(p), samples=samples)


if __name__ == "__main__":
    main()
