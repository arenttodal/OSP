"""Render every direction and family study at final quality, then composite them.

    python scripts/render_all.py <blender-python> [--quality final] [--only directions|families]

Deterministic: fixed seeds, no adaptive sampling; re-running reproduces the same pixels.
Existing study renders are kept when LAB_SKIP_EXISTING=1 (resume after an interruption).
"""
import argparse, json, os, subprocess, sys, time

LAB = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("python")
    ap.add_argument("--quality", default="final")
    ap.add_argument("--only", choices=["directions", "families"])
    ap.add_argument("--directions", nargs="*", help="a subset of directions")
    ap.add_argument("--families-first", action="store_true")
    a = ap.parse_args()
    with open(os.path.join(LAB, "blender", "configs", "directions.json"), "r", encoding="utf-8") as f:
        directions = list(json.load(f)["directions"])
    if a.directions:
        directions = [d for d in directions if d in a.directions]
    blender = os.path.join(LAB, "blender")
    t0 = time.time()

    def run_directions():
        for d in directions:
            subprocess.run([a.python, "generate.py", "scene", f"direction:{d}", "--quality", a.quality], cwd=blender, check=True,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            print(f"[{time.time() - t0:7.0f}s] chassis {d}", flush=True)
            for scale in ("2", "1"):
                subprocess.run([sys.executable, "scripts/composite_gui.py", "direction", d, "--scale", scale], cwd=LAB, check=True)

    def run_families():
        for fam in ("buttons", "tabs", "sliders", "panels", "backgrounds", "details"):
            subprocess.run([a.python, "generate.py", "scene", f"family:{fam}", "--quality", a.quality], cwd=blender, check=True,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            print(f"[{time.time() - t0:7.0f}s] family {fam}", flush=True)
            subprocess.run([sys.executable, "scripts/composite_gui.py", "family", fam], cwd=LAB, check=True, stdout=subprocess.DEVNULL)

    order = [run_families, run_directions] if a.families_first else [run_directions, run_families]
    for step in order:
        if a.only is None or step.__name__ == "run_" + a.only:
            step()
    print(f"done in {time.time() - t0:.0f}s", flush=True)


if __name__ == "__main__":
    main()
