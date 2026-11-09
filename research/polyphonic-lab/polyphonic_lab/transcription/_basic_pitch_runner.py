"""Runs Basic Pitch in its own environment (.venv-basic-pitch) and writes its note events as JSON.

Executed as a script by basic_pitch_adapter (never imported by the lab): python <this> in.wav out.json
Only basic_pitch and the standard library are used here, so the lab's own environment stays free of
TensorFlow / ONNX.
"""

import hashlib
import json
import os
import sys
import time


def model_identity(path: str) -> dict:
    h = hashlib.sha256()
    files = []
    if os.path.isdir(path):
        for root, _, names in sorted(os.walk(path)):
            for n in sorted(names):
                files.append(os.path.join(root, n))
    else:
        files.append(path)
    for f in files:
        with open(f, "rb") as fh:
            h.update(fh.read())
    return {"path": os.path.basename(path.rstrip("/")), "sha256": h.hexdigest(), "files": len(files)}


def main() -> int:
    src, dst = sys.argv[1], sys.argv[2]
    import basic_pitch
    from basic_pitch import ICASSP_2022_MODEL_PATH, build_icassp_2022_model_path, FilenameSuffix
    from basic_pitch.inference import predict

    model_path = ICASSP_2022_MODEL_PATH
    try:
        model_path = build_icassp_2022_model_path(FilenameSuffix.onnx)
    except Exception:
        pass
    t0 = time.time()
    _, _, notes = predict(src, model_path)
    elapsed = time.time() - t0
    out = {
        "basicPitchVersion": getattr(basic_pitch, "__version__", "0.4.0"),
        "model": model_identity(str(model_path)),
        "seconds": round(elapsed, 3),
        "notes": [{"onset": float(n[0]), "offset": float(n[1]), "pitch_midi": int(n[2]), "amplitude": float(n[3])} for n in notes],
    }
    with open(dst, "w") as fh:
        json.dump(out, fh)
    return 0


if __name__ == "__main__":
    sys.exit(main())
