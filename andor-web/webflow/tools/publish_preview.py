#!/usr/bin/env python3
"""Publish a hosted preview of the page to the R2 bucket (stdlib only), for dialling in the design.

  publish_preview.py <account id> <bucket> <dist dir> [--prefix osp]

Uploads <dist>/andor.min.css and andor.min.js to <prefix>/v1/ (short cache) and <dist>/preview.html to
<prefix>/preview/index.html (no-cache). Build first with
  node webflow/build.mjs --preview --media https://<bucket public URL>/osp/v1 --waitlist <endpoint> --out <dist>
Needs CLOUDFLARE_API_TOKEN with 'Workers R2 Storage: Edit' (in a cloud session whose proxy injects the
token, set it to any placeholder). Media is published separately with deploy_r2.py.
"""
import os, sys, urllib.request

ACC, BUCKET, DIST = sys.argv[1:4]
PREFIX = sys.argv[sys.argv.index("--prefix") + 1] if "--prefix" in sys.argv else "osp"
API = f"https://api.cloudflare.com/client/v4/accounts/{ACC}/r2/buckets/{BUCKET}/objects/"
TOKEN = os.environ.get("CLOUDFLARE_API_TOKEN", "proxy-injected")
for name, key, ctype, cache in [
    ("andor.min.css", f"{PREFIX}/v1/andor.min.css", "text/css; charset=utf-8", "public, max-age=300"),
    ("andor.min.js", f"{PREFIX}/v1/andor.min.js", "text/javascript; charset=utf-8", "public, max-age=300"),
    ("preview.html", f"{PREFIX}/preview/index.html", "text/html; charset=utf-8", "no-cache"),
]:
    with open(os.path.join(DIST, name), "rb") as f:
        req = urllib.request.Request(API + key, data=f.read(), method="PUT",
                                     headers={"Authorization": f"Bearer {TOKEN}", "Content-Type": ctype, "Cache-Control": cache})
    with urllib.request.urlopen(req, timeout=120) as r:
        print(r.status, key)
