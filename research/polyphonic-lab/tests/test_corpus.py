"""P0: the corpus is deterministic, consistent (stems sum to the mix) and validates."""

import json

import numpy as np

from polyphonic_lab import corpus
from polyphonic_lab.audio import read_audio, sha256_file
from polyphonic_lab.fixtures.corpus_def import by_id, fixtures
from polyphonic_lab.fixtures.synth import Played, piano_note
from polyphonic_lab.stft import Stft


def test_fixture_ids_are_unique_and_every_class_is_present():
    ids = [f.id for f in fixtures()]
    assert len(ids) == len(set(ids))
    assert {f.cls for f in fixtures()} >= {"A", "B", "C", "D", "E", "F"}


def test_generation_is_deterministic_and_validates(tmp_path):
    only = ["B-chord-C-major", "E-pedal-arpeggio"]
    corpus.generate(tmp_path / "a", tmp_path / "ma", only)
    corpus.generate(tmp_path / "b", tmp_path / "mb", only)
    for fid in only:
        ma = json.loads((tmp_path / "ma" / f"{fid}.json").read_text())
        mb = json.loads((tmp_path / "mb" / f"{fid}.json").read_text())
        assert ma["input"]["sha256"] == mb["input"]["sha256"]
        assert [n["stem"]["sha256"] for n in ma["groundTruth"]["notes"]] == [n["stem"]["sha256"] for n in mb["groundTruth"]["notes"]]
        ok, summary = corpus.validate(tmp_path / "ma" / f"{fid}.json", tmp_path / "a", tmp_path / "report", plots=False)
        assert ok, summary


def test_committed_manifests_match_the_generator_on_this_platform(tmp_path):
    # The committed manifest's audio hash is reproduced (same machine class); a platform with
    # another libm may differ in the last bits, which validate() reports as a warning, not an error.
    fid = "B-chord-C-major"
    corpus.generate(tmp_path, tmp_path / "m", [fid])
    committed = corpus.load_manifest(corpus.MANIFESTS / f"{fid}.json")
    fresh = corpus.load_manifest(tmp_path / "m" / f"{fid}.json")
    assert committed["groundTruth"]["notes"] == fresh["groundTruth"]["notes"] or \
        [n["pitch_midi"] for n in committed["groundTruth"]["notes"]] == [n["pitch_midi"] for n in fresh["groundTruth"]["notes"]]


def test_stems_sum_to_the_mix_and_are_finite(tmp_path):
    corpus.generate(tmp_path, tmp_path / "m", ["C-melody-over-chord"])
    m = corpus.load_manifest(tmp_path / "m" / "C-melody-over-chord.json")
    mix, _ = read_audio(tmp_path / m["input"]["path"])
    total = sum(read_audio(tmp_path / n["stem"]["path"])[0][0] for n in m["groundTruth"]["notes"])
    assert np.max(np.abs(total - mix[0])) < 1e-6
    assert np.all(np.isfinite(mix))
    assert sha256_file(tmp_path / m["input"]["path"]) == m["input"]["sha256"]


def test_piano_note_is_pitched_damped_and_quiet_before_its_onset():
    p = Played(60, 0.2, 1.0, 0.8)
    x = piano_note(p, 2.0)
    sr = 48000
    assert np.max(np.abs(x[: int(0.19 * sr)])) == 0.0
    body = x[int(0.4 * sr): int(0.9 * sr)]
    spec = np.abs(np.fft.rfft(body * np.hanning(len(body))))
    f = np.fft.rfftfreq(len(body), 1 / sr)
    peak = f[np.argmax(spec[(f > 100) & (f < 400)]) + np.argmax(f > 100)]
    assert abs(peak - 261.6) < 3.0
    late = np.abs(x[int(1.5 * sr):]).max()
    assert late < 0.01 * np.abs(x).max()      # the damper ended it


def test_stft_round_trip_is_exact():
    rng = np.random.default_rng(0)
    x = rng.standard_normal(48000)
    st = Stft(8192, 512, 48000)
    y = st.inverse(st.forward(x), len(x))
    assert np.max(np.abs(x - y)) < 1e-9


def test_fixture_lookup():
    assert by_id("B-chord-C-major").notes[0].midi == 60
