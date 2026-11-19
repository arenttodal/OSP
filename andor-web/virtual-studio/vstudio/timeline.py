"""The Virtual Studio timeline: one continuous time base for capture, animation and audio.

Every event is stored in seconds (float) on the session's sample clock (`sampleRate`). Nothing is
stored in frames: the plugin capture snapshots at frame boundaries of whichever frame rate a
render needs (24 fps cinematic, 30 fps delivery), each boundary at sample round(n * sr / fps), and
Blender places key events at their exact (fractional) frame times. Note events are therefore never
reconstructed by interpolating frames.

Note sources: notes read from the performance MIDI are the musician's input and drive the physical
keys ("source": "input"). Notes the instrument generates itself (the arpeggiator) are not in the
input and must never move a key; they appear only in the captured interface (display, keyboard
indicators).
"""
import csv
import json
import math

SCHEMA = 2   # performance.json v2 (v1 = the Phase 5 proof of concept: 30 fps frame-aligned)


def pair_notes(events, end_time):
    """note_on/note_off events -> [{note, channel, on, off, velocity, release_velocity, source}].
    A note-on for a key already down closes the previous one at that instant (a retrigger)."""
    open_notes, notes = {}, []
    for e in events:
        key = (e["channel"], e.get("note"))
        if e["type"] == "note_on":
            if key in open_notes:
                prev = open_notes.pop(key)
                prev["off"] = e["t"]
                notes.append(prev)
            open_notes[key] = {"note": e["note"], "channel": e["channel"], "on": e["t"], "off": None,
                               "velocity": e["velocity"], "release_velocity": None, "source": "input"}
        elif e["type"] == "note_off" and key in open_notes:
            n = open_notes.pop(key)
            n["off"], n["release_velocity"] = e["t"], e.get("velocity")
            notes.append(n)
    for n in open_notes.values():   # held to the end of the excerpt
        n["off"] = end_time
        n["held_to_end"] = True
        notes.append(n)
    notes.sort(key=lambda n: (n["on"], n["note"]))
    return notes


def read_automation(path, tempo_seconds=None):
    """Automation lanes -> {param: [[t, value], ...]} sorted by time.
    CSV: columns `time_s,param,value` (or `beats,param,value` with the MIDI file's tempo map).
    JSON: {param: [[t, value], ...]} (seconds)."""
    lanes = {}
    if path.endswith(".json"):
        lanes = {k: [[float(t), float(v)] for t, v in rows] for k, rows in json.load(open(path)).items()}
    else:
        with open(path, newline="") as f:
            for row in csv.DictReader(f):
                if "time_s" in row:
                    t = float(row["time_s"])
                elif "beats" in row and tempo_seconds is not None:
                    t = tempo_seconds(float(row["beats"]))
                else:
                    raise ValueError(f"{path}: need a time_s column (or beats with a MIDI tempo map)")
                lanes.setdefault(row["param"], []).append([t, float(row["value"])])
    for rows in lanes.values():
        rows.sort()
    return lanes


def build(smf, automation=None, *, sample_rate=48000.0, start=0.0, end=None, tail=1.0, meta=None):
    """A performance (schema 2) from a parsed SMF (smf.read) and optional automation lanes.
    start/end (seconds) select an excerpt; times are re-based so the excerpt starts at 0."""
    ev = [e for e in smf["events"] if e["t"] >= start and (end is None or e["t"] <= end)]
    last = max((e["t"] for e in ev), default=start)
    stop = end if end is not None else last + tail
    rebased = [dict(e, t=e["t"] - start) for e in ev]
    duration = stop - start
    notes = pair_notes([e for e in rebased if e["type"] in ("note_on", "note_off")], duration)
    bend = [[e["t"], max(-1.0, min(1.0, e["value"] / 8192.0))] for e in rebased if e["type"] == "pitch_bend"]
    cc = {}
    for e in rebased:
        if e["type"] == "cc":
            cc.setdefault(str(e["cc"]), []).append([e["t"], e["value"]])
    mod = [[t, v / 127.0] for t, v in cc.get("1", [])]
    sustain = [[t, 1.0 if v >= 64 else 0.0] for t, v in cc.get("64", [])]
    lanes = {}
    for k, rows in (automation or {}).items():
        lanes[k] = [[t - start, v] for t, v in rows if t >= start and (end is None or t <= end)]
    perf = {
        "schemaVersion": SCHEMA,
        "timebase": {"unit": "seconds", "sampleRate": sample_rate, "duration": duration,
                     "excerpt": {"start": start, "end": stop}},
        "notes": notes,
        "pitchBend": bend,
        "modWheel": mod,
        "sustain": sustain,
        "cc": cc,
        "automation": lanes,
        "tempo": [[smf["seconds"](tick) - start, 60e6 / us] for tick, us in smf["tempo"]],
    }
    perf.update(meta or {})
    return perf


def frame_count(duration, fps):
    return int(math.ceil(duration * fps - 1e-9))


def frame_time(n, fps):
    """Start of frame n (0-based) in seconds; the capture snapshots the state at this instant."""
    return n / fps


def frame_sample(n, fps, sample_rate):
    """The sample index where frame n starts (exact for 24/30 fps at 48 kHz; rounded otherwise,
    cumulatively, so frames never drift from the audio)."""
    return int(round(n * sample_rate / fps))


def value_at(rows, t, default=0.0, step=False):
    """A lane's value at time t: linear between points (or held, step=True)."""
    if not rows:
        return default
    if t <= rows[0][0]:
        return rows[0][1]
    for (t0, v0), (t1, v1) in zip(rows, rows[1:]):
        if t0 <= t < t1:
            return v0 if step or t1 == t0 else v0 + (v1 - v0) * (t - t0) / (t1 - t0)
    return rows[-1][1]


def check(perf):
    """Basic sanity: returns a list of problems (empty: fine)."""
    p = []
    d = perf["timebase"]["duration"]
    for n in perf["notes"]:
        if not (0 <= n["on"] <= n["off"] <= d + 1e-6):
            p.append(f"note {n['note']} at {n['on']:.3f}s: on/off out of range")
        if not 1 <= n["velocity"] <= 127:
            p.append(f"note {n['note']} at {n['on']:.3f}s: velocity {n['velocity']}")
    for k, rows in perf["automation"].items():
        if any(b[0] < a[0] for a, b in zip(rows, rows[1:])):
            p.append(f"automation {k}: not time-ordered")
    return p
