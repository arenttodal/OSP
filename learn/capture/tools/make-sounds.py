#!/usr/bin/env python3
"""Writes the basic tutorial sounds the captures use where no recording fits.

  python3 learn/capture/tools/make-sounds.py      # -> learn/capture/fixtures/generated/*.wav

Pure arithmetic (no randomness), so every run writes identical files:
  Saw C4.wav    a band-limited sawtooth at C4, 6 s, held
  Sine C4.wav   a sine at C4, 6 s, held
  Pluck C4.wav  a sine at C4 with a very short decay, 1.2 s
48 kHz, 24-bit, mono. Needs only the Python standard library.
"""
import math
import pathlib
import struct
import wave

SR = 48000
C4 = 261.6255653005986
OUT = pathlib.Path(__file__).resolve().parent.parent / "fixtures" / "generated"


def envelope(n, attack, release):
    a, r = int(attack * SR), int(release * SR)
    return [min(1.0, i / a if a else 1.0, (n - 1 - i) / r if r else 1.0) for i in range(n)]


def saw(t):
    # Additive sawtooth with every harmonic below 18 kHz, so nothing aliases.
    return sum(math.sin(2 * math.pi * C4 * k * t) / k for k in range(1, int(18000 / C4) + 1))


def write(name, samples, peak_db):
    top = max(abs(x) for x in samples) or 1.0
    gain = 10 ** (peak_db / 20) / top
    frames = bytearray()
    for x in samples:
        v = max(-8388607, min(8388607, round(x * gain * 8388607)))
        frames += struct.pack("<i", v)[:3]
    OUT.mkdir(parents=True, exist_ok=True)
    with wave.open(str(OUT / name), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(3)
        w.setframerate(SR)
        w.writeframes(bytes(frames))
    print("wrote", OUT / name)


def main():
    n = 6 * SR
    env = envelope(n, 0.015, 0.4)
    write("Saw C4.wav", [saw(i / SR) * env[i] for i in range(n)], -6.0)
    write("Sine C4.wav", [math.sin(2 * math.pi * C4 * i / SR) * env[i] for i in range(n)], -6.0)
    n = int(1.2 * SR)
    attack = int(0.003 * SR)
    write("Pluck C4.wav", [math.sin(2 * math.pi * C4 * i / SR) * min(1.0, i / attack) * math.exp(-(i / SR) / 0.12) for i in range(n)], -3.0)


if __name__ == "__main__":
    main()
