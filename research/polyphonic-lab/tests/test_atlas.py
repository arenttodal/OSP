"""P3: sample preparation, selection, the Note Atlas exporter and the renders' MIDI."""

import json
import struct

import numpy as np
import pytest

from polyphonic_lab.audio import content_id
from polyphonic_lab.corpus import render_fixture, sound_end
from polyphonic_lab.extraction.outputs import write_extraction
from polyphonic_lab.extraction.separate import ExtractConfig, extract
from polyphonic_lab.fixtures.corpus_def import by_id
from polyphonic_lab.mapping import render
from polyphonic_lab.mapping.atlas import export_atlas
from polyphonic_lab.mapping.prepare import PreparedNote, select
from polyphonic_lab.transcription.events import NoteEvent

SR = 48000
# Every field OSP writes for a source and a note (src/io/NoteAtlasJson.cpp); the exporter must
# write the same set (diagnostics, grainRegion and repeatAt are optional and not written).
SOURCE_KEYS = {"id", "contentHash", "origin", "fileName", "relativePath", "lastKnownPath", "managedCopy", "sampleRate", "numChannels", "numFrames"}
NOTE_KEYS = {"id", "source", "sourceStart", "sourceEnd", "attackStart", "sustain", "releaseStart", "fadeIn", "fadeOut", "pitchHz",
             "midiPitch", "rootMidi", "cents", "stabilityCents", "vibratoCents", "vibratoHz", "voicing", "confidence", "peakDb",
             "rmsDb", "articulation", "origin", "method", "methodVersion", "corrections", "included", "assignedKey",
             "velocityLayer", "group", "variation"}


@pytest.fixture(scope="module")
def c_major_extraction(tmp_path_factory):
    fx = by_id("B-chord-C-major")
    mix, stems, _ = render_fixture(fx)
    ev = [NoteEvent(id=f"n{i}", pitch_midi=p.midi, onset_seconds=p.onset, offset_seconds=p.offset,
                    extra={"sound_end_seconds": sound_end(s, SR)}) for i, (p, s) in enumerate(zip(fx.notes, stems))]
    cfg = ExtractConfig(method="C")
    ex = extract(mix[None, :], SR, ev, cfg)
    out = tmp_path_factory.mktemp("c-major") / "extraction"
    write_extraction(ex, out, cfg, {"input": "B-chord-C-major/mix.wav"}, plots=False)
    return out


def test_exported_atlas_has_osp_layout_regions_and_hashes(c_major_extraction, tmp_path):
    r = export_atlas(c_major_extraction, tmp_path / "inst", "cmaj")
    assert r["included"] == 3 and r["keys"] == [60, 64, 67]
    atlas = json.loads((tmp_path / "inst" / "cmaj.andoratlas").read_text())
    assert atlas["format"] == "andor.noteAtlas" and atlas["schemaVersion"] == 2 and atlas["mapping"]["fill"]
    for s in atlas["sources"]:
        assert set(s) == SOURCE_KEYS
        wav = tmp_path / "inst" / s["relativePath"]
        assert wav.exists() and s["contentHash"] == content_id(wav)
    for n in atlas["notes"]:
        assert set(n) == NOTE_KEYS
        assert 0 <= n["sourceStart"] <= n["attackStart"] <= n["sustain"]["start"] <= n["sustain"]["end"] <= n["releaseStart"] <= n["sourceEnd"]
        assert abs(n["cents"]) < 5 and n["confidence"] > 0.5
    sidecar = json.loads((tmp_path / "inst" / "cmaj.osp-poly.json").read_text())
    assert sidecar["schema"] == "osp-poly.atlas-sidecar/1" and len(sidecar["notes"]) == 3


def test_compact_mapping_puts_notes_on_white_keys_without_fill(c_major_extraction, tmp_path):
    export_atlas(c_major_extraction, tmp_path / "c", "c", mapping="compact")
    atlas = json.loads((tmp_path / "c" / "c.andoratlas").read_text())
    assert [n["assignedKey"] for n in atlas["notes"]] == [60, 62, 64] and not atlas["mapping"]["fill"]


def _p(i, key, conf, peak=-20.0, held=2.0):
    return PreparedNote(index=i, id=f"x{i}", name="", sample_path="", sample_rate=SR, frames=SR, attack_start=0,
                        sustain=(0, 1), release_start=1, pitch_hz=0, midi_pitch=key, root_midi=key, cents=0.0,
                        confidence=conf, peak_db=peak, rms_db=peak - 12, articulation="sustained", extra={"heldSeconds": held})


def test_selection_keeps_one_take_per_key_and_says_why():
    notes = select([_p(0, 60, 0.6), _p(1, 60, 0.9), _p(2, 64, 0.8), _p(3, 67, 0.9, peak=-80.0)])
    assert [n.included for n in notes] == [False, True, True, False]
    assert "scores higher" in notes[0].excluded_because and "quiet" in notes[3].excluded_because
    both = select([_p(0, 60, 0.6), _p(1, 60, 0.9)], include="all")
    assert all(n.included for n in both)


def test_midi_writer_writes_a_valid_format_0_file(tmp_path):
    p = render.write_midi(tmp_path / "x.mid", [(0.0, 0.5, 60, 100), (0.5, 1.0, 64, 90)])
    data = p.read_bytes()
    assert data[:4] == b"MThd" and struct.unpack(">IHHH", data[4:14]) == (6, 0, 1, render.TPQ)
    track = data[22:]
    assert track.count(bytes([0x90, 60, 100])) == 1 and track.count(bytes([0x80, 64, 0])) == 1 and track.endswith(b"\xff\x2f\x00")


@pytest.mark.skipif(render.renderer() is None, reason="research-renderer is not built")
def test_osp_renders_the_exported_atlas(c_major_extraction, tmp_path):
    r = export_atlas(c_major_extraction, tmp_path / "inst", "cmaj")
    rr = render.render_demos(tmp_path / "inst" / "cmaj.andoratlas", tmp_path / "renders", r["keys"])
    assert rr["rendered"], rr
    for name, e in rr["renders"].items():
        assert e["exit"] == 0 and e["peakDbfs"] > -60.0 and e["finite"], (name, e)
