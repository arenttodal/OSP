#!/usr/bin/env python3
"""Builds the Gate 1 review page from learn/assets/osp/manifest.json.

  python3 learn/review/build.py            # writes learn/review/index.html
  (serve the learn/ folder, e.g. `python3 -m http.server -d learn`, and open /review/)

The page reads nothing by hand: every title, file, mark, check and disclosure comes from the
manifest the capture pipeline wrote. Paths are relative to learn/ (assets/osp/...), so the same
page works served from learn/ and published with its assets beside it.
"""
import argparse
import html
import json
import pathlib

ap = argparse.ArgumentParser()
ap.add_argument("--prefix", default="../assets/osp/", help="URL of learn/assets/osp/ as seen from the page")
ap.add_argument("--out", default=None, help="where to write the page (default learn/review/index.html)")
args = ap.parse_args()
P = args.prefix

root = pathlib.Path(__file__).resolve().parent.parent
manifest = json.loads((root / "assets/osp/manifest.json").read_text())
by_id = {e["scenario"]: e for e in manifest["scenes"]}

# Groups, guide titles and step captions come from the public guides (learn/site/src/content/guides),
# so the board and the site never disagree about what a step says.
import re
GROUP_TITLES = [("make-an-instrument", "Make an instrument"), ("play-and-layer", "Play and layer"), ("synth-imprint", "SYNTH & IMPRINT"),
                ("arpeggiator", "Arpeggiator"), ("granular-key-dna", "Granular & KEY DNA"), ("shape-and-finish", "Shape and finish")]
guides = []
for f in sorted((root / "site/src/content/guides").glob("*.mdx")):
    fm = f.read_text().split("---")[1]
    get = lambda k: (re.search(rf"^{k}: (.*)$", fm, re.M) or [None, ""])[1]
    steps = {m.group(1): json.loads(m.group(2)) for m in re.finditer(r'^  - \{ id: ([a-z0-9-]+), instruction: ("(?:[^"\\]|\\.)*")', fm, re.M)}
    guides.append({"group": get("group"), "order": int(get("order") or 0), "scene": get("scene"), "title": json.loads(get("title") or '""'), "steps": steps,
                   "about": json.loads(get("about") or '""')})
STEP_TEXT = {}
for g in guides:
    for k, v in g["steps"].items():
        STEP_TEXT[(g["scene"], k)] = v
ABOUT = {g["scene"]: g["about"] for g in guides}
GROUPS = []
for i, (slug, title) in enumerate(GROUP_TITLES):
    ids = [g["scene"] for g in sorted(guides, key=lambda g: g["order"]) if g["group"] == slug]
    GROUPS.append((str(i + 1), title, ids))

def esc(s):
    return html.escape(str(s), quote=True)


def card(e):
    v = e["video"]
    files = v["files"]
    val = e["validation"]
    marks = sorted(e["marks"].items(), key=lambda kv: kv[1])
    steps = "".join(
        f'<li><button type="button" class="step" data-t="{t}"><span class="n">{i + 1}</span>'
        f'<span class="tx">{esc(STEP_TEXT.get((e["scenario"], m), m))}</span><span class="tm">{t:.1f} s</span></button></li>'
        for i, (m, t) in enumerate(marks))
    checks = "".join(
        f'<li class="{"ok" if c["passed"] else "bad"}">{"✓" if c["passed"] else "✗"} {esc(c["check"])}'
        f'{(" — " + esc(c["detail"])) if c.get("detail") else ""}</li>' for c in val["checks"])
    fixtures = "".join(f'<li><code>{esc(f["id"])}</code> {esc(f["file"])} <span class="sha">{esc(f["sha256"][:12])}</span></li>' for f in e["fixtures"])
    disclosures = "".join(f"<li>{esc(d)}</li>" for d in e["disclosures"])
    status = val["status"]
    return f'''
    <article class="scene" id="{esc(e["scenario"].split(".")[-1])}">
      <header><h3>{esc(e["title"])}</h3><span class="pill {status}">{esc(status)}</span></header>
      <div class="player">
        <video muted loop playsinline preload="metadata" poster="{esc(P + e["poster"]["path"])}" width="{v["width"]}" height="{v["height"]}">
          <source src="{esc(P + files["webm"]["path"])}" type="video/webm">
          <source src="{esc(P + files["mp4"]["path"])}" type="video/mp4">
        </video>
      </div>
      <ol class="steps">{steps}</ol>
      <dl class="meta">
        <div><dt>Duration</dt><dd>{v["duration"]:.1f} s · {v["frames"]} frames · {v["fps"]} fps</dd></div>
        <div><dt>Output</dt><dd>{v["width"]}×{v["height"]} · WebM {files["webm"]["bytes"] // 1024} KB · MP4 {files["mp4"]["bytes"] // 1024} KB</dd></div>
        <div><dt>Expectations</dt><dd>{esc(val["expectations"])} passed</dd></div>
        <div><dt>Capture</dt><dd>{e["run"]["meanFrameMs"]:.0f} ms per frame (slowdown ×{e["run"]["slowdown"]})</dd></div>
      </dl>
      <details><summary>Checks, fixtures and disclosures</summary>
        <ul class="checks">{checks}</ul>
        <h4>Fixtures</h4><ul class="plain">{fixtures}</ul>
        {"<h4>Disclosures</h4><ul class='plain'>" + disclosures + "</ul>" if disclosures else ""}
        <p class="small">Scenario <code>{esc(e["scenario"])}</code> · product commit <code>{esc(e["productCommit"])}</code> ·
        <a href="{esc(P + files["mp4"]["path"])}">MP4</a> · <a href="{esc(P + files["webm"]["path"])}">WebM</a> · <a href="{esc(P + e["poster"]["path"])}">poster</a></p>
      </details>
      {f'<p class="about"><span class="label">What this shows</span>{esc(ABOUT[e["scenario"]])}</p>' if ABOUT.get(e["scenario"]) else ""}
    </article>'''


sections = []
for letter, name, ids in GROUPS:
    present = [by_id[i] for i in ids if i in by_id]
    missing = [i for i in ids if i not in by_id]
    body = "".join(card(e) for e in present)
    if missing:
        body += "".join(f'<article class="scene missing"><p>Not captured: <code>{esc(m)}</code></p></article>' for m in missing)
    sections.append(f'<section class="demo" id="demo-{letter}"><h2><span class="letter">{letter}</span>{esc(name)}</h2><div class="grid">{body}</div></section>')

total = len(manifest["scenes"])
passed = sum(1 for e in manifest["scenes"] if e["validation"]["status"] == "passed")
page = (root / "review/template.html").read_text()
page = page.replace("{{SECTIONS}}", "".join(sections))
page = page.replace("{{SUMMARY}}", f"{passed} of {total} scenes passed validation · product commit {esc(manifest['productCommit'])} · none approved yet")
(pathlib.Path(args.out) if args.out else root / "review/index.html").write_text(page)
print(f"review page: {total} scenes")
