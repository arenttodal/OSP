#!/usr/bin/env python3
"""Publish the Webflow page's media to a Cloudflare R2 bucket through the Cloudflare API (stdlib only).

  deploy_r2.py <account id> <bucket> <media dir> [--prefix osp/v1] [--cache <seconds>] [--dry-run]

Needs CLOUDFLARE_API_TOKEN with 'Workers R2 Storage: Edit' on <account id>. It
1. creates the bucket if it does not exist;
2. sets CORS so any site can read (fonts are fetched cross-origin and need it);
3. enables the bucket's public r2.dev URL and prints it (use a custom domain for production traffic);
4. uploads every file under <media dir> to <prefix>/…, with its content type and a cache lifetime:
   a year (immutable) for media under a versioned prefix, an hour for andor.min.css / andor.min.js;
5. re-running uploads everything again (about 57 MB): objects are simply replaced.
"""
import json, mimetypes, os, sys, time, urllib.error, urllib.request

ACC, BUCKET, SRC = sys.argv[1:4]
PREFIX = sys.argv[sys.argv.index("--prefix") + 1] if "--prefix" in sys.argv else "osp/v1"
DRY = "--dry-run" in sys.argv
CACHE = int(sys.argv[sys.argv.index("--cache") + 1]) if "--cache" in sys.argv else None  # e.g. 300 for review pages that change
TOKEN = os.environ["CLOUDFLARE_API_TOKEN"]
API = f"https://api.cloudflare.com/client/v4/accounts/{ACC}/r2/buckets"
TYPES = {".avif": "image/avif", ".webp": "image/webp", ".jpg": "image/jpeg", ".mp4": "video/mp4", ".woff2": "font/woff2",
         ".js": "text/javascript; charset=utf-8", ".css": "text/css; charset=utf-8", ".json": "application/json", ".html": "text/html; charset=utf-8", ".png": "image/png"}


def call(method, url, body=None, headers=None, raw=False, tries=6):
    for k in range(tries):                      # Cloudflare throttles bursts with HTTP 429: back off and retry
        st, j = _call(method, url, body, headers, raw)
        if st != 429:
            return st, j
        time.sleep(min(60, 4 * 2 ** k))
    return st, j


def _call(method, url, body=None, headers=None, raw=False):
    h = {"Authorization": f"Bearer {TOKEN}"}
    h.update(headers or {})
    if body is not None and not isinstance(body, bytes):
        body = json.dumps(body).encode(); h["Content-Type"] = "application/json"
    req = urllib.request.Request(url, data=body, method=method, headers=h)
    try:
        with urllib.request.urlopen(req, timeout=300) as r:
            data = r.read()
            return r.status, (data if raw else json.loads(data or b"{}"))
    except urllib.error.HTTPError as e:
        data = e.read()
        try:
            return e.code, json.loads(data)
        except ValueError:
            return e.code, {"raw": data[:300].decode(errors="replace")}


def ok(st, j, what):
    if st >= 300 or (isinstance(j, dict) and j.get("success") is False):
        sys.exit(f"{what} failed: HTTP {st} {json.dumps(j)[:400]}")


# 1. bucket
st, j = call("GET", f"{API}/{BUCKET}")
if st == 404:   # create only when the bucket is really missing (not on a throttle or auth error)
    if DRY:
        print("would create bucket", BUCKET)
    else:
        st, j = call("POST", API, {"name": BUCKET}); ok(st, j, "create bucket"); print("created bucket", BUCKET)
elif st >= 300:
    sys.exit(f"bucket check failed: HTTP {st} {json.dumps(j)[:300]}")
else:
    print("bucket exists:", BUCKET)
if not DRY:
    # 2. CORS: read-only from anywhere
    st, j = call("PUT", f"{API}/{BUCKET}/cors", {"rules": [{"allowed": {"origins": ["*"], "methods": ["GET", "HEAD"], "headers": ["*"]},
                                                            "exposeHeaders": ["Content-Length", "Content-Type"], "maxAgeSeconds": 86400}]})
    ok(st, j, "set CORS")
    # 3. public r2.dev URL
    st, j = call("PUT", f"{API}/{BUCKET}/domains/managed", {"enabled": True}); ok(st, j, "enable public URL")
    domain = (j.get("result") or {}).get("domain")
    print("public URL:", f"https://{domain}/{PREFIX}" if domain else "(see the bucket's settings)")

# 4. upload
files = sorted(os.path.join(dp, f) for dp, _, fs in os.walk(SRC) for f in fs)
total = up = 0
for path in files:
    rel = os.path.relpath(path, SRC).replace(os.sep, "/")
    key = f"{PREFIX}/{rel}"
    ext = os.path.splitext(path)[1].lower()
    ctype = TYPES.get(ext) or mimetypes.guess_type(path)[0] or "application/octet-stream"
    if CACHE is not None:
        cache = f"public, max-age={CACHE}"
    elif ext == ".html":
        cache = "no-cache"
    else:
        cache = "public, max-age=3600" if rel in ("andor.min.css", "andor.min.js", "media.json") else "public, max-age=31536000, immutable"
    size = os.path.getsize(path); total += size
    if DRY:
        continue
    with open(path, "rb") as f:
        st, j = call("PUT", f"{API}/{BUCKET}/objects/{key}", f.read(), {"Content-Type": ctype, "Cache-Control": cache})
    ok(st, j, f"upload {key}")
    up += 1
    if up % 25 == 0:
        print(f"  {up}/{len(files)} uploaded")
print(f"{len(files)} files, {total / 1e6:.1f} MB; uploaded {up}{' (dry run)' if DRY else ''}")
