#!/usr/bin/env python3
"""The ChipBoy Solo demo (docs/COMMANDS_AND_TEMPO.md section 227): a .cbsolo
file to load into a Solo instance -- the MIDI map demo's bank, eight sounds
that use it, a small command library, and the row set to the first sound.

  solo-demo.cbsolo    load it from Solo's Setup page (Load Solo file...)

Play C-2 and up on PU1; the keys under C-2 recall the sounds (B-1 is sound 1,
A#1 sound 2, and so on down), and so does a MIDI program change.

    python3 tools/demo/make_solo.py [--out Demo/solo]
"""

import argparse
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_midimap import bank, cmd   # noqa: E402


def sound(slot, name, inst, table=0, cmd1=None, cmd2=None):
    s = {"slot": slot, "name": name, "inst": inst, "table": table}
    if cmd1:
        s["cmd1"] = cmd1
    if cmd2:
        s["cmd2"] = cmd2
    return s


def solo_state(b):
    I, T = b.instrument, (lambda name: b.table_slot[name])
    sounds = [
        sound(1, "Lead vib", I("lead"), cmd1=cmd("V", 4, 6)),
        sound(2, "Pluck", I("pluck")),
        sound(3, "Bass 25", I("bass25")),
        sound(4, "Arp minor", I("arp")),
        sound(5, "Lead fade", I("lead"), cmd1=cmd("E", 12, 3)),
        sound(6, "Pluck octaves", I("pluck"), table=T("octave-drop")),
        sound(7, "Lead retrig", I("lead"), cmd1=cmd("R", 12, 2)),
        sound(8, "Pluck cut", I("pluck"), cmd1=cmd("K", 6)),
    ]
    commands = [
        {"slot": 1, "name": "wide vib", "cmd": cmd("V", 8, 4)},
        {"slot": 2, "name": "fade 12 / 3", "cmd": cmd("E", 12, 3)},
        {"slot": 3, "name": "cut at 6", "cmd": cmd("K", 6)},
        {"slot": 4, "name": "left only", "cmd": cmd("O", 1)},
        {"slot": 5, "name": "retrig", "cmd": cmd("R", 12, 2)},
    ]
    # No key maps in the file: Solo lays its default for every channel.
    return {"sounds": sounds, "commands": commands}


def solo_file(b):
    return {"format": "chipboy-solo", "version": 1, "channel": 0,
            "bank": b.var(), "solo": solo_state(b),
            "params": {"channel": 0, "s_instrument": b.instrument("lead"), "s_cmd1_type": 0, "sound": 1}}


def main():
    ap = argparse.ArgumentParser(description="generate the ChipBoy Solo demo")
    ap.add_argument("--out", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "Demo", "solo"))
    args = ap.parse_args()
    out = os.path.normpath(args.out)
    os.makedirs(out, exist_ok=True)
    text = json.dumps(solo_file(bank()), separators=(",", ":"), ensure_ascii=True)
    path = os.path.join(out, "solo-demo.cbsolo")
    with open(path, "w", newline="\n", encoding="utf-8") as f:
        f.write(text)
    print("solo-demo.cbsolo  %6d bytes  8 sounds, 5 library entries" % len(text))


if __name__ == "__main__":
    main()
