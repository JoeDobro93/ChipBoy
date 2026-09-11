#!/usr/bin/env python3
"""ChipBoy demo song generator (docs/COMMANDS_AND_TEMPO.md section 24).

Writes six original songs under Demo/songs (or --out), each a **format 6**
`.cbsong`: the song *and* the bank it plays through, so opening one brings its
own sounds with it (section 18). Every phrase carries its own length and its
groove, and each channel moves to its next row when its own phrase ends
(section 25), so the widths a section asks for become phrase lengths.

  groove-study.cbsong      7/5 swing, an 8 8 8 triplet section, a G mid-song
  meter-study.cbsong       3/4 at twelve steps, a 7/8 bar, a 5/4 stretch
  route-theme.cbsong       a bright handheld-RPG route theme
  puffball-bounce.cbsong   a bouncy platformer tune: W duties, V, staccato bass
  neon-grid.cbsong         wave-channel kick and snare built out of tables
  wave-study.cbsong        half-time frame sweeps and tempo-synced P wobbles

Standard library only. Deterministic: two runs give byte-identical files.

    python3 tools/demo/make_songs.py [--out Demo/songs] [--list]

The music is original: nothing here is transcribed from a game, a record or
LSDj. What each song *demonstrates* is the point -- every one is a page of the
command table played rather than written down.

How a song is written here
--------------------------
A song is Python data. Its bank is a handful of `pulse()`, `wave()`, `noise()`,
`table()` and `wav()` calls, named rather than numbered; its music is a list of
**sections**, each a bar count, a groove and one bar of text per channel. A bar
of text is its steps, separated by spaces (`|` reads as a bar line and is
ignored):

    "C-4@lead . . . | ~E-4 . . . | off . . . | . . . +V 0 4"

    .          an empty step               off        a note off
    C-4        a note (C, octave 4)        ~C-4       explicitly bare
    @lead      the instrument column       :96        the VEL column
    %shape     the table column            +V0,4      a command, up to two

A note with an instrument is *plain* (it loads the instrument and triggers);
one without is *bare* -- the pitch changes and nothing else (section 8). Every
letter, its arguments and the channels it means anything on are checked here
against section 2, and every name is resolved against the song's own bank, so a
song that this script writes is a song the plugin can play.
"""

import argparse
import json
import math
import os
import re
import sys

# ---------------------------------------------------------------------------
# notes
# ---------------------------------------------------------------------------

SEMITONE = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}
NOTE_RE = re.compile(r"^([A-G])([-#b])(-?\d)$")
NOTE_OFF = 255


def note(name):
    """"C-4" is middle C (MIDI 60), "F#3" and "Bb2" read as you would expect."""
    m = NOTE_RE.match(name)
    if not m:
        raise ValueError("not a note: %r" % name)
    letter, accidental, octave = m.group(1), m.group(2), int(m.group(3))
    value = SEMITONE[letter] + {"-": 0, "#": 1, "b": -1}[accidental] + (octave + 1) * 12
    if not 1 <= value <= 127:
        raise ValueError("note out of range: %r" % name)
    return value


def note_name(value):
    if value == NOTE_OFF:
        return "off"
    names = ["C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-"]
    return "%s%d" % (names[value % 12], value // 12 - 1)


# ---------------------------------------------------------------------------
# commands (docs/COMMANDS_AND_TEMPO.md section 2)
# ---------------------------------------------------------------------------

# letter: (arguments, x range, y range, where it means something)
# Section 103: the wave bank is WAVE_SLOTS slots of WAVE_FRAMES frames, one flat
# table, and every slot is all sixteen whether or not anything was drawn in it.
WAVE_SLOTS, WAVE_FRAMES = 16, 16

#   P  pulse channels        W  the wave channel        N  the noise channel
#   1  PU1 only              *  every channel (the timeline or the master)
CMD_SPEC = {
    "A": (1, (0, 64), None, "PWN"),          # table slot, 0 stops
    "C": (2, (0, 60), (0, 60), "PW"),        # chord: semitones
    "D": (1, (0, 255), None, "PWN"),         # delay in ticks
    "E": (2, (0, 15), (0, 15), "PWN"),       # envelope; on WAV x is the level 0-3
    "F": (1, (1, 16), None, "W"),            # frame
    "G": (1, (0, 16), None, "*"),            # groove slot, 0 straight
    "H": (2, (0, 15), (0, 15), ""),          # tables only: times, row (section 34)
    "K": (1, (0, 255), None, "PWN"),         # kill after x ticks
    "L": (1, (0, 255), None, "PW"),          # slide: x + 1 updates, in semitones
    "M": (2, (0, 15), (0, 15), "*"),         # master volume, per side (nibbles)
    "O": (1, (0, 3), None, "PWN"),           # pan
    "P": (1, (-128, 255), None, "PW"),       # bend speed, two's complement (34)
    "R": (2, (0, 15), (0, 15), "PWN"),       # retrigger: signed nibble, y x (rate+1)+1 ticks
    "S": (2, (0, 7), (0, 15), "1"),          # sweep, PU1 only: rate, NR10's low nibble
    "T": (1, (40, 295), None, "*"),          # tempo in BPM; stored as LSDj's byte (34)
    "V": (2, (0, 15), (0, 15), "PW"),        # vibrato: 64/(x+1) updates a cycle, depth
    "W": (1, (0, WAVE_SLOTS), None, "PW"),   # duty (pulse) / wave slot (WAV), section 103
    "Z": (2, (0, 15), (0, 15), "PWN"),       # random (nibbles, section 34)
}

CHANNEL_CLASS = ["P", "P", "W", "N"]     # PU1 PU2 WAV NOI
CHANNEL_NAME = ["PU1", "PU2", "WAV", "NOI"]
# The period register runs out at the bottom: a pulse channel is 131072 / (2048
# - p) Hz, so it stops at 64 Hz, and the wave channel at half that. A note
# below its channel's floor is not quiet, it is nothing at all -- the driver
# has no period for it and the voice never starts -- so it is an error here.
LOWEST_NOTE = [36, 36, 24, 1]            # C-2, C-2, C-1, anything on noise

REVERT = 1                               # bank::kRevert: the letter's revert form


class Cmd:
    """One command: a letter and its two arguments, or the revert form."""

    __slots__ = ("letter", "x", "y", "revert")

    def __init__(self, letter, x=0, y=0, revert=False):
        self.letter, self.x, self.y, self.revert = letter, x, y, revert

    def var(self):
        return {"c": self.letter, "a": self.x, "b": self.y, "x": REVERT if self.revert else 0}

    def key(self):
        return (self.letter, self.x, self.y, self.revert)

    def __repr__(self):
        return "%s=" % self.letter if self.revert else "%s %d %d" % (self.letter, self.x, self.y)


def parse_cmd(text, where, channel=None, in_table=False):
    """`V9,4`, `W2`, `E15,2`, `G=` (the revert form). `channel` is 0-3 for a
    cell, None inside a table (where the instrument's own kind decides)."""
    letter = text[0].upper()
    if letter not in CMD_SPEC:
        raise ValueError("%s: no such command letter %r (section 2)" % (where, letter))
    nargs, xr, yr, channels = CMD_SPEC[letter]
    rest = text[1:].strip()
    if rest == "=":
        # The revert form: the letter with nothing to say but "put this back".
        if letter in "CDHKLRZ":
            raise ValueError("%s: %s is a per-note letter and has no revert form" % (where, letter))
        return Cmd(letter, 0, 0, revert=True)
    if letter == "H" and not in_table:
        raise ValueError("%s: H only means something inside a table (section 2)" % where)
    if channel is not None and letter != "H":
        allowed = channels == "*" or CHANNEL_CLASS[channel] in channels or (channels == "1" and channel == 0)
        if not allowed:
            raise ValueError("%s: %s does nothing on %s (section 2)" % (where, letter, CHANNEL_NAME[channel]))
    args = [a for a in re.split(r"[,\s]+", rest) if a != ""]
    if len(args) > nargs:
        raise ValueError("%s: %s takes %d argument(s), got %r" % (where, letter, nargs, rest))
    values = [int(a) for a in args] + [0] * (nargs - len(args))
    x = values[0]
    y = values[1] if nargs > 1 else 0
    lo, hi = xr
    # W is a duty on a pulse channel and a wave slot on the wave channel; E's
    # x is a level there. The ranges follow the channel.
    if letter == "W" and channel is not None:
        lo, hi = (0, 3) if CHANNEL_CLASS[channel] == "P" else (1, 64)
    if letter == "E" and channel == 2:
        lo, hi = 0, 3
    if not lo <= x <= hi:
        raise ValueError("%s: %s x = %d is outside %d-%d" % (where, letter, x, lo, hi))
    if yr is not None and not yr[0] <= y <= yr[1]:
        raise ValueError("%s: %s y = %d is outside %d-%d" % (where, letter, y, yr[0], yr[1]))
    # Section 34: P is stored two's complement, so a song may write -14 and the
    # file carries 242; T is stored as LSDj's byte, so 40-255 BPM is the number
    # itself and 256-295 wraps to 00-27.
    if letter == "P":
        x &= 0xFF
    elif letter == "T":
        x = x if x <= 255 else x - 256
    return Cmd(letter, x, y)


# ---------------------------------------------------------------------------
# waves (the generated shapes of Source/core/Bank/Bank.cpp, in Python)
# ---------------------------------------------------------------------------

def q4(x):
    """A -1..1 sample as one of the wave RAM's sixteen levels."""
    return max(0, min(15, int(math.floor(7.5 + 7.5 * x + 0.5))))


def frame_sine():
    return [q4(math.sin(2.0 * math.pi * (i + 0.5) / 32.0)) for i in range(32)]


def frame_triangle():
    return [i if i < 16 else 31 - i for i in range(32)]


def frame_saw():
    return [i // 2 for i in range(32)]


def frame_pulse(width):
    return [15 if i < width else 0 for i in range(32)]


def frame_mix(a, b, t):
    return [max(0, min(15, int(math.floor(a[i] * (1.0 - t) + b[i] * t + 0.5)))) for i in range(32)]


def frame_harmonics(*amps):
    """A frame from a harmonic series: amps are the amplitudes of harmonic 1, 2, 3..."""
    out = []
    for i in range(32):
        v = 0.0
        for h, a in enumerate(amps, start=1):
            v += a * math.sin(2.0 * math.pi * h * (i + 0.5) / 32.0)
        out.append(q4(max(-1.0, min(1.0, v))))
    return out


def frame_grit(seed):
    """A deterministic ragged frame: the wave channel's own kind of noise."""
    state = seed & 0xFFFFFFFF
    out = []
    for _ in range(32):
        state = (state * 1103515245 + 12345) & 0xFFFFFFFF
        out.append((state >> 20) & 15)
    return out


def morph(a, b, count):
    """`count` frames stepping from shape a to shape b."""
    return [frame_mix(a, b, k / float(count - 1)) for k in range(count)]


def wave_run(length):
    """Section 65: the frames a run of `length` visits on a WAVE_FRAMES wave."""
    if length <= 0 or length > WAVE_FRAMES:
        length = WAVE_FRAMES
    if length == 1:
        return [0]
    return [min(WAVE_FRAMES - 1, (i * WAVE_FRAMES) // (length - 1)) for i in range(length)]


def spread_frames(frames):
    """Section 103: lay `frames` on the sixteen at the positions a run of that
    many visits, holding each shape until the next, so a run of `len(frames)`
    walks exactly the shapes given, in order."""
    out = [frames[0]] * WAVE_FRAMES
    at = wave_run(len(frames))
    for k, f in enumerate(frames):
        for i in range(at[k], WAVE_FRAMES):
            out[i] = f
    return out


# ---------------------------------------------------------------------------
# the bank
# ---------------------------------------------------------------------------

PULSE, WAVE, KIT, NOISE = 0, 1, 2, 3
PAN_OFF, PAN_L, PAN_R, PAN_BOTH = 0, 1, 2, 3
ENV_DOWN, ENV_UP = 0, 1
OFF_KILL, OFF_RELEASE, OFF_IGNORE = 0, 1, 2
VIB_TRI, VIB_SAW, VIB_SQUARE = 0, 1, 2
VIB_DOWN, VIB_UP = 0, 1
FAST, TICK, STEP, DRUM = 0, 1, 2, 3          # pitch speed (section 7)
TABLE_TICK, TABLE_STEP = 0, 1
LEGATO, RETRIG = 0, 1
FRAME_LOOP, FRAME_ONCE, FRAME_PINGPONG = 0, 1, 2
END_LOOP, END_HOP, END_STOP = 0, 1, 2


class Bank:
    """The sounds one song owns. Slots are handed out in definition order and
    everything is named, so the music never mentions a number."""

    def __init__(self, name):
        self.name = name
        self.instruments = []                # (name, dict)
        self.tables = []
        self.waves = []
        self.instrument_slot = {}
        self.table_slot = {}
        self.wave_slot = {}
        self.wave_frames = {}

    # -- instruments ------------------------------------------------------
    def _add_instrument(self, name, fields):
        if name in self.instrument_slot:
            raise ValueError("instrument %r twice" % name)
        slot = len(self.instruments) + 1
        if slot > 128:
            raise ValueError("more than 128 instruments")
        self.instrument_slot[name] = slot
        self.instruments.append((name, fields))
        return slot

    def pulse(self, name, duty=2, vol=15, rate=0, up=False, **kw):
        f = dict(type=PULSE, duty=duty, envVol=vol, envRate=rate, envDir=ENV_UP if up else ENV_DOWN)
        return self._add_instrument(name, self._common(f, **kw))

    def wave(self, name, wave="", level=3, advance=0, loop=FRAME_LOOP, frames=None, **kw):
        # Section 103: every slot is WAVE_FRAMES frames, so the run has to say how
        # many of them to visit. Left out, it is the count the wave was given --
        # the shapes `wav()` was called with, in order.
        if frames is None:
            frames = self.wave_frames.get(wave, 0) if wave else 0
        f = dict(type=WAVE, wave=wave, waveLevel=level, frameAdvance=advance, frameLoop=loop,
                 frameLength=frames)
        return self._add_instrument(name, self._common(f, **kw))

    def noise(self, name, vol=15, rate=0, lfsr7=False, shift=5, div=1, sweep=0, manual=True, **kw):
        f = dict(type=NOISE, envVol=vol, envRate=rate, envDir=ENV_DOWN, lfsr7=lfsr7,
                 noiseManual=manual, noiseShift=shift, noiseDivisor=div, noiseSweep=sweep)
        return self._add_instrument(name, self._common(f, **kw))

    @staticmethod
    def _common(fields, table="", length=0, pan=PAN_BOTH, note_off=None, overlap=None,
                pitch=FAST, rate_cmd=0, table_mode=TABLE_TICK, vib=None, transpose=True,
                sweep_rate=0, sweep_down=False, sweep_shift=0, duty_seq=None):
        kind = fields["type"]
        fields.update(
            table=table, length=length, pan=pan,
            noteOff=note_off if note_off is not None else (OFF_IGNORE if kind == KIT else OFF_KILL),
            overlap=overlap if overlap is not None else (RETRIG if kind in (KIT, NOISE) else LEGATO),
            pitchSpeed=pitch, cmdRate=rate_cmd, tableMode=table_mode, transpose=transpose,
            vib=vib or (VIB_TRI, VIB_DOWN, 8, 0, 0),
            sweepRate=sweep_rate, sweepDown=sweep_down, sweepShift=sweep_shift,
            dutySeq=list(duty_seq or []))
        return fields

    # -- tables, waves ----------------------------------------------------
    def table(self, name, steps, end=END_LOOP, hop=1):
        """`steps` is a list of step texts: "v15", "t-12", "+P-38+E3", "." ..."""
        if name in self.table_slot:
            raise ValueError("table %r twice" % name)
        slot = len(self.tables) + 1
        if slot > 64:
            raise ValueError("more than 64 tables")
        self.table_slot[name] = slot
        self.tables.append((name, [parse_table_step(s, "table %s step %d" % (name, i))
                                   for i, s in enumerate(steps)], end, hop))
        return slot

    def wav(self, name, frames):
        """A wave slot.  Section 103: a slot is always WAVE_FRAMES frames, so the
        shapes given are laid on the sixteen at the positions a run of that many
        visits (section 65) and each is held until the next.  An instrument
        playing this wave then walks exactly the shapes given, in order, by
        taking `frames = len(frames)` -- which `wave()` does by itself."""
        if name in self.wave_slot:
            raise ValueError("wave %r twice" % name)
        slot = len(self.waves) + 1
        if slot > WAVE_SLOTS:
            raise ValueError("more than %d waves" % WAVE_SLOTS)
        if not 1 <= len(frames) <= WAVE_FRAMES:
            raise ValueError("wave %r: %d frames (1-%d)" % (name, len(frames), WAVE_FRAMES))
        self.wave_slot[name] = slot
        self.wave_frames[name] = len(frames)
        self.waves.append((name, spread_frames(frames)))
        return slot

    # -- resolution -------------------------------------------------------
    def instrument(self, name, where=""):
        if name not in self.instrument_slot:
            raise ValueError("%s: no instrument named %r in bank %r" % (where, name, self.name))
        return self.instrument_slot[name]

    def kind_of(self, name):
        return self.instruments[self.instrument_slot[name] - 1][1]["type"]

    def var(self):
        instruments = []
        for i, (name, f) in enumerate(self.instruments, start=1):
            shape, direction, speed, depth, delay = f["vib"]
            instruments.append({
                "slot": i, "name": name, "type": f["type"],
                "pan": f["pan"], "length": f["length"],
                "table": self.table_slot[f["table"]] if f["table"] else 0,
                "transpose": bool(f["transpose"]), "noteOff": f["noteOff"], "overlap": f["overlap"],
                "pitchSpeed": f["pitchSpeed"], "cmdRate": f["cmdRate"], "tableMode": f["tableMode"],
                "vibShape": shape, "vibDir": direction, "vibSpeed": speed, "vibDepth": depth, "vibDelay": delay,
                "duty": f.get("duty", 2), "dutySeq": f["dutySeq"],
                "envVol": f.get("envVol", 15), "envDir": f.get("envDir", ENV_DOWN), "envRate": f.get("envRate", 0),
                "sweepRate": f["sweepRate"], "sweepDown": bool(f["sweepDown"]), "sweepShift": f["sweepShift"],
                "wave": self.wave_slot[f["wave"]] if f.get("wave") else 1,
                "frameAdvance": f.get("frameAdvance", 0), "frameLoop": f.get("frameLoop", FRAME_LOOP),
                "waveLevel": f.get("waveLevel", 3),
                "kit": 1, "kitLoop": 0,
                "lfsr7": bool(f.get("lfsr7", False)), "noiseManual": bool(f.get("noiseManual", False)),
                "noiseShift": f.get("noiseShift", 5), "noiseDivisor": f.get("noiseDivisor", 1),
                "noiseSweep": f.get("noiseSweep", 0)})
        tables = []
        for i, (name, steps, end, hop) in enumerate(self.tables, start=1):
            rows = []
            for vol, transpose, cmds in steps + [(-1, None, [])] * (16 - len(steps)):
                row = {"vol": vol}
                if transpose is not None:
                    row["trn"] = transpose
                for k, c in enumerate(cmds, start=1):
                    row["c%d" % k] = c.var()
                rows.append(row)
            tables.append({"slot": i, "name": name, "end": end, "hop": hop, "steps": rows})
        waves = [{"slot": i, "name": name, "frames": frames}
                 for i, (name, frames) in enumerate(self.waves, start=1)]
        return {"format": "chipboy-bank", "version": 1,
                "instruments": instruments, "tables": tables, "waves": waves, "kits": []}


def parse_table_step(text, where):
    """A table step: "v15" a volume, "t-12" a transpose, "+P-38" a command."""
    vol, transpose, cmds = -1, None, []
    for field in [f for f in text.replace("+", " +").split() if f not in (".", "")]:
        if field.startswith("+"):
            if len(cmds) == 2:
                raise ValueError("%s: a table step holds two commands" % where)
            cmds.append(parse_cmd(field[1:], where, channel=None, in_table=True))
        elif field[0] == "v":
            vol = int(field[1:])
            if not 0 <= vol <= 15:
                raise ValueError("%s: volume %d is outside 0-15" % (where, vol))
        elif field[0] == "t":
            transpose = int(field[1:])
            if not -60 <= transpose <= 60:
                raise ValueError("%s: transpose %d is outside -60..60" % (where, transpose))
        else:
            raise ValueError("%s: %r is not a table field" % (where, field))
    return (vol, transpose, cmds)


# ---------------------------------------------------------------------------
# cells
# ---------------------------------------------------------------------------

class Cell:
    __slots__ = ("note", "vel", "inst", "table", "cmds")

    def __init__(self):
        self.note = 0
        self.vel = 0
        self.inst = 0
        self.table = 0
        self.cmds = []

    def empty(self):
        return self.note == 0 and self.vel == 0 and self.inst == 0 and self.table == 0 and not self.cmds

    def key(self):
        return (self.note, self.vel, self.inst, self.table, tuple(c.key() for c in self.cmds))

    def var(self, step):
        out = {"s": step}
        if self.note:
            out["n"] = self.note
        if self.vel:
            out["v"] = self.vel
        if self.inst:
            out["i"] = self.inst
        if self.table:
            out["t"] = self.table
        for k, c in enumerate(self.cmds, start=1):
            out["c%d" % k] = c.var()
        return out


TOKEN_RE = re.compile(r"^(~?)([A-G][-#b]-?\d|off)?((?:@[A-Za-z0-9_-]+)?)((?::\d+)?)((?:%[A-Za-z0-9_-]+)?)(.*)$")


def parse_cell(token, channel, bank, where):
    """One step of a bar of text. See this file's docstring for the grammar."""
    cell = Cell()
    if token == ".":
        return cell
    m = TOKEN_RE.match(token)
    if not m:
        raise ValueError("%s: %r is not a step" % (where, token))
    bare, pitch, inst, vel, table, rest = m.groups()
    if pitch == "off":
        cell.note = NOTE_OFF
    elif pitch:
        cell.note = note(pitch)
        if cell.note < LOWEST_NOTE[channel]:
            raise ValueError("%s: %s is below %s's lowest note, %s -- the channel has no period for it"
                             % (where, pitch, CHANNEL_NAME[channel], note_name(LOWEST_NOTE[channel])))
    if inst:
        if bare:
            raise ValueError("%s: %r is both bare and instrumented" % (where, token))
        name = inst[1:]
        cell.inst = bank.instrument(name, where)
        kind = bank.kind_of(name)
        wanted = {0: PULSE, 1: PULSE, 2: WAVE, 3: NOISE}[channel]
        if kind != wanted and not (channel == 2 and kind == KIT):
            raise ValueError("%s: instrument %r is not a %s instrument" % (where, name, CHANNEL_NAME[channel]))
    if vel:
        cell.vel = int(vel[1:])
        if not 1 <= cell.vel <= 127:
            raise ValueError("%s: velocity %d is outside 1-127" % (where, cell.vel))
    if table:
        name = table[1:]
        if name not in bank.table_slot:
            raise ValueError("%s: no table named %r" % (where, name))
        cell.table = bank.table_slot[name]
    for part in [p for p in rest.split("+") if p != ""]:
        if len(cell.cmds) == 2:
            raise ValueError("%s: a cell holds two commands (section 9.1)" % where)
        cell.cmds.append(parse_cmd(part, where, channel=channel))
    if cell.empty() and token not in (".",):
        raise ValueError("%s: %r says nothing" % (where, token))
    return cell


def add_cmd(text, cmd, step=0):
    """The same bar with one more command on a step -- an empty step becomes a
    cell that holds nothing but the command, which is how a G lands on every
    channel at once without touching the notes."""
    tokens = [t for t in text.replace("|", " ").split() if t]
    while len(tokens) <= step:
        tokens.append(".")
    token = tokens[step]
    tokens[step] = ("" if token == "." else token) + "+" + cmd
    return " ".join(tokens)


def lay(steps, items, inst=None):
    """A bar of text from (step, token) pairs: everything else is empty. A
    token with a note and no `@` takes `inst`; `~` keeps it bare."""
    cells = ["."] * steps
    for at, token in items:
        if not 0 <= at < steps:
            raise ValueError("step %d is outside a %d-step bar" % (at, steps))
        if cells[at] != ".":
            raise ValueError("two things on step %d" % at)
        if inst and "@" not in token and not token.startswith("~") and not token.startswith("+"):
            head, _, tail = token.partition("+")
            token = head + "@" + inst + ("+" + tail if tail else "")
        cells[at] = token
    return " ".join(cells)


# ---------------------------------------------------------------------------
# the song
# ---------------------------------------------------------------------------

STRAIGHT = [6, 6]


class Song:
    """Sections of bars, interned into phrases: the chains, the phrases, the
    grooves and the bank, ready to be written as format 6. A bar of text is a
    phrase of that many steps; two sections of different widths therefore make
    two phrases, which is section 25's rule at generation time."""

    def __init__(self, title, bank, tempo, beats_per_bar=4, steps_per_bar=16,
                 grooves=None, blurb=""):
        self.title, self.bank, self.tempo = title, bank, float(tempo)
        self.beats_per_bar = float(beats_per_bar)
        self.steps_per_bar = int(steps_per_bar)
        self.blurb = blurb
        self.grooves = [list(STRAIGHT) for _ in range(16)]
        for slot, ticks in (grooves or {}).items():
            if not 1 <= slot <= 16:
                raise ValueError("groove slot %d is outside 1-16" % slot)
            if not 1 <= len(ticks) <= 16 or any(not 1 <= t <= 48 for t in ticks):
                raise ValueError("groove %d: sixteen counts of 1-48 at most" % slot)
            self.grooves[slot - 1] = list(ticks)
        self.chains = [[], [], [], []]
        self.bar_steps = []
        self.phrases = []                     # (groove, [Cell])
        self.phrase_steps = []                # the widest bar each phrase plays in
        self._index = {}

    # -- writing the music ------------------------------------------------
    def row_ticks(self, groove, steps):
        """How long a phrase of that width under that groove lasts, in ticks:
        a step is the groove's entry for it, six at the straight groove."""
        g = self.grooves[groove - 1] if 1 <= groove <= 16 else list(STRAIGHT)
        return sum(g[i % len(g)] for i in range(steps))

    def section(self, bars, groove=0, steps=0, **rows):
        """`bars` bars in which every channel plays its row. A row is one bar
        of text, a list of that many, or None for a channel that rests (a note
        off at its first tick, section 9.1).

        A row with no phrase at all lasts ninety-six ticks -- sixteen straight
        steps (section 25) -- so where the section's own rows are another
        length the rest is written as a phrase of that width holding the note
        off, which sounds the same and keeps the channel with the others."""
        for name in rows:
            if name not in CHANNEL_NAME:
                raise ValueError("no channel called %r" % name)
        first = len(self.chains[0])
        width = steps or self.steps_per_bar
        for bar in range(bars):
            self.bar_steps.append(steps)
            for ch, name in enumerate(CHANNEL_NAME):
                row = rows.get(name)
                if isinstance(row, (list, tuple)):
                    if len(row) != bars:
                        raise ValueError("%s: %d bars of text for a %d-bar section" % (name, len(row), bars))
                    text = row[bar]
                else:
                    text = row
                where = "%s bar %d %s" % (self.title, first + bar + 1, name)
                if text is None and self.row_ticks(groove, width) != 6 * 16:
                    text = "off"                     # a rest the length of the section's own rows
                self.chains[ch].append(0 if text is None else self._phrase(text, groove, width, ch, where))
        return self

    def _phrase(self, text, groove, steps, channel, where):
        cells = []
        tokens = [t for t in text.replace("|", " ").split() if t]
        if len(tokens) > steps:
            raise ValueError("%s: %d steps of text in a %d-step bar" % (where, len(tokens), steps))
        for i, token in enumerate(tokens):
            cells.append(parse_cell(token, channel, self.bank, "%s step %d" % (where, i)))
        # The length is part of the phrase now, so it is part of its identity.
        key = (groove, steps, tuple(c.key() for c in cells))
        if key not in self._index:
            self.phrases.append((groove, cells))
            self.phrase_steps.append(steps)
            self._index[key] = len(self.phrases)
            if len(self.phrases) > 255:
                raise ValueError("%s: more than 255 phrases" % self.title)
        return self._index[key]

    # -- what came out ----------------------------------------------------
    def bars(self):
        return len(self.chains[0])

    def instruments_used(self):
        used = set()
        for chain in self.chains:
            for slot in chain:
                if slot:
                    for cell in self.phrases[slot - 1][1]:
                        if cell.inst:
                            used.add(cell.inst)
        return sorted(used)

    def busy_bars(self, channel):
        """Which bars this channel starts a note in: the arrangement, checked."""
        out = []
        for bar, slot in enumerate(self.chains[channel]):
            if slot and any(c.note not in (0, NOTE_OFF) for c in self.phrases[slot - 1][1]):
                out.append(bar + 1)
        return out

    def check(self):
        """What a song has to be before it is written (section 24)."""
        n = self.bars()
        # Nothing in the bank that the song never plays: a song file carries
        # its sounds, so every one of them should be one you can hear.
        used = set(self.instruments_used())
        for slot, (name, _f) in enumerate(self.bank.instruments, start=1):
            if slot not in used:
                raise ValueError("%s: instrument %r is in the bank and never played" % (self.title, name))
        tables = set(self.bank.table_slot[f["table"]] for _n, f in self.bank.instruments if f["table"])
        for chain in self.chains:
            for slot in chain:
                if slot:
                    tables |= set(c.table for c in self.phrases[slot - 1][1] if c.table)
        for slot, (name, _steps, _end, _hop) in enumerate(self.bank.tables, start=1):
            if slot not in tables:
                raise ValueError("%s: table %r is in the bank and never runs" % (self.title, name))
        waves = set(self.bank.wave_slot[f["wave"]] for _n, f in self.bank.instruments if f.get("wave"))
        for slot in self.chains[2]:                      # W names a wave slot on WAV alone
            if slot:
                for cell in self.phrases[slot - 1][1]:
                    for cmd in cell.cmds:
                        if cmd.letter == "W" and not cmd.revert:
                            waves.add(cmd.x)
        for slot, (name, _frames) in enumerate(self.bank.waves, start=1):
            if slot not in waves:
                raise ValueError("%s: wave %r is in the bank and never sounds" % (self.title, name))
        if not 16 <= n <= 32:
            raise ValueError("%s: %d bars (16-32)" % (self.title, n))
        for ch in range(4):
            busy = self.busy_bars(ch)
            if not busy:
                raise ValueError("%s: %s never plays a note" % (self.title, CHANNEL_NAME[ch]))
            if not any(b <= 8 for b in busy):
                raise ValueError("%s: %s plays nothing in the first eight bars, which is what the "
                                 "check listens to" % (self.title, CHANNEL_NAME[ch]))
        for bar in range(n):
            playing = sum(1 for ch in range(4) if self.chains[ch][bar])
            if playing < 2:
                raise ValueError("%s: bar %d has %d channel(s) playing (two at least)"
                                 % (self.title, bar + 1, playing))
        # Every groove a phrase or a G names has to fill its bar, or the steps
        # past the end never fire (section 9.2) -- which is deliberate in the
        # triplet grooves and a mistake anywhere else, so it is reported.
        for slot, (groove, cells) in enumerate(self.phrases, start=1):
            if groove == 0:
                continue
            ticks = self.grooves[groove - 1]
            last = max([i for i, c in enumerate(cells) if not c.empty()] or [0])
            reach = sum(ticks[i % len(ticks)] for i in range(last + 1))
            if reach > 6 * self.phrase_steps[slot - 1]:
                raise ValueError("%s: phrase %d writes step %d, which groove %d puts past the bar's end"
                                 % (self.title, slot, last, groove))
        return self

    # -- the file ---------------------------------------------------------
    def song_var(self):
        phrases = []
        for i, (groove, cells) in enumerate(self.phrases, start=1):
            written = [c.var(k) for k, c in enumerate(cells) if not c.empty()]
            phrases.append({"slot": i, "groove": groove,
                            "steps": self.phrase_steps[i - 1], "cells": written})
        return {"format": "chipboy-song", "version": 6,
                "tempoBpm": self.tempo, "songStartSeconds": 0.0,
                "phrases": phrases,
                "chains": [list(c) for c in self.chains],
                "noteSource": [1, 1, 1, 1],          # every channel plays its own cells (Trkr)
                "recordArm": [True, True, True, True],
                "grooves": [list(g) for g in self.grooves]}

    def file_var(self):
        names = {}
        for slot in self.instruments_used():
            names[str(slot)] = self.bank.instruments[slot - 1][0]
        return {"format": "chipboy-song-file", "version": 6,
                "bank": self.bank.name, "instruments": names,
                "bankData": self.bank.var(), "song": self.song_var()}

    def write(self, path):
        text = json.dumps(self.file_var(), separators=(",", ":"), ensure_ascii=True)
        with open(path, "w", newline="\n", encoding="utf-8") as f:
            f.write(text)
        return len(text)

    def summary(self):
        return "%-22s %3d bars  %5.1f BPM  %g/4  %2d steps/bar  %2d instruments  %3d phrases" % (
            self.title, self.bars(), self.tempo, self.beats_per_bar, self.steps_per_bar,
            len(self.bank.instruments), len(self.phrases))


# ---------------------------------------------------------------------------
# 1. groove-study -- the same tune under four feels
# ---------------------------------------------------------------------------

def groove_study():
    b = Bank("Groove Study")
    b.wav("triangle", [frame_triangle()])
    b.table("kick-shape", ["v15", "v11", "v7", "v3"], end=END_STOP)
    b.pulse("lead", duty=2, vol=13, rate=0, vib=(VIB_TRI, VIB_DOWN, 9, 2, 8))
    b.pulse("pluck", duty=1, vol=15, rate=3, pitch=TICK)
    b.wave("bass", wave="triangle", level=3)
    b.noise("kick", vol=15, rate=2, shift=6, div=3, sweep=-1, table="kick-shape")
    b.noise("snare", vol=13, rate=3, shift=4, div=4)
    b.noise("hat", vol=8, rate=1, lfsr7=True, shift=1, div=4)
    b.noise("crash", vol=12, rate=6, shift=2, div=1)

    # The tune: four bars over Am F G Am, written once on the sixteenth grid
    # and once on the triplet grid, the same notes both times.
    melody16 = [
        [(0, "E-4"), (4, "A-4"), (6, "B-4"), (8, "C-5"), (12, "B-4")],
        [(0, "A-4"), (6, "G-4"), (8, "F-4"), (12, "E-4")],
        [(0, "D-4"), (4, "G-4"), (6, "A-4"), (8, "B-4"), (12, "A-4")],
        [(0, "E-4"), (8, "A-4")]]
    melody12 = [
        [(0, "E-4"), (3, "A-4"), (4, "B-4"), (6, "C-5"), (9, "B-4")],
        [(0, "A-4"), (4, "G-4"), (6, "F-4"), (9, "E-4")],
        [(0, "D-4"), (3, "G-4"), (4, "A-4"), (6, "B-4"), (9, "A-4")],
        [(0, "E-4"), (6, "A-4")]]
    stabs16 = [
        [(2, "A-3"), (6, "C-4"), (10, "E-4"), (14, "C-4")],
        [(2, "F-3"), (6, "A-3"), (10, "C-4"), (14, "A-3")],
        [(2, "G-3"), (6, "B-3"), (10, "D-4"), (14, "B-3")],
        [(2, "A-3"), (6, "C-4"), (10, "E-4"), (14, "A-3")]]
    stabs12 = [
        [(1, "A-3"), (4, "C-4"), (7, "E-4"), (10, "C-4")],
        [(1, "F-3"), (4, "A-3"), (7, "C-4"), (10, "A-3")],
        [(1, "G-3"), (4, "B-3"), (7, "D-4"), (10, "B-3")],
        [(1, "A-3"), (4, "C-4"), (7, "E-4"), (10, "A-3")]]
    bass16 = [
        [(0, "A-2"), (8, "A-2"), (12, "E-3")],
        [(0, "F-2"), (8, "F-2"), (12, "C-3")],
        [(0, "G-2"), (8, "G-2"), (12, "D-3")],
        [(0, "A-2"), (8, "E-2")]]
    bass12 = [
        [(0, "A-2"), (6, "A-2"), (9, "E-3")],
        [(0, "F-2"), (6, "F-2"), (9, "C-3")],
        [(0, "G-2"), (6, "G-2"), (9, "D-3")],
        [(0, "A-2"), (6, "E-2")]]

    lead = [lay(16, m, "lead") for m in melody16]
    lead_t = [lay(12, m, "lead") for m in melody12]
    stab = [lay(16, m, "pluck") for m in stabs16]
    stab_t = [lay(12, m, "pluck") for m in stabs12]
    low = [lay(16, m, "bass") for m in bass16]
    low_t = [lay(12, m, "bass") for m in bass12]

    K, S, H, h, x = "C-2@kick", "D-2@snare", "F#2@hat", "F#2@hat:70", "."
    beat = " ".join([K, x, h, x, S, x, h, x, K, x, h, K, S, x, h, h])
    beat2 = " ".join([K, x, h, x, S, x, h, K, x, K, h, x, S, x, h, S])
    beat_t = " ".join([K, x, h, S, x, h, K, x, h, S, x, h])
    hats = " ".join([H, x, h, x, H, x, h, x, H, x, h, x, H, x, h, x])

    s = Song("Groove Study", b, tempo=132, grooves={1: [7, 5], 2: [8, 4], 3: [8, 8, 8],
                                                    4: [5, 7], 5: [9, 3], 6: [4, 4, 4]},
             blurb="one tune, four feels")
    # Bars 1-2: straight, so the ear has something to measure the swing against.
    s.section(2, groove=0,
              PU1=[None, lay(16, [(12, "E-4")], "lead")],
              PU2=[None, stab[0]],
              WAV=[low[0], low[0]],
              NOI=[hats, beat])
    # Bars 3-10: groove 1, seven ticks then five -- LSDj's 58 % swing.
    s.section(4, groove=1, PU1=lead, PU2=stab, WAV=low, NOI=[beat, beat, beat, beat2])
    s.section(4, groove=1, PU1=lead, PU2=stab, WAV=low, NOI=[beat, beat, beat, beat2])
    # Bars 11-14: groove 3 is 8 8 8, so twelve steps fill the bar as triplet
    # eighths and the last four never fire. The same notes, three to a beat.
    s.section(4, groove=3, PU1=lead_t, PU2=stab_t, WAV=low_t,
              NOI=[beat_t, beat_t, beat_t, beat_t])
    # Bars 15-18: the phrases still say groove 1, and a G on every channel's
    # first step overrides it with groove 2 -- 8 4, a two-to-one shuffle.
    g2 = lambda text: add_cmd(text, "G2")
    s.section(4, groove=1,
              PU1=[g2(lead[0])] + lead[1:], PU2=[g2(stab[0])] + stab[1:],
              WAV=[g2(low[0])] + low[1:], NOI=[g2(beat), beat, beat, beat2])
    # Bars 19-22: G reverts, so every channel is back on the phrase's own 7 5.
    rev = lambda text: add_cmd(text, "G=")
    s.section(4, groove=1,
              PU1=[rev(lead[0])] + lead[1:], PU2=[rev(stab[0])] + stab[1:],
              WAV=[rev(low[0])] + low[1:], NOI=[rev(beat), beat, beat, beat2])
    # Bars 23-24: G 0 is the straight grid again, and the song ends rather
    # than looping -- E gives the lead's last note a decay it does not have of
    # its own, and K stops the wave bass a bar and three quarters later, so
    # the chord rings through the empty bar and goes out (sections 2 and 12).
    s.section(1, groove=1,
              PU1=lay(16, [(0, "A-4@lead+G0"), (8, "E-4@lead+E13,7")]),
              PU2=lay(16, [(0, "C-4@pluck+G0"), (8, "A-3@pluck")]),
              WAV=lay(16, [(0, "A-2@bass+G0"), (8, "A-2@bass+K132")]),
              NOI=lay(16, [(0, "C#3@crash+G0"), (8, "C-2@kick")]))
    s.section(1, groove=0, PU1=".", PU2=".", WAV=".", NOI=".")
    return s.check()


# ---------------------------------------------------------------------------
# 2. meter-study -- 3/4, a 7/8 bar, a 5/4 stretch, one tempo throughout
# ---------------------------------------------------------------------------

def meter_study():
    b = Bank("Meter Study")
    b.wav("triangle", [frame_triangle()])
    b.wav("reed", [frame_harmonics(1.0, 0.0, 0.45, 0.0, 0.2)])
    b.pulse("flute", duty=1, vol=12, rate=0, vib=(VIB_TRI, VIB_DOWN, 8, 3, 12))
    b.pulse("chord", duty=0, vol=12, rate=3)
    b.wave("bass", wave="triangle", level=3)
    b.wave("reed-bass", wave="reed", level=2)
    b.noise("kick", vol=15, rate=3, shift=6, div=3, sweep=-1)
    b.noise("snare", vol=12, rate=3, shift=4, div=4)
    b.noise("hat", vol=7, rate=2, lfsr7=True, shift=1, div=4)

    # A waltz in G: twelve steps to a 3/4 bar, so a step is a sixteenth and a
    # beat is four of them, exactly as in a 4/4 bar at sixteen steps.
    waltz = [
        lay(12, [(0, "D-4"), (4, "G-4"), (8, "B-4")], "flute"),
        lay(12, [(0, "C-5"), (4, "B-4"), (6, "A-4"), (8, "G-4")], "flute"),
        lay(12, [(0, "A-4"), (4, "D-5"), (8, "C-5")], "flute"),
        lay(12, [(0, "B-4"), (6, "A-4"), (8, "G-4")], "flute"),
        lay(12, [(0, "E-5"), (4, "D-5"), (8, "B-4")], "flute"),
        lay(12, [(0, "C-5"), (4, "A-4"), (8, "F#4")], "flute")]
    oom = [
        lay(12, [(0, "G-2"), (4, "D-3"), (8, "B-2")], "bass"),
        lay(12, [(0, "C-3"), (4, "G-3"), (8, "E-3")], "bass"),
        lay(12, [(0, "D-3"), (4, "A-3"), (8, "F#3")], "bass"),
        lay(12, [(0, "G-2"), (4, "D-3"), (8, "G-3")], "bass"),
        lay(12, [(0, "E-3"), (4, "B-3"), (8, "G-3")], "bass"),
        lay(12, [(0, "A-2"), (4, "E-3"), (8, "C#3")], "bass")]
    pah = [
        lay(12, [(4, "B-3"), (8, "D-4")], "chord"),
        lay(12, [(4, "E-4"), (8, "G-4")], "chord"),
        lay(12, [(4, "F#4"), (8, "A-4")], "chord"),
        lay(12, [(4, "D-4"), (8, "B-3")], "chord"),
        lay(12, [(4, "G-4"), (8, "B-4")], "chord"),
        lay(12, [(4, "E-4"), (8, "A-4")], "chord")]
    K, S, H, h, x = "C-2@kick", "D-2@snare", "F#2@hat", "F#2@hat:70", "."
    three = " ".join([K, x, x, x, H, x, x, x, h, x, x, x])
    three_fill = " ".join([K, x, x, x, H, x, S, x, h, x, S, x])

    s = Song("Meter Study", b, tempo=126, beats_per_bar=3, steps_per_bar=12,
             grooves={1: [7, 5]}, blurb="3/4, a 7/8 bar and a 5/4 stretch at one tempo")
    # Bars 1-2: the pulse of the bar, alone.
    s.section(2, PU1=[None, lay(12, [(8, "B-4")], "flute")], PU2=[None, pah[0]],
              WAV=[oom[0], oom[0]], NOI=[three, three])
    # Bars 3-7: the waltz.
    s.section(5, PU1=waltz[:4] + [waltz[4]], PU2=pah[:4] + [pah[4]],
              WAV=oom[:4] + [oom[4]], NOI=[three, three, three, three_fill, three])
    # Bar 8: fourteen steps -- 84 ticks, seven eighths. The bar is half a beat
    # longer than its neighbours and every channel takes the same ruler.
    s.section(1, steps=14,
              PU1=lay(14, [(0, "B-4"), (4, "A-4"), (8, "G-4"), (12, "F#4")], "flute"),
              PU2=lay(14, [(4, "D-4"), (10, "C-4")], "chord"),
              WAV=lay(14, [(0, "D-3"), (4, "A-2"), (8, "D-3"), (12, "A-2")], "bass"),
              NOI=" ".join([K, x, x, x, H, x, x, x, h, x, x, x, S, x]))
    # Bars 9-14: back in three, the reed bass under it.
    s.section(6, PU1=waltz, PU2=pah,
              WAV=[t.replace("@bass", "@reed-bass") for t in oom],
              NOI=[three, three, three_fill, three, three, three_fill])
    # Bars 15-16: twenty steps -- 120 ticks, five beats. Nothing about the
    # tempo changes; the bar is simply longer, and the tune stretches into it.
    five_a = lay(20, [(0, "G-4"), (4, "B-4"), (8, "D-5"), (12, "C-5"), (16, "B-4")], "flute")
    five_b = lay(20, [(0, "A-4"), (4, "C-5"), (8, "E-5"), (12, "D-5"), (16, "C-5")], "flute")
    s.section(1, steps=20, PU1=five_a, PU2=lay(20, [(8, "G-4"), (16, "D-4")], "chord"),
              WAV=lay(20, [(0, "G-2"), (4, "D-3"), (8, "G-2"), (12, "D-3"), (16, "B-2")], "bass"),
              NOI=" ".join([K, x, x, x, H, x, x, x, S, x, x, x, H, x, x, x, h, x, S, x]))
    s.section(1, steps=20, PU1=five_b, PU2=lay(20, [(8, "A-4"), (16, "E-4")], "chord"),
              WAV=lay(20, [(0, "A-2"), (4, "E-3"), (8, "A-2"), (12, "E-3"), (16, "C#3")], "bass"),
              NOI=" ".join([K, x, x, x, H, x, x, x, S, x, x, x, H, x, K, x, h, x, S, x]))
    # Bars 17-20: three again, and out.
    s.section(3, PU1=waltz[3:6], PU2=pah[3:6], WAV=oom[3:6], NOI=[three, three, three_fill])
    # The last bar ends the piece: E fades the flute, K stops the bass at the
    # end of the bar (the wave channel has a level, not an envelope).
    s.section(1, PU1=lay(12, [(0, "G-4@flute+E12,7")]), PU2=lay(12, [(0, "D-4")], "chord"),
              WAV=lay(12, [(0, "G-2@bass+K66")]), NOI=lay(12, [(0, "C-2")], "kick"))
    return s.check()


# ---------------------------------------------------------------------------
# 3. route-theme -- a bright walking theme in the early-handheld manner
# ---------------------------------------------------------------------------

def route_theme():
    b = Bank("Route Theme")
    b.wav("triangle", [frame_triangle()])
    b.wav("round", [frame_harmonics(1.0, 0.3, 0.12)])
    b.pulse("lead", duty=2, vol=13, rate=0, vib=(VIB_TRI, VIB_DOWN, 10, 2, 10))
    b.pulse("arp", duty=0, vol=9, rate=0, rate_cmd=0)
    b.wave("bass", wave="triangle", level=3)
    b.wave("horn", wave="round", level=3)
    b.noise("kick", vol=15, rate=2, shift=6, div=3, sweep=-1)
    b.noise("snare", vol=13, rate=3, shift=4, div=4)
    b.noise("hat", vol=8, rate=1, lfsr7=True, shift=1, div=4)
    b.noise("open-hat", vol=8, rate=4, lfsr7=True, shift=1, div=4)

    lead = [lay(16, m, "lead") for m in [
        [(0, "E-4"), (4, "G-4"), (6, "E-4"), (8, "C-5"), (12, "B-4")],
        [(0, "D-5"), (4, "B-4"), (8, "G-4"), (12, "A-4")],
        [(0, "C-5"), (4, "A-4"), (6, "C-5"), (8, "E-5"), (12, "D-5")],
        [(0, "C-5"), (4, "A-4"), (8, "F-4"), (12, "G-4")],
        [(0, "E-4"), (4, "G-4"), (6, "C-5"), (8, "B-4"), (12, "G-4")],
        [(0, "A-4"), (4, "B-4"), (8, "D-5"), (12, "B-4")],
        [(0, "A-4"), (4, "F-4"), (8, "G-4"), (12, "A-4")],
        [(0, "G-4"), (8, "E-4")]]]
    bridge = [lay(16, m, "lead") for m in [
        [(0, "A-4"), (3, "B-4"), (4, "C-5"), (8, "B-4"), (12, "A-4")],
        [(0, "F-4"), (4, "A-4"), (8, "C-5"), (11, "D-5"), (12, "C-5")],
        [(0, "G-4"), (4, "B-4"), (8, "D-5"), (12, "E-5")],
        [(0, "D-5"), (4, "C-5"), (8, "B-4"), (12, "G-4")],
        [(0, "A-4"), (3, "C-5"), (4, "E-5"), (8, "D-5"), (12, "C-5")],
        [(0, "F-5"), (4, "E-5"), (8, "D-5"), (12, "C-5")],
        [(0, "B-4"), (4, "D-5"), (8, "G-5"), (12, "F-5")],
        [(0, "E-5"), (8, "C-5")]]]
    # The chords are an arpeggio each: C is a note plus C 4 7, A minor plus
    # C 3 7, and the cycle steps once a tick (section 7's command rate 0).
    arp = {
        "C": lay(16, [(0, "C-3+C4,7"), (8, "C-3+C4,7")], "arp"),
        "G": lay(16, [(0, "G-2+C4,7"), (8, "G-2+C4,7")], "arp"),
        "Am": lay(16, [(0, "A-2+C3,7"), (8, "A-2+C3,7")], "arp"),
        "F": lay(16, [(0, "F-2+C4,7"), (8, "F-2+C4,7")], "arp"),
        "Dm": lay(16, [(0, "D-3+C3,7"), (8, "D-3+C3,7")], "arp"),
        "Em": lay(16, [(0, "E-3+C3,7"), (8, "E-3+C3,7")], "arp")}
    walk = {
        "C": lay(16, [(0, "C-2"), (4, "G-2"), (8, "C-3"), (12, "G-2")], "bass"),
        "G": lay(16, [(0, "G-1"), (4, "D-2"), (8, "G-2"), (12, "B-2")], "bass"),
        "Am": lay(16, [(0, "A-1"), (4, "E-2"), (8, "A-2"), (12, "E-2")], "bass"),
        "F": lay(16, [(0, "F-1"), (4, "C-2"), (8, "F-2"), (12, "A-2")], "bass"),
        "Dm": lay(16, [(0, "D-2"), (4, "A-2"), (8, "D-3"), (12, "A-2")], "bass"),
        "Em": lay(16, [(0, "E-2"), (4, "B-2"), (8, "E-3"), (12, "B-2")], "bass")}

    K, S, H, h, O, x = "C-2@kick", "D-2@snare", "F#2@hat", "F#2@hat:70", "A#2@open-hat", "."
    beat = " ".join([K, x, h, x, S, x, h, x, K, x, h, K, S, x, h, O])
    beat2 = " ".join([K, x, h, x, S, x, h, K, x, K, h, x, S, S, h, O])
    quiet = " ".join([x, x, h, x, H, x, h, x, x, x, h, x, H, x, h, x])

    s = Song("Route Theme", b, tempo=132, blurb="a bright route theme with an arpeggio chord track")
    progression = ["C", "G", "Am", "F", "C", "G", "F", "C"]
    bridge_chords = ["Am", "F", "C", "G", "Am", "F", "Dm", "Em"]
    # Bars 1-4: the walk and the chords come up first, the horn answering.
    s.section(4, PU1=[None, None, lay(16, [(12, "G-4")], "lead"), lay(16, [(12, "E-4"), (14, "G-4")], "lead")],
              PU2=[arp[c] for c in ["C", "C", "G", "G"]],
              WAV=[walk[c] for c in ["C", "C", "G", "G"]],
              NOI=[quiet, quiet, beat, beat2])
    # Bars 5-12: the theme.
    s.section(8, PU1=lead, PU2=[arp[c] for c in progression], WAV=[walk[c] for c in progression],
              NOI=[beat, beat, beat, beat2, beat, beat, beat, beat2])
    # Bars 13-20: the second strain, higher, the horn under the last two bars.
    s.section(8, PU1=bridge, PU2=[arp[c] for c in bridge_chords],
              WAV=[walk[c] for c in bridge_chords[:6]]
                  + [lay(16, [(0, "D-3"), (8, "F-3"), (12, "A-3")], "horn"),
                     lay(16, [(0, "E-3"), (8, "G-3"), (12, "B-3")], "horn")],
              NOI=[beat, beat, beat, beat2, beat, beat, beat2, beat2])
    # Bars 21-24: the theme's last line again, and a clean loop back to bar 1.
    s.section(4, PU1=lead[4:8], PU2=[arp[c] for c in progression[4:8]],
              WAV=[walk[c] for c in progression[4:8]], NOI=[beat, beat, beat, beat2])
    return s.check()


# ---------------------------------------------------------------------------
# 4. puffball-bounce -- duty changes, vibrato, a staccato bass, a bridge
# ---------------------------------------------------------------------------

def puffball_bounce():
    b = Bank("Puffball Bounce")
    b.wav("quarter", [frame_pulse(8)])
    b.wav("soft", [frame_harmonics(1.0, 0.25)])
    b.pulse("melody", duty=2, vol=13, rate=0, vib=(VIB_TRI, VIB_DOWN, 11, 3, 6))
    b.pulse("harmony", duty=1, vol=12, rate=2)
    b.pulse("whoop", duty=2, vol=14, rate=3, sweep_rate=3, sweep_shift=3)
    b.wave("bass", wave="quarter", level=3)
    b.wave("pad", wave="soft", level=2)
    b.noise("kick", vol=15, rate=2, shift=6, div=2, sweep=-1)
    b.noise("snare", vol=13, rate=3, shift=4, div=4)
    b.noise("hat", vol=8, rate=1, lfsr7=True, shift=1, div=4)

    # F major, and it hops: the melody is written on the offbeats as often as
    # on the beats. W changes the duty per section, V leans on the long notes.
    tune = [lay(16, m, "melody") for m in [
        [(0, "F-4"), (2, "A-4"), (4, "C-5"), (6, "A-4"), (8, "F-4"), (12, "G-4")],
        [(0, "A-4"), (2, "G-4"), (4, "F-4"), (8, "D-4"), (12, "F-4")],
        [(0, "B-4"), (2, "A-4"), (4, "G-4"), (6, "A-4"), (8, "B-4"), (12, "D-5")],
        [(0, "C-5+V0,4"), (8, "A-4")],
        [(0, "F-4"), (2, "A-4"), (4, "C-5"), (6, "F-5"), (8, "E-5"), (12, "C-5")],
        [(0, "D-5"), (2, "C-5"), (4, "A-4"), (8, "G-4"), (12, "A-4")],
        [(0, "B-4"), (4, "D-5"), (6, "C-5"), (8, "A-4"), (12, "G-4")],
        [(0, "F-4+V0,5"), (10, "C-4")]]]
    bridge = [lay(16, m, "melody") for m in [
        [(0, "D-5"), (3, "C-5"), (4, "B-4"), (8, "G-4"), (12, "B-4")],
        [(0, "C-5"), (3, "B-4"), (4, "A-4"), (8, "F-4"), (12, "A-4")],
        [(0, "B-4"), (4, "D-5"), (8, "G-5"), (12, "F-5")],
        [(0, "E-5+V0,6"), (8, "D-5")],
        [(0, "G-4"), (3, "B-4"), (4, "D-5"), (8, "C-5"), (12, "B-4")],
        [(0, "A-4"), (4, "C-5"), (8, "F-5"), (12, "E-5")]]]
    harm = [lay(16, m, "harmony") for m in [
        [(2, "C-4"), (6, "F-4"), (10, "A-4"), (14, "F-4")],
        [(2, "D-4"), (6, "F-4"), (10, "A-4"), (14, "D-4")],
        [(2, "B-3"), (6, "D-4"), (10, "F-4"), (14, "D-4")],
        [(2, "C-4"), (6, "E-4"), (10, "G-4"), (14, "E-4")]]]
    # The bass is killed three ticks after each note, so it is all attack:
    # K is per-note and leaves nothing behind (section 12).
    def hop(root, fifth):
        return lay(16, [(0, root + "+K3"), (4, fifth + "+K3"), (8, root + "+K3"),
                        (10, fifth + "+K3"), (14, root + "+K3")], "bass")
    low = {"F": hop("F-2", "C-3"), "Dm": hop("D-2", "A-2"), "Bb": hop("A#2", "F-3"),
           "C": hop("C-2", "G-2"), "G": hop("G-2", "D-3"), "Am": hop("A-2", "E-3")}

    K, S, H, h, x = "C-2@kick", "D-2@snare", "F#2@hat", "F#2@hat:64", "."
    beat = " ".join([K, x, h, x, S, x, h, x, K, K, h, x, S, x, h, h])
    beat2 = " ".join([K, x, h, x, S, x, h, K, x, K, h, h, S, S, h, S])
    light = " ".join([K, x, h, x, S, x, h, x, x, x, h, x, S, x, h, x])

    s = Song("Puffball Bounce", b, tempo=150, blurb="duty changes, vibrato and a staccato bass")
    # Bars 1-2: the bass hops in on its own, W picks the fat 50 % duty for the
    # melody's entry.
    s.section(2, PU1=[None, lay(16, [(12, "C-4@melody+W2")])], PU2=[None, harm[0]],
              WAV=[low["F"], low["F"]], NOI=[light, beat])
    # Bars 3-10: the tune, twenty-five percent duty from bar 7 (W 1).
    s.section(8, PU1=tune[:4] + [tune[4].replace("F-4@melody", "F-4@melody+W1", 1)] + tune[5:],
              PU2=harm * 2, WAV=[low[c] for c in ["F", "Dm", "Bb", "C", "F", "Dm", "C", "F"]],
              NOI=[beat, beat, beat, beat2, beat, beat, beat, beat2])
    # Bars 11-16: the bridge -- thin duty (W 0), the pad underneath, and a
    # sweep on PU1's last note (S is PU1 only, section 2).
    s.section(6, PU1=[bridge[0].replace("D-5@melody", "D-5@melody+W0", 1)] + bridge[1:],
              PU2=[harm[2], harm[3], harm[2], harm[3], harm[0], harm[1]],
              WAV=[lay(16, [(0, "G-2"), (8, "D-3")], "pad"), lay(16, [(0, "F-2"), (8, "C-3")], "pad"),
                   lay(16, [(0, "G-2"), (8, "B-2")], "pad"), lay(16, [(0, "C-3"), (8, "G-2")], "pad"),
                   low["Am"], low["C"]],
              NOI=[light, light, beat, beat, beat, beat2])
    # Bars 17-24: back to the tune at the fat duty, and out on a whoop.
    s.section(8, PU1=[tune[0].replace("F-4@melody", "F-4@melody+W2", 1)] + tune[1:7]
                     + [lay(16, [(0, "F-4@melody"), (8, "F-3@whoop")])],
              PU2=harm * 2, WAV=[low[c] for c in ["F", "Dm", "Bb", "C", "F", "G", "C", "F"]],
              NOI=[beat, beat, beat, beat2, beat, beat, beat2, beat2])
    return s.check()


# ---------------------------------------------------------------------------
# 5. neon-grid -- the drums are the wave channel, and tables shape them
# ---------------------------------------------------------------------------

def neon_grid():
    b = Bank("Neon Grid")
    b.wav("body", [frame_harmonics(1.0, 0.2)])
    b.wav("grit", [frame_grit(0x1234 + 977 * k) for k in range(6)])
    # The kick: a wave note whose table drops it in semitones (Drum pitch
    # speed, section 7) while E steps the wave channel's level down.
    # P's step comes from the measured table now and Drum moves the period
    # register (section 34, docs/LSDJ_PARITY.md 5): -38 is about fifteen period
    # units an update, the fall this kick had, and 0 is what stops a bend.
    b.table("kick-drop", ["+P-38+E3", "+P-38+E3", "+P0+E2", "+E1", "+E0"], end=END_STOP)
    # The snare: the same channel, running through the grit wave's frames with
    # F while the level falls -- a frame run is the wave channel's noise.
    b.table("snare-hit", ["+F1+E3", "+F3+E3", "+F5+E2", "+F6+E1", "+E0"], end=END_STOP)
    b.table("stab", ["v15", "v13", "v11", "v9", "v7", "v5", "v3", "v1"], end=END_STOP)
    b.pulse("lead", duty=2, vol=13, rate=0, vib=(VIB_SQUARE, VIB_DOWN, 12, 2, 14))
    b.pulse("stabs", duty=1, vol=14, rate=0, table="stab")
    b.pulse("sub", duty=3, vol=14, rate=0)
    b.wave("kick", wave="body", level=3, pitch=DRUM, table="kick-drop", overlap=RETRIG)
    b.wave("snare", wave="grit", level=3, table="snare-hit", overlap=RETRIG)
    b.noise("hat", vol=7, rate=1, lfsr7=True, shift=1, div=4)
    b.noise("open", vol=7, rate=4, lfsr7=True, shift=1, div=4)
    b.noise("rim", vol=10, rate=2, lfsr7=True, shift=3, div=2)

    # The wave channel is the drum machine: a kick note is a pitched note that
    # falls, a snare note is a frame run. Both start their table at note-on.
    KK, SN, x = "A-2@kick", "A-3@snare", "."
    four = " ".join([KK, x, x, x, SN, x, x, x, KK, x, x, KK, SN, x, x, x])
    four2 = " ".join([KK, x, x, KK, SN, x, x, x, KK, x, SN, x, SN, x, SN, SN])
    half = " ".join([KK, x, x, x, x, x, x, x, SN, x, x, x, x, x, KK, x])
    H, h, O, R, xx = "F#2@hat", "F#2@hat:64", "A#2@open", "D-2@rim", "."
    hats = " ".join([H, h, h, h, H, h, h, O, H, h, h, h, H, h, R, h])
    hats2 = " ".join([H, h, h, h, H, h, R, h, H, h, h, O, H, R, h, R])
    hats_quiet = " ".join([H, xx, h, xx, H, xx, h, xx, H, xx, h, xx, H, xx, O, xx])

    lead = [lay(16, m, "lead") for m in [
        [(0, "A-4"), (3, "C-5"), (6, "E-5"), (8, "D-5"), (12, "C-5")],
        [(0, "E-5"), (4, "C-5"), (8, "A-4"), (11, "B-4"), (12, "C-5")],
        [(0, "F-4"), (3, "A-4"), (6, "C-5"), (8, "B-4"), (12, "A-4")],
        [(0, "G-4"), (4, "B-4"), (8, "D-5"), (12, "E-5")],
        [(0, "A-4"), (3, "E-5"), (6, "D-5"), (8, "C-5"), (12, "B-4")],
        [(0, "C-5"), (4, "B-4"), (8, "G-4"), (12, "A-4")],
        [(0, "D-5"), (3, "C-5"), (6, "B-4"), (8, "A-4"), (12, "G-4")],
        [(0, "A-4"), (8, "E-4")]]]
    # PU2 is the low end: sixteenth stabs through the "stab" table.
    def drive(root):
        return lay(16, [(0, root), (2, root), (5, root), (8, root), (10, root), (13, root)], "stabs")
    stabs = {n: drive(n) for n in ["A-2", "F-2", "G-2", "E-2", "C-2", "D-2"]}
    sub = {n: lay(16, [(0, n), (8, n)], "sub") for n in ["A-2", "F-2", "G-2", "E-2", "C-2", "D-2"]}

    s = Song("Neon Grid", b, tempo=140, blurb="a wave-channel drum machine driven by tables")
    # Bars 1-4: the drum machine alone, then the low end.
    s.section(4, PU1=[None, None, None, lay(16, [(12, "E-4")], "lead")],
              PU2=[None, None, stabs["A-2"], stabs["A-2"]],
              WAV=[half, half, four, four2], NOI=[hats_quiet, hats_quiet, hats, hats2])
    # Bars 5-12: the tune over A minor - F - G - E minor.
    s.section(8, PU1=lead, PU2=[stabs[n] for n in ["A-2", "F-2", "G-2", "E-2", "A-2", "F-2", "D-2", "E-2"]],
              WAV=[four, four, four, four2, four, four, four, four2],
              NOI=[hats, hats, hats, hats2, hats, hats, hats, hats2])
    # Bars 13-16: half time -- the same kick and snare, half as often, the sub
    # holding under it.
    s.section(4, PU1=[lead[4], lead[5], lead[6], lead[7]],
              PU2=[sub[n] for n in ["A-2", "F-2", "G-2", "E-2"]],
              WAV=[half, half, half, four2], NOI=[hats_quiet, hats_quiet, hats_quiet, hats2])
    # Bars 17-24: everything, and the last bar leaves the grid running.
    s.section(8, PU1=lead, PU2=[stabs[n] for n in ["A-2", "F-2", "G-2", "E-2", "C-2", "D-2", "E-2", "A-2"]],
              WAV=[four, four2, four, four2, four, four2, four, four2],
              NOI=[hats, hats2, hats, hats2, hats, hats2, hats2, hats2])
    return s.check()


# ---------------------------------------------------------------------------
# 6. wave-study -- frames, wave slots and tempo-synced bends
# ---------------------------------------------------------------------------

def wave_study():
    b = Bank("Wave Study")
    b.wav("morph", morph(frame_triangle(), frame_saw(), 8))
    b.wav("hollow", morph(frame_pulse(4), frame_pulse(16), 6))
    b.wav("glass", [frame_harmonics(1.0, 0.0, 0.5, 0.0, 0.3, 0.0, 0.2)])
    b.wav("sine", [frame_sine()])
    # The lead's pitch runs on the tick, so P and V follow the tempo rather
    # than the wall clock (section 7); the command rate slows them by three.
    b.wave("wave-lead", wave="morph", level=3, pitch=TICK, rate_cmd=2,
           vib=(VIB_TRI, VIB_DOWN, 6, 4, 4))
    b.wave("wave-pad", wave="glass", level=2, advance=6, loop=FRAME_PINGPONG)
    b.wave("sine-sub", wave="sine", level=3)
    b.pulse("keys", duty=1, vol=12, rate=3)
    b.pulse("bass", duty=2, vol=14, rate=0)
    b.noise("kick", vol=15, rate=2, shift=6, div=3, sweep=-1)
    b.noise("snare", vol=12, rate=4, shift=4, div=4)
    b.noise("hat", vol=6, rate=1, lfsr7=True, shift=1, div=4)

    # D minor, half time: the kick on one, the snare on three, and the wave
    # channel doing all the moving.
    K, S, H, h, x = "C-2@kick", "D-2@snare", "F#2@hat", "F#2@hat:56", "."
    slow = " ".join([K, x, x, x, x, x, h, x, S, x, x, x, x, x, h, x])
    slow2 = " ".join([K, x, x, x, x, x, h, K, S, x, x, x, h, x, S, h])
    slow_open = " ".join([K, x, x, x, h, x, h, x, S, x, x, K, x, x, h, x])
    drop = " ".join([K, x, x, x, x, x, x, x, x, x, x, x, x, x, x, x])

    # F walks the frame of the morph wave under a held note; W changes the
    # wave slot outright. Both persist until the next instrument (section 12).
    sweep_up = lay(16, [(0, "D-3@wave-lead+F1"), (4, "~D-3+F3"), (8, "~D-3+F5"), (12, "~D-3+F8")])
    sweep_dn = lay(16, [(0, "A-2@wave-lead+F8"), (4, "~A-2+F6"), (8, "~A-2+F4"), (12, "~A-2+F1")])
    # A wobble: P is a bend per tick here, so the wobble is tempo-synced, and
    # every plain note-on puts the offset back to zero (section 7).
    # The wobble in 1/256 semitones an update: 27 and 30 are the table's
    # nearest steps to the eight and twelve period units this had (section 34).
    wobble = lay(16, [(0, "F-3@wave-lead+F4"), (4, "~F-3+P27"), (8, "~F-3+P-27"), (12, "~F-3+P0")])
    wobble2 = lay(16, [(0, "G-3@wave-lead+F6"), (4, "~G-3+P30"), (8, "~G-3+P-30"), (12, "~G-3+P0")])
    switch = lay(16, [(0, "D-3@wave-lead+W2"), (6, "~F-3"), (8, "~A-3+F3"), (12, "~D-4+F6")])
    back = lay(16, [(0, "A-2@wave-lead+W1"), (4, "~C-3+F2"), (8, "~D-3+F5"), (12, "~F-3+F7")])
    lift = lay(16, [(0, "D-3@wave-lead+V0,7"), (8, "~A-3")])
    pad = [lay(16, [(0, n)], "wave-pad") for n in ["D-3", "F-3", "A#2", "C-3"]]

    keys = [lay(16, m, "keys") for m in [
        [(4, "D-4"), (12, "F-4")],
        [(4, "C-4"), (12, "E-4")],
        [(4, "A#3"), (12, "D-4")],
        [(4, "C-4"), (10, "G-4")]]]
    bass = {n: lay(16, [(0, n), (8, n), (14, n)], "bass")
            for n in ["D-2", "A#2", "C-3", "A-2", "F-2", "G-2"]}

    s = Song("Wave Study", b, tempo=100, grooves={1: [7, 5], 2: [8, 4]},
             blurb="frame sweeps, wave switches and tempo-synced bends, half time")
    # Bars 1-4: the wave channel alone, sweeping its frames, then the rest.
    s.section(4, groove=1,
              PU1=[None, None, keys[0], keys[1]],
              PU2=[None, bass["D-2"], bass["D-2"], bass["A#2"]],
              WAV=[sweep_up, sweep_dn, sweep_up, switch],
              NOI=[drop, slow, slow, slow2])
    # Bars 5-12: the tune, the wobble under it. The bass's last note carries a
    # K so it stops for the drop: a note-off would only fall back to the note
    # under it on the held stack, and K clears the stack (section 8).
    s.section(8, groove=1,
              PU1=keys * 2, PU2=[bass[n] for n in ["D-2", "A#2", "C-3", "A-2", "D-2", "A#2", "F-2"]]
                                + [lay(16, [(0, "A-2"), (8, "A-2"), (14, "A-2+K24")], "bass")],
              WAV=[wobble, sweep_up, wobble2, switch, wobble, sweep_dn, wobble2, back],
              NOI=[slow, slow, slow, slow2, slow, slow_open, slow, slow2])
    # Bars 13-16: the drop -- the wave channel and the kick, nothing else.
    s.section(4, groove=2,
              PU1=[None, None, None, None],
              PU2=[None, None, bass["D-2"], bass["A#2"]],
              WAV=[lift, sweep_dn, wobble, switch],
              NOI=[drop, drop, slow, slow2])
    # Bars 17-24: everything back, the pad holding under the last four.
    s.section(4, groove=1,
              PU1=keys, PU2=[bass[n] for n in ["D-2", "A#2", "C-3", "G-2"]],
              WAV=[wobble, sweep_up, wobble2, back],
              NOI=[slow, slow, slow_open, slow2])
    # The last bar lands on the sine sub, a whole note under the pad's frames,
    # and ends: E fades the bass, K stops the sub two bars later.
    s.section(4, groove=1,
              PU1=keys, PU2=[bass[n] for n in ["D-2", "A#2", "C-3"]]
                            + [lay(16, [(0, "D-2@bass+E14,7")])],
              WAV=pad[:3] + [lay(16, [(0, "D-2@sine-sub+K90")])],
              NOI=[slow, slow, slow, slow2])
    return s.check()


# ---------------------------------------------------------------------------
# the six
# ---------------------------------------------------------------------------

SONGS = [
    ("groove-study", groove_study),
    ("meter-study", meter_study),
    ("route-theme", route_theme),
    ("puffball-bounce", puffball_bounce),
    ("neon-grid", neon_grid),
    ("wave-study", wave_study),
]


def main():
    ap = argparse.ArgumentParser(description="generate the ChipBoy demo songs")
    ap.add_argument("--out", default=os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                                  "..", "..", "Demo", "songs"))
    ap.add_argument("--list", action="store_true", help="describe the songs without writing them")
    args = ap.parse_args()

    out = os.path.normpath(args.out)
    if not args.list:
        os.makedirs(out, exist_ok=True)
    for name, build in SONGS:
        song = build()
        line = song.summary()
        if args.list:
            print(line)
            print("    %s" % song.blurb)
            for ch in range(4):
                busy = song.busy_bars(ch)
                print("    %s plays in %d of %d bars: %s" % (CHANNEL_NAME[ch], len(busy), song.bars(),
                                                             compress(busy)))
            continue
        size = song.write(os.path.join(out, name + ".cbsong"))
        print("%s.cbsong  %6d bytes  %s" % (name, size, line))
    return 0


def compress(bars):
    """[1,2,3,7,8] as "1-3 7-8": the arrangement at a glance."""
    if not bars:
        return "never"
    runs, start, last = [], bars[0], bars[0]
    for bar in bars[1:]:
        if bar == last + 1:
            last = bar
            continue
        runs.append((start, last))
        start = last = bar
    runs.append((start, last))
    return " ".join("%d" % a if a == b else "%d-%d" % (a, b) for a, b in runs)


if __name__ == "__main__":
    sys.exit(main())
