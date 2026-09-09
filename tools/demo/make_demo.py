#!/usr/bin/env python3
"""ChipBoy demo project generator.

Writes four files under Demo/ (or --out):

  chipboy_demo.mid    a Standard MIDI File, format 1, 960 ticks per quarter,
                      120 BPM, 4/4, 16 bars. One track per hardware channel on
                      MIDI channels 1-4: PU1 lead, PU2 bass, WAV wave bass,
                      NOI drums.
  ChipBoy Demo.rpp    a Reaper project: one track holding the ChipBoy VST3, the
                      same notes as one MIDI item, and automation envelopes for
                      the command slots and the few channel fields the demo
                      shows off.
  ChipBoy Demo (song tempo).rpp
                      the same track with Tempo source = Song and Song tempo =
                      150, so the tracker's ticks run faster than the host's
                      120 BPM, and a T command in PU1's second slot that drops
                      the song's tempo to 100 for bars 9-12.
  PARAMETERS.md       the plugin's host-visible parameter table in host order,
                      with the normalised values Reaper stores and the VST3
                      parameter ids JUCE derives.

Standard library only. Deterministic: two runs give byte-identical files.

    python3 tools/demo/make_demo.py [--out Demo] [--paramdump PATH]

  --paramdump PATH    cross-check the embedded parameter table against the
                      chipboy_paramdump tool: PATH is the executable, or a
                      file holding its output.

The parameter order is the order Source/plugin/shared/Parameters.cpp adds the
parameters: addGlobalParameters, then addChannelParameters for channels 1-4.
Every channel has the same fifteen (docs/COMMANDS_AND_TEMPO.md section 3) and
only Level differs between the kinds. JUCE hands the list to VST3 hosts in that
order; the wrapper's own Bypass parameter and its MIDI-CC emulation parameters
come after it.
"""

import argparse
import base64
import os
import struct
import subprocess
import sys
import textwrap
import uuid

# ---------------------------------------------------------------------------
# timing
# ---------------------------------------------------------------------------

PPQ = 960                      # ticks per quarter note, in the .mid and the .rpp
BPM = 120
BEATS_PER_BAR = 4
BARS = 16
TICKS_PER_BAR = PPQ * BEATS_PER_BAR
TOTAL_TICKS = TICKS_PER_BAR * BARS
SECONDS_PER_BEAT = 60.0 / BPM
SONG_SECONDS = BARS * BEATS_PER_BAR * SECONDS_PER_BEAT   # 32 s

# The second project's tempo: the song owns it, so the tracker's ticks are
# 150 x 24 / 60 = 60 Hz against the host's 48 Hz, and a T command in a slot
# drops it to 100 for bars 9-12 (docs/COMMANDS_AND_TEMPO.md section 4).
SONG_TEMPO = 150
SONG_TEMPO_DROP = 100

# The plugin's VST3 identity: JucePlugin_ManufacturerCode 'Chpb' and
# JucePlugin_PluginCode 'Chby' (CMakeLists.txt), which JUCE turns into the
# component class id ABCDEF01 9182FAEB <manufacturer> <plugin> with
# JUCE_VST3_CAN_REPLACE_VST2=0. The built bundle's moduleinfo.json shows the
# same CID.
MANUFACTURER_CODE = 0x43687062   # 'Chpb'
PLUGIN_CODE = 0x43686279         # 'Chby'
VST3_CID = "ABCDEF01" + "9182FAEB" + "%08X%08X" % (MANUFACTURER_CODE, PLUGIN_CODE)

# Reaper writes a plugin's state as base64 lines of this length and joins
# them again when it reads the project.
RPP_BASE64_COLS = 128

# A fixed project timestamp, so the output never changes between runs.
PROJECT_TIMESTAMP = 1788825600   # 2026-09-07 00:00:00 UTC

# Deterministic GUIDs: uuid5 of a fixed namespace and a name.
GUID_NAMESPACE = uuid.UUID("8f5a3c4e-6b2d-4f1a-9e0c-7d3b2a1c0f9e")


def guid(name):
    return "{" + str(uuid.uuid5(GUID_NAMESPACE, "chipboy-demo/" + name)).upper() + "}"


def bar(n, beat=0.0):
    """Absolute beat of bar n (1-based) plus a beat offset (0-based)."""
    return (n - 1) * BEATS_PER_BAR + beat


def seconds(beats):
    return beats * SECONDS_PER_BEAT


def ticks(beats):
    return int(round(beats * PPQ))


# ---------------------------------------------------------------------------
# the parameter table (Source/plugin/shared/Parameters.cpp, in add order)
# ---------------------------------------------------------------------------

# The letters a command slot can hold, in the order Parameters.cpp builds the
# choice: "none", then the enum's order without H (tables only). A lane stores
# the index. What x and y mean is per letter -- docs/COMMANDS_AND_TEMPO.md
# section 2.
CMD_CHOICES = ["none", "A", "C", "D", "E", "F", "G", "K", "L", "M",
               "O", "P", "R", "S", "T", "V", "W", "Z"]
CMD = {letter: index for index, letter in enumerate(CMD_CHOICES)}


def env_y(rate, rising=False):
    """The E command's y: the speed in 0-7, plus 8 for a rising envelope."""
    return (rate & 7) | (8 if rising else 0)


class Param:
    def __init__(self, pid, name, kind, lo, hi, default, choices=None, note=""):
        self.pid, self.name, self.kind = pid, name, kind
        self.lo, self.hi, self.default = lo, hi, default
        self.choices, self.note = choices, note

    def normalised(self, value):
        """What Reaper stores for a VST3 parameter: (v - min) / (max - min)."""
        return (float(value) - self.lo) / (self.hi - self.lo)

    def range_text(self):
        if self.kind == "choice":
            return "%d-%d (%s)" % (self.lo, self.hi, ", ".join(self.choices))
        if self.kind == "bool":
            return "0-1 (off, on)"
        if self.kind == "float":
            return "%g-%g %s, continuous" % (self.lo, self.hi, self.note)
        return "%d-%d%s" % (self.lo, self.hi, (" (" + self.note + ")") if self.note else "")


def parameter_table():
    table = []

    def choice(pid, name, choices, default):
        table.append(Param(pid, name, "choice", 0, len(choices) - 1, default, choices))

    def integer(pid, name, lo, hi, default, note=""):
        table.append(Param(pid, name, "int", lo, hi, default, None, note))

    def boolean(pid, name, default):
        table.append(Param(pid, name, "bool", 0, 1, 1 if default else 0, ["off", "on"]))

    def real(pid, name, lo, hi, default, unit):
        table.append(Param(pid, name, "float", lo, hi, default, None, unit))

    # addGlobalParameters
    choice("model", "Model", ["DMG", "CGB", "RAW"], 0)
    integer("master_l", "Master Volume L", 0, 7, 7, "NR50; 0 is 1/8, not mute")
    integer("master_r", "Master Volume R", 0, 7, 7, "NR50; 0 is 1/8, not mute")
    real("trim", "Output Trim", -40.0, 6.0, -6.0, "dB")
    boolean("noise", "Headphone Noise", True)
    boolean("lcd", "LCD Whine", True)
    choice("bass_mod", "CGB Bass Mod", ["stock", "x10", "x47"], 0)
    boolean("vol_edges", "Volume Writes At Edges", False)
    boolean("declick", "De-click", False)
    real("declick_ms", "De-click ms", 0.5, 5.0, 2.0, "ms")
    boolean("soften", "Soften Master Pops", False)
    # Tempo (docs/COMMANDS_AND_TEMPO.md section 4): ticks are always 24 to the
    # beat, and the only choice left is where the beat comes from.
    choice("tempo_source", "Tempo Source", ["Host", "Song"], 0)
    integer("song_tempo", "Song Tempo", 40, 255, 120, "BPM; the Song source's base tempo")
    boolean("notes_on_tick", "Quantize Notes To Ticks", False)
    boolean("link", "Link Mode", False)
    boolean("hex", "Hex Display", True)

    # addChannelParameters, channels 1-4 with withSource = true. A channel is a
    # tracker row (section 3): an instrument, a table, the four performance
    # fields, two command slots of three parameters each, and the three
    # switches. Only Level differs between the kinds.
    kinds = ["pulse1", "pulse2", "wave", "noise"]
    names = ["PU1 ", "PU2 ", "WAV ", "NOI "]
    source_default = {"pulse1": 0, "pulse2": 2, "wave": 3, "noise": 4}
    instrument_default = {"pulse1": 1, "pulse2": 3, "wave": 7, "noise": 11}
    for ch, kind in enumerate(kinds):
        px, nm = "ch%d_" % (ch + 1), names[ch]
        wave = kind == "wave"
        sources = ["Omni"] + ["MIDI %d" % i for i in range(1, 17)] + ["Off"]
        choice(px + "source", nm + "Source", sources, source_default[kind])
        integer(px + "instrument", nm + "Instrument", 0, 128, instrument_default[kind], "0 = none")
        integer(px + "table", nm + "Table", 0, 64, 0, "0 = the instrument's")
        if wave:
            integer(px + "level", nm + "Level", 0, 4, 4, "0 mute, 1 25%, 2 50%, 3 100%, 4 = the instrument's")
        else:
            integer(px + "level", nm + "Level", 0, 16, 16, "16 = the instrument's")
        choice(px + "pan", nm + "Pan", ["off", "L", "R", "both", "inst"], 4)
        integer(px + "transpose", nm + "Transpose", -60, 60, 0, "semitones")
        for slot in (1, 2):
            choice("%scmd%d_type" % (px, slot), "%sCMD%d" % (nm, slot), CMD_CHOICES, 0)
            integer("%scmd%d_x" % (px, slot), "%sCMD%d x" % (nm, slot), 0, 255, 0, "the letter's x")
            integer("%scmd%d_y" % (px, slot), "%sCMD%d y" % (nm, slot), 0, 255, 0, "the letter's y")
        boolean(px + "live_follow", nm + "Live Follow", False)
        choice(px + "velocity", nm + "Velocity", ["start volume", "instrument bank", "ignored"], 0)
        boolean(px + "keyswitch", nm + "Keyswitches", False)

    assert len(table) == 76, len(table)
    return table


def vst3_param_id(pid):
    """JUCE's VST3 parameter id for a string parameter id: String::hashCode()
    (h = 31 * h + c, 32-bit) with the top bit cleared for Studio One."""
    h = 0
    for c in pid:
        h = (31 * h + ord(c)) & 0xFFFFFFFF
    return h & 0x7FFFFFFF


def reaper_vst3_number(cid_hex):
    """The number Reaper writes before {CID} on a VST3 line: the 32-bit FNV-1a
    hash of the class id in its COM (Windows in-memory) byte order, with the
    top bit cleared. Checked against ids in projects saved by Reaper."""
    raw = bytes.fromhex(cid_hex)
    com = raw[0:4][::-1] + raw[4:6][::-1] + raw[6:8][::-1] + raw[8:16]
    h = 0x811C9DC5
    for c in com:
        h ^= c
        h = (h * 0x01000193) & 0xFFFFFFFF
    return h & 0x7FFFFFFF


# ---------------------------------------------------------------------------
# the tune
# ---------------------------------------------------------------------------

# Everything the demo plays has to fit in a tracker cell, so the whole tune
# sits on the step grid (docs/COMMANDS_AND_TEMPO.md section 9.5): sixteen
# steps to a bar, a step being a sixteenth = 0.25 beat. Two rules follow, and
# the generator asserts both:
#
#   * every note starts on a step and ends one step before the next note on
#     its channel, so its note-off has a step of its own to be recorded in
#     (a note-off sharing a step with a note-on is not written as a cell --
#     the next note ends it -- and the recorded song would not reproduce the
#     register writes the release made);
#   * every automation point sits on a step where its channel has no note
#     starting, because a slot fires at the tick and a note-on that shares
#     that tick runs first: the recorded cell would carry the new command on
#     the note, and playing it back would apply it a note early.
#
# No mod wheel and no pitch wheel: a tracker holds commands, not wheel moves,
# so the vibrato ride is a V slot and the bends are L and P slots.

# Events are tuples (tick, priority, channel, kind, a, b). The priority orders
# events that share a tick: keyswitches select before notes start, note-offs
# precede note-ons.
PRIO_KEYSWITCH, PRIO_OFF, PRIO_ON = 0, 1, 2

PU1, PU2, WAV, NOI = 0, 1, 2, 3

STEP = 0.25                   # a sixteenth: one tracker step at 16 steps per bar
STEPS_PER_BAR = 16

# Factory bank slots (Source/core/Bank/Bank.cpp)
SLOT_SQUARE_LEAD, SLOT_PLUCK, SLOT_BASS25 = 1, 2, 3
SLOT_TRIANGLE_BASS, SLOT_SAW, SLOT_ORGAN_FRAMES, SLOT_TRI_TO_SAW = 7, 8, 9, 10
SLOT_KICK, SLOT_SNARE, SLOT_HAT_CLOSED, SLOT_HAT_OPEN, SLOT_CRASH = 11, 12, 13, 14, 15
SLOT_PULSE_KICK = 17          # Drum pitch speed, table 7 "Drum drop": P falls in semitones

# Factory waves, the slots a W command names on the wave channel: 1 Triangle,
# 2 Saw, 3 Sine, 4 Pulse 25, 5 Organ (four frames), 6 Tri to saw (six frames).
WAVE_TRIANGLE, WAVE_SAW, WAVE_ORGAN, WAVE_TRI_TO_SAW = 1, 2, 5, 6

# Keyswitch octaves (Source/core/Driver/Driver.cpp, keyswitchBase): notes
# base..base+11 select slots 1..12 and never sound. Pulse channels: base 24.
# Wave, kit and noise channels: base 12. Slot 17 is out of that octave's
# reach, so PU1's kick instrument arrives through the Instrument lane instead.
KEYSWITCH_BASE_PULSE, KEYSWITCH_BASE_OTHER = 24, 12

# Drum hits: the note numbers are only labels for the piano roll (the factory
# noise instruments are fixed-pitch); the velocity selects the instrument.
# With the channel's Velocity mode set to "instrument bank" the driver adds
# velocity / 8 to the base slot, so with Kick (11) as the base:
DRUM_KICK = (36, 4)           # slot 11
DRUM_SNARE = (38, 12)         # slot 12
DRUM_HAT_CLOSED = (42, 20)    # slot 13
DRUM_HAT_OPEN = (46, 28)      # slot 14
DRUM_CRASH = (49, 36)         # slot 15

# The L slide's duration on the lead (bars 13-14). The lead's pitch speed is
# Fast (Bank.cpp instrument 1), so the unit is the pitch clock's 1/358 s and
# the slide takes x + 1 of them: 31 updates is 87 ms (COMMANDS_AND_TEMPO 7).
LEAD_SLIDE = 30
# The P bend in bars 15-16. The argument is two's complement now and the step
# comes from the measured table (section 34, docs/LSDJ_PARITY.md 5): -14 is
# 32/256 of a semitone a pitch update, an eighth of a semitone, which is the
# downward lean out of the attack this lane had when P was -2 period units.
LEAD_BEND = 256 - 14

# Chords per bar: A minor, F, C, G, repeated.
CHORD_ROOTS = [57, 53, 48, 55]                     # A3 F3 C3 G3 (lead register - 12)


def chord_of_bar(n):
    return CHORD_ROOTS[(n - 1) % 4]


def on_step(beat):
    """Is this beat a step boundary (a sixteenth)?"""
    return abs(beat / STEP - round(beat / STEP)) < 1e-9


class Song:
    """The tune as note onsets per channel; lengths come from the grid.

    A note runs until one step before the next note on its channel, which is
    where its note-off goes -- the step the recorder writes OFF into. The last
    note of each channel is given a tail so that it, too, ends on a step.
    """

    def __init__(self):
        self.events = []
        self.onsets = [[] for _ in range(4)]      # (beat, pitch, velocity)
        self.tail = [1.0, 1.0, 1.0, 1.0]          # beats the last note of each channel lasts

    def note(self, ch, pitch, start, velocity=100):
        assert on_step(start), "note off the step grid: %g" % start
        self.onsets[ch].append((start, pitch, velocity))

    def keyswitch(self, ch, slot, at_tick):
        base = KEYSWITCH_BASE_PULSE if ch in (PU1, PU2) else KEYSWITCH_BASE_OTHER
        assert 1 <= slot <= 12
        note = base + slot - 1
        self.events.append((at_tick, PRIO_KEYSWITCH, ch, "on", note, 100))
        self.events.append((at_tick + 30, PRIO_KEYSWITCH, ch, "off", note, 0))

    def onset_beats(self, ch):
        return set(round(b / STEP) for b, _p, _v in self.onsets[ch])

    def finish(self):
        """Turn the onsets into note-on / note-off events on the step grid."""
        for ch in range(4):
            notes = sorted(self.onsets[ch])
            for i, (start, pitch, vel) in enumerate(notes):
                end = notes[i + 1][0] - STEP if i + 1 < len(notes) else start + self.tail[ch]
                assert end > start, "channel %d: notes at %g are less than two steps apart" % (ch, start)
                assert on_step(end)
                self.events.append((ticks(start), PRIO_ON, ch, "on", pitch, vel))
                self.events.append((ticks(end), PRIO_OFF, ch, "off", pitch, 0))
        return self

    def sorted_events(self):
        return sorted(self.events)


def build_song():
    s = Song()

    # --- PU1: the lead -----------------------------------------------------
    lead = 100   # velocity -> envelope start volume 12 of 15
    # Bars 1-4: the theme.
    theme = [
        (69, 0), (72, .5), (76, 1), (81, 1.5), (79, 2), (76, 3),
        (77, 4), (76, 4.5), (74, 5), (72, 5.5), (74, 6),
        (76, 8), (79, 8.5), (84, 9), (83, 9.5), (79, 10), (76, 11),
        (74, 12), (71, 12.5), (67, 13), (71, 13.5), (74, 14), (76, 15),
    ]
    for pitch, start in theme:
        s.note(PU1, pitch, bar(1, start), lead)
    # Bars 5-8: long notes under PU1's second command slot, V, whose depth the
    # automation rides a notch a beat -- the mod wheel's job, in a cell.
    sustained = [
        (81, 0), (76, 2),
        (77, 4), (72, 6), (74, 7),
        (76, 8), (79, 11),
        (74, 12), (71, 14), (67, 15),
    ]
    for pitch, start in sustained:
        s.note(PU1, pitch, bar(5, start), lead)
    # Bars 9-12: eighth notes; the duty changes per bar through PU1's first
    # command slot (W, one duty per bar).
    eighths = [
        (69, 0), (69, .5), (72, 1), (76, 1.5), (81, 2), (79, 2.5), (76, 3), (72, 3.5),
        (77, 4), (77, 4.5), (81, 5), (77, 5.5), (76, 6), (74, 6.5), (72, 7), (74, 7.5),
        (76, 8), (76, 8.5), (79, 9), (84, 9.5), (83, 10), (79, 10.5), (76, 11), (79, 11.5),
        (74, 12), (71, 12.5), (67, 13), (71, 13.5), (74, 14), (76, 15), (79, 15.5),
    ]
    for pitch, start in eighths:
        s.note(PU1, pitch, bar(9, start), lead)
    # Bars 13-14: the same slot holds L, so every note slides in from the one
    # before it -- the portamento the pitch wheel used to draw by hand.
    for pitch, start in [(79, 0), (76, 2), (77, 4), (81, 5), (84, 6)]:
        s.note(PU1, pitch, bar(13, start), lead)
    # Bars 15-16: L gives way to P, a Fast bend that leans every note down.
    for pitch, start in [(76, 0), (79, 2), (76, 3), (74, 4), (81, 5.5)]:
        s.note(PU1, pitch, bar(15, start), lead)
    s.tail[PU1] = 2.5                                          # to bar 17's first step

    # --- PU2: the bass -----------------------------------------------------
    # Velocity is the envelope start volume: 127 -> 15, 112 -> 14, 104 -> 13,
    # 88 -> 11, 72 -> 9.
    for n in range(1, 15):
        root = chord_of_bar(n) - 12                            # A2 F2 C2 G2 register
        fifth, octave = root + 7, root + 12
        if 5 <= n <= 8:
            pattern = [(root, 0, 127), (root, 1.5, 88), (fifth, 2, 104), (root, 3, 112)]
        else:
            pattern = [(root, 0, 127), (root, .5, 88), (fifth, 1, 104), (root, 1.5, 72),
                       (octave, 2, 112), (root, 2.5, 72), (fifth, 3, 104), (root, 3.5, 88)]
        for pitch, start, vel in pattern:
            s.note(PU2, pitch, bar(n, start), vel)
    # Bars 15-16: the Instrument lane hands PU2 factory slot 17, Pulse kick --
    # Drum pitch speed, and its table 7 does the drop -- for a kick pattern,
    # then gives the Pluck back for the last bass note.
    for start in [0, 1, 1.5, 2, 3]:
        s.note(PU2, 48, bar(15, start), 120)
    for start in [0, 1, 2, 2.5]:
        s.note(PU2, 48, bar(16, start), 120)
    s.note(PU2, chord_of_bar(16) - 12, bar(16, 3), 112)
    s.tail[PU2] = 1.0

    # --- WAV: the wave bass ------------------------------------------------
    # Keyswitches (base 12 on the wave channel) choose the instrument per
    # section: 18 -> slot 7 Triangle bass, 21 -> slot 10 Tri to saw,
    # 20 -> slot 9 Organ frames. A keyswitch note selects and never sounds, so
    # it writes no register and is never recorded as a cell.
    s.keyswitch(WAV, SLOT_TRIANGLE_BASS, 0)
    s.keyswitch(WAV, SLOT_TRI_TO_SAW, ticks(bar(5)) - 30)
    s.keyswitch(WAV, SLOT_TRIANGLE_BASS, ticks(bar(9)) - 30)
    s.keyswitch(WAV, SLOT_ORGAN_FRAMES, ticks(bar(13)) - 30)
    for n in range(1, BARS + 1):
        root = chord_of_bar(n) - 24                            # A1 F1 C2 G1 register
        fifth, octave = root + 7, root + 12
        if n <= 4:
            pattern = [(root, 0), (fifth, 2), (root, 3)]
        elif n <= 8:
            pattern = [(root, 0), (fifth, 2)]
        elif n <= 12:
            pattern = [(root, 0), (root, .5), (fifth, 1), (root, 1.5),
                       (octave, 2), (root, 2.5), (fifth, 3), (root, 3.5)]
        elif n < 16:
            pattern = [(root, 0), (fifth, 2)]
        else:
            pattern = [(root, 0), (root, 2)]
        for pitch, start in pattern:
            s.note(WAV, pitch, bar(n, start), 100)
    s.tail[WAV] = 2.0

    # --- NOI: the drums ----------------------------------------------------
    # The keyswitch (base 12 on the noise channel) selects Kick, slot 11, as
    # the base; velocity zones of 8 pick the drum relative to it.
    for n in (1, 5, 9, 13):
        s.keyswitch(NOI, SLOT_KICK, 0 if n == 1 else ticks(bar(n)) - 30)
    for n in range(1, BARS + 1):
        hits = [(0, DRUM_CRASH if n in (5, 9, 13) else DRUM_KICK), (.5, DRUM_HAT_CLOSED), (1, DRUM_SNARE)]
        if n % 4 == 2:
            hits += [(1.75, DRUM_KICK), (2.5, DRUM_HAT_CLOSED)]     # the kick pushed off the beat
        else:
            hits += [(1.5, DRUM_HAT_CLOSED), (2, DRUM_KICK), (2.5, DRUM_HAT_CLOSED)]
        hits.append((3, DRUM_SNARE))
        hits.append((3.5, DRUM_SNARE if n % 4 == 0 else DRUM_HAT_OPEN))
        for start, (note, vel) in hits:
            s.note(NOI, note, bar(n, start), vel)
    s.tail[NOI] = 0.5

    return s.finish()


# ---------------------------------------------------------------------------
# the automation
# ---------------------------------------------------------------------------

class Envelope:
    def __init__(self, pid, points):
        self.pid = pid
        self.points = points   # (beat, parameter value), value held until the next point


def build_envelopes(song, song_tempo=False):
    """The lanes the demo draws, as (beat, value) points.

    Everything the performance does is either a command slot -- a letter, an x
    and a y, in force until the letter changes -- or one of the few channel
    fields left (docs/COMMANDS_AND_TEMPO.md section 3). A slot fires at the next
    tick when any of its three parameters changes, and again at every note-on
    after the instrument and its table, so a letter set for a bar shapes every
    note in that bar; putting the letter back to none reverts what it changed to
    the instrument's own value.

    Every point sits on a step, one step before the bar it is meant for so that
    it is already in force when that bar's first note starts, and never on a
    step where its own channel starts a note (checked below).

    With song_tempo the project runs on the song's clock instead of the host's,
    and PU1's second slot carries T for bars 9-12.
    """
    out = []
    add = lambda pid, points: out.append(Envelope(pid, points))
    # One step before bar n: where a lane changes so the bar's first note has
    # it. The step before that (two steps early) is where a letter that must
    # revert first goes.
    pre = lambda n, back=1: bar(n) - back * STEP

    if song_tempo:
        # The song owns the tempo: the tracker's ticks run at 150 BPM against
        # the host's 120, and the T below overrides the base from bar 9.
        add("tempo_source", [(0, 1)])                           # Song
        add("song_tempo", [(0, SONG_TEMPO)])

    # PU1 CMD1: W for the duty over bars 9-12, then L (portamento) for bars
    # 13-14 and P (a Fast bend) for bars 15-16. One slot, three letters.
    add("ch1_cmd1_type", [(0, CMD["none"]), (pre(9), CMD["W"]),
                          (pre(13, 3), CMD["none"]), (pre(13), CMD["L"]), (pre(15), CMD["P"])])
    add("ch1_cmd1_x", [(0, 0), (pre(9), 0), (pre(10), 1), (pre(11), 2), (pre(12), 3),
                       (pre(13, 3), 0), (pre(13), LEAD_SLIDE), (pre(15), LEAD_BEND)])

    # PU1 CMD2 = V: the vibrato over bars 5-8. The depth rides a notch a beat
    # through the slot's y, on step boundaries -- what the mod wheel used to
    # do. In the song-tempo project the same lane then becomes T for bars
    # 9-12: one slot, two letters, never at the same time.
    ride = [1, 2, 3, 4, 5, 6, 7, 8, 8, 7, 6, 5, 4, 3, 2, 1]
    cmd2_type = [(0, CMD["none"]), (pre(5), CMD["V"])]
    cmd2_x = [(0, 0), (pre(5), 3), (pre(7), 6)]
    cmd2_y = [(0, 0)] + [(pre(5) + j, ride[j]) for j in range(len(ride))]
    if song_tempo:
        cmd2_type += [(pre(9), CMD["T"]), (pre(13, 3), CMD["none"])]
        cmd2_x += [(pre(9), SONG_TEMPO_DROP), (pre(13, 3), 0)]
        cmd2_y += [(pre(9), 0)]
    else:
        # The letter goes back to none at bar 9 and the lead's own vibrato --
        # speed, depth and its ten-tick delay -- is what the notes get. The
        # recorder writes that as V's revert form, which is the same thing
        # said in a cell (section 9.4).
        cmd2_type += [(pre(9), CMD["none"])]
        cmd2_x += [(pre(9), 0)]
        cmd2_y += [(pre(9), 0)]
    add("ch1_cmd2_type", cmd2_type)
    add("ch1_cmd2_x", cmd2_x)
    add("ch1_cmd2_y", cmd2_y)

    # PU2's instrument: the Pluck, then factory slot 17 "Pulse kick" for the
    # kick pattern of bars 15-16, then the Pluck again for the last note. The
    # keyswitch octave only reaches slots 1-12, so this is the Instrument lane.
    add("ch2_instrument", [(0, SLOT_PLUCK), (pre(15), SLOT_PULSE_KICK), (bar(16, 2.75), SLOT_PLUCK)])

    # PU2 CMD1 = E: the envelope, alternating pluck and long by the bar. While
    # E is in force it sets the start volume, so the bass's velocity accents
    # step aside for four bars; the letter goes back to none at bar 13 and
    # they come back. The recorder writes that as E's revert form, which puts
    # the envelope back and leaves nothing in force (section 9.4).
    add("ch2_cmd1_type", [(0, CMD["none"]), (pre(9), CMD["E"]), (pre(13), CMD["none"])])
    add("ch2_cmd1_x", [(0, 0), (pre(9), 15), (pre(10), 11), (pre(11), 15), (pre(12), 12), (pre(13), 0)])
    add("ch2_cmd1_y", [(0, 0), (pre(9), env_y(3)), (pre(10), env_y(0)), (pre(11), env_y(2)), (pre(12), env_y(0)), (pre(13), 0)])

    # WAV CMD1 = W (the wave slot) and CMD2 = F (the frame): saw for bar 9, then
    # the six-frame Tri-to-saw walked from its triangle end to its saw end.
    # Both letters go back to none at bar 13, where a keyswitch hands WAV the
    # Organ frames: nothing is left in force, so the new instrument plays its
    # own wave from its own first frame (section 3).
    add("ch3_cmd1_type", [(0, CMD["none"]), (pre(9), CMD["W"]), (pre(13), CMD["none"])])
    add("ch3_cmd1_x", [(0, 0), (pre(9), WAVE_SAW), (pre(10), WAVE_TRI_TO_SAW), (pre(13), 0)])
    add("ch3_cmd2_type", [(0, CMD["none"]), (pre(10), CMD["F"]), (pre(13), CMD["none"])])
    add("ch3_cmd2_x", [(0, 0), (pre(10), 1), (pre(11), 3), (pre(12), 6), (pre(13), 0)])

    # NOI CMD1 = M: the master volume dips over the last two beats of bar 8 and
    # comes back before bar 9 -- a command in a cell, not the master parameters.
    add("ch4_cmd1_type", [(0, CMD["none"]), (bar(8, 1.75), CMD["M"])])
    add("ch4_cmd1_x", [(0, 0), (bar(8, 1.75), 5), (bar(8, 2.75), 3), (bar(8, 3.75), 7)])
    add("ch4_cmd1_y", [(0, 0), (bar(8, 1.75), 5), (bar(8, 2.75), 3), (bar(8, 3.75), 7)])

    check_envelopes(song, out)
    return out


def static_parameters():
    """The channel fields the demo sets once and leaves alone.

    They are not recorded (docs/COMMANDS_AND_TEMPO.md section 9.4 keeps Level,
    Pan, Transpose and the switches out of the cells) and still apply when the
    recorded song plays back, so the record test sets them in both passes.
    """
    return [
        ("ch1_source", 1),          # PU1 stops being omni: MIDI 1
        ("ch3_keyswitch", 1),
        ("ch4_keyswitch", 1),
        ("ch4_velocity", 1),        # instrument bank: velocity picks the drum
    ]


def hardware_envelopes():
    """De-click and the model: not tracker-level, and not part of the record
    test (they are the analog stage, not a cell). The Reaper projects draw
    them; `chipboy_recordtest` leaves both at their defaults in both passes."""
    return [Envelope("declick", [(0, 0), (bar(14), 1), (bar(15), 0)]),
            Envelope("model", [(0, 0), (bar(15), 1), (bar(16), 2)])]


def check_envelopes(song, envelopes):
    """Every point on a step, and never on a step where its own channel starts
    a note (see the note at the top of "the tune")."""
    for env in envelopes:
        ch = int(env.pid[2]) - 1 if env.pid.startswith("ch") else -1
        onsets = song.onset_beats(ch) if 0 <= ch < 4 else set()
        last = None
        for beat, _value in env.points:
            assert on_step(beat), "%s: automation point off the step grid at beat %g" % (env.pid, beat)
            assert last is None or beat > last, "%s: automation points out of order" % env.pid
            last = beat
            if beat <= 0.0:
                continue          # the lane's starting value, in force before a note plays
            assert round(beat / STEP) not in onsets, \
                "%s: automation point at beat %g shares a step with a note on channel %d" % (env.pid, beat, ch + 1)


# ---------------------------------------------------------------------------
# writers
# ---------------------------------------------------------------------------

def vlq(n):
    out = [n & 0x7F]
    n >>= 7
    while n:
        out.append(0x80 | (n & 0x7F))
        n >>= 7
    return bytes(reversed(out))


def midi_track(chunks):
    data = b"".join(chunks)
    return b"MTrk" + struct.pack(">I", len(data)) + data


def meta(delta, kind, payload):
    return vlq(delta) + bytes([0xFF, kind]) + vlq(len(payload)) + payload


def write_midi(path, song):
    track_names = ["PU1 lead", "PU2 bass", "WAV wave bass", "NOI drums"]
    tracks = []
    tempo = b"".join([
        meta(0, 0x03, b"ChipBoy demo"),
        meta(0, 0x51, struct.pack(">I", int(60000000 / BPM))[1:]),
        meta(0, 0x58, bytes([4, 2, 24, 8])),
        meta(TOTAL_TICKS, 0x2F, b""),
    ])
    tracks.append(midi_track([tempo]))
    events = song.sorted_events()
    for ch in range(4):
        chunks = [meta(0, 0x03, track_names[ch].encode("ascii")), meta(0, 0x20, bytes([ch]))]
        last = 0
        for tick, _prio, ech, kind, a, b in events:
            if ech != ch:
                continue
            delta = tick - last
            last = tick
            if kind == "on":
                chunks.append(vlq(delta) + bytes([0x90 | ch, a, b]))
            elif kind == "off":
                chunks.append(vlq(delta) + bytes([0x80 | ch, a, 0]))
            elif kind == "cc":
                chunks.append(vlq(delta) + bytes([0xB0 | ch, a, b]))
            elif kind == "bend":
                chunks.append(vlq(delta) + bytes([0xE0 | ch, a, b]))
        chunks.append(meta(max(0, TOTAL_TICKS - last), 0x2F, b""))
        tracks.append(midi_track(chunks))
    header = b"MThd" + struct.pack(">IHHH", 6, 1, len(tracks), PPQ)
    with open(path, "wb") as f:
        f.write(header + b"".join(tracks))


def fmt(x):
    """A number the way Reaper writes it: no exponent, no trailing zeros."""
    if isinstance(x, int):
        return str(x)
    s = "%.9f" % x
    s = s.rstrip("0").rstrip(".")
    return s if s else "0"


def vst_chunk_lines(state=b""):
    """Reaper's state chunk for a VST3 with no inputs and a stereo output.
    Layout: id, magic 0xFEED5EEE, input count and masks, output count and
    masks, **the state's size**, 1, 0x10FFFF; then that many bytes of plugin
    state; then the empty program and preset names and the terminator.

    With no state (the first two projects) the size is zero and nothing sits
    between the header and the terminator: the plugin loads its defaults and
    the envelopes set everything the demo needs. The hybrid project passes the
    bytes `chipboy_recordtest --write-state` wrote, which carry the song, its
    bank and the four Hybrid channels (section 20).

    Reaper reads a chunk as base64 lines and joins what they decode to, so the
    header, the state and the terminator are separate lines and the state is
    split at RPP_BASE64_COLS characters -- a multiple of four, so every line
    is whole bytes wherever the join happens."""
    number = reaper_vst3_number(VST3_CID)
    header = struct.pack("<IIII", number, 0xFEED5EEE, 0, 2)
    header += struct.pack("<QQ", 1, 2)
    header += struct.pack("<III", len(state), 1, 0x0010FFFF)
    trailer = bytes([0, 0]) + struct.pack("<I", 0x10)
    lines = [base64.b64encode(header).decode("ascii")]
    encoded = base64.b64encode(state).decode("ascii")
    lines += [encoded[i:i + RPP_BASE64_COLS] for i in range(0, len(encoded), RPP_BASE64_COLS)]
    lines.append(base64.b64encode(trailer).decode("ascii"))
    return lines


def write_rpp(path, song, envelopes, table, tag="", state=b""):
    # tag keeps the two projects' GUIDs apart while the derivation stays the
    # same: uuid5 of the fixed namespace over "chipboy-demo/" + the name.
    g = lambda name: guid(tag + name)
    by_id = {p.pid: (i, p) for i, p in enumerate(table)}
    number = reaper_vst3_number(VST3_CID)
    L = []
    w = L.append
    w('<REAPER_PROJECT 0.1 "7.0/win64" %d' % PROJECT_TIMESTAMP)
    w('  RIPPLE 0')
    w('  GROUPOVERRIDE 0 0 0')
    w('  AUTOXFADE 129')
    w('  ENVATTACH 3')
    w('  MIXERUIFLAGS 11 48')
    w('  PEAKGAIN 1')
    w('  FEEDBACK 0')
    w('  PANLAW 1')
    w('  PROJOFFS 0 0 0')
    w('  MAXPROJLEN 0 600')
    w('  GRID 3199 8 1 8 1 0 0 0')
    w('  TIMEMODE 1 5 -1 30 0 0 -1')
    w('  PANMODE 3')
    w('  CURSOR 0')
    w('  ZOOM 40 0 0')
    w('  VZOOMEX 6 0')
    w('  LOOP 0')
    w('  LOOPGRAN 0 4')
    w('  TIMELOCKMODE 1')
    w('  TEMPOENVLOCKMODE 1')
    w('  ITEMMIX 1')
    w('  SAMPLERATE 48000 0 0')
    w('  LOCK 1')
    w('  GLOBAL_AUTO -1')
    w('  TEMPO %d %d 4' % (BPM, BEATS_PER_BAR))
    w('  PLAYRATE 1 0 0.25 4')
    w('  SELECTION 0 0')
    w('  SELECTION2 0 0')
    w('  MASTERAUTOMODE 0')
    w('  MASTERTRACKHEIGHT 0 0')
    w('  MASTERMUTESOLO 0')
    w('  MASTER_NCH 2 2')
    w('  MASTER_VOLUME 1 0 -1 -1 1')
    w('  MASTER_PANMODE 3')
    w('  MASTER_FX 1')
    w('  MASTER_SEL 0')
    # --- the track --------------------------------------------------------
    w('  <TRACK %s' % g("track"))
    w('    NAME ChipBoy')
    w('    PEAKCOL 16576')
    w('    BEAT -1')
    w('    AUTOMODE 0')
    w('    VOLPAN 1 0 -1 -1 1')
    w('    MUTESOLO 0 0 0')
    w('    IPHASE 0')
    w('    PLAYOFFS 0 1')
    w('    ISBUS 0 0')
    w('    BUSCOMP 0 0 0 0 0')
    w('    SHOWINMIX 1 0.6667 0.5 1 0.5 0 0 0')
    w('    SEL 1')
    w('    REC 0 0 1 0 0 0 0 0')
    w('    VU 2')
    w('    TRACKHEIGHT 0 0 0 0 0 0 0')
    w('    INQ 0 0 0 0.5 100 0 0 100')
    w('    NCHAN 2')
    w('    FX 1')
    w('    TRACKID %s' % g("track"))
    w('    PERF 0')
    w('    MIDIOUT -1')
    w('    MAINSEND 1 0')
    w('    <FXCHAIN')
    w('      SHOW 0')
    w('      LASTSEL 0')
    w('      DOCKED 0')
    w('      BYPASS 0 0 0')
    w('      <VST "VST3i: ChipBoy (ChipBoy)" ChipBoy.vst3 0 "" %d{%s} ""' % (number, VST3_CID))
    for line in vst_chunk_lines(state):
        w('        ' + line)
    w('      >')
    w('      FLOATPOS 0 0 0 0')
    w('      FXID %s' % g("fx"))
    for env in envelopes:
        index, p = by_id[env.pid]
        w('      <PARMENV %d:%d 0 1 0.5 "%s / ChipBoy"' % (index, vst3_param_id(p.pid), p.name))
        w('        EGUID %s' % g("env/" + env.pid))
        w('        ACT 1 -1')
        w('        VIS 1 1 1')
        w('        LANEHEIGHT 0 0')
        w('        ARM 0')
        w('        DEFSHAPE 1 -1 -1')
        for beat, value in env.points:
            w('        PT %s %s 1' % (fmt(seconds(float(beat))), fmt(p.normalised(value))))
        w('      >')
    w('      WAK 0 0')
    w('    >')
    # --- the MIDI item ----------------------------------------------------
    w('    <ITEM')
    w('      POSITION 0')
    w('      SNAPOFFS 0')
    w('      LENGTH %s' % fmt(SONG_SECONDS))
    w('      LOOP 0')
    w('      ALLTAKES 0')
    w('      FADEIN 0 0 0 0 0 0 0')
    w('      FADEOUT 0 0 0 0 0 0 0')
    w('      MUTE 0 0')
    w('      SEL 0')
    w('      IGUID %s' % g("item"))
    w('      IID 1')
    w('      NAME "ChipBoy demo (MIDI channels 1-4)"')
    w('      VOLPAN 1 0 1 -1')
    w('      SOFFS 0 0')
    w('      PLAYRATE 1 1 0 -1 0 0.0025')
    w('      CHANMODE 0')
    w('      GUID %s' % g("take"))
    w('      <SOURCE MIDI')
    w('        HASDATA 1 %d QN' % PPQ)
    w('        CCINTERP 32')
    w('        POOLEDEVTS %s' % g("pooledevts"))
    last = 0
    for tick, _prio, ch, kind, a, b in song.sorted_events():
        delta = tick - last
        last = tick
        if kind == "on":
            status = 0x90 | ch
        elif kind == "off":
            status = 0x80 | ch
        elif kind == "cc":
            status = 0xB0 | ch
        else:
            status = 0xE0 | ch
        w('        E %d %02x %02x %02x' % (delta, status, a, b))
    w('        E %d b0 7b 00' % (TOTAL_TICKS - last))       # all notes off closes the source
    w('        CCINTERP 32')
    w('        GUID %s' % g("source"))
    w('        IGNTEMPO 0 %d %d 4' % (BPM, BEATS_PER_BAR))
    w('        SRCCOLOR 0')
    w('        VELLANE -1 100 0')
    w('        KEYSNAP 0')
    w('        TRACKSEL 0')
    w('      >')
    w('    >')
    w('  >')
    w('>')
    with open(path, "w", newline="\r\n", encoding="utf-8") as f:
        f.write("\n".join(L) + "\n")


def write_automation_json(path, table, statics, lanes, hardware):
    """Demo/chipboy_demo_automation.json -- the demo's automation as data.

    The Reaper projects, the MIDI file and this JSON all come from the same
    Python tables, so a tool that cannot read an .rpp still drives exactly what
    the demo draws. `chipboy_recordtest` is that tool.

        {
          "format": "chipboy-demo-automation", "version": 1,
          "bpm": 120, "beatsPerBar": 4, "bars": 16, "step": 0.25,
          "static": { "<param id>": value, ... },
          "lanes":  { "<param id>": [[beat, value], ...], ... },
          "hardware": { "<param id>": [[beat, value], ...], ... }
        }

    `static` holds the parameters the demo sets once before anything plays and
    never moves; `lanes` the ones it automates. Both are in **plugin units** --
    the parameter's own range from PARAMETERS.md, so a command type is the
    index into the letter list, an x or a y is 0-255 and an instrument is its
    slot -- not the 0-1 a host stores. A lane's value is the value of the last
    point at or before a moment, held until the next point; `beat` is quarter
    notes from the start of bar 1 and always lands on a step (0.25 beat).
    `hardware` is De-click and the model: the analog stage rather than
    anything a tracker cell can hold, drawn by the projects and left alone by
    the record test.
    """
    ids = {p.pid for p in table}
    L = []
    w = L.append
    w("{")
    w('  "format": "chipboy-demo-automation",')
    w('  "version": 1,')
    w('  "bpm": %s, "beatsPerBar": %d, "bars": %d, "step": %s,' % (fmt(float(BPM)), BEATS_PER_BAR, BARS, fmt(STEP)))
    w('  "static": {')
    rows = []
    for pid, value in statics:
        assert pid in ids, pid
        rows.append('    "%s": %s' % (pid, fmt(value)))
    w(",\n".join(rows))
    w("  },")
    for name, group in (("lanes", lanes), ("hardware", hardware)):
        w('  "%s": {' % name)
        rows = []
        for env in group:
            assert env.pid in ids, env.pid
            points = ", ".join("[%s, %s]" % (fmt(float(beat)), fmt(value)) for beat, value in env.points)
            rows.append('    "%s": [%s]' % (env.pid, points))
        w(",\n".join(rows))
        w("  }," if name == "lanes" else "  }")
    w("}")
    with open(path, "w", newline="\n", encoding="utf-8") as f:
        f.write("\n".join(L) + "\n")


def write_parameters_md(path, table, envelopes, song_tempo_envelopes):
    ids = lambda es: ", ".join("`%s`" % e.pid for e in es)
    extra = [e for e in song_tempo_envelopes if e.pid not in {x.pid for x in envelopes}]
    L = []
    w = L.append
    w("# ChipBoy parameters, in host order")
    w("")
    w("Generated by `tools/demo/make_demo.py`. The order is the order")
    w("`Source/plugin/shared/Parameters.cpp` adds the parameters: the globals from")
    w("`addGlobalParameters`, then channels 1-4 from `addChannelParameters`. Every")
    w("channel has the same fifteen -- source, instrument, table, level, pan,")
    w("transpose, two command slots of three parameters each, live follow, velocity")
    w("and keyswitches -- and only Level differs between the kinds (0-16 on the")
    w("pulse and noise channels, 0-4 on the wave channel). JUCE presents the list to")
    w("VST3 hosts in exactly this order; the wrapper's own Bypass parameter and,")
    w("after it, its MIDI CC emulation parameters follow the %d below." % len(table))
    w("`chipboy_paramdump` (a CMake target of the plugin build) prints the same table")
    w("from the running code, and `make_demo.py --paramdump <path>` checks this file")
    w("against it.")
    w("")
    w("The **index** is what Reaper's `PARMENV` lines use. The **VST3 id** is what")
    w("JUCE derives from the id string (`String::hashCode()` with the top bit")
    w("cleared); Reaper writes it after the index (`index:id`) so an envelope")
    w("survives a change of order. Envelope values are normalised: for an")
    w("integer or choice parameter the stored value is `(v - min) / (max - min)`,")
    w("and the plugin rounds to the nearest step when it reads it back.")
    w("")
    w("| Index | Id | Name | Range | Default | VST3 id |")
    w("|---:|---|---|---|---:|---:|")
    for i, p in enumerate(table):
        default = fmt(p.default) if p.kind == "float" else str(p.default)
        if p.kind == "choice":
            default += " (%s)" % p.choices[int(p.default)]
        w("| %d | `%s` | %s | %s | %s | %d |" % (i, p.pid, p.name, p.range_text(), default, vst3_param_id(p.pid)))
    w("")
    w("## The two command slots")
    w("")
    w("A slot is three parameters -- the letter, `x` and `y` -- and it is *in force*")
    w("rather than momentary. It fires at the next tick whenever one of the three")
    w("changes, and again at every note-on, after the instrument and its table, so a")
    w("letter drawn across a bar shapes every note in that bar. Put the letter back")
    w("to `none` and what it changed reverts to the instrument's own value (`E` `F`")
    w("`O` `S` `V` `W`), to zero (`P`), to the parameter (`M`, `T`), or stops (`A`, `G`).")
    w("The recorder writes that as the letter's *revert cell* -- the same letter saying")
    w("\"put this back\" rather than a value -- so a recorded song reverts exactly where")
    w("the lane did. CMD1 is applied before CMD2.")
    w("")
    w("| Letter | Meaning | x | y |")
    w("|---|---|---|---|")
    for letter, meaning, x, y in [
        ("A", "table", "table slot 1-64, 0 stops", "-"),
        ("C", "chord", "semitones", "semitones"),
        ("D", "delay", "ticks", "-"),
        ("E", "envelope", "volume 0-15 (wave level 0-3 on WAV)", "0-7 decay, 8-15 attack"),
        ("F", "frame", "frame 1-16 (WAV)", "-"),
        ("G", "groove", "groove slot 1-16, 0 straight", "-"),
        ("K", "kill", "ticks after the note-on", "-"),
        ("L", "slide", "x + 1 updates, linear in semitones", "-"),
        ("M", "master volume", "left 0-7", "right 0-7"),
        ("O", "pan", "0 off, 1 L, 2 R, 3 both", "-"),
        ("P", "pitch offset", "two's complement -128..127, the measured step table", "-"),
        ("R", "retrigger", "signed nibble of volume per retrigger, 8 resyncs", "y x (rate + 1) + 1 ticks"),
        ("S", "sweep (PU1)", "rate 0-7", "NR10's low nibble: 0-7 up, 8-15 down"),
        ("T", "tempo", "LSDj's byte: 28-FF is 40-255 BPM, 00-27 is 256-295", "-"),
        ("V", "vibrato", "speed 0-15, 64/(x+1) updates a cycle", "depth 0-15, 1/8 to 8 semitones"),
        ("W", "wave", "duty 0-3 on a pulse, wave slot 1-64 on WAV", "-"),
        ("Z", "random", "randomises the other slot's x, up to x", "-"),
    ]:
        w("| `%s` | %s | %s | %s |" % (letter, meaning, x, y))
    w("")
    w("`H` (hop) exists in table steps only, so it is not in the lane's choice; the")
    w("full definitions are in [`../docs/COMMANDS_AND_TEMPO.md`](../docs/COMMANDS_AND_TEMPO.md)")
    w("section 2.")
    w("")
    w("## The rest of the channel")
    w("")
    w("Three fields carry a position meaning \"use the instrument's value\": the top")
    w("value of Level (16, or 4 on WAV), the `inst` choice of Pan, and 0 of Table.")
    w("Automation overrides the instrument only where a lane is drawn away from that")
    w("position. Everything a lane used to override silently -- duty, envelope,")
    w("sweep, wave, frame, vibrato, arpeggio, detune, LFSR width -- is now the")
    w("instrument's alone, or arrives as a command.")
    w("")
    w("Ticks are 24 to the beat, from **Tempo Source** (`tempo_source`): the host's")
    w("tempo, or the song's own **Song Tempo** (`song_tempo`, 40-255 BPM) with `T`")
    w("commands over it. **Quantize Notes To Ticks** (`notes_on_tick`) holds incoming")
    w("note-ons and note-offs until the next tick; bends and controllers are never")
    w("quantised, and tracker cells are always on ticks.")
    w("")
    w("## What the demo automates")
    w("")
    for line in textwrap.wrap("`ChipBoy Demo.rpp` draws " + ids(envelopes) + ".", 78):
        w(line)
    w("")
    for line in textwrap.wrap("`ChipBoy Demo (song tempo).rpp` draws the same lanes plus "
                              + ids(extra) + ", and PU1's second slot carries `T` for bars"
                              " 9-12 instead of going back to `none` at bar 9.", 78):
        w(line)
    w("")
    w("VST3 class id of ChipBoy: `%s`; Reaper's number for it: `%d`." % (VST3_CID, reaper_vst3_number(VST3_CID)))
    with open(path, "w", newline="\n", encoding="utf-8") as f:
        f.write("\n".join(L) + "\n")


# ---------------------------------------------------------------------------
# checking against chipboy_paramdump
# ---------------------------------------------------------------------------

def check_against_paramdump(path, table):
    if os.path.isfile(path) and not os.access(path, os.X_OK) or path.endswith(".tsv") or path.endswith(".txt"):
        with open(path, encoding="utf-8") as f:
            text = f.read()
    else:
        text = subprocess.run([path], check=True, capture_output=True, text=True).stdout
    rows = [line.split("\t") for line in text.splitlines() if line.strip()]
    problems = []
    if len(rows) != len(table):
        problems.append("paramdump lists %d parameters, this table %d" % (len(rows), len(table)))
    for i, (row, p) in enumerate(zip(rows, table)):
        index, pid, name, lo, hi, default = int(row[0]), row[1], row[2], float(row[3]), float(row[4]), float(row[5])
        if index != i or pid != p.pid or name != p.name:
            problems.append("index %d: tool says %s '%s', table says %s '%s'" % (i, pid, name, p.pid, p.name))
        if abs(lo - p.lo) > 1e-6 or abs(hi - p.hi) > 1e-6 or abs(default - p.default) > 1e-6:
            problems.append("%s: tool range %g-%g default %g, table %g-%g default %g" % (pid, lo, hi, default, p.lo, p.hi, p.default))
    for problem in problems:
        print("paramdump check:", problem)
    print("paramdump check: %s (%d parameters)" % ("mismatch" if problems else "identical", len(rows)))
    return not problems


# ---------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description="generate the ChipBoy demo project")
    ap.add_argument("--out", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "Demo"))
    ap.add_argument("--paramdump", help="chipboy_paramdump executable, or a file with its output, to check the table against")
    args = ap.parse_args()

    table = parameter_table()
    song = build_song()
    statics = static_parameters()
    hardware = hardware_envelopes()
    lanes = build_envelopes(song)
    song_tempo_lanes = build_envelopes(song, song_tempo=True)
    fixed = [Envelope(pid, [(0, value)]) for pid, value in statics]
    envelopes = fixed + lanes + hardware
    song_tempo_envelopes = fixed + song_tempo_lanes + hardware
    out = os.path.normpath(args.out)
    os.makedirs(out, exist_ok=True)
    write_midi(os.path.join(out, "chipboy_demo.mid"), song)
    write_rpp(os.path.join(out, "ChipBoy Demo.rpp"), song, envelopes, table)
    write_rpp(os.path.join(out, "ChipBoy Demo (song tempo).rpp"), song, song_tempo_envelopes, table, "song-tempo/")
    # The hybrid project (docs/COMMANDS_AND_TEMPO.md section 20): the same MIDI
    # item, the plugin's saved state instead of the automation -- the demo song
    # in a tab with its own bank and all four channels on Hybrid -- and only
    # the two lanes that are not tracker-level, the model and De-click.
    state_path = os.path.join(out, "chipboy_demo_hybrid.state")
    hybrid_written = os.path.isfile(state_path)
    if hybrid_written:
        with open(state_path, "rb") as f:
            state = f.read()
        write_rpp(os.path.join(out, "ChipBoy Demo (hybrid).rpp"), song, hardware, table, "hybrid/", state)
    else:
        print("no plugin state at %s: the hybrid project is not written."
              " Write it with chipboy_recordtest --write-state" % state_path, file=sys.stderr)
    write_parameters_md(os.path.join(out, "PARAMETERS.md"), table, envelopes, song_tempo_envelopes)
    write_automation_json(os.path.join(out, "chipboy_demo_automation.json"), table, statics, lanes, hardware)
    n = len(song.events)
    print("wrote %s: %d MIDI events, %d + %d envelopes, %d parameters%s"
          % (out, n, len(envelopes), len(song_tempo_envelopes), len(table),
             ", hybrid state %d bytes" % len(state) if hybrid_written else ""))
    ok = hybrid_written
    if args.paramdump:
        ok = check_against_paramdump(args.paramdump, table)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
