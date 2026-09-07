// ChipBoy -- the analog output stage's constants, per console.
//
// Everything here is either measured (HARDWARE_REFERENCE.md section 12) or an
// estimate that says so. Units: one channel's DAC swings +-1.0 between digital
// 15 and digital 0 ("rail units"); four channels at master volume 7 therefore
// sum to +-4.0.
#pragma once

#include <cstdint>

namespace chipboy {

enum class Console : uint8_t { DMG = 0, CGB = 1 };

struct AnalogModel {
    Console console = Console::DMG;

    /// One-pole AC-coupling high-pass, as a per-CPU-cycle factor. Measured.
    double couplingPerCycle = 0.9999629793785858;

    /// Amplifier bandwidth as a one-pole low-pass. Only a lower bound was
    /// measured (>= 39 kHz, interface-limited); this is an estimate above it,
    /// chosen so that a too-low guess cannot dull the audible band.
    double ampCutoffHz = 80000.0;

    /// Soft clip, in rail units. NOT measured: no clipping was observed with
    /// four channels at full scale because their peaks never aligned. Set so
    /// it only engages above three and a half aligned channels.
    double clipKnee = 3.5;
    double clipLimit = 4.5;

    /// Noise floor, rail units, from the LCD-on capture. The measured total
    /// RMS (0-96 kHz) is split between a white floor and three lines at their
    /// measured prominences (section 12.4).
    double hissRms = 0.0;         ///< white floor, RMS over 0-96 kHz
    double lcdLineHz = 4194304.0 / 456.0;   ///< 9198.4 Hz, the LCD line rate
    double lcdLineAmp = 0.0;      ///< sine amplitude at lcdLineHz
    double lcdLine2Amp = 0.0;     ///< ... and at twice it
    double frameHz = 4194304.0 / 70224.0;   ///< 59.73 Hz, the frame rate
    double frameAmp = 0.0;

    static AnalogModel dmg()
    {
        AnalogModel m;
        m.console = Console::DMG;
        m.couplingPerCycle = 0.9999629793785858;   // tau 6.44 ms, fc 24.7 Hz
        // -70.7 dBFS RMS against a 0.0552 rail; lines +26 / +20 / +20 dB.
        m.hissRms = 5.15e-3;
        m.lcdLineAmp = 1.39e-3;
        m.lcdLine2Amp = 0.70e-3;
        m.frameAmp = 0.70e-3;
        return m;
    }
    static AnalogModel cgb()
    {
        AnalogModel m;
        m.console = Console::CGB;
        m.couplingPerCycle = 0.999493525022113;    // tau 0.471 ms, fc 338 Hz (this unit)
        // -71.8 dBFS RMS against a 0.054 rail; lines +43 / +24 / +34 dB, which
        // dominate the total, so the white floor is what remains.
        m.hissRms = 2.72e-3;
        m.lcdLineAmp = 5.2e-3;
        m.lcdLine2Amp = 0.58e-3;
        m.frameAmp = 1.85e-3;
        return m;
    }
    static AnalogModel forConsole(Console c) { return c == Console::CGB ? cgb() : dmg(); }
};

} // namespace chipboy
