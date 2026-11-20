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

GROUPS = [
    ("A", "Load and play", ["osp.scene.a-load-and-play"]),
    ("B", "Phrase Transformer", ["osp.scene.b-phrase-map-notes"]),
    ("C", "Live recording", ["osp.scene.c1-record-input", "osp.scene.c2-grab-last"]),
    ("D", "Arpeggiator", ["osp.scene.d1-arp-per-source", "osp.scene.d2-arp-strum", "osp.scene.d3-satellite"]),
]

STEP_TEXT = {
    "drop-file": "Drag an audio file onto the empty sound area.",
    "wait-ready": "OSP analyses the recording; the card shows it.",
    "play-keys": "Play notes from the keyboard.",
    "drop-phrase": "Drop a recording of several notes. OSP asks: Identify notes?",
    "map-notes": "Click MAP NOTES. Each note found is placed on its own key.",
    "open-notes": "Click NOTES to see the notes and the keys they play.",
    "play-mapped": "Play the keys: each plays a recorded note.",
    "open-record": "On an empty instrument, click Record… to open CAPTURE.",
    "record": "Click RECORD and play into the input.",
    "stop": "Click STOP. The take is trimmed and its notes are mapped.",
    "open-capture": "Click REC on the card while audio arrives at the input.",
    "grab-last": "Click 10 s under GRAB LAST to keep what was just played.",
    "linked": "With LINK on, every layer plays the same pattern.",
    "link-off": "Click LINK to turn it off. EDIT A | B | C appears.",
    "edit-b": "Choose EDIT B, then a pattern for layer B (Down).",
    "edit-c": "Choose EDIT C, then another pattern (Up/Down).",
    "plays-off-a": "Click A under PLAYS: layer A holds the chord while B and C arpeggiate.",
    "choose-chord": "With EDIT C chosen, choose Chord in PATTERN.",
    "open-strum": "Click the name STRUM to open its panel.",
    "strum-on": "Switch STRUM on: the chord's notes now follow one another.",
    "direction": "Choose DOWN to strum from the top note.",
    "spread": "Turn SPREAD to lengthen the strum.",
    "open-satellite": "Click the name SATELLITE to open its panel.",
    "satellite-on": "Switch layer A's SATELLITE on: it moves between L and R.",
    "orbit": "Choose ORBIT: a round path, quieter and darker behind.",
    "rate": "Turn RATE to change how fast it moves (a note value in SYNC).",
}


def esc(s):
    return html.escape(str(s), quote=True)


def card(e):
    v = e["video"]
    files = v["files"]
    val = e["validation"]
    marks = sorted(e["marks"].items(), key=lambda kv: kv[1])
    steps = "".join(
        f'<li><button type="button" class="step" data-t="{t}"><span class="n">{i + 1}</span>'
        f'<span class="tx">{esc(STEP_TEXT.get(m, m))}</span><span class="tm">{t:.1f} s</span></button></li>'
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
