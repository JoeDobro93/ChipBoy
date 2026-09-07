#!/usr/bin/env python3
"""ChipBoy demo project generator.

Writes three files under Demo/ (or --out):

  chipboy_demo.mid    a Standard MIDI File, format 1, 960 ticks per quarter,
                      120 BPM, 4/4, 16 bars. One track per hardware channel on
                      MIDI channels 1-4: PU1 lead, PU2 bass, WAV wave bass,
                      NOI drums.
  ChipBoy Demo.rpp    a Reaper project: one track holding the ChipBoy VST3, the
                      same notes as one MIDI item, and automation envelopes for
                      the parameters the demo shows off.
  PARAMETERS.md       the plugin's host-visible parameter table in host order,
                      with the normalised values Reaper stores and the VST3
                      parameter ids JUCE derives.

Standard library only. Deterministic: two runs give byte-identical files.

    python3 tools/demo/make_demo.py [--out Demo] [--paramdump PATH]

  --paramdump PATH    cross-check the embedded parameter table against the
                      chipboy_paramdump tool: PATH is the executable, or a
                      file holding its output.

The parameter order is the order Source/plugin/shared/Parameters.cpp adds the
parameters (addGlobalParameters, then addChannelParameters for channels 1-4,
whose per-kind conditionals decide which ids exist for a channel). JUCE hands
the list to VST3 hosts in that order; the wrapper's own Bypass parameter and
its MIDI-CC emulation parameters come after it.
"""

import argparse
import base64
import os
import struct
import subprocess
import sys
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

# The plugin's VST3 identity: JucePlugin_ManufacturerCode 'Chpb' and
# JucePlugin_PluginCode 'Chby' (CMakeLists.txt), which JUCE turns into the
# component class id ABCDEF01 9182FAEB <manufacturer> <plugin> with
# JUCE_VST3_CAN_REPLACE_VST2=0. The built bundle's moduleinfo.json shows the
# same CID.
MANUFACTURER_CODE = 0x43687062   # 'Chpb'
PLUGIN_CODE = 0x43686279         # 'Chby'
VST3_CID = "ABCDEF01" + "9182FAEB" + "%08X%08X" % (MANUFACTURER_CODE, PLUGIN_CODE)

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
    boolean("lcd", "LCD On", True)
    choice("bass_mod", "CGB Bass Mod", ["stock", "x10", "x47"], 0)
    boolean("vol_edges", "Volume Writes At Edges", False)
    boolean("declick", "De-click", False)
    real("declick_ms", "De-click ms", 0.5, 5.0, 2.0, "ms")
    boolean("soften", "Soften Master Pops", False)
    choice("tick_source", "Tick Source", ["Host", "V-blank", "Custom"], 0)
    integer("ticks_per_beat", "Ticks Per Beat", 1, 48, 24)
    real("tick_hz", "Tick Rate", 1.0, 240.0, 60.0, "Hz")
    boolean("link", "Link Mode", False)
    boolean("hex", "Hex Display", False)

    # addChannelParameters, channels 1-4 with withSource = true
    kinds = ["pulse1", "pulse2", "wave", "noise"]
    names = ["PU1 ", "PU2 ", "WAV ", "NOI "]
    source_default = {"pulse1": 0, "pulse2": 2, "wave": 3, "noise": 4}
    instrument_default = {"pulse1": 1, "pulse2": 3, "wave": 7, "noise": 11}
    for ch, kind in enumerate(kinds):
        px, nm = "ch%d_" % (ch + 1), names[ch]
        pulse = kind in ("pulse1", "pulse2")
        wave = kind == "wave"
        noise = kind == "noise"
        pu1 = kind == "pulse1"
        sources = ["Omni"] + ["MIDI %d" % i for i in range(1, 17)] + ["Off"]
        choice(px + "source", nm + "Source", sources, source_default[kind])
        integer(px + "instrument", nm + "Instrument", 0, 128, instrument_default[kind], "0 = none")
        integer(px + "table", nm + "Table", 0, 64, 0, "0 = the instrument's")
        if wave:
            integer(px + "level", nm + "Level", 0, 4, 4, "0 mute, 1 25%, 2 50%, 3 100%, 4 = the instrument's")
        else:
            integer(px + "level", nm + "Level", 0, 16, 16, "16 = the instrument's")
        choice(px + "pan", nm + "Pan", ["off", "L", "R", "both", "inst"], 4)
        if wave:
            integer(px + "wave", nm + "Wave", 0, 64, 0, "0 = the instrument's")
            integer(px + "frame", nm + "Frame", 0, 16, 0, "0 = automatic")
        integer(px + "transpose", nm + "Transpose", -60, 60, 0, "semitones")
        integer(px + "detune", nm + "Detune", -128, 127, 0, "period units")
        integer(px + "vib_speed", nm + "Vibrato Speed", 0, 15, 0, "0 = the instrument's")
        integer(px + "vib_depth", nm + "Vibrato Depth", 0, 16, 16, "16 = the instrument's")
        integer(px + "arp", nm + "Arpeggio", 0, 64, 0, "0 = none, else a table slot")
        if pulse or noise:
            integer(px + "env_vol", nm + "Envelope Volume", 0, 16, 16, "16 = the instrument's")
            choice(px + "env_dir", nm + "Envelope Direction", ["down", "up", "inst"], 2)
            integer(px + "env_rate", nm + "Envelope Rate", 0, 8, 8, "8 = the instrument's")
        if pulse:
            choice(px + "duty", nm + "Duty", ["12.5%", "25%", "50%", "75%", "inst"], 4)
        if pu1:
            integer(px + "sweep_rate", nm + "Sweep Rate", 0, 8, 8, "8 = the instrument's")
            choice(px + "sweep_dir", nm + "Sweep Direction", ["up", "down", "inst"], 2)
            integer(px + "sweep_shift", nm + "Sweep Shift", 0, 8, 8, "8 = the instrument's")
        if noise:
            choice(px + "lfsr", nm + "LFSR", ["15-bit", "7-bit", "inst"], 2)
        boolean(px + "live_follow", nm + "Live Follow", False)
        choice(px + "velocity", nm + "Velocity", ["start volume", "instrument bank", "ignored"], 0)
        boolean(px + "keyswitch", nm + "Keyswitches", False)

    assert len(table) == 85, len(table)
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

# Events are tuples (tick, priority, channel, kind, a, b). The priority orders
# events that share a tick: keyswitches select before notes start, bends and
# controllers precede the note they shape, note-offs precede note-ons.
PRIO_KEYSWITCH, PRIO_BEND, PRIO_CC, PRIO_OFF, PRIO_ON = 0, 1, 2, 3, 4

PU1, PU2, WAV, NOI = 0, 1, 2, 3
BEND_CENTRE = 8192

# Factory bank slots (Source/core/Bank/Bank.cpp)
SLOT_SQUARE_LEAD, SLOT_PLUCK, SLOT_BASS25 = 1, 2, 3
SLOT_TRIANGLE_BASS, SLOT_SAW, SLOT_ORGAN_FRAMES, SLOT_TRI_TO_SAW = 7, 8, 9, 10
SLOT_KICK, SLOT_SNARE, SLOT_HAT_CLOSED, SLOT_HAT_OPEN, SLOT_CRASH = 11, 12, 13, 14, 15

# Keyswitch octaves (Source/core/Driver/Driver.cpp, keyswitchBase): notes
# base..base+11 select slots 1..12 and never sound. Pulse channels: base 24.
# Wave, kit and noise channels: base 12.
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

NOTE_GAP = 10                 # ticks between a note-off and the next note-on

# Chords per bar: A minor, F, C, G, repeated.
CHORD_ROOTS = [57, 53, 48, 55]                     # A3 F3 C3 G3 (lead register - 12)


def chord_of_bar(n):
    return CHORD_ROOTS[(n - 1) % 4]


class Song:
    def __init__(self):
        self.events = []

    def note(self, ch, pitch, start, length, velocity):
        on = ticks(start)
        off = max(on + 1, ticks(start + length) - NOTE_GAP)
        self.events.append((on, PRIO_ON, ch, "on", pitch, velocity))
        self.events.append((off, PRIO_OFF, ch, "off", pitch, 0))

    def keyswitch(self, ch, slot, at_tick):
        base = KEYSWITCH_BASE_PULSE if ch in (PU1, PU2) else KEYSWITCH_BASE_OTHER
        assert 1 <= slot <= 12
        note = base + slot - 1
        self.events.append((at_tick, PRIO_KEYSWITCH, ch, "on", note, 100))
        self.events.append((at_tick + 30, PRIO_KEYSWITCH, ch, "off", note, 0))

    def cc(self, ch, number, value, at_tick):
        self.events.append((at_tick, PRIO_CC, ch, "cc", number, max(0, min(127, int(round(value))))))

    def bend(self, ch, value, at_tick):
        v = max(0, min(16383, int(round(BEND_CENTRE + value))))
        self.events.append((at_tick, PRIO_BEND, ch, "bend", v & 0x7F, v >> 7))

    def bend_ramp(self, ch, start_beat, end_beat, from_value, to_value, step_ticks=40):
        t0, t1 = ticks(start_beat), ticks(end_beat)
        t = t0
        while t <= t1:
            x = (t - t0) / float(t1 - t0) if t1 > t0 else 1.0
            self.bend(ch, from_value + (to_value - from_value) * x, t)
            t += step_ticks
        if (t1 - t0) % step_ticks:
            self.bend(ch, to_value, t1)

    def sorted_events(self):
        return sorted(self.events)


def mod_wheel_value(beat):
    """CC1 curve for bars 5-8: up over bars 5-6, full through bar 7, down over bar 8."""
    if beat < bar(5) or beat >= bar(9):
        return 0
    if beat < bar(7):
        return 127.0 * (beat - bar(5)) / (bar(7) - bar(5))
    if beat < bar(8):
        return 127.0
    return 127.0 * (1.0 - (beat - bar(8)) / (bar(9) - bar(8)))


def build_song():
    s = Song()

    # --- PU1: the lead -----------------------------------------------------
    lead = 100   # velocity -> envelope start volume 12 of 15
    # Bars 1-4: the theme.
    theme = [
        (69, 0, .5), (72, .5, .5), (76, 1, .5), (81, 1.5, .5), (79, 2, 1), (76, 3, 1),
        (77, 4, .5), (76, 4.5, .5), (74, 5, .5), (72, 5.5, .5), (74, 6, 2),
        (76, 8, .5), (79, 8.5, .5), (84, 9, .5), (83, 9.5, .5), (79, 10, 1), (76, 11, 1),
        (74, 12, .5), (71, 12.5, .5), (67, 13, .5), (71, 13.5, .5), (74, 14, 1), (76, 15, 1),
    ]
    for pitch, start, length in theme:
        s.note(PU1, pitch, bar(1, start), length, lead)
    # Bars 5-8: long notes under the mod wheel (CC1 = vibrato depth 0-15).
    sustained = [
        (81, 0, 2), (76, 2, 2),
        (77, 4, 2), (72, 6, 1), (74, 7, 1),
        (76, 8, 3), (79, 11, 1),
        (74, 12, 2), (71, 14, 1), (67, 15, 1),
    ]
    for pitch, start, length in sustained:
        b = bar(5, start)
        s.note(PU1, pitch, b, length, lead)
        # the depth in force right after the note starts (a new note latches
        # the instrument's own depth, so the wheel is re-sent)
        s.cc(PU1, 1, mod_wheel_value(b), ticks(b) + 5)
    beat = bar(5)
    while beat < bar(9):
        s.cc(PU1, 1, mod_wheel_value(beat), ticks(beat))
        beat += 0.25
    s.cc(PU1, 1, 0, ticks(bar(9)))
    # Bars 9-12: eighth notes; the duty changes per bar through automation.
    eighths = [
        (69, 0), (69, .5), (72, 1), (76, 1.5), (81, 2), (79, 2.5), (76, 3), (72, 3.5),
        (77, 4), (77, 4.5), (81, 5), (77, 5.5), (76, 6), (74, 6.5), (72, 7), (74, 7.5),
        (76, 8), (76, 8.5), (79, 9), (84, 9.5), (83, 10), (79, 10.5), (76, 11), (79, 11.5),
        (74, 12), (71, 12.5), (67, 13), (71, 13.5), (74, 14), (76, 15), (79, 15.5),
    ]
    for pitch, start in eighths:
        length = 1.0 if start == 14 else 0.5
        s.note(PU1, pitch, bar(9, start), length, lead)
    # Bars 13-16: pitch bends (range +-2 semitones).
    s.bend(PU1, 0, 0)
    s.bend(PU1, 0, ticks(bar(13)) - 20)
    s.note(PU1, 79, bar(13, 0), 2, lead)                       # G5 bent up to A5
    s.bend_ramp(PU1, bar(13, 0), bar(13, 1), 0, 8191)
    s.bend(PU1, 0, ticks(bar(13, 2)) - 20)
    s.note(PU1, 76, bar(13, 2), 2, lead)                       # E5 falling to D5
    s.bend_ramp(PU1, bar(13, 3), bar(14, 0), 0, -8191)
    s.bend(PU1, 0, ticks(bar(14, 0)) - 20)
    s.note(PU1, 77, bar(14, 0), 1, lead)
    s.note(PU1, 81, bar(14, 1), 1, lead)
    s.note(PU1, 84, bar(14, 2), 2, lead)                       # C6 easing down to B5
    s.bend_ramp(PU1, bar(14, 3), bar(15, 0), 0, -4096)
    s.bend(PU1, -8191, ticks(bar(15, 0)) - 20)                 # start a whole tone low ...
    s.note(PU1, 76, bar(15, 0), 2, lead)
    s.bend_ramp(PU1, bar(15, 0), bar(15, 1), -8191, 0)         # ... and scoop into E5
    s.bend(PU1, 0, ticks(bar(15, 2)) - 20)
    s.note(PU1, 79, bar(15, 2), 1, lead)
    s.note(PU1, 76, bar(15, 3), 1, lead)
    s.note(PU1, 74, bar(16, 0), 1.5, lead)
    s.note(PU1, 81, bar(16, 1.5), 2.5, lead)                   # the last note dives out
    s.bend_ramp(PU1, bar(16, 3), bar(17, 0), 0, -8191)

    # --- PU2: the bass -----------------------------------------------------
    # Velocity is the envelope start volume: 127 -> 15, 112 -> 14, 104 -> 13,
    # 88 -> 11, 72 -> 9.
    for n in range(1, BARS + 1):
        root = chord_of_bar(n) - 12                            # A2 F2 C2 G2 register
        fifth, octave = root + 7, root + 12
        if 5 <= n <= 8:
            pattern = [(root, 0, 1.5, 127), (root, 1.5, .5, 88), (fifth, 2, 1, 104), (root, 3, 1, 112)]
        else:
            pattern = [(root, 0, .5, 127), (root, .5, .5, 88), (fifth, 1, .5, 104), (root, 1.5, .5, 72),
                       (octave, 2, .5, 112), (root, 2.5, .5, 72), (fifth, 3, .5, 104), (root, 3.5, .5, 88)]
        for pitch, start, length, vel in pattern:
            s.note(PU2, pitch, bar(n, start), length * 0.9, vel)

    # --- WAV: the wave bass ------------------------------------------------
    # Keyswitches (base 12 on the wave channel) choose the instrument per
    # section: 18 -> slot 7 Triangle bass, 21 -> slot 10 Tri to saw,
    # 20 -> slot 9 Organ frames.
    s.keyswitch(WAV, SLOT_TRIANGLE_BASS, 0)
    s.keyswitch(WAV, SLOT_TRI_TO_SAW, ticks(bar(5)) - 30)
    s.keyswitch(WAV, SLOT_TRIANGLE_BASS, ticks(bar(9)) - 30)
    s.keyswitch(WAV, SLOT_ORGAN_FRAMES, ticks(bar(13)) - 30)
    for n in range(1, BARS + 1):
        root = chord_of_bar(n) - 24                            # A1 F1 C2 G1 register
        fifth, octave = root + 7, root + 12
        if n <= 4:
            pattern = [(root, 0, 2), (fifth, 2, 1), (root, 3, 1)]
        elif n <= 8:
            pattern = [(root, 0, 2), (fifth, 2, 2)]
        elif n <= 12:
            pattern = [(root, 0, .5), (root, .5, .5), (fifth, 1, .5), (root, 1.5, .5),
                       (octave, 2, .5), (root, 2.5, .5), (fifth, 3, .5), (root, 3.5, .5)]
        elif n < 16:
            pattern = [(root, 0, 2), (fifth, 2, 2)]
        else:
            pattern = [(root, 0, 2), (root, 2, 2)]
        for pitch, start, length in pattern:
            s.note(WAV, pitch, bar(n, start), length * 0.95, 100)

    # --- NOI: the drums ----------------------------------------------------
    # The keyswitch (base 12 on the noise channel) selects Kick, slot 11, as
    # the base; velocity zones of 8 pick the drum relative to it.
    for n in (1, 5, 9, 13):
        s.keyswitch(NOI, SLOT_KICK, 0 if n == 1 else ticks(bar(n)) - 30)
    for n in range(1, BARS + 1):
        hits = []
        if n in (5, 9, 13):
            hits.append((0, DRUM_CRASH, 0.95))                 # rings until the snare
        else:
            hits.append((0, DRUM_KICK, .45))
            hits.append((.5, DRUM_HAT_CLOSED, .45))
        hits.append((1, DRUM_SNARE, .45))
        hits.append((1.5, DRUM_HAT_CLOSED, .2 if n % 4 == 2 else .45))
        if n % 4 == 2:
            hits.append((1.75, DRUM_KICK, .2))
        hits.append((2, DRUM_KICK, .45))
        hits.append((2.5, DRUM_HAT_CLOSED, .45))
        if n % 4 == 0:                                          # a snare fill
            hits += [(3, DRUM_SNARE, .2), (3.25, DRUM_SNARE, .2), (3.5, DRUM_SNARE, .2), (3.75, DRUM_SNARE, .2)]
        else:
            hits.append((3, DRUM_SNARE, .45))
            hits.append((3.5, DRUM_HAT_OPEN, .45))
        for start, (note, vel), length in hits:
            s.note(NOI, note, bar(n, start), length, vel)

    return s


# ---------------------------------------------------------------------------
# the automation
# ---------------------------------------------------------------------------

class Envelope:
    def __init__(self, pid, points):
        self.pid = pid
        self.points = points   # (seconds, parameter value), value held until the next point


EARLY = 0.01   # seconds before a bar line, so a value is in force for the bar's first note


def build_envelopes():
    b = lambda n: seconds(bar(n)) - EARLY
    return [
        Envelope("ch1_source", [(0, 1)]),                       # PU1 stops being omni: MIDI 1
        Envelope("ch3_keyswitch", [(0, 1)]),
        Envelope("ch4_keyswitch", [(0, 1)]),
        Envelope("ch4_velocity", [(0, 1)]),                     # instrument bank
        Envelope("ch2_instrument", [(0, SLOT_PLUCK)]),
        Envelope("ch1_duty", [(0, 4), (b(9), 0), (b(10), 1), (b(11), 2), (b(12), 3), (b(13), 4)]),
        Envelope("ch2_env_rate", [(0, 8), (b(9), 1), (b(11), 4), (b(13), 8)]),
        Envelope("ch3_wave", [(0, 0), (b(9), 2), (b(10), 6), (b(13), 0)]),
        Envelope("ch3_frame", [(0, 0), (b(10), 1), (b(11), 3), (b(12), 6), (b(13), 0)]),
        Envelope("master_l", [(0, 7), (seconds(bar(8, 2)), 5), (seconds(bar(8, 3)), 3), (b(9), 7)]),
        Envelope("master_r", [(0, 7), (seconds(bar(8, 2)), 5), (seconds(bar(8, 3)), 3), (b(9), 7)]),
        Envelope("declick", [(0, 0), (b(14), 1), (b(15), 0)]),
        Envelope("model", [(0, 0), (b(15), 1), (b(16), 2)]),
    ]


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


def vst_chunk_lines():
    """Reaper's state chunk for a VST3 with no inputs and a stereo output, and
    no saved plugin state (the plugin loads its defaults; the envelopes set
    everything the demo needs). Layout: id, magic 0xFEED5EEE, input count and
    masks, output count and masks, state size, 1, 0x10FFFF; then the empty
    program and preset names and the terminator."""
    number = reaper_vst3_number(VST3_CID)
    header = struct.pack("<IIII", number, 0xFEED5EEE, 0, 2)
    header += struct.pack("<QQ", 1, 2)
    header += struct.pack("<III", 0, 1, 0x0010FFFF)
    trailer = bytes([0, 0]) + struct.pack("<I", 0x10)
    return [base64.b64encode(header).decode("ascii"), base64.b64encode(trailer).decode("ascii")]


def write_rpp(path, song, envelopes, table):
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
    w('  <TRACK %s' % guid("track"))
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
    w('    TRACKID %s' % guid("track"))
    w('    PERF 0')
    w('    MIDIOUT -1')
    w('    MAINSEND 1 0')
    w('    <FXCHAIN')
    w('      SHOW 0')
    w('      LASTSEL 0')
    w('      DOCKED 0')
    w('      BYPASS 0 0 0')
    w('      <VST "VST3i: ChipBoy (ChipBoy)" ChipBoy.vst3 0 "" %d{%s} ""' % (number, VST3_CID))
    for line in vst_chunk_lines():
        w('        ' + line)
    w('      >')
    w('      FLOATPOS 0 0 0 0')
    w('      FXID %s' % guid("fx"))
    for env in envelopes:
        index, p = by_id[env.pid]
        w('      <PARMENV %d:%d 0 1 0.5 "%s / ChipBoy"' % (index, vst3_param_id(p.pid), p.name))
        w('        EGUID %s' % guid("env/" + env.pid))
        w('        ACT 1 -1')
        w('        VIS 1 1 1')
        w('        LANEHEIGHT 0 0')
        w('        ARM 0')
        w('        DEFSHAPE 1 -1 -1')
        for t, value in env.points:
            w('        PT %s %s 1' % (fmt(float(t)), fmt(p.normalised(value))))
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
    w('      IGUID %s' % guid("item"))
    w('      IID 1')
    w('      NAME "ChipBoy demo (MIDI channels 1-4)"')
    w('      VOLPAN 1 0 1 -1')
    w('      SOFFS 0 0')
    w('      PLAYRATE 1 1 0 -1 0 0.0025')
    w('      CHANMODE 0')
    w('      GUID %s' % guid("take"))
    w('      <SOURCE MIDI')
    w('        HASDATA 1 %d QN' % PPQ)
    w('        CCINTERP 32')
    w('        POOLEDEVTS %s' % guid("pooledevts"))
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
    w('        GUID %s' % guid("source"))
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


def write_parameters_md(path, table):
    L = []
    w = L.append
    w("# ChipBoy parameters, in host order")
    w("")
    w("Generated by `tools/demo/make_demo.py`. The order is the order")
    w("`Source/plugin/shared/Parameters.cpp` adds the parameters: the globals from")
    w("`addGlobalParameters`, then channels 1-4 from `addChannelParameters`, whose")
    w("per-kind conditionals decide which ids a channel has (only the wave channel")
    w("has Wave and Frame; only pulse channels have Duty; only PU1 has the sweep;")
    w("only NOI has LFSR). JUCE presents the list to VST3 hosts in exactly this")
    w("order; the wrapper's own Bypass parameter and, after it, its MIDI CC")
    w("emulation parameters follow the 85 below. `chipboy_paramdump` (a CMake target")
    w("of the plugin build) prints the same table from the running code, and")
    w("`make_demo.py --paramdump <path>` checks this file against it.")
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
    w("Per-channel parameters carry an extra position meaning \"use the instrument's")
    w("value\" (the top value of Level, Vibrato Depth, Envelope Volume/Rate, Sweep")
    w("Rate/Shift; the `inst` choice of Pan, Envelope Direction, Duty, Sweep")
    w("Direction, LFSR; 0 of Table, Wave, Vibrato Speed). Automation overrides the")
    w("instrument only where a lane is drawn away from that position.")
    w("")
    w("The demo project automates: `ch1_source`, `ch3_keyswitch`, `ch4_keyswitch`,")
    w("`ch4_velocity`, `ch2_instrument`, `ch1_duty`, `ch2_env_rate`, `ch3_wave`,")
    w("`ch3_frame`, `master_l`, `master_r`, `declick`, `model`.")
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
    envelopes = build_envelopes()
    out = os.path.normpath(args.out)
    os.makedirs(out, exist_ok=True)
    write_midi(os.path.join(out, "chipboy_demo.mid"), song)
    write_rpp(os.path.join(out, "ChipBoy Demo.rpp"), song, envelopes, table)
    write_parameters_md(os.path.join(out, "PARAMETERS.md"), table)
    n = len(song.events)
    print("wrote %s: %d MIDI events, %d envelopes, %d parameters" % (out, n, len(envelopes), len(table)))
    ok = True
    if args.paramdump:
        ok = check_against_paramdump(args.paramdump, table)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
