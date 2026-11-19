#!/usr/bin/env python3
"""A deterministic technical test performance (never flagship material): every case the keyboard
and control animation must handle, in 10 s at 120 BPM.

  python3 examples/make_test_performance.py <out dir>   -> test_performance.mid, test_automation.csv

  0.0-1.0  soft legato (velocity 30): C4 -> E4 overlapping by 60 ms
  1.0-2.0  hard staccato (velocity 120): G4 x2
  2.0-3.0  fast repeated notes: C5 x8 at 1/16 (125 ms), velocity 90
  3.0-5.0  sustained chord C3 E3 G3 B3, mod wheel swell 0 -> 127, pitch bend up and back
  5.0-7.0  held chord F3 A3 C4 with the arpeggiator on (automation): the keys stay down as held,
           the arpeggio shows only in the interface
  7.0-9.0  legato melody with FILTER, REIMAGINED, MOVEMENT, SPACE and layer A TUNE automated
  9.1-9.9  CHARACTER moved by MIDI CC 22 over a held note (does the editor knob follow a CC macro?)
"""
import os
import struct
import sys

out = sys.argv[1]
os.makedirs(out, exist_ok=True)
PPQ, TEMPO = 960, 500000   # 120 BPM: one beat = 0.5 s, so 1 s = 1920 ticks
tick = lambda s: int(round(s * 2 * PPQ))
ev = []   # (tick, bytes)
note = lambda on, off, n, v: ev.extend([(tick(on), bytes([0x90, n, v])), (tick(off), bytes([0x80, n, 64]))])

note(0.00, 0.56, 60, 30); note(0.50, 1.00, 64, 30)
note(1.00, 1.15, 67, 120); note(1.50, 1.65, 67, 120)
for i in range(8):
    note(2.0 + i * 0.125, 2.0 + i * 0.125 + 0.09, 72, 90)
for n in (48, 52, 55, 59):
    note(3.0, 4.95, n, 80)
for i in range(41):   # mod swell 3.0 -> 4.0
    ev.append((tick(3.0 + i * 0.025), bytes([0xB0, 1, min(127, round(i * 127 / 40))])))
for i in range(41):   # pitch bend up and back 4.0 -> 4.8
    import math
    v = 8192 + round(4096 * math.sin(math.pi * i / 40))
    ev.append((tick(4.0 + i * 0.02), bytes([0xE0, v & 0x7F, (v >> 7) & 0x7F])))
ev.append((tick(4.9), bytes([0xB0, 1, 0])))
for n in (53, 57, 60):
    note(5.0, 6.95, n, 85)
mel = [(7.0, 62), (7.4, 64), (7.8, 65), (8.2, 67), (8.6, 69)]
for i, (t, n) in enumerate(mel):
    note(t, t + 0.45, n, 95)   # 50 ms legato overlap
# 9.1-9.9: CHARACTER performed by MIDI CC 22 (OSP's CC 20-27 macro controllers), a held note under it,
# to verify whether the editor's knob follows a CC-performed macro (the host parameter does not move).
note(9.1, 9.95, 57, 80)
for i in range(33):
    ev.append((tick(9.1 + i * 0.025), bytes([0xB0, 22, 40 + round(70 * i / 32)])))

ev.sort(key=lambda e: (e[0], 0 if (e[1][0] & 0xF0) == 0x80 else 1))


def vlq(n):
    b = [n & 0x7F]
    n >>= 7
    while n:
        b.insert(0, (n & 0x7F) | 0x80)
        n >>= 7
    return bytes(b)


trk0 = vlq(0) + b"\xff\x51\x03" + TEMPO.to_bytes(3, "big") + vlq(0) + b"\xff\x58\x04\x04\x02\x18\x08" + vlq(0) + b"\xff\x2f\x00"
body, last = b"", 0
for t, b in ev:
    body += vlq(t - last) + b
    last = t
body += vlq(0) + b"\xff\x2f\x00"
data = b"MThd" + struct.pack(">IHHH", 6, 1, 2, PPQ)
for tr in (trk0, body):
    data += b"MTrk" + struct.pack(">I", len(tr)) + tr
open(os.path.join(out, "test_performance.mid"), "wb").write(data)

rows = [("time_s", "param", "value")]
rows += [(0.0, "arp.enabled", 0), (4.99, "arp.enabled", 0), (5.0, "arp.enabled", 1), (6.99, "arp.enabled", 1), (7.0, "arp.enabled", 0)]
rows += [(0.0, "character", 74), (7.0, "character", 74), (8.0, "character", 35), (9.0, "character", 88)]
rows += [(0.0, "reimagined", 0), (7.0, "reimagined", 0), (8.5, "reimagined", 60), (9.5, "reimagined", 20)]
rows += [(0.0, "motion", 28), (7.2, "motion", 28), (9.0, "motion", 70)]
rows += [(0.0, "space", 46), (7.5, "space", 46), (9.5, "space", 80)]
rows += [(0.0, "layerA.tune", 0), (7.6, "layerA.tune", 0), (8.0, "layerA.tune", 7), (8.8, "layerA.tune", 0)]
with open(os.path.join(out, "test_automation.csv"), "w") as f:
    for r in rows:
        f.write(",".join(str(x) for x in r) + "\n")
print("wrote", out)
