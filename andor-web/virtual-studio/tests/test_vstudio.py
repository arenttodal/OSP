"""Virtual Studio self-tests (standard library only): python3 -m unittest discover -s tests"""
import json, os, struct, sys, tempfile, unittest
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
from vstudio import smf, timeline, project


def vlq(n):
    out = [n & 0x7F]
    n >>= 7
    while n:
        out.insert(0, (n & 0x7F) | 0x80); n >>= 7
    return bytes(out)


def write_smf(path, tracks, division=480):
    """tracks: [[(delta_ticks, bytes)]] -> format 1 SMF."""
    data = b"MThd" + struct.pack(">IHHH", 6, 1, len(tracks), division)
    for tr in tracks:
        body = b"".join(vlq(d) + ev for d, ev in tr) + vlq(0) + b"\xff\x2f\x00"
        data += b"MTrk" + struct.pack(">I", len(body)) + body
    open(path, "wb").write(data)


class SmfTiming(unittest.TestCase):
    def test_tempo_map_running_status_and_bend(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "t.mid")
            # Track 0: 120 BPM, then 60 BPM at beat 2. Track 1: notes with running status, a bend, CC1, sustain.
            t0 = [(0, b"\xff\x51\x03" + (500000).to_bytes(3, "big")), (960, b"\xff\x51\x03" + (1000000).to_bytes(3, "big"))]
            t1 = [(0, b"\x90\x3c\x64"), (480, b"\x40\x50"),          # C4 on at 0, E4 on at beat 1 (running status)
                  (480, b"\x80\x3c\x40"), (0, b"\x80\x40\x40"),      # both off at beat 2 (1.0 s)
                  (0, b"\xe0\x00\x60"),                              # pitch bend +4096 at 1.0 s
                  (480, b"\xb0\x01\x7f"), (0, b"\xb0\x40\x7f"),      # mod 127, sustain on at beat 3 (2.0 s at 60 BPM)
                  (0, b"\x90\x3c\x64"), (240, b"\x90\x3c\x00"),      # C4 on at 2.0 s, note-on velocity 0 = off at 2.5 s
                  (0, b"\x90\x3c\x7f"), (0, b"\x90\x3c\x40")]        # retrigger: C4 at 2.5 s twice (second closes first)
            write_smf(p, [t0, t1])
            s = smf.read(p)
            ons = [e for e in s["events"] if e["type"] == "note_on"]
            self.assertAlmostEqual(ons[0]["t"], 0.0)
            self.assertAlmostEqual(ons[1]["t"], 0.5)
            self.assertAlmostEqual(ons[2]["t"], 2.0)
            bend = [e for e in s["events"] if e["type"] == "pitch_bend"][0]
            self.assertEqual(bend["value"], 4096)
            self.assertAlmostEqual(bend["t"], 1.0)
            perf = timeline.build(s, sample_rate=48000.0, tail=0.5)
            n = perf["notes"]
            self.assertEqual([(x["note"], round(x["on"], 3), round(x["off"], 3)) for x in n[:3]],
                             [(60, 0.0, 1.0), (64, 0.5, 1.0), (60, 2.0, 2.5)])
            self.assertTrue(all(x["source"] == "input" for x in n))
            self.assertEqual(perf["pitchBend"][0], [1.0, 0.5])
            self.assertEqual(perf["sustain"][0], [2.0, 1.0])
            self.assertEqual(timeline.check(perf), [])

    def test_frame_boundaries_are_exact_on_the_sample_clock(self):
        self.assertEqual(timeline.frame_sample(24, 24, 48000), 48000)
        self.assertEqual(timeline.frame_sample(30, 30, 48000), 48000)
        self.assertEqual(timeline.frame_sample(1, 24, 44100), 1838)          # 1837.5 rounds
        self.assertEqual(timeline.frame_sample(240, 24, 44100), 441000)      # no cumulative drift
        self.assertEqual(timeline.frame_count(8.0, 24), 192)
        self.assertEqual(timeline.frame_count(8.0, 30), 240)


class ProjectIntegrity(unittest.TestCase):
    def test_inventory_verify_and_png_check(self):
        import zlib
        with tempfile.TemporaryDirectory() as d:
            project.init(d, "t-001")
            raw = b"\x00\xff\x00\x00"
            def chunk(t, b): return struct.pack(">I", len(b)) + t + b + struct.pack(">I", zlib.crc32(t + b) & 0xFFFFFFFF)
            png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 1, 1, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b"")
            os.makedirs(os.path.join(d, "captures", "24fps", "face"))
            good = os.path.join(d, "captures", "24fps", "face", "0001.png")
            open(good, "wb").write(png)
            open(os.path.join(d, "captures", "24fps", "face", "0002.png"), "wb").write(png[:-6])   # truncated
            self.assertTrue(project.png_ok(good))
            done, todo = project.complete_frames(os.path.join(d, "captures", "24fps", "face"), 3)
            self.assertEqual((done, todo), ([1], [2, 3]))
            inv = project.inventory(d)
            self.assertEqual(inv["invalid"], ["captures/24fps/face/0002.png"])
            os.remove(os.path.join(d, "captures", "24fps", "face", "0002.png"))
            open(good, "ab").write(b"x")
            r = project.verify(d)
            self.assertEqual(r["missing"], ["captures/24fps/face/0002.png"])
            self.assertEqual(r["changed"], ["captures/24fps/face/0001.png"])


if __name__ == "__main__":
    unittest.main()
