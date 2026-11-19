"""Standard MIDI File reader (formats 0 and 1), no dependencies.

Returns every channel event with its absolute time in seconds, computed through the file's own
tempo map (all tracks' tempo changes merged, as a DAW does). Only what a performance needs is
decoded: note on/off, control change, pitch bend, channel pressure, program change, plus tempo and
time-signature meta events. Running status is honoured; SysEx and other meta events are skipped.
SMPTE time division is supported (frames per second x ticks per frame).
"""
import struct

NOTE_OFF, NOTE_ON, POLY_AT, CC, PROGRAM, CHAN_AT, PITCH = 0x8, 0x9, 0xA, 0xB, 0xC, 0xD, 0xE


def _vlq(data, i):
    value = 0
    while True:
        b = data[i]
        i += 1
        value = (value << 7) | (b & 0x7F)
        if not b & 0x80:
            return value, i


def read(path):
    """-> dict(format, division, tracks: [[(tick, kind, fields)]], tempo: [(tick, us_per_quarter)],
    timesig: [(tick, num, den)], events: [ {t, tick, track, type, channel, ...} ] sorted by time)."""
    data = open(path, "rb").read()
    if data[:4] != b"MThd":
        raise ValueError(f"{path}: not a Standard MIDI File")
    hlen = struct.unpack(">I", data[4:8])[0]
    fmt, ntrk, division = struct.unpack(">HHH", data[8:14])
    if fmt not in (0, 1):
        raise ValueError(f"{path}: SMF format {fmt} is not supported (0 or 1 expected)")
    i = 8 + hlen
    raw = []
    tempo = []
    timesig = []
    for track in range(ntrk):
        while data[i:i + 4] != b"MTrk":   # skip unknown chunks
            clen = struct.unpack(">I", data[i + 4:i + 8])[0]
            i += 8 + clen
        tlen = struct.unpack(">I", data[i + 4:i + 8])[0]
        j, end = i + 8, i + 8 + tlen
        tick, status = 0, None
        while j < end:
            delta, j = _vlq(data, j)
            tick += delta
            b = data[j]
            if b == 0xFF:   # meta
                mtype = data[j + 1]
                mlen, k = _vlq(data, j + 2)
                body = data[k:k + mlen]
                j = k + mlen
                if mtype == 0x51 and mlen == 3:
                    tempo.append((tick, (body[0] << 16) | (body[1] << 8) | body[2]))
                elif mtype == 0x58 and mlen >= 2:
                    timesig.append((tick, body[0], 2 ** body[1]))
                elif mtype == 0x2F:
                    break
                continue
            if b in (0xF0, 0xF7):   # SysEx
                slen, k = _vlq(data, j + 1)
                j = k + slen
                continue
            if b & 0x80:
                status = b
                j += 1
            elif status is None:
                raise ValueError(f"{path}: running status without a status byte (track {track})")
            kind, ch = status >> 4, status & 0x0F
            n = 1 if kind in (PROGRAM, CHAN_AT) else 2
            d = data[j:j + n]
            j += n
            raw.append((tick, track, kind, ch, d))
        i = end
    if not tempo or tempo[0][0] != 0:
        tempo.insert(0, (0, 500000))   # the SMF default: 120 BPM
    tempo.sort()

    def seconds(tick):
        if division & 0x8000:   # SMPTE
            fps = 256 - (division >> 8)
            tpf = division & 0xFF
            return tick / (fps * tpf)
        t, last_tick, us = 0.0, 0, tempo[0][1]
        for tt, tus in tempo:
            if tt > tick:
                break
            t += (tt - last_tick) * us / (division * 1e6)
            last_tick, us = tt, tus
        return t + (tick - last_tick) * us / (division * 1e6)

    events = []
    for tick, track, kind, ch, d in raw:
        e = {"t": seconds(tick), "tick": tick, "track": track, "channel": ch}
        if kind == NOTE_ON and d[1] > 0:
            e.update(type="note_on", note=d[0], velocity=d[1])
        elif kind == NOTE_OFF or (kind == NOTE_ON and d[1] == 0):
            e.update(type="note_off", note=d[0], velocity=d[1])
        elif kind == CC:
            e.update(type="cc", cc=d[0], value=d[1])
        elif kind == PITCH:
            e.update(type="pitch_bend", value=((d[1] << 7) | d[0]) - 8192)
        elif kind == CHAN_AT:
            e.update(type="channel_pressure", value=d[0])
        elif kind == POLY_AT:
            e.update(type="poly_pressure", note=d[0], value=d[1])
        elif kind == PROGRAM:
            e.update(type="program", value=d[0])
        events.append(e)
    # Stable: same-time note-offs before note-ons (a retrigger is off-then-on), else file order.
    order = {"note_off": 0}
    events.sort(key=lambda e: (e["t"], order.get(e["type"], 1)))
    return {"format": fmt, "division": division, "tempo": tempo, "timesig": timesig, "events": events,
            "seconds": seconds}
