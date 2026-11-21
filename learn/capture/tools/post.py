#!/usr/bin/env python3
"""ANDOR Learn Capture Studio: post-processing for one captured scene.

Reads a runner output folder (master.mkv, frames.json, run.json) and its scenario, and writes the
web assets and a manifest entry:

  <assets>/<slug>/<slug>.webm      VP9, no audio            (desktop)
  <assets>/<slug>/<slug>.mp4       H.264 High, yuv420p      (fallback)
  <assets>/<slug>/poster.webp      a still at rest after the last result (PNG master beside master.mkv)
  <assets>/<slug>/steps/<mark>.webp  one still per step (the end of each mark)
  <assets>/<slug>/entry.json       this scene's andor.learn.assets entry
  <out>/post-report.json           every check, passed or not

Editing rules (owner, Gate 1): one fixed crop per scene (no zooms or camera moves); a small neutral
pointer drawn at the exact coordinates the runner sent; a thin outline on the active target only
while the scenario names one; the file badge only while a drop is travelling; no transitions.
The loop restarts on the first frame (a clean reset), never a crossfade.

  post.py --run learn/capture/out/a-load-and-play --scenario learn/capture/scenarios/a-load-and-play.json \
          --assets learn/assets/osp [--commit <sha>] [--width 1600]
Needs Pillow and ffmpeg.
"""
import argparse
import hashlib
import json
import math
import pathlib
import subprocess
import sys

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont, ImageStat

ACCENT = (189, 101, 61)       # ANDOR terracotta (web token --accent)
INK = (29, 35, 33)            # --osp-display
PAPER = (244, 241, 233)       # --dark-text


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


TARGET_LUFS = -16.0


def audio_stats(path):
    """EBU R128 loudness of a file (ffmpeg loudnorm's measurement pass) and its duration."""
    r = subprocess.run(["ffmpeg", "-hide_banner", "-nostats", "-i", str(path), "-af", f"loudnorm=I={TARGET_LUFS}:TP=-1.5:LRA=20:print_format=json",
                        "-f", "null", "-"], capture_output=True, text=True)
    j = json.loads(r.stderr[r.stderr.rindex("{"):r.stderr.rindex("}") + 1])
    d = subprocess.run(["ffprobe", "-v", "error", "-show_entries", "format=duration", "-of", "csv=p=0", str(path)], capture_output=True, text=True).stdout
    f = lambda k: max(-70.0, float(j[k])) if j[k] not in ("-inf", "inf") else -70.0
    return {"I": f("input_i"), "TP": f("input_tp"), "LRA": max(0.0, float(j["input_lra"])), "thresh": f("input_thresh"),
            "offset": float(j["target_offset"]), "duration": float(d)}


def probe(path):
    out = subprocess.run(["ffprobe", "-v", "error", "-count_frames", "-select_streams", "v:0",
                          "-show_entries", "stream=width,height,nb_read_frames,r_frame_rate,codec_name",
                          "-show_entries", "format=duration", "-of", "json", str(path)],
                         check=True, capture_output=True, text=True).stdout
    j = json.loads(out)
    s = j["streams"][0]
    return {"width": int(s["width"]), "height": int(s["height"]), "frames": int(s["nb_read_frames"]),
            "fps": s["r_frame_rate"], "codec": s["codec_name"], "duration": float(j["format"]["duration"])}


def decode(master, w, h):
    """Yields RGB frames of the lossless master."""
    p = subprocess.Popen(["ffmpeg", "-loglevel", "error", "-i", str(master), "-f", "rawvideo", "-pix_fmt", "rgb24", "-"],
                         stdout=subprocess.PIPE)
    size = w * h * 3
    while True:
        buf = p.stdout.read(size)
        if len(buf) < size:
            break
        yield Image.frombytes("RGB", (w, h), buf)
    p.wait()


def crop_box(frames, scale, src_w, src_h, context, aspect):
    """One fixed crop for the whole scene: the union of every frame's focus, plus context, at the
    scenario's aspect ratio, inside the editor."""
    box = None
    for f in frames:
        if "focus" in f:
            x, y, w, h = f["focus"]
            r = (x, y, x + w, y + h)
            box = r if box is None else (min(box[0], r[0]), min(box[1], r[1]), max(box[2], r[2]), max(box[3], r[3]))
    ew, eh = src_w / scale, src_h / scale
    if box is None:
        box = (0, 0, ew, eh)
    bw, bh = box[2] - box[0], box[3] - box[1]
    pad = context * max(bw, bh)
    box = (box[0] - pad, box[1] - pad, box[2] + pad, box[3] + pad)
    bw, bh = box[2] - box[0], box[3] - box[1]
    if aspect == "auto":
        target = bw / bh   # the focus box's own shape (tall panels: no forced 16:10)
    else:
        aw, ah = (int(v) for v in aspect.split(":"))
        target = aw / ah
    cx, cy = (box[0] + box[2]) / 2, (box[1] + box[3]) / 2
    if bw / bh < target:
        bw = bh * target
    else:
        bh = bw / target
    bw, bh = min(bw, ew), min(bh, eh)
    if bw / bh > target + 1e-6:
        bw = bh * target
    elif bw / bh < target - 1e-6:
        bh = bw / target
    x0 = min(max(cx - bw / 2, 0), ew - bw)
    y0 = min(max(cy - bh / 2, 0), eh - bh)
    return tuple(int(round(v * scale)) for v in (x0, y0, x0 + bw, y0 + bh))


class Overlay:
    """The restrained annotation layer, drawn at output resolution."""

    def __init__(self, k, crop, scale):
        self.k = k                    # output px per editor px
        self.crop = crop
        self.scale = scale
        try:
            self.font = ImageFont.truetype("DejaVuSans.ttf", max(12, int(13 * k)))
        except OSError:
            self.font = ImageFont.load_default()
        self.cursor = self._cursor()

    def to_out(self, x, y):
        return ((x * self.scale - self.crop[0]) * self.k / self.scale, (y * self.scale - self.crop[1]) * self.k / self.scale)

    def _cursor(self):
        # A small neutral arrow (about 19 px at 1600 px output), supersampled for a clean edge.
        s = 4
        h = max(16, int(17 * self.k))
        w = int(h * 0.72)
        pts = [(0, 0), (0, 0.80), (0.22, 0.62), (0.36, 0.95), (0.50, 0.89), (0.36, 0.57), (0.64, 0.57)]
        img = Image.new("RGBA", ((w + 4) * s, (h + 4) * s), (0, 0, 0, 0))
        d = ImageDraw.Draw(img)
        poly = [((2 + px * w * 1.0) * s, (2 + py * h) * s) for px, py in pts]
        shadow = Image.new("RGBA", img.size, (0, 0, 0, 0))
        ImageDraw.Draw(shadow).polygon([(x + s, y + 2 * s) for x, y in poly], fill=(0, 0, 0, 70))
        img = Image.alpha_composite(img, shadow.filter(ImageFilter.GaussianBlur(2 * s)))
        d = ImageDraw.Draw(img)
        d.polygon(poly, fill=PAPER + (255,), outline=INK + (255,), width=int(1.3 * s))
        return img.resize((w + 4, h + 4), Image.LANCZOS)

    def outline(self, img, rect, alpha):
        x, y, w, h = rect
        x0, y0 = self.to_out(x, y)
        x1, y1 = self.to_out(x + w, y + h)
        g = 3 * self.k
        layer = Image.new("RGBA", img.size, (0, 0, 0, 0))
        d = ImageDraw.Draw(layer)
        d.rounded_rectangle((x0 - g, y0 - g, x1 + g, y1 + g), radius=6 * self.k, outline=ACCENT + (int(235 * alpha),), width=max(2, int(round(2 * self.k))))
        return Image.alpha_composite(img, layer)

    def press(self, img, x, y, age):
        # A soft ring at the tip for a few frames after a press (no ripple animation).
        if age > 5:
            return img
        ox, oy = self.to_out(x, y)
        r = (7 + age * 1.2) * self.k
        layer = Image.new("RGBA", img.size, (0, 0, 0, 0))
        ImageDraw.Draw(layer).ellipse((ox - r, oy - r, ox + r, oy + r), outline=PAPER + (int(180 * (1 - age / 6)),), width=max(2, int(2 * self.k)))
        return Image.alpha_composite(img, layer)

    def label(self, img, text):
        # A quiet label for an input that has no picture in the interface (the sustain pedal).
        pad = 8 * self.k
        tw = self.font.getlength(text)
        w, h = tw + pad * 2 + 14 * self.k, 28 * self.k
        x0, y0 = 16 * self.k, img.size[1] - h - 16 * self.k
        layer = Image.new("RGBA", img.size, (0, 0, 0, 0))
        d = ImageDraw.Draw(layer)
        d.rounded_rectangle((x0, y0, x0 + w, y0 + h), radius=h / 2, fill=INK + (225,))
        d.ellipse((x0 + pad, y0 + h / 2 - 4 * self.k, x0 + pad + 8 * self.k, y0 + h / 2 + 4 * self.k), fill=ACCENT + (255,))
        d.text((x0 + pad + 14 * self.k, y0 + h / 2), text, font=self.font, fill=PAPER + (255,), anchor="lm")
        return Image.alpha_composite(img, layer)

    def pointer(self, img, x, y):
        ox, oy = self.to_out(x, y)
        img.alpha_composite(self.cursor, (int(round(ox - 2)), int(round(oy - 2))))
        return img

    def badge(self, img, x, y, name):
        ox, oy = self.to_out(x, y)
        tw = self.font.getlength(name)
        pad = 7 * self.k
        bw, bh = tw + pad * 2 + 16 * self.k, 26 * self.k
        bx, by = ox + 16 * self.k, oy + 14 * self.k
        layer = Image.new("RGBA", img.size, (0, 0, 0, 0))
        d = ImageDraw.Draw(layer)
        d.rounded_rectangle((bx, by, bx + bw, by + bh), radius=5 * self.k, fill=PAPER + (240,), outline=(133, 140, 130, 255), width=1)
        # a plain document glyph
        gx, gy = bx + pad, by + 6 * self.k
        d.rectangle((gx, gy, gx + 9 * self.k, gy + 13 * self.k), outline=INK + (255,), width=max(1, int(self.k)))
        d.text((gx + 15 * self.k, by + bh / 2), name, font=self.font, fill=INK + (255,), anchor="lm")
        return Image.alpha_composite(img, layer)


def luminance_std(img):
    return ImageStat.Stat(img.convert("L")).stddev[0]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--run", required=True)
    ap.add_argument("--scenario", required=True)
    ap.add_argument("--assets", required=True)
    ap.add_argument("--commit", default="")
    ap.add_argument("--width", type=int, default=1600)
    a = ap.parse_args()

    run_dir = pathlib.Path(a.run)
    scenario = json.loads(pathlib.Path(a.scenario).read_text())
    slug = scenario["id"].split(".")[-1]
    out_dir = pathlib.Path(a.assets) / slug
    run = json.loads((run_dir / "run.json").read_text())
    frames_doc = json.loads((run_dir / "frames.json").read_text())
    frames = frames_doc["frames"]
    scale = float(frames_doc["scale"])
    fps = int(frames_doc["fps"])
    checks = []

    def check(name, ok, detail=""):
        checks.append({"check": name, "passed": bool(ok), "detail": detail})

    check("runner status", run["status"] == "passed", run.get("failure", ""))
    check("expectations", run["status"] == "passed", f'{run["expectationsPassed"]} passed')
    if run["status"] != "passed":
        (run_dir / "post-report.json").write_text(json.dumps({"scenario": scenario["id"], "status": "failed", "checks": checks}, indent=2))
        print("runner failed; no assets written")
        return 1

    (out_dir / "steps").mkdir(parents=True, exist_ok=True)   # only once the run passed
    master = run_dir / "master.mkv"
    info = probe(master)
    check("master frame count = frames.json", info["frames"] == len(frames), f'{info["frames"]} / {len(frames)}')
    sizes = sorted({(f["w"], f["h"]) for f in frames})
    # The window may grow during a scene (ARP's panel); the runner then films it on one canvas.
    check("every editor size fits the canvas", all(w * scale <= info["width"] + 1 and h * scale <= info["height"] + 1 for w, h in sizes), str(sizes))

    cfg = scenario.get("frame", {})
    crop = crop_box(frames, scale, info["width"], info["height"], float(cfg.get("context", 0.05)), cfg.get("aspect", "16:10"))
    cw, ch = crop[2] - crop[0], crop[3] - crop[1]
    out_w = min(a.width, cw)
    out_w -= out_w % 2
    out_h = int(round(out_w * ch / cw))
    out_h -= out_h % 2
    k = out_w / (cw / scale)   # output px per editor px
    ov = Overlay(k, crop, scale)

    # Highlight fade: the frames since the current highlight began.
    marks_at = {}
    for i, f in enumerate(frames):
        for m in (f.get("marks") or "").split(","):
            if m:
                marks_at[m] = i

    encoders = []
    webm = out_dir / f"{slug}.webm"
    mp4 = out_dir / f"{slug}.mp4"
    common = ["ffmpeg", "-loglevel", "error", "-y", "-f", "rawvideo", "-pix_fmt", "rgb24", "-s", f"{out_w}x{out_h}", "-r", str(fps), "-i", "-", "-an"]
    encoders.append(subprocess.Popen(common + ["-c:v", "libvpx-vp9", "-b:v", "0", "-crf", "36", "-row-mt", "1", "-deadline", "good", "-cpu-used", "4",
                                               "-pix_fmt", "yuv420p", str(webm)], stdin=subprocess.PIPE))
    encoders.append(subprocess.Popen(common + ["-c:v", "libx264", "-profile:v", "high", "-crf", "22", "-preset", "slow", "-pix_fmt", "yuv420p",
                                               "-movflags", "+faststart", str(mp4)], stdin=subprocess.PIPE))

    blank, prev, still_runs, run_len = 0, None, [], 0
    prev_final = None
    diffs = []
    rendered = []
    hl_since, last_hl, last_down, press_age = 0, None, False, 99
    for i, img in enumerate(decode(master, info["width"], info["height"])):
        f = frames[i]
        base = img.crop(crop).resize((out_w, out_h), Image.LANCZOS)
        if luminance_std(base) < 2.0:
            blank += 1
        if prev is not None:
            d = ImageStat.Stat(ImageChops.difference(base, prev).convert("L")).mean[0]
            diffs.append(d)
        prev = base
        canvas = base.convert("RGBA")
        hl = tuple(f["hl"]) if "hl" in f else None
        if hl != last_hl:
            hl_since, last_hl = i, hl
        if hl:
            canvas = ov.outline(canvas, hl, min(1.0, (i - hl_since + 1) / 9.0))
        if f["down"] and not last_down:
            press_age = 0
        last_down = f["down"]
        if press_age < 99:
            canvas = ov.press(canvas, f["x"], f["y"], press_age)
            press_age += 1
        if "badge" in f:
            canvas = ov.badge(canvas, f["bx"], f["by"], f["badge"])
        if f.get("sustain"):
            canvas = ov.label(canvas, "Sustain pedal down")
        if f["shown"] or "badge" in f:
            canvas = ov.pointer(canvas, f["x"], f["y"])
        final = canvas.convert("RGB")
        # "Frozen" is judged on what the viewer sees (the pointer moving over a still interface
        # is not a frozen video): the annotated frame against the previous annotated frame.
        if prev_final is not None:
            fd = ImageStat.Stat(ImageChops.difference(final, prev_final).convert("L")).mean[0]
            run_len = run_len + 1 if fd < 0.01 else 0
            still_runs.append(run_len)
        prev_final = final
        data = final.tobytes()
        for e in encoders:
            e.stdin.write(data)
        rendered.append((i, base))   # (unannotated, for stills selection)
        if len(rendered) > 64:
            rendered.pop(0)
        # step stills: the last frame of each mark, annotated as the viewer sees it at that moment
        nxt = frames[i + 1].get("marks") if i + 1 < len(frames) else "END"
        if nxt:
            current = None
            for m, at in sorted(marks_at.items(), key=lambda kv: kv[1]):
                if at <= i:
                    current = m
            if current:
                still = final.resize((960, round(final.height * 960 / final.width)), Image.LANCZOS) if final.width > 960 else final
                still.save(out_dir / "steps" / f"{current}.webp", "WEBP", quality=82)
        if i == len(frames) - 1:
            last_frame = final
    for e in encoders:
        e.stdin.close()
        e.wait()
        check(f"encoder exit ({e.args[e.args.index('-c:v') + 1]})", e.returncode == 0)

    # Soundtrack: the instrument's own output, recorded by the runner in step with the frames
    # (frame n starts at sample n * 1600). Levelled to one loudness for every clip, a short fade
    # at each end for the loop, then muxed in without touching the video.
    audio = None
    audio_src = run_dir / "audio.wav"
    silent_clip = False
    if audio_src.exists() and not run.get("notesPlayed"):
        # A clip that plays no notes (saving, exporting) has nothing to hear: it stays silent.
        silent_clip = audio_stats(audio_src)["I"] <= -60.0
        if silent_clip:
            check("no notes played: clip stays silent", True)
    if audio_src.exists() and not silent_clip:
        stats = audio_stats(audio_src)
        check("audio length = video length", abs(stats["duration"] - len(frames) / fps) < 0.01, f'{stats["duration"]:.3f} s')
        check("audio is not silent", stats["I"] > -60.0, f'{stats["I"]:.1f} LUFS before levelling')
        master_wav = run_dir / "audio-master.wav"
        dur = len(frames) / fps
        level = (f'loudnorm=I={TARGET_LUFS}:TP=-1.5:LRA=20:measured_I={stats["I"]}:measured_TP={stats["TP"]}:'
                 f'measured_LRA={stats["LRA"]}:measured_thresh={stats["thresh"]}:offset={stats["offset"]}:linear=true')
        subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-i", str(audio_src), "-af",
                        f"{level},aresample=48000,afade=t=in:d=0.02,afade=t=out:st={dur - 0.3:.3f}:d=0.3", "-c:a", "pcm_f32le", str(master_wav)], check=True)
        for path, codec in ((webm, ["-c:a", "libopus", "-b:a", "96k"]), (mp4, ["-c:a", "aac", "-b:a", "128k"])):
            tmp = path.with_name(path.stem + ".tmp" + path.suffix)
            r = subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-i", str(path), "-i", str(master_wav), "-map", "0:v", "-map", "1:a",
                                "-c:v", "copy", *codec, "-t", f"{dur:.3f}"] + (["-movflags", "+faststart"] if path.suffix == ".mp4" else []) + [str(tmp)])
            check(f"soundtrack muxed ({path.suffix[1:]})", r.returncode == 0)
            if r.returncode == 0:
                tmp.replace(path)
        after = audio_stats(master_wav)
        check(f"audio levelled to {TARGET_LUFS} LUFS (±1.5)", abs(after["I"] - TARGET_LUFS) <= 1.5, f'{after["I"]:.1f} LUFS, true peak {after["TP"]:.1f} dBTP')
        audio = {"source": "the instrument's output during the filmed frames", "loudness": round(after["I"], 1), "truePeak": round(after["TP"], 1),
                 "webm": "opus 96k", "mp4": "aac 128k"}

    # Poster: in the final hold, the frame that changes least (nothing animating), unannotated
    # except for what is on screen in the instrument itself.
    tail = rendered[-min(len(rendered), int(fps * 1.0)):]
    best = min(tail, key=lambda t: diffs[t[0] - 1] if t[0] > 0 else 0)
    poster_png, poster_webp = run_dir / "poster.png", out_dir / "poster.webp"   # PNG master stays outside Git
    best[1].save(poster_png)
    best[1].save(poster_webp, "WEBP", quality=88)

    check("no blank frames", blank == 0, f"{blank} blank")
    holds = sum(float(x.get("seconds", 0)) for s in scenario["steps"] for x in s["do"] if x.get("do") == "hold")
    longest_still = max(still_runs) / fps if still_runs else 0
    # A stuck capture shows as a long run of identical frames; calm holds between actions are wanted.
    limit = max(2.5, max(x.get("seconds", 0) for s in scenario["steps"] for x in s["do"] if x.get("do") in ("hold", "hover")) + 1.0)
    check(f"no frozen run longer than {limit:.1f} s", longest_still <= limit + 1e-9, f"longest unchanged run {longest_still:.2f} s")
    w_info, m_info = probe(webm), probe(mp4)
    for name, inf in (("webm", w_info), ("mp4", m_info)):
        check(f"{name} frames = master", inf["frames"] == len(frames), f'{inf["frames"]}')
        check(f"{name} size", (inf["width"], inf["height"]) == (out_w, out_h), f'{inf["width"]}x{inf["height"]}')
    duration = len(frames) / fps
    check("duration 6-15 s (one clip) or declared", 6.0 <= duration <= float(scenario.get("maxSeconds", 15.0)) + 1e-9, f"{duration:.2f} s")
    check("poster not blank", luminance_std(best[1]) > 2.0)

    approved = pathlib.Path(a.assets) / slug / "approved-poster.png"
    if approved.exists():
        ref = Image.open(approved).convert("RGB").resize(best[1].size)
        diff = ImageStat.Stat(ImageChops.difference(ref, best[1]).convert("L")).mean[0]
        check("poster vs approved poster (mean abs diff < 3)", diff < 3.0, f"{diff:.2f}")
        visual = round(diff, 2)
    else:
        check("approved baseline", True, "none yet: first capture (to be approved)")
        visual = None

    status = "passed" if all(c["passed"] for c in checks) else "failed"
    rel = lambda p: str(p.relative_to(pathlib.Path(a.assets)))
    entry = {
        "schemaVersion": 1,
        "kind": "andor.learn.assets.entry",
        "scenario": scenario["id"],
        "title": scenario.get("title", ""),
        "scenarioDigest": "sha256:" + sha256(a.scenario),
        "product": scenario["product"],
        "productCommit": a.commit,
        "environment": {"os": "linux", "display": "Xvfb + openbox", "render": "JUCE software renderer, ceramic look, 2x",
                        "timing": "frame clock: 1600 samples of audio and one message-loop pass per frame; wall-clock UI timings run faster than the video (see run.slowdown)"},
        "guides": scenario.get("guides", []),
        "learn": scenario.get("learn", {}),
        "targets": scenario.get("dependsOn", {}).get("targets", []),
        "fixtures": run.get("fixtures", []),
        "disclosures": scenario.get("disclosures", []),
        "source": {"width": info["width"], "height": info["height"], "fps": fps, "frames": len(frames), "crop": list(crop)},
        "video": {"width": out_w, "height": out_h, "fps": fps, "frames": len(frames), "duration": round(duration, 3),
                  "files": {"webm": {"path": rel(webm), "codec": "vp9", "bytes": webm.stat().st_size, "sha256": sha256(webm)},
                            "mp4": {"path": rel(mp4), "codec": "h264", "bytes": mp4.stat().st_size, "sha256": sha256(mp4)}}},
        "audio": audio,
        "marks": {m: round(at / fps, 3) for m, at in sorted(marks_at.items(), key=lambda kv: kv[1])},
        "steps": {m: rel(out_dir / "steps" / f"{m}.webp") for m in marks_at},
        "poster": {"path": rel(poster_webp), "frame": best[0], "sha256": sha256(poster_webp), "master": str(poster_png)},
        "run": {"wallSeconds": run["wallSeconds"], "meanFrameMs": round(run["meanFrameMs"], 1),
                "slowdown": round(run["meanFrameMs"] / (1000.0 / fps), 2), "notesPlayed": run.get("notesPlayed")},
        "validation": {"status": status, "expectations": f'{run["expectationsPassed"]}/{len(run["expectations"])}',
                       "blankFrames": blank, "approvedPosterDiff": visual, "checks": checks},
        "approved": False,
    }
    (out_dir / "entry.json").write_text(json.dumps(entry, indent=2))
    (run_dir / "post-report.json").write_text(json.dumps({"scenario": scenario["id"], "status": status, "checks": checks,
                                                          "expectations": run["expectations"]}, indent=2))
    print(f"{scenario['id']}: {status}  {out_w}x{out_h}  {duration:.1f} s  webm {webm.stat().st_size // 1024} KB  mp4 {mp4.stat().st_size // 1024} KB")
    for c in checks:
        if not c["passed"]:
            print("  FAILED:", c["check"], c["detail"])
    return 0 if status == "passed" else 1


if __name__ == "__main__":
    sys.exit(main())
