#!/usr/bin/env python3
"""ANDOR Virtual Studio: performance projects for the cinematography pipeline (internal tool).

  python3 vs.py init      <project> --id 002-nyckelharpa-take3 [--title ...]
  python3 vs.py ingest    <project> --midi take.mid [--automation lanes.csv] [--audio bounce.wav]
                          [--source a.wav ...] [--preset state.xml] --build <commit> [--sample-rate 48000]
                          [--start S] [--end S] [--tail 1.0]
  python3 vs.py check     <project>                 # timeline sanity (notes, lanes, timing)
  python3 vs.py preflight <project> --fps 24 --capture-frame-bytes N [--render-frame-bytes N --shots K]
  python3 vs.py inventory <project> [--inputs-only] # sha256 + integrity of every file
  python3 vs.py verify    <project>                 # a copy against its inventory (restore check)

Only Python's standard library is used, so a project can be verified and re-ingested on any
machine. The plugin capture (C++, the plugin's test harness) and the Blender animation read the
same performance/performance.json; see README.md.
"""
import argparse
import json
import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from vstudio import project, smf, timeline  # noqa: E402


def cmd_init(a):
    m = project.init(a.project, a.id, a.title)
    print("project", a.project, "id", m["id"])


def copy_in(src, root, sub, name=None):
    dst = os.path.join(root, sub, name or os.path.basename(src))
    if os.path.abspath(src) != os.path.abspath(dst):
        shutil.copy2(src, dst)
    return os.path.relpath(dst, root)


def cmd_ingest(a):
    m = project.init(a.project, a.id or os.path.basename(os.path.abspath(a.project)))
    mid_rel = copy_in(a.midi, a.project, "performance", "input.mid")
    parsed = smf.read(os.path.join(a.project, mid_rel))
    lanes, auto_rel = None, None
    if a.automation:
        auto_rel = copy_in(a.automation, a.project, "performance",
                           "automation" + os.path.splitext(a.automation)[1])
        ticks_per_beat = parsed["division"] if not parsed["division"] & 0x8000 else None
        beats = (lambda b: parsed["seconds"](int(round(b * ticks_per_beat)))) if ticks_per_beat else None
        lanes = timeline.read_automation(os.path.join(a.project, auto_rel), beats)
    perf = timeline.build(parsed, lanes, sample_rate=a.sample_rate, start=a.start, end=a.end, tail=a.tail,
                          meta={"id": m["id"], "plugin": "ANDOR OSP", "build": a.build,
                                "preset": os.path.basename(a.preset) if a.preset else None,
                                "inputs": {"midi": mid_rel, "automation": auto_rel}})
    problems = timeline.check(perf)
    project.write_json(os.path.join(a.project, "performance", "performance.json"), perf)
    m["plugin"]["build"] = a.build
    m["performance"] = "performance/performance.json"
    if a.audio:
        m["audio"]["daw_bounce"] = copy_in(a.audio, a.project, "audio", "daw_bounce.wav")
    for s in a.source or []:
        rel = copy_in(s, a.project, "sources")
        if rel not in m["sources"]:
            m["sources"].append(rel)
    if a.preset:
        m["preset"] = copy_in(a.preset, a.project, "preset")
    project.write_json(os.path.join(a.project, "manifest.json"), m)
    n_in = len(perf["notes"])
    print(f"ingested {n_in} input notes, {len(perf['pitchBend'])} pitch-bend and "
          f"{sum(len(v) for v in perf['cc'].values())} CC events, {len(perf['automation'])} automation lanes; "
          f"duration {perf['timebase']['duration']:.3f} s")
    for p in problems:
        print("WARNING", p)


def cmd_check(a):
    perf = json.load(open(os.path.join(a.project, "performance", "performance.json")))
    problems = timeline.check(perf)
    d = perf["timebase"]["duration"]
    for fps in (24, 30):
        n = timeline.frame_count(d, fps)
        print(f"{fps} fps: {n} frames; last frame starts at sample "
              f"{timeline.frame_sample(n - 1, fps, perf['timebase']['sampleRate'])}")
    print("problems:", problems or "none")
    return 1 if problems else 0


def cmd_preflight(a):
    perf = json.load(open(os.path.join(a.project, "performance", "performance.json")))
    n = timeline.frame_count(perf["timebase"]["duration"], a.fps)
    plan = [{"kind": "capture", "frames": n, "bytes_per_frame": a.capture_frame_bytes}]
    if a.render_frame_bytes:
        plan.append({"kind": "render", "frames": n * a.shots, "bytes_per_frame": a.render_frame_bytes})
    need, free, ok = project.preflight(a.project, plan)
    print(f"{n} frames at {a.fps} fps: needs about {need / 1e9:.2f} GB (25 % margin), {free / 1e9:.2f} GB free -> "
          + ("OK" if ok else "NOT ENOUGH SPACE"))
    return 0 if ok else 2


def cmd_inventory(a):
    inv = project.inventory(a.project, include_derived=not a.inputs_only)
    print(f"{len(inv['files'])} files, {inv['totalBytes'] / 1e6:.1f} MB; invalid: {inv['invalid'] or 'none'}")
    return 1 if inv["invalid"] else 0


def cmd_verify(a):
    r = project.verify(a.project)
    bad = r["missing"] or r["changed"] or r["invalid"]
    print(f"{r['files']} files checked; missing {len(r['missing'])}, changed {len(r['changed'])}, invalid {len(r['invalid'])}")
    for k in ("missing", "changed", "invalid"):
        for p in r[k][:20]:
            print(f"  {k}: {p}")
    return 1 if bad else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("init"); s.add_argument("project"); s.add_argument("--id", required=True); s.add_argument("--title", default="")
    s.set_defaults(f=cmd_init)
    s = sub.add_parser("ingest"); s.add_argument("project"); s.add_argument("--id"); s.add_argument("--midi", required=True)
    s.add_argument("--automation"); s.add_argument("--audio"); s.add_argument("--source", action="append"); s.add_argument("--preset")
    s.add_argument("--build", required=True); s.add_argument("--sample-rate", type=float, default=48000.0)
    s.add_argument("--start", type=float, default=0.0); s.add_argument("--end", type=float); s.add_argument("--tail", type=float, default=1.0)
    s.set_defaults(f=cmd_ingest)
    s = sub.add_parser("check"); s.add_argument("project"); s.set_defaults(f=cmd_check)
    s = sub.add_parser("preflight"); s.add_argument("project"); s.add_argument("--fps", type=float, default=24)
    s.add_argument("--capture-frame-bytes", type=int, required=True); s.add_argument("--render-frame-bytes", type=int)
    s.add_argument("--shots", type=int, default=1); s.set_defaults(f=cmd_preflight)
    s = sub.add_parser("inventory"); s.add_argument("project"); s.add_argument("--inputs-only", action="store_true")
    s.set_defaults(f=cmd_inventory)
    s = sub.add_parser("verify"); s.add_argument("project"); s.set_defaults(f=cmd_verify)
    a = ap.parse_args()
    sys.exit(a.f(a) or 0)


if __name__ == "__main__":
    main()
