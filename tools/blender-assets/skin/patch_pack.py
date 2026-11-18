"""Replaces named pieces of a shipped ceramic pack with new raw renders (review pilots only).

    python3 tools/blender-assets/skin/patch_pack.py <shipped ceramic-skin.zip> <raw dir> <out zip>

Every piece of pieces.py whose raw render exists in <raw dir> ("<name>.png", or for a pocket
"<name>.with.png" + "<name>.without.png") is processed exactly as pack.py processes it (alpha:
feathered straight alpha; pocket: the overlay from the two renders; opaque: JPEG) at its scale
and an exact 2x level, and replaces that piece's entry; every other piece and the manifest's
format stay as shipped. The runtime is unchanged: a pilot is a resource swap.
"""
import json
import os
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import pieces as P  # noqa: E402
from pack import encode, face_colour, feather, halve, load, overlay  # noqa: E402


def patch(shipped, raw, out):
    zin = zipfile.ZipFile(shipped)
    manifest = json.loads(zin.read("manifest.json"))
    by_name = {e["name"]: e for e in manifest["pieces"]}
    replaced, names = {}, []
    for p in P.pieces():
        name = p["name"]
        if name not in by_name:
            continue
        if p["kind"] == "pocket":
            paths = [os.path.join(raw, f"{name}.{s}.png") for s in ("with", "without")]
            if not all(os.path.exists(x) for x in paths):
                continue
            img, kind = feather(overlay(load(paths[0]), load(paths[1])), p["margin"] * p["scale"]), "overlay"
        else:
            path = os.path.join(raw, name + ".png")
            if not os.path.exists(path):
                continue
            img = load(path)
            kind = "opaque" if p["kind"] == "opaque" else "alpha"
            if kind == "alpha":
                img = feather(img, p["margin"] * p["scale"])
        entry = by_name[name]
        if kind != "overlay":
            entry["face"] = face_colour(img, p["margin"] * p["scale"])
        if kind == "opaque":
            entry["edge"] = "#" + "".join(f"{int(round(v * 255)):02x}" for v in img[2, 2, :3])
        scales = [p["scale"]] + ([2] if p["scale"] == 4 else [])
        level, levels = img, []
        for s in scales:
            if s != scales[0]:
                level = halve(level)
            file = f"pieces/{name}@{s}x." + ("jpg" if kind == "opaque" else "png")
            replaced[file] = encode(level, opaque=kind == "opaque")
            levels.append({"scale": s, "file": file, "width": int(level.shape[1]), "height": int(level.shape[0])})
        entry["levels"] = levels
        names.append(name)
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    with zipfile.ZipFile(out, "w", zipfile.ZIP_STORED) as z:
        z.writestr("manifest.json", json.dumps(manifest, indent=1))
        for info in zin.infolist():
            if info.filename == "manifest.json" or info.filename in replaced:
                continue
            z.writestr(info.filename, zin.read(info.filename))
        for file, data in sorted(replaced.items()):
            z.writestr(file, data)
    print(f"{len(names)} pieces replaced -> {out}")
    return names


if __name__ == "__main__":
    patch(*sys.argv[1:4])
