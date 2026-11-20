#!/usr/bin/env python3
"""Runs on a rented GPU pod (stdlib only): installs Blender, takes the benchmark bundle over HTTP, runs
run_benchmark.sh on the GPU and serves the results back. Driven from the build machine over the
provider's HTTPS port proxy; every request must carry the pod's X-Bench-Token.

  PUT /bundle.zip   the benchmark bundle (make_bundle.sh); starts the run when Blender is ready
  GET /status       JSON: phase, device, log tail
  GET /results.zip  the bench_<device>/ folder, when phase is "done"

It never holds a provider key, so it cannot stop its own machine: the driver terminates the pod.
"""
import http.server
import json
import os
import shutil
import subprocess
import threading
import zipfile

TOKEN = os.environ["BENCH_TOKEN"]
W = "/work"
BLENDER_URL = "https://download.blender.org/release/Blender4.2/blender-4.2.0-linux-x64.tar.xz"
BLENDER = "/opt/blender/blender"
os.makedirs(W, exist_ok=True)
state = {"phase": "setup", "device": None, "blender_ready": False, "bundle": False, "error": None}
LOG = os.path.join(W, "runner.log")


def log(msg):
    with open(LOG, "a") as f:
        f.write(msg.rstrip() + "\n")


def sh(cmd):
    log("$ " + cmd)
    r = subprocess.run(cmd, shell=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    log(r.stdout[-4000:])
    if r.returncode:
        raise RuntimeError(f"exit {r.returncode}: {cmd}")


def setup():
    try:
        sh("apt-get update -qq && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq xz-utils curl unzip zip "
           "libxrender1 libxi6 libxkbcommon0 libsm6 libgl1 libegl1 libxxf86vm1 libxfixes3 libx11-6 libxext6 > /dev/null")
        sh(f"curl -sSL -o /opt/blender.tar.xz {BLENDER_URL} && mkdir -p /opt/blender && "
           "tar -xJf /opt/blender.tar.xz -C /opt/blender --strip-components=1 && rm /opt/blender.tar.xz")
        sh(f"{BLENDER} --version | head -1; nvidia-smi --query-gpu=name,driver_version,memory.total --format=csv")
        state["blender_ready"] = True
        maybe_run()
    except Exception as e:  # noqa: BLE001
        state.update(phase="error", error=str(e))


def run():
    try:
        state.update(phase="running", error=None)
        d = os.path.join(W, "bundle")
        shutil.rmtree(d, ignore_errors=True)
        zipfile.ZipFile(os.path.join(W, "bundle.zip")).extractall(d)
        sh(f"cd {d} && sha256sum -c --quiet SHA256SUMS && chmod +x run_benchmark.sh")
        for dev in ("OPTIX", "CUDA"):
            state["device"] = dev
            sh(f"cd {d} && ./run_benchmark.sh {dev} {BLENDER}")
            t = json.load(open(os.path.join(d, f"bench_{dev}", "timings.json")))
            if t and not any("no " in n for n in t[0]["devices"]):
                break
            log(f"{dev}: no GPU device found, trying the next backend")
        out = os.path.join(W, "results.zip")
        sh(f"cd {d} && rm -f {out} && zip -qr {out} bench_{state['device']} && nvidia-smi > bench_nvidia_smi.txt && zip -q {out} bench_nvidia_smi.txt")
        state["phase"] = "done"
    except Exception as e:  # noqa: BLE001
        state.update(phase="error", error=str(e))


def maybe_run():
    if state["blender_ready"] and state["bundle"] and state["phase"] in ("setup", "ready", "done", "error"):
        threading.Thread(target=run, daemon=True).start()
    elif state["blender_ready"] and state["phase"] == "setup":
        state["phase"] = "ready"


class H(http.server.BaseHTTPRequestHandler):
    def ok(self):
        if self.headers.get("X-Bench-Token") != TOKEN:
            self.send_error(403)
            return False
        return True

    def do_GET(self):
        if not self.ok():
            return
        if self.path == "/status":
            tail = open(LOG).read()[-3000:] if os.path.exists(LOG) else ""
            body = json.dumps(dict(state, log=tail)).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
        elif self.path == "/results.zip" and state["phase"] == "done":
            body = open(os.path.join(W, "results.zip"), "rb").read()
            self.send_response(200)
            self.send_header("Content-Type", "application/zip")
        else:
            self.send_error(404)
            return
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_PUT(self):
        if not self.ok():
            return
        if self.path != "/bundle.zip":
            self.send_error(404)
            return
        n = int(self.headers["Content-Length"])
        with open(os.path.join(W, "bundle.zip"), "wb") as f:
            while n > 0:
                chunk = self.rfile.read(min(n, 1 << 20))
                f.write(chunk)
                n -= len(chunk)
        state["bundle"] = True
        maybe_run()
        self.send_response(200)
        self.end_headers()
        self.wfile.write(b"ok\n")

    def log_message(self, *a):
        pass


threading.Thread(target=setup, daemon=True).start()
http.server.ThreadingHTTPServer(("0.0.0.0", 8000), H).serve_forever()
