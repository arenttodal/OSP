"""P1: the shared event schema, the metrics, and the harmonic detector on controlled material."""

import numpy as np
import pytest

from polyphonic_lab import corpus
from polyphonic_lab.config import harmonic_config, load
from polyphonic_lab.corpus import render_fixture
from polyphonic_lab.evaluation.transcription import evaluate
from polyphonic_lab.fixtures.corpus_def import by_id
from polyphonic_lab.transcription import basic_pitch_adapter, heuristic
from polyphonic_lab.transcription.events import NoteEvent, hz_to_midi, load_events, midi_to_hz, note_name, save_events


def _truth(fid):
    fx = by_id(fid)
    mix, _, _ = render_fixture(fx)
    ref = [NoteEvent(id=f"n{i}", pitch_midi=p.midi, onset_seconds=p.onset, offset_seconds=p.offset) for i, p in enumerate(fx.notes)]
    return mix[None, :], ref


def test_pitch_conversions_and_names():
    assert midi_to_hz(69) == pytest.approx(440.0)
    assert hz_to_midi(261.6256) == pytest.approx(60.0, abs=1e-3)
    assert note_name(60) == "C4" and note_name(61) == "C#4"


def test_events_round_trip(tmp_path):
    ev = [NoteEvent(id="note-0001", pitch_midi=60, onset_seconds=0.1, offset_seconds=1.2, confidence=0.9, detector="harmonic")]
    p = save_events(tmp_path / "n.json", ev, {"x": 1})
    back = load_events(p)
    assert back[0].pitch_midi == 60 and back[0].onset_seconds == pytest.approx(0.1) and back[0].detector == "harmonic"


def test_metrics_are_perfect_for_the_truth_and_count_errors():
    ref = [NoteEvent(id="a", pitch_midi=60, onset_seconds=0.1, offset_seconds=1.0),
           NoteEvent(id="b", pitch_midi=64, onset_seconds=0.1, offset_seconds=1.0)]
    assert evaluate(ref, ref)["f1"] == 1.0
    est = [NoteEvent(id="a", pitch_midi=60, onset_seconds=0.12, offset_seconds=1.0),
           NoteEvent(id="c", pitch_midi=76, onset_seconds=0.1, offset_seconds=1.0)]   # E5: an octave above E4
    r = evaluate(ref, est)
    assert r["missed"] == 1 and r["falseNotes"] == 1 and r["octaveErrors"] == 1
    assert r["onsetErrorMedianMs"] == pytest.approx(20.0, abs=0.5)


@pytest.mark.parametrize("fid", ["B-chord-C-major", "B-chord-A-minor", "B-chord-D-minor7", "C-two-150ms", "A-single-G4", "A-single-C2"])
def test_harmonic_detector_on_controlled_material(fid):
    mix, ref = _truth(fid)
    est = heuristic.detect(mix, 48000, harmonic_config(load(None)))
    r = evaluate(ref, est)
    assert r["f1"] == 1.0, [(e.name, e.onset_seconds) for e in est]
    assert r["onsetErrorMedianMs"] <= 20.0
    assert r["octaveErrors"] == 0


def test_harmonic_detector_is_deterministic_and_handles_silence():
    mix, _ = _truth("B-chord-C-major")
    cfg = harmonic_config(load(None))
    a = [(e.pitch_midi, e.onset_seconds, e.offset_seconds) for e in heuristic.detect(mix, 48000, cfg)]
    b = [(e.pitch_midi, e.onset_seconds, e.offset_seconds) for e in heuristic.detect(mix, 48000, cfg)]
    assert a == b
    assert heuristic.detect(np.zeros((1, 48000)), 48000, cfg) == []


def test_harmonic_ghosts_are_removed_but_struck_octaves_stay():
    cfg = heuristic.HarmonicConfig()
    lo = NoteEvent(id="", pitch_midi=55, onset_seconds=0.1, offset_seconds=2.0, extra={"peak_salience": 10.0})
    ghost = NoteEvent(id="", pitch_midi=67, onset_seconds=0.6, offset_seconds=1.5, extra={"peak_salience": 6.0})   # late, no attack
    struck = NoteEvent(id="", pitch_midi=67, onset_seconds=0.1, offset_seconds=1.5, extra={"peak_salience": 8.0})  # with the chord, strong
    keep = heuristic.remove_harmonic_ghosts([lo, ghost], np.array([0.1]), cfg, 48000)
    assert ghost not in keep and lo in keep
    keep = heuristic.remove_harmonic_ghosts([lo, struck], np.array([0.1]), cfg, 48000)
    assert struck in keep


@pytest.mark.skipif(not basic_pitch_adapter.available(), reason="the optional Basic Pitch environment is not installed")
def test_basic_pitch_adapter_returns_shared_events(tmp_path):
    from polyphonic_lab.audio import write_audio
    mix, ref = _truth("B-chord-C-major")
    wav = write_audio(tmp_path / "c.wav", mix, 48000)
    est, meta = basic_pitch_adapter.detect(wav)
    assert meta["model"]["sha256"] and all(e.detector == "basic-pitch" for e in est)
    assert evaluate(ref, est)["recall"] == 1.0
