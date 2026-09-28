#!/usr/bin/env python3
"""The groove-fit cases (docs/plan-groove-inference.md, docs/COMMANDS_AND_TEMPO.md
section 226): a MIDI file whose bars each play one case for the recorder's
groove fit, and the song file to record it into.

  groove-cases.cbsong   a bank with one pulse lead and a map: MIDI channel 1
                        plays PU1 with the lead; Auto groove on
  groove-cases.mid      sixteen bars at 120 BPM on channel 1, a case a bar
  groove-cases.expect   what every recorded row must read as, for the CTest

Load the .cbsong, arm PU1, press Rec and play the file in with Tempo source
Host at 120 BPM and Quantize on; the rows the tracker writes are the cases.
`chipboy_recordtest --record-midi groove-cases.cbsong groove-cases.mid OUT.cbsong 18 --expect groove-cases.expect`
does the same, lists what each row became and checks it against the expect file.

    python3 tools/demo/make_groovecases.py [--out Demo/midi-map]
"""

import argparse
import json
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_songs import Bank, VIB_TRI, VIB_DOWN   # noqa: E402

TEMPO = 120.0
PPQ = 96                    # four MIDI ticks a ChipBoy tick
BAR = 96                    # ticks

# Each case: a name, and its onsets as ticks into the bar (a tick is 1/24 of a
# beat). Some run two bars; the list then holds ticks past 96.
CASES = [
    ("1 straight sixteenths -> 6", [i * 6 for i in range(16)]),
    ("2 shuffle with rests -> 8 4", [0, 8, 12, 24, 32, 36, 48, 56, 60, 72, 80, 84]),
    ("3 downbeats only -> stays 8 4", [0, 12, 24, 36, 48, 60, 72, 84]),
    ("4 drifting swing 7 8 8 9 -> literal, a 16-entry groove", [0, 7, 12, 20, 24, 32, 36, 45, 48, 56, 60, 68, 72, 80, 84, 93]),
    ("5 eighth triplets -> 8", [i * 8 for i in range(12)]),
    ("6 straight again -> 6", [i * 6 for i in range(16)]),
    ("7-8 thirty-seconds two bars -> 3, 3", [i * 3 for i in range(64)]),
    ("9 twelve straight then eight fast -> 6 then G 3", [i * 6 for i in range(12)] + [72 + i * 3 for i in range(8)]),
    ("10-11 a run from mid-bar into the next -> 6 then G 3 | 3 then G straight", [i * 6 for i in range(8)] + [48 + i * 3 for i in range(24)] + [120 + i * 6 for i in range(12)]),
    ("12 one note four ticks late -> literal, the bar's own gaps", [i * 6 for i in range(16) if i != 6] + [40]),
    ("13 empty bar", []),
    ("14 a single note", [24]),
    ("15 sixteenth triplets -> 4", [i * 4 for i in range(24)]),
    ("16 swung thirty-seconds -> 4 2", sum(([i * 6, i * 6 + 4] for i in range(16)), [])),
]

SCALE = [60, 62, 64, 65, 67, 69, 71, 72]

# What the recorder must make of each row (groove-cases.expect): the steps,
# the phrase's groove by its entries, the notes, and every G by the groove it
# names. Bar 13 is empty and gets no phrase, so it has no line.
EXPECT = [
    "PU1 row 1: 16 steps, straight, 16 notes",
    "PU1 row 2: 16 steps, 8 4, 12 notes",
    "PU1 row 3: 16 steps, 8 4, 8 notes",
    "PU1 row 4: 16 steps, 7 5 8 4 8 4 9 3 8 4 8 4 8 4 9 3, 16 notes",
    "PU1 row 5: 12 steps, 8, 12 notes",
    "PU1 row 6: 16 steps, straight, 16 notes",
    "PU1 row 7: 32 steps, 3, 32 notes",
    "PU1 row 8: 32 steps, 3, 32 notes",
    "PU1 row 9: 20 steps, straight, 20 notes, G 3 @13",
    "PU1 row 10: 24 steps, straight, 24 notes, G = @1, G 3 @9",
    "PU1 row 11: 20 steps, 3, 20 notes, G = @1, G straight @9",
    "PU1 row 12: 16 steps, 6 6 6 6 6 10 2 6 6 6 6 6 6 6 6 6, 16 notes, G = @1",
    "PU1 row 14: 16 steps, straight, 1 notes",
    "PU1 row 15: 24 steps, 4, 24 notes",
    "PU1 row 16: 32 steps, 4 2, 32 notes",
]


def bank():
    b = Bank("Groove Cases")
    b.pulse("lead", duty=2, vol=13, rate=0, vib=(VIB_TRI, VIB_DOWN, 10, 2, 10))
    return b


def song_file(b):
    chans = [{"target": -1, "regions": [{"from": 1, "inst": 0, "table": 0}]} for _ in range(16)]
    chans[0] = {"target": 0, "regions": [{"from": 1, "inst": b.instrument("lead"), "table": 0}]}
    song = {"format": "chipboy-song", "version": 7, "tempoBpm": TEMPO, "songStartSeconds": 0.0,
            "phrases": [], "chains": [[], [], [], []], "noteSource": [0, 0, 0, 0],
            "recordArm": [True, True, True, True], "grooves": [[6, 6] for _ in range(16)],
            "midiMap": {"on": True, "channels": chans}, "autoGroove": True}
    return {"format": "chipboy-song-file", "version": 7, "bank": b.name,
            "instruments": {str(b.instrument("lead")): "lead"}, "bankData": b.var(), "song": song}


def vlq(v):
    out = [v & 0x7F]
    v >>= 7
    while v:
        out.append(0x80 | (v & 0x7F))
        v >>= 7
    return bytes(reversed(out))


def meta(kind, data):
    return b"\xff" + bytes([kind]) + vlq(len(data)) + data


def track(events, end):
    body = b""
    at = 0
    for tick, order, data in sorted(events, key=lambda e: (e[0], e[1])):
        body += vlq(tick - at) + data
        at = tick
    body += vlq(max(0, end - at)) + meta(0x2F, b"")
    return b"MTrk" + struct.pack(">I", len(body)) + body


def midi_file():
    """Every onset a 96th before its tick, so Quantize lands it on the tick;
    a note lasts to a tick before the next onset (a sixteenth at most), so
    its OFF has a step of its own."""
    per = PPQ // 24
    events = [(0, 0, meta(0x03, b"groove cases: channel 1 -> PU1"))]
    bar = 0
    n = 0
    for name, onsets in CASES:
        events.append((bar * BAR * per, 0, meta(0x06, name.encode("ascii"))))
        span = 2 if any(t >= BAR for t in onsets) else 1
        ons = sorted(onsets)
        for i, t in enumerate(ons):
            nxt = ons[i + 1] if i + 1 < len(ons) else span * BAR
            length = max(1, min(6, nxt - t - 1)) if nxt - t > 1 else 1
            note = SCALE[n % len(SCALE)]
            n += 1
            start = max(0, (bar * BAR + t) * per - 1)
            end = max(start + 1, (bar * BAR + t + length) * per - 1)
            events.append((start, 2, bytes([0x90, note, 100])))
            events.append((end, 1, bytes([0x80, note, 0])))
        bar += span
    end = bar * BAR * per
    zero = [(0, 0, meta(0x03, b"ChipBoy groove cases")),
            (0, 0, meta(0x51, struct.pack(">I", int(60e6 / TEMPO))[1:])),
            (0, 0, meta(0x58, bytes([4, 2, 24, 8])))]
    head = b"MThd" + struct.pack(">IHHH", 6, 1, 2, PPQ)
    return head + track(zero, end) + track(events, end), bar


def main():
    ap = argparse.ArgumentParser(description="generate the ChipBoy groove-fit cases")
    ap.add_argument("--out", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "Demo", "midi-map"))
    args = ap.parse_args()
    out = os.path.normpath(args.out)
    os.makedirs(out, exist_ok=True)
    b = bank()
    text = json.dumps(song_file(b), separators=(",", ":"), ensure_ascii=True)
    with open(os.path.join(out, "groove-cases.cbsong"), "w", newline="\n", encoding="utf-8") as f:
        f.write(text)
    with open(os.path.join(out, "groove-cases.expect"), "w", newline="\n", encoding="utf-8") as f:
        f.write("# what chipboy_recordtest --record-midi must make of groove-cases.mid, row by row\n")
        f.write("\n".join(EXPECT) + "\n")
    data, bars = midi_file()
    with open(os.path.join(out, "groove-cases.mid"), "wb") as f:
        f.write(data)
    print("groove-cases.cbsong  %6d bytes" % len(text))
    print("groove-cases.mid     %6d bytes  %d cases over %d bars at %g BPM" % (len(data), len(CASES), bars, TEMPO))
    return 0


if __name__ == "__main__":
    sys.exit(main())
