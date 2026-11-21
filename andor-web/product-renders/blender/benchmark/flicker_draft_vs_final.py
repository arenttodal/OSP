"""Draft vs final, every shot: consecutive frames (768 px wide, luma) aligned by phase correlation so camera
motion cancels; the residual is change that is not motion (noise, denoiser boiling). Decoded from the delivered
H.264 files, so both sides include their own compression."""
import json, subprocess, sys
import numpy as np
sys.path.insert(0, sys.argv[1])  # folder holding analyse2_core.py
from analyse2_core import shift_est, fshift
def frames(path, start, n=48, w=768):
    h = 432
    raw = subprocess.run(["ffmpeg", "-nostdin", "-v", "error", "-i", path, "-vf", f"select=gte(n\\,{start}),scale={w}:{h}:flags=lanczos,format=rgb24",
                          "-frames:v", str(n), "-f", "rawvideo", "-"], capture_output=True).stdout
    a = np.frombuffer(raw, np.uint8).reshape(-1, h, w, 3).astype(np.float64)
    return a @ [0.2126, 0.7152, 0.0722]
def resid(fr):
    c = fr[:, 60:372, 100:668]; out = []
    for a, b in zip(c[:-1], c[1:]):
        dy, dx = shift_est(a, b); r = np.abs(a - fshift(b, dy, dx))[8:-8, 8:-8]; out.append((r.mean(), np.percentile(r, 99.5)))
    o = np.array(out); return float(o[:, 0].mean()), float(o[:, 1].mean())
res = {}
for shot, draft, final, start in json.loads(sys.argv[2]):
    (dm, dp), (fm, fp) = resid(frames(draft, start)), resid(frames(final, start))
    res[shot] = {"mean": {"draft": round(dm, 2), "final": round(fm, 2)}, "p99_5": {"draft": round(dp, 2), "final": round(fp, 2)},
                 "p99_5_reduction_pct": round(100 * (1 - fp / dp))}
    print(f"{shot:18s} mean {dm:5.2f} -> {fm:5.2f}   p99.5 {dp:6.2f} -> {fp:6.2f}  ({res[shot]['p99_5_reduction_pct']}% less)")
json.dump(res, open(sys.argv[3], "w"), indent=1)
