#!/usr/bin/env python3
"""Collects every scene's entry.json into learn/assets/osp/manifest.json (andor.learn.assets)."""
import json, pathlib, sys
assets = pathlib.Path(sys.argv[1])
entries = [json.loads(p.read_text()) for p in sorted(assets.glob("*/entry.json"))]
manifest = {"schemaVersion": 1, "kind": "andor.learn.assets", "product": "osp",
            "productCommit": sys.argv[2] if len(sys.argv) > 2 else "", "studioCommit": sys.argv[3] if len(sys.argv) > 3 else "",
            "scenes": entries}
(assets / "manifest.json").write_text(json.dumps(manifest, indent=2))
print(f"manifest: {len(entries)} scenes, {sum(1 for e in entries if e['validation']['status'] == 'passed')} passed validation, "
      f"{sum(1 for e in entries if e['approved'])} approved")
