// ChipBoy -- the wave synth (docs/COMMANDS_AND_TEMPO.md section 33).
//
// A frame is 32 samples of 4 bits, so a wave is a cycle of 32 points and its
// spectrum is sixteen harmonics. The synth builds one from a source, runs it
// through a chain of shapers, and quantises it back to the chip's sixteen
// levels; a run of frames morphs the numbers from a start state to an end
// state, so a run sweeps a filter or a pulse width the way LSDj's start and
// end waves do.
//
// It is core code: no JUCE, no allocation beyond the frames it hands back,
// and deterministic -- the same Synth renders the same bytes on every
// platform, because the filters are per-harmonic gains of a 32-point
// transform rather than a running filter whose state depends on where it
// started.
#pragma once

#include "core/Bank/Bank.h"

#include <vector>

namespace chipboy::bank {

/// How many frames a Synth's run holds: its `frames`, clamped to 1-16 and to
/// what fits after `first`.
int synthFrameCount(const Synth& s);
/// The slot frame the run starts at: `first`, clamped to 0-15.
int synthFirstFrame(const Synth& s);

/// One frame of the run. `index` is 0 .. synthFrameCount - 1 and picks the
/// morph position: 0 is the start state exactly, the last frame is the end
/// state exactly, and a one-frame run is the start. `drawn` is the wave the
/// Drawn source reads -- the slot's own first frame -- and is ignored by
/// every other source.
Frame synthesizeFrame(const Synth& s, const Frame& drawn, int index);

/// The whole run, `synthFrameCount(s)` frames, oldest first. `out` is
/// resized to the count.
void synthesize(const Synth& s, const Frame& drawn, std::vector<Frame>& out);

/// Generate's write (section 36): the run goes into the wave's frames from
/// `synthFirstFrame(s)` on, the wave grows to reach the run's last frame when
/// it is shorter (the new frames before the run copy its last frame), and
/// every frame outside the run stays as it was. Never more than kMaxFrames.
void synthWriteRun(const Synth& s, const std::vector<Frame>& run, Wave& w);

/// The letters a panel puts on the controls. Never null.
const char* synthSourceName(SynthSource s);
/// What a shape is, for a tooltip. Never null.
const char* synthSourceHelp(SynthSource s);
const char* synthShaperName(SynthShaper s);
/// What a shaper's amount does, for a tooltip: whether its sign matters and
/// what the number means. Never null.
const char* synthShaperHelp(SynthShaper s);
/// Whether this shaper reads the stage's resonance (the four filters do).
bool synthShaperHasResonance(SynthShaper s);
/// Whether the source reads the state's width (Square) or its partials
/// (Additive), so a panel can show only the fields that do something.
bool synthSourceHasWidth(SynthSource s);
bool synthSourceHasPartials(SynthSource s);

} // namespace chipboy::bank
