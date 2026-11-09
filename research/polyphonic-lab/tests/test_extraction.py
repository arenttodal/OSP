"""P2: templates, masks, reconstruction and the separation metrics on controlled material."""

import numpy as np
import pytest

from polyphonic_lab.corpus import render_fixture, sound_end
from polyphonic_lab.evaluation import separation
from polyphonic_lab.extraction import masks
from polyphonic_lab.extraction.outputs import load_extraction, write_extraction
from polyphonic_lab.extraction.separate import ExtractConfig, extract, trimmed
from polyphonic_lab.fixtures.corpus_def import by_id
from polyphonic_lab.transcription.events import NoteEvent

SR = 48000


def _fixture(fid):
    fx = by_id(fid)
    mix, stems, _ = render_fixture(fx)
    ev = [NoteEvent(id=f"n{i}", pitch_midi=p.midi, onset_seconds=p.onset, offset_seconds=p.offset,
                    extra={"sound_end_seconds": sound_end(s, SR)}) for i, (p, s) in enumerate(zip(fx.notes, stems))]
    return mix, stems, ev


def _by_event(ex, events):
    order = {t.event.id: i for i, t in enumerate(ex.templates)}
    return [ex.notes[order[e.id]] for e in events]


def test_si_sdr_and_projection_metrics():
    rng = np.random.default_rng(1)
    a, b = rng.standard_normal(4800), rng.standard_normal(4800)
    assert separation.si_sdr(a, a) == separation.CAP_DB                 # exact: capped at 100 dB
    assert separation.si_sdr(0.5 * a, a) == separation.CAP_DB          # scale-invariant
    assert separation.si_sdr(a + b, a) == pytest.approx(0.0, abs=0.5)  # equal-power interference
    m = separation.projection_metrics([a + 0.1 * b], [a, b])[0]
    assert m["sir"] == pytest.approx(20.0, abs=0.5) and m["sar"] == separation.CAP_DB


def test_masks_are_finite_in_range_and_sum_to_one():
    mix, _, ev = _fixture("B-chord-C-major")
    for method in ("A", "B"):
        ex = extract(mix[None, :], SR, ev, ExtractConfig(method=method))
        chk = masks.check_masks(ex.mask_result)
        assert chk["finite"] and chk["inRange"] and chk["sumMaxError"] < 1e-9


@pytest.mark.parametrize("method", ["A", "B", "C"])
def test_notes_plus_residual_reconstruct_the_mixture(method):
    mix, _, ev = _fixture("C-two-150ms")
    ex = extract(mix[None, :], SR, ev, ExtractConfig(method=method))
    assert ex.consistency()["maxAbsError"] < 1e-9


def test_templates_find_the_tuning_and_inharmonicity():
    mix, _, ev = _fixture("A-single-C4")
    ex = extract(mix[None, :], SR, ev, ExtractConfig(method="B"))
    t = ex.templates[0]
    assert abs(t.cents) < 3.0                       # the synthetic C4 is in tune within its unison detune
    assert 1e-5 < t.stiffness < 1e-3                # a stiff string, not a harmonic one
    assert t.partials_hz[19] > 20 * t.f0            # partial 20 is sharp of 20 f0


def test_c_major_separates_into_c_e_and_g():
    """The first milestone's material: each extracted note is closer to its own stem than the mixture
    is (by more than 10 dB), interference is well below the target, and the pitch is right."""
    mix, stems, ev = _fixture("B-chord-C-major")
    ex = extract(mix[None, :], SR, ev, ExtractConfig(method="C"))
    est = _by_event(ex, ev)
    r = separation.evaluate_notes(mix, SR, ev, stems, ev, est)
    assert r["missed"] == 0 and r["falseNotes"] == 0
    for n in r["notes"]:
        assert n["siSdr"] - n["mixtureSiSdr"] > 10.0, n
        assert n["sir"] > 15.0, n
        assert abs(n["pitchErrorCents"]) < 2.0, n


def test_attack_allocation_restores_the_onset():
    mix, stems, ev = _fixture("C-two-150ms")
    res = {}
    for method in ("B", "C"):
        ex = extract(mix[None, :], SR, ev, ExtractConfig(method=method))
        res[method] = separation.evaluate_notes(mix, SR, ev, stems, ev, _by_event(ex, ev))
    assert res["C"]["siSdrMedian"] > res["B"]["siSdrMedian"] + 3.0
    assert abs(res["C"]["attackEnergyErrorDbMedian"]) < abs(res["B"]["attackEnergyErrorDbMedian"])


def test_extraction_is_deterministic_and_handles_silence_and_no_notes():
    mix, _, ev = _fixture("B-chord-C-major")
    a = extract(mix[None, :], SR, ev, ExtractConfig(method="C"))
    b = extract(mix[None, :], SR, ev, ExtractConfig(method="C"))
    assert all(np.array_equal(x, y) for x, y in zip(a.notes, b.notes))
    empty = extract(mix[None, :], SR, [], ExtractConfig(method="C"))
    assert empty.notes == [] and np.allclose(empty.residual, mix)
    quiet = extract(np.zeros((1, SR)), SR, [NoteEvent(id="x", pitch_midi=60, onset_seconds=0.1, offset_seconds=0.5)], ExtractConfig(method="C"))
    assert all(np.all(np.isfinite(n)) for n in quiet.notes) and quiet.consistency()["maxAbsError"] < 1e-12


def test_trimmed_sample_and_written_extraction(tmp_path):
    mix, _, ev = _fixture("A-single-C4-short")
    cfg = ExtractConfig(method="C")
    ex = extract(mix[None, :], SR, ev, cfg)
    seg, start, end = trimmed(ex, 0, cfg)
    assert start == pytest.approx(ev[0].onset_seconds - 0.02)
    assert ev[0].offset_seconds < end < ev[0].offset_seconds + 1.0      # the damped tail, then silence
    assert abs(seg[0]) < 1e-6 and abs(seg[-1]) < 1e-6                  # faded at both ends
    doc = write_extraction(ex, tmp_path / "x", cfg, plots=False)
    back = load_extraction(tmp_path / "x")
    assert back["notes"][0]["pitch_midi"] == 60 and (tmp_path / "x" / back["notes"][0]["sample"]["path"]).exists()
    assert doc["consistency"]["maxAbsError"] < 1e-9


def test_unknown_method_and_residual_are_rejected():
    with pytest.raises(ValueError):
        extract(np.zeros((1, 4800)), SR, [], ExtractConfig(method="Z"))
    with pytest.raises(ValueError):
        extract(np.zeros((1, 4800)), SR, [], ExtractConfig(method="B", residual="lots"))
