"""The reference corpus: every fixture as data (notes, instrument, duration, what is hard about it).

Classes (spec §6): A single piano notes, B chords, C overlapping attacks, D a phrase (synthetic
stand-in until a recorded performance is supplied), E sustain pedal, F other instruments.
G (mixed piano / bass / vocal stems) belongs to P5 and is not generated here.
"""

from __future__ import annotations

from dataclasses import dataclass, field

from .synth import Played

N = {"C2": 36, "G2": 43, "C3": 48, "E3": 52, "G3": 55, "A3": 57, "B3": 59, "C4": 60, "D4": 62, "E4": 64, "F4": 65,
     "F#4": 66, "G4": 67, "A4": 69, "B4": 71, "C5": 72, "D5": 74, "E5": 76, "G5": 79, "C6": 84}


@dataclass
class Fixture:
    id: str
    cls: str                      # A..F
    family: str                   # piano | organ | guitar | strings | voice
    complexity: str               # single | chord | overlap | phrase | pedal | other
    duration: float
    notes: list[Played]
    pedal: bool = False
    expected_failures: list[str] = field(default_factory=list)
    description: str = ""


def _chord(names, onset=0.1, held=2.0, velocities=None, instrument="piano"):
    vel = velocities or [0.78, 0.72, 0.75, 0.7, 0.74, 0.71]
    return [Played(N[n], onset, onset + held, vel[i % len(vel)], instrument) for i, n in enumerate(names)]


def fixtures() -> list[Fixture]:
    out: list[Fixture] = []
    # A: single notes (the quality a clean extraction should approach).
    for name in ("C4", "E4", "G4", "C5", "C2"):
        out.append(Fixture(f"A-single-{name}", "A", "piano", "single", 3.0, [Played(N[name], 0.1, 2.1, 0.8)],
                           description=f"{name}, held 2 s, velocity 0.8"))
    out.append(Fixture("A-single-C4-soft", "A", "piano", "single", 3.0, [Played(N["C4"], 0.1, 2.1, 0.4)],
                       description="C4, held 2 s, velocity 0.4 (darker, quieter)"))
    out.append(Fixture("A-single-C4-short", "A", "piano", "single", 1.5, [Played(N["C4"], 0.1, 0.6, 0.8)],
                       description="C4, held 0.5 s (damped early)"))
    # B: chords (simultaneous attacks).
    chords = {
        "C-major": ("C4", "E4", "G4"),
        "A-minor": ("A3", "C4", "E4"),
        "C-major7": ("C4", "E4", "G4", "B4"),
        "D-minor7": ("D4", "F4", "A4", "C5"),
        "close-C-D-E": ("C4", "D4", "E4"),
        "wide-C2-G3-E4-C6": ("C2", "G3", "E4", "C6"),
    }
    hard = {
        "C-major7": ["B4 and C5 a semitone apart from C4's 2nd harmonic region: shared partials"],
        "close-C-D-E": ["whole-tone cluster: partials of C4, D4, E4 interleave closely"],
        "wide-C2-G3-E4-C6": ["C6 coincides with harmonics of C2 (2^5 / 2^3 of C2..C4 family): octave ambiguity"],
        "D-minor7": ["D4 and A4 a fifth apart: every 3rd partial of D4 meets every 2nd of A4"],
    }
    for name, notes in chords.items():
        out.append(Fixture(f"B-chord-{name}", "B", "piano", "chord", 3.2, _chord(notes),
                           expected_failures=hard.get(name, []), description=f"{' '.join(notes)} struck together, held 2 s"))
    # C: overlapping attacks.
    out.append(Fixture("C-two-together", "C", "piano", "overlap", 3.0,
                       [Played(N["C4"], 0.1, 2.0, 0.75), Played(N["G4"], 0.1, 2.0, 0.7)], description="C4 + G4 together"))
    out.append(Fixture("C-two-150ms", "C", "piano", "overlap", 3.0,
                       [Played(N["C4"], 0.1, 2.0, 0.75), Played(N["E4"], 0.25, 2.1, 0.7)], description="C4, then E4 150 ms later"))
    out.append(Fixture("C-sustain-then-entry", "C", "piano", "overlap", 4.0,
                       [Played(N["C3"], 0.1, 3.2, 0.7), Played(N["E4"], 1.0, 2.6, 0.75)],
                       description="C3 held 3 s; E4 enters at 1 s over its decay"))
    out.append(Fixture("C-repeated-G4", "C", "piano", "overlap", 3.0,
                       [Played(N["G4"], 0.1, 0.5, 0.75), Played(N["G4"], 0.6, 1.0, 0.65), Played(N["G4"], 1.1, 2.0, 0.8)],
                       expected_failures=["repeated attacks of one key: tails overlap the next onset"],
                       description="G4 struck three times, 0.5 s apart"))
    out.append(Fixture("C-melody-over-chord", "C", "piano", "overlap", 4.5,
                       _chord(("C3", "E3", "G3"), onset=0.1, held=3.6, velocities=[0.6, 0.55, 0.58]) +
                       [Played(N["E5"], 0.5, 1.2, 0.8), Played(N["D5"], 1.2, 1.9, 0.75), Played(N["C5"], 1.9, 2.6, 0.78),
                        Played(N["G4"], 2.6, 3.6, 0.72)],
                       description="a four-note melody over a held C3 triad"))
    # D (synthetic stand-in): a short progression with melody, humanised timing and velocity.
    prog = [(0.10, ("C3", "G3", "E4")), (1.10, ("A3", "C4", "E4")), (2.10, ("F4", "A3", "C4")), (3.10, ("G2", "B3", "D4"))]
    notes: list[Played] = []
    jitter = [0.0, 0.012, -0.008, 0.015, -0.01, 0.006]
    for bar, (t, names) in enumerate(prog):
        for i, n in enumerate(names):
            notes.append(Played(N[n], t + jitter[(bar + i) % 6], t + 0.95, 0.55 + 0.05 * ((bar + i) % 3)))
    for i, (n, t) in enumerate((("G5", 0.6), ("E5", 1.6), ("C5", 2.35), ("D5", 2.6), ("B4", 3.6))):
        notes.append(Played(N[n], t + jitter[i % 6], t + 0.5 + (0.3 if i == 4 else 0.0), 0.8 - 0.03 * i))
    out.append(Fixture("D-phrase-synthetic", "D", "piano", "phrase", 5.2, notes,
                       expected_failures=["synthetic stand-in: a recorded performance (Fixture D proper) is pending"],
                       description="C - Am - F - G with a melody, timing within +-15 ms, varied velocities"))
    # E: sustain pedal (no dampers, sympathetic resonance in the residual).
    arp = [Played(N[n], 0.1 + 0.3 * i, 3.6, 0.65 + 0.04 * i) for i, n in enumerate(("C3", "E3", "G3", "C4"))]
    out.append(Fixture("E-pedal-arpeggio", "E", "piano", "pedal", 4.5, arp, pedal=True,
                       expected_failures=["pedal: long overlapping tails and sympathetic resonance (residual)"],
                       description="C3 E3 G3 C4 arpeggiated with the pedal down"))
    # F: other instruments (secondary benchmarks).
    out.append(Fixture("F-organ-C-major", "F", "organ", "other", 3.0, _chord(("C4", "E4", "G4"), instrument="organ"),
                       expected_failures=["organ: exactly harmonic, stationary; shared partials between chord tones"],
                       description="drawbar organ C major"))
    strum = [Played(m, 0.1 + 0.015 * i, 2.8, 0.7, "guitar") for i, m in enumerate((40, 47, 52, 56, 59, 64))]
    out.append(Fixture("F-guitar-E-major", "F", "guitar", "other", 3.5, strum, description="E major strum (Karplus-Strong)"))
    out.append(Fixture("F-strings-A-minor", "F", "strings", "other", 3.5, _chord(("A3", "C4", "E4"), held=2.8, instrument="strings"),
                       expected_failures=["vibrato: partials move; templates must follow pitch"],
                       description="string ensemble A minor with vibrato"))
    out.append(Fixture("F-voices-A-major", "F", "voice", "other", 3.5,
                       [Played(57, 0.1, 2.9, 0.8, "voice"), Played(61, 0.15, 2.9, 0.7, "voice"), Played(64, 0.12, 2.9, 0.75, "voice")],
                       expected_failures=["voices: vibrato, formants, breath; harmony shares partials"],
                       description="three-part vocal harmony on 'ah'"))
    return out


def by_id(fid: str) -> Fixture:
    for f in fixtures():
        if f.id == fid:
            return f
    raise KeyError(fid)
