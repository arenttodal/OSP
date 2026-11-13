#!/usr/bin/env python3
"""Writes the MS5 demo inputs for automatic multi-sample mapping (no recorded audio).

  python3 make_demo.py <out-dir>

<out-dir>/violin-pizz/   12 synthetic pizzicato one-shots (Karplus-Strong string plus a
                         small violin-like body), named as a sample library names them:
                         Violin_Pizz_<note>_<take>.wav. Takes of one note differ by their
                         pluck noise and their level (C4's three are 0, -5 and -9 dB: unlabeled
                         louder and softer performances, which must stay round robins).
<out-dir>/midi/          the demo phrases (SMF type 0, 480 ticks per quarter at 120 BPM).

Pure Python and deterministic: the same files on every run (a fixed LCG per take).
"""
import math
import os
import struct
import sys
import wave

RATE = 48000

def lcg(seed):
    state = (seed * 2654435761 + 12345) & 0xFFFFFFFF
    while True:
        state = (1664525 * state + 1013904223) & 0xFFFFFFFF
        yield state / 2147483648.0 - 1.0

def pizz(midi, seed, gain_db, seconds=1.4):
    f0 = 440.0 * 2.0 ** ((midi - 69) / 12.0)
    period = RATE / f0
    # Loop delay = n + allpass (frac) + 0.5 (the averaging filter) = the period: in tune.
    n = int(period - 0.5)
    frac = period - 0.5 - n
    c = (1.0 - frac) / (1.0 + frac)
    noise = lcg(seed)
    # A short, bright excitation (a finger pluck), lightly low-passed.
    buf = [0.0] * n
    prev = 0.0
    for i in range(n):
        prev = 0.6 * next(noise) + 0.4 * prev
        buf[i] = prev
    # Pizzicato decays fast, faster up high.
    t60 = max(0.35, 1.3 - 0.012 * (midi - 55))
    loss = 10.0 ** (-3.0 / (t60 * f0))
    out = []
    total = int(seconds * RATE)
    idx = 0
    last = 0.0
    apx1 = apy1 = 0.0
    for _ in range(total):
        s = buf[idx]
        avg = loss * 0.5 * (s + last)
        last = s
        ap = c * avg + apx1 - c * apy1
        apx1, apy1 = avg, ap
        buf[idx] = ap
        idx = (idx + 1) % n
        out.append(s)
    # Body: two resonances (air ~ 280 Hz, wood ~ 470 Hz) plus a brightness bump ~ 2.8 kHz.
    def resonator(x, freq, q, mix):
        w = 2.0 * math.pi * freq / RATE
        r = math.exp(-w / (2.0 * q))
        a1, a2 = -2.0 * r * math.cos(w), r * r
        y1 = y2 = 0.0
        res = []
        for v in x:
            y = (1.0 - r) * v - a1 * y1 - a2 * y2
            y2, y1 = y1, y
            res.append(y)
        return [xi + mix * yi for xi, yi in zip(x, res)]
    out = resonator(out, 280.0, 8.0, 2.5)
    out = resonator(out, 470.0, 10.0, 2.0)
    out = resonator(out, 2800.0, 3.0, 1.2)
    # A 2 ms fade-in and a 30 ms fade-out; the level.
    peak = max(abs(v) for v in out) or 1.0
    g = 10.0 ** (gain_db / 20.0) * 0.7 / peak
    fade_in, fade_out = int(0.002 * RATE), int(0.03 * RATE)
    for i in range(total):
        e = min(1.0, i / fade_in) * min(1.0, (total - 1 - i) / fade_out)
        out[i] *= g * e
    return out

def write_wav(path, samples):
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(3)
        w.setframerate(RATE)
        frames = bytearray()
        for v in samples:
            iv = max(-8388607, min(8388607, int(round(v * 8388607.0))))
            frames += struct.pack("<i", iv)[:3]
        w.writeframes(bytes(frames))

NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
def note_name(midi):
    return NAMES[midi % 12] + str(midi // 12 - 1)

# (root, take gains in dB). No F roots: a lone "F" in a name reads as forte.
SET = [(55, [0, -1.5]), (60, [0, -5, -9]), (67, [0, -2, -1]), (74, [0, -1.5]), (81, [0, -2])]

def vlq(value):
    out = [value & 0x7F]
    value >>= 7
    while value:
        out.insert(0, (value & 0x7F) | 0x80)
        value >>= 7
    return bytes(out)

def write_midi(path, notes):
    """notes: (start_beats, length_beats, midi, velocity)."""
    tpq = 480
    events = []
    for start, length, midi, vel in notes:
        events.append((int(round(start * tpq)), 1, bytes([0x90, midi, vel])))
        events.append((int(round((start + length) * tpq)), 0, bytes([0x80, midi, 0])))
    events.sort(key=lambda e: (e[0], e[1]))
    track = bytearray(b"\x00\xFF\x51\x03\x07\xA1\x20")   # 120 BPM
    now = 0
    for tick, _, data in events:
        track += vlq(tick - now) + data
        now = tick
    track += b"\x00\xFF\x2F\x00"
    with open(path, "wb") as f:
        f.write(b"MThd" + struct.pack(">IHHH", 6, 0, 1, tpq))
        f.write(b"MTrk" + struct.pack(">I", len(track)) + track)

def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "demo"
    pizz_dir = os.path.join(out, "violin-pizz")
    midi_dir = os.path.join(out, "midi")
    os.makedirs(pizz_dir, exist_ok=True)
    os.makedirs(midi_dir, exist_ok=True)
    for root, gains in SET:
        for take, gain in enumerate(gains):
            name = "Violin_Pizz_%s_%02d.wav" % (note_name(root).replace("#", "s"), take + 1)
            write_wav(os.path.join(pizz_dir, name), pizz(root, 1000 * root + take, gain))
    step = 0.5   # eighth notes at 120 BPM: 0.25 s
    up = list(range(55, 82))
    chromatic = [(i * step, step * 0.9, m, 96) for i, m in enumerate(up + up[-2::-1])]
    write_midi(os.path.join(midi_dir, "chromatic-up-down.mid"), chromatic)
    write_midi(os.path.join(midi_dir, "repeated-C4.mid"), [(i * 1.0, 0.8, 60, 100) for i in range(9)])
    write_midi(os.path.join(midi_dir, "repeated-D4-filled.mid"), [(i * 1.0, 0.8, 62, 100) for i in range(9)])
    chords = []
    for i, chord in enumerate([[55, 62, 71], [60, 64, 67, 72], [62, 69, 74, 78], [55, 59, 67, 74, 79]]):
        chords += [(i * 4.0, 3.0, m, 92) for m in chord]
    write_midi(os.path.join(midi_dir, "chords.mid"), chords)
    melody = [60, 62, 64, 67, 69, 67, 64, 62, 60, 55, 60]
    write_midi(os.path.join(midi_dir, "melody.mid"), [(i * 1.0, 1.4, m, 96) for i, m in enumerate(melody)])
    print("wrote", pizz_dir, "and", midi_dir)

if __name__ == "__main__":
    main()
