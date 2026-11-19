"""A self-contained Virtual Studio performance project: layout, manifest, integrity, preflight.

    <project>/
      manifest.json            identity, plugin build, preset, sources, settings, file roles
      inventory.json           every file: role, bytes, sha256 (written by `vs.py inventory`)
      REBUILD.md               how to rebuild everything derived from the inputs
      performance/             input.mid, automation.(csv|json), performance.json (the timeline)
      audio/                   daw_bounce.wav (the audio of record), headless.wav (capture check)
      sources/                 the source recordings the preset uses
      preset/                  the plugin state / preset file
      scene/                   cameras.json, lights.json, render.json (Blender settings)
      captures/<fps>fps/       face/NNNN.png, editor/NNNN.png, knobs/NNNN.json (plugin frames)
      renders/<shot>/<fps>fps/ frames/NNNN.png, <shot>.mp4, render_meta.json

Inputs (performance/, audio/, sources/, preset/, scene/) are the record; captures/ and renders/
are derived and can be rebuilt from them with the pinned plugin build and Blender version.
Large media are not committed to Git: the project lives in production storage, and the
inventory's checksums prove a copy is complete and unchanged.
"""
import hashlib
import json
import os
import shutil
import struct
import time
import zlib

SCHEMA = 1
INPUT_DIRS = ("performance", "audio", "sources", "preset", "scene")
DERIVED_DIRS = ("captures", "renders")


def init(root, project_id, title=""):
    for d in INPUT_DIRS + DERIVED_DIRS:
        os.makedirs(os.path.join(root, d), exist_ok=True)
    mpath = os.path.join(root, "manifest.json")
    if not os.path.exists(mpath):
        write_json(mpath, {
            "schemaVersion": SCHEMA, "id": project_id, "title": title,
            "created": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
            "plugin": {"product": "ANDOR OSP", "build": None, "buildNote": None},
            "preset": None, "sources": [], "performance": None, "audio": {}, "scene": {},
            "notes": "Inputs are the record; captures/ and renders/ are derived (see REBUILD.md).",
        })
    return load_manifest(root)


def load_manifest(root):
    return json.load(open(os.path.join(root, "manifest.json")))


def write_json(path, obj):
    tmp = path + ".tmp"
    with open(tmp, "w") as f:
        json.dump(obj, f, indent=1)
        f.write("\n")
    os.replace(tmp, path)   # atomic: a crash never leaves half a manifest


def sha256(path, chunk=1 << 20):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while True:
            b = f.read(chunk)
            if not b:
                return h.hexdigest()
            h.update(b)


def png_ok(path):
    """A PNG is complete when every chunk's CRC is right and it ends with IEND (a write that ran
    out of disk leaves a short or empty file; a truncated one fails the CRC walk)."""
    try:
        with open(path, "rb") as f:
            if f.read(8) != b"\x89PNG\r\n\x1a\n":
                return False
            while True:
                head = f.read(8)
                if len(head) < 8:
                    return False
                length, ctype = struct.unpack(">I4s", head)
                body = f.read(length)
                crc = f.read(4)
                if len(body) < length or len(crc) < 4:
                    return False
                if zlib.crc32(ctype + body) & 0xFFFFFFFF != struct.unpack(">I", crc)[0]:
                    return False
                if ctype == b"IEND":
                    return True
    except OSError:
        return False


def wav_ok(path):
    """RIFF/WAVE with a data chunk whose declared size fits the file."""
    try:
        size = os.path.getsize(path)
        with open(path, "rb") as f:
            h = f.read(12)
            if h[:4] != b"RIFF" or h[8:12] != b"WAVE":
                return False
            pos = 12
            while pos + 8 <= size:
                f.seek(pos)
                cid, clen = struct.unpack("<4sI", f.read(8))
                if cid == b"data":
                    return pos + 8 + clen <= size
                pos += 8 + clen + (clen & 1)
    except (OSError, struct.error):
        return False
    return False


def file_ok(path):
    p = path.lower()
    if p.endswith(".png"):
        return png_ok(path)
    if p.endswith(".wav"):
        return wav_ok(path)
    if p.endswith(".json"):
        try:
            json.load(open(path))
            return True
        except (OSError, ValueError):
            return False
    return os.path.getsize(path) > 0


def role_of(rel):
    top = rel.split(os.sep, 1)[0]
    return "input" if top in INPUT_DIRS else "derived" if top in DERIVED_DIRS else "meta"


def inventory(root, include_derived=True):
    """Checksums every file (inventory.json). Derived media can be skipped for a fast inputs-only
    inventory; the rebuild check then re-derives them."""
    files = []
    for dirpath, _, names in os.walk(root):
        for n in sorted(names):
            full = os.path.join(dirpath, n)
            rel = os.path.relpath(full, root)
            if rel in ("inventory.json",) or rel.endswith(".tmp"):
                continue
            role = role_of(rel)
            if role == "derived" and not include_derived:
                continue
            files.append({"path": rel, "role": role, "bytes": os.path.getsize(full), "sha256": sha256(full),
                          "valid": file_ok(full)})
    files.sort(key=lambda e: e["path"])
    inv = {"schemaVersion": SCHEMA, "project": load_manifest(root)["id"],
           "written": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
           "files": files, "totalBytes": sum(e["bytes"] for e in files),
           "invalid": [e["path"] for e in files if not e["valid"]]}
    write_json(os.path.join(root, "inventory.json"), inv)
    return inv


def verify(root):
    """Against inventory.json: missing, changed or invalid files (empty lists: the copy is whole)."""
    inv = json.load(open(os.path.join(root, "inventory.json")))
    missing, changed, invalid = [], [], []
    for e in inv["files"]:
        full = os.path.join(root, e["path"])
        if not os.path.exists(full):
            missing.append(e["path"])
        elif os.path.getsize(full) != e["bytes"] or sha256(full) != e["sha256"]:
            changed.append(e["path"])
        elif not file_ok(full):
            invalid.append(e["path"])
    return {"files": len(inv["files"]), "missing": missing, "changed": changed, "invalid": invalid}


def preflight(root, plan, margin=1.25):
    """Estimated bytes a capture / render plan needs, against the free space where it will be
    written. plan: [{"kind": "capture"|"render", "frames": n, "bytes_per_frame": b}].
    Returns (needed, free, ok). bytes_per_frame comes from a measured frame of the same size."""
    needed = int(sum(p["frames"] * p["bytes_per_frame"] for p in plan) * margin)
    free = shutil.disk_usage(root).free
    return needed, free, needed <= free


def complete_frames(folder, count, pattern="{:04d}.png", first=1):
    """Which of the numbered frames are complete (png_ok); the rest are (re)rendered."""
    done, todo = [], []
    for n in range(first, first + count):
        path = os.path.join(folder, pattern.format(n))
        (done if os.path.exists(path) and png_ok(path) else todo).append(n)
    return done, todo
