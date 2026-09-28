#!/usr/bin/env python3
"""The MIDI map demo (docs/COMMANDS_AND_TEMPO.md section 225, UI_DESIGN D-UI-43).

Writes two files under Demo/midi-map (or --out):

  midi-map-demo.cbsong   a song file with no cells at all: the bank the demo
                         plays through and the MIDI map, switched on, that
                         routes seven MIDI channels to the four voices
  midi-map-demo.mid      eight bars, one track per MIDI channel, that play
                         through that map -- every feature of the map in turn

Open the .cbsong in the Tracker tab (Load song...), put the .mid on the
ChipBoy track in the host, and play. Nothing in the song file plays by itself:
the notes come from the host, the map says what they load and fire.

Standard library only; deterministic: two runs give byte-identical files.

    python3 tools/demo/make_midimap.py [--out Demo/midi-map]
"""

import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_songs import (Bank, note, END_LOOP, END_STOP, TICK, VIB_TRI, VIB_DOWN,   # noqa: E402
                        frame_triangle, frame_saw, frame_harmonics)

TEMPO = 120.0
PPQ = 480


# ---------------------------------------------------------------------------
# the bank: named, so the map and the tracks below say what they mean
# ---------------------------------------------------------------------------

def bank():
    b = Bank("MIDI Map Demo")
    b.wav("triangle", [frame_triangle()])
    b.wav("saw", [frame_saw()])
    b.wav("organ", [frame_harmonics(1.0, 0.5, 0.25)])
    b.table("arp-minor", ["t0", "t3", "t7", "t12"], end=END_LOOP)
    b.table("octave-drop", ["t0", "t-12"], end=END_LOOP)
    b.table("kick-shape", ["v15", "v11", "v7", "v3"], end=END_STOP)
    b.pulse("lead", duty=2, vol=13, rate=0, vib=(VIB_TRI, VIB_DOWN, 10, 2, 10))
    b.pulse("pluck", duty=1, vol=15, rate=2, pitch=TICK)
    b.pulse("bass25", duty=1, vol=14, rate=0)
    b.pulse("arp", duty=0, vol=12, rate=0, table="arp-minor")
    b.wave("saw-pad", wave="saw", level=3)
    b.wave("tri-bass", wave="triangle", level=3)
    b.wave("organ", wave="organ", level=3)
    b.noise("kick", vol=15, rate=1, shift=6, div=3, sweep=-1, table="kick-shape")
    b.noise("snare", vol=13, rate=2, shift=4, div=4)
    b.noise("hat", vol=9, rate=1, lfsr7=True, shift=1, div=4)
    return b


def cmd(letter, x=0, y=0):
    return {"c": letter, "a": x, "b": y, "x": 0}


def region(from_vel, inst=0, table=0, cmd1=None, cmd2=None):
    r = {"from": from_vel, "inst": inst, "table": table}
    if cmd1:
        r["cmd1"] = cmd1
    if cmd2:
        r["cmd2"] = cmd2
    return r


PU1, PU2, WAV, NOI = 0, 1, 2, 3


def midi_map(b):
    """Seven MIDI channels, and what each one demonstrates (the track names
    in the .mid say the same)."""
    I, T = b.instrument, (lambda name: b.table_slot[name])
    chans = [{"target": -1, "regions": [region(1)]} for _ in range(16)]
    # 1 -> PU1: an accent is a plain note, a soft note is bare -- legato by velocity.
    chans[0] = {"target": PU1, "regions": [region(1), region(71, I("lead"))]}
    # 2 -> WAV: example B -- soft notes keep the pad and add a vibrato.
    chans[1] = {"target": WAV, "regions": [region(1, cmd1=cmd("V", 4, 6)), region(65, I("saw-pad"))]}
    # 3 -> PU2: example C -- soft notes keep the bass and get a decaying envelope.
    chans[2] = {"target": PU2, "regions": [region(1, cmd1=cmd("E", 8, 3)), region(65, I("bass25"))]}
    # 4 -> NOI: the velocity picks the drum.
    chans[3] = {"target": NOI, "regions": [region(1, I("hat")), region(41, I("snare")), region(91, I("kick"))]}
    # 5 -> PU1 as well: a second MIDI channel on the same voice, plucks a K cuts short.
    chans[4] = {"target": PU1, "regions": [region(1, I("pluck"), cmd1=cmd("K", 3))]}
    # 6 -> PU2: an instrument whose own table arpeggiates.
    chans[5] = {"target": PU2, "regions": [region(1, I("arp"))]}
    # 7 -> WAV: a region's TBL column -- a bass that drops an octave every other tick.
    chans[6] = {"target": WAV, "regions": [region(1, I("tri-bass"), T("octave-drop"))]}
    return {"on": True, "channels": chans}


def song_file(b):
    used = sorted({r["inst"] for c in midi_map(b)["channels"] for r in c["regions"] if r["inst"]})
    names = {str(slot): b.instruments[slot - 1][0] for slot in used}
    song = {"format": "chipboy-song", "version": 7,
            "tempoBpm": TEMPO, "songStartSeconds": 0.0,
            "phrases": [], "chains": [[], [], [], []],
            "noteSource": [0, 0, 0, 0],            # every channel plays MIDI
            "recordArm": [True, True, True, True],
            "grooves": [[6, 6] for _ in range(16)],
            "midiMap": midi_map(b)}
    return {"format": "chipboy-song-file", "version": 7,
            "bank": b.name, "instruments": names,
            "bankData": b.var(), "song": song}


# ---------------------------------------------------------------------------
# the music: (start beat, length in beats, note, velocity) per MIDI channel
# ---------------------------------------------------------------------------

def bar(n):
    return 4.0 * (n - 1)


def tracks():
    t = {}
    # 1 PU1 -- bars 1-2 and 5-6: accents (100) retrigger the lead, soft notes (50) slide bare.
    lead = []
    tune = [("C-5", 1.0, 100), ("D-5", 0.5, 50), ("E-5", 0.5, 50), ("G-5", 1.0, 100), ("E-5", 1.0, 50),
            ("A-5", 1.5, 100), ("G-5", 0.5, 50), ("E-5", 1.0, 50), ("C-5", 1.0, 100)]
    for start in (bar(1), bar(5)):
        at = start
        for name, length, vel in tune:
            lead.append((at, length, note(name), vel))
            at += length
    t[1] = ("1 PU1 lead: >70 attacks (plain), <=70 slides (bare)", lead)
    # 2 WAV -- bars 3-4: example B. A C for a whole note at 100; a D at 40 halfway
    # through it, still on the pad, with the vibrato the soft region carries.
    pad = [(bar(3), 4.0, note("C-3"), 100), (bar(3) + 2.0, 2.0, note("D-3"), 40),
           (bar(4), 1.0, note("E-3"), 100), (bar(4) + 1.0, 1.0, note("F-3"), 40),
           (bar(4) + 2.0, 1.0, note("G-3"), 40), (bar(4) + 3.0, 1.0, note("A-3"), 100)]
    t[2] = ("2 WAV pad: >64 loads saw-pad, <=64 bare + V 4,6 (example B)", pad)
    # 3 PU2 -- bars 5-6: example C. Four quarters, the first at 100 (bass25
    # loads), the rest at 40 (still bass25, each with E 8,3).
    bass = []
    for k, (n, vel) in enumerate([("C-4", 100), ("C-4", 40), ("C-4", 40), ("C-4", 40),
                                  ("E-4", 100), ("D-4", 40), ("C-4", 40), ("G-3", 40)]):
        bass.append((bar(5) + k, 1.0, note(n), vel))
    t[3] = ("3 PU2 bass: >64 loads bass25, <=64 bare + E 8,3 (example C)", bass)
    # 4 NOI -- every bar: the velocity is the drum. Kick 110, snare 80, hat 40.
    drums = []
    for b_ in range(1, 9):
        for beat, vel in ((0.0, 110), (1.0, 80), (2.0, 110), (2.5, 110), (3.0, 80)):
            drums.append((bar(b_) + beat, 0.25, 60, vel))
        for eighth in (0.5, 1.5, 3.5):
            drums.append((bar(b_) + eighth, 0.25, 60, 40))
    t[4] = ("4 NOI drums: <=40 hat, 41-90 snare, >90 kick", sorted(drums))
    # 5 PU1 too -- bars 7-8: sixteenth plucks, K 3 in the region cuts each short.
    plucks = []
    at = bar(7)
    for k in range(32):
        plucks.append((at, 0.25, note(["C-6", "E-6", "G-6", "C-7"][k % 4]), 90))
        at += 0.25
    t[5] = ("5 PU1 plucks: pluck + K 3 -- a second MIDI channel on PU1", plucks)
    # 6 PU2 -- bars 7-8: held chords on an instrument whose table arpeggiates.
    t[6] = ("6 PU2 arp: the instrument's table does the chord", [
        (bar(7), 2.0, note("C-4"), 100), (bar(7) + 2.0, 2.0, note("A-3"), 100),
        (bar(8), 2.0, note("F-3"), 100), (bar(8) + 2.0, 2.0, note("G-3"), 100)])
    # 7 WAV -- bars 7-8: an eighth-note bass whose region's TBL drops an octave.
    tri = []
    at = bar(7)
    for k in range(16):
        tri.append((at, 0.5, note(["C-3", "C-3", "G-2", "C-3"][k % 4]), 100))
        at += 0.5
    t[7] = ("7 WAV bass: tri-bass + TBL octave-drop", tri)
    return t


# ---------------------------------------------------------------------------
# a standard MIDI file, format 1
# ---------------------------------------------------------------------------

def vlq(v):
    out = [v & 0x7F]
    v >>= 7
    while v:
        out.append(0x80 | (v & 0x7F))
        v >>= 7
    return bytes(reversed(out))


def meta(kind, data):
    return b"\xff" + bytes([kind]) + vlq(len(data)) + data


def track(events, end_tick):
    """events: (tick, order, bytes) -- offs (0) before ons (1) at one tick."""
    body = b""
    at = 0
    for tick, _order, data in sorted(events, key=lambda e: (e[0], e[1])):
        body += vlq(tick - at) + data
        at = tick
    body += vlq(max(0, end_tick - at)) + meta(0x2F, b"")
    return b"MTrk" + struct.pack(">I", len(body)) + body


def midi_file(b):
    t = tracks()
    end = int(PPQ * 4 * 8)
    chunks = []
    zero = [(0, 0, meta(0x03, b"ChipBoy MIDI map demo")),
            (0, 0, meta(0x51, struct.pack(">I", int(60e6 / TEMPO))[1:])),
            (0, 0, meta(0x58, bytes([4, 2, 24, 8]))),
            (0, 0, meta(0x01, b"Open midi-map-demo.cbsong in ChipBoy's Tracker tab, then play this file into it: "
                              b"the MIDI tab's map routes each channel and its velocity regions say what the notes load and fire."))]
    chunks.append(track(zero, end))
    for ch in sorted(t):
        name, notes = t[ch]
        ev = [(0, 0, meta(0x03, name.encode("ascii")))]
        status_on, status_off = 0x90 | (ch - 1), 0x80 | (ch - 1)
        for start, length, n, vel in notes:
            a, z = int(round(start * PPQ)), int(round((start + length) * PPQ))
            ev.append((a, 1, bytes([status_on, n, vel])))
            ev.append((z, 0, bytes([status_off, n, 0])))
        chunks.append(track(ev, end))
    head = b"MThd" + struct.pack(">IHHH", 6, 1, len(chunks), PPQ)
    return head + b"".join(chunks)


def main():
    ap = argparse.ArgumentParser(description="generate the ChipBoy MIDI map demo")
    ap.add_argument("--out", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "Demo", "midi-map"))
    args = ap.parse_args()
    out = os.path.normpath(args.out)
    os.makedirs(out, exist_ok=True)
    import json
    b = bank()
    text = json.dumps(song_file(b), separators=(",", ":"), ensure_ascii=True)
    with open(os.path.join(out, "midi-map-demo.cbsong"), "w", newline="\n", encoding="utf-8") as f:
        f.write(text)
    data = midi_file(b)
    with open(os.path.join(out, "midi-map-demo.mid"), "wb") as f:
        f.write(data)
    print("midi-map-demo.cbsong  %6d bytes  %d instruments, %d MIDI channels mapped"
          % (len(text), len(b.instruments), sum(1 for c in midi_map(b)["channels"] if c["target"] >= 0)))
    print("midi-map-demo.mid     %6d bytes  %d tracks, 8 bars at %g BPM" % (len(data), len(tracks()), TEMPO))
    return 0


if __name__ == "__main__":
    sys.exit(main())
