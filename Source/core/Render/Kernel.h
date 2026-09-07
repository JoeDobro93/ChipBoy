// ChipBoy -- the band-limited step (BLEP) kernel.
//
// One continuous-time kernel serves both renderers: a Kaiser-windowed sinc at
// the working rate, convolved with the amplifier's one-pole low-pass so the
// analog bandwidth is exact rather than approximated per sample. The table
// holds its step response S(d) and the one-sample difference T(d) = S(d) -
// S(d - 1) on a fine grid; the fast path adds h * T at each step, the
// reference path integrates S over every CPU cycle (spec section 7).
#pragma once

#include <vector>

namespace chipboy::render {

struct Kernel {
    int phases = 256;     ///< table entries per working sample
    int head = 8;         ///< samples of look-ahead the step is delayed by
    int span = 0;         ///< T(d) is nonzero for d in (-head, -head + span)
    double sampleRate = 0;
    std::vector<float> step;    ///< S(-head + i / phases), i = 0 .. span * phases
    std::vector<float> delta;   ///< T(-head + i / phases), same grid

    /// Design for a working sample rate. `ampCutoffHz` is the one-pole
    /// low-pass folded in; `stopDb` the windowed sinc's stopband.
    static Kernel design(double fsWorking, double ampCutoffHz,
                         int halfLength = 8, int phases = 256, double stopDb = 96.0);

    /// S at an offset in samples, linearly interpolated: 0 before the table,
    /// 1 after it.
    double stepAt(double d) const;
};

/// Kaiser-windowed sinc low-pass: `taps` (odd) coefficients summing to 1,
/// cutoff in cycles per sample, stopband attenuation in dB. Used for the
/// decimator by both renderers.
std::vector<float> designLowpass(int taps, double cutoff, double stopDb);

} // namespace chipboy::render
