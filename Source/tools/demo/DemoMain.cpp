// chipboy_demo -- renders a short built-in tune through the APU core and the
// analog model to a WAV file. The first thing you can hear (spec section 17,
// M2). Usage: chipboy_demo out.wav [--cgb] [--no-noise] [--rate 48000]
#include "core/Analog/AnalogModel.h"
#include "tools/harness/Script.h"
#include "tools/harness/Wav.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace chipboy;
using namespace chipboy::harness;

namespace {

uint16_t pulseReg(double hz) { return uint16_t(std::lround(2048.0 - 131072.0 / hz)); }
double   midiHz(int n)       { return 440.0 * std::pow(2.0, (n - 69) / 12.0); }

std::vector<Write> tune()
{
    std::vector<Write> s;
    auto at = [&](double t, uint16_t a, uint8_t v) { s.push_back({uint64_t(t * kCpuHz), a, v}); };
    auto freq = [&](double t, uint16_t lo, uint16_t hi, double hz, uint8_t bits) {
        const uint16_t f = pulseReg(hz);
        at(t, lo, uint8_t(f & 0xFF));
        at(t, hi, uint8_t(bits | (f >> 8)));
    };
    const double step = 0.11;

    // Wave RAM: a triangle. Written before the channel is enabled.
    static const uint8_t tri[16] = { 0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF,
                                     0xFE, 0xDC, 0xBA, 0x98, 0x76, 0x54, 0x32, 0x10 };
    for (int i = 0; i < 16; ++i) at(0.0, uint16_t(0xFF30 + i), tri[i]);
    at(0.0, 0xFF25, 0xFF);
    at(0.0, 0xFF24, 0x77);

    // Four bars: Am Am F G.  Bass roots (MIDI), chord tones for the arpeggio.
    const int roots[4] = { 45, 45, 41, 43 };
    const int chords[4][3] = { {69, 72, 76}, {69, 72, 76}, {65, 69, 72}, {67, 71, 74} };
    // Lead, one note per step (0 = rest, -1 = tie).
    const int lead[64] = {
        81, -1, 79, -1, 76, -1, 79, 81, -1, -1, 84, -1, 81, -1, 79, -1,
        76, -1, -1, 72, 74, -1, 76, -1, 79, -1, 76, -1, 74, -1, 72, -1,
        69, -1, 72, -1, 76, -1, 77, -1, 81, -1, 77, -1, 76, -1, 72, -1,
        74, -1, 79, -1, 83, -1, 86, -1, 83, -1, 79, -1, 74, -1, 0, 0 };

    for (int i = 0; i < 64; ++i) {
        const double t = 0.02 + i * step;
        const int bar = i / 16, pos = i % 16;

        // CH1 lead: volume 11, decay 2, duty 50% (25% on the last bar).
        if (lead[i] > 0) {
            at(t, 0xFF11, uint8_t(bar == 3 ? 0x40 : 0x80));
            at(t, 0xFF12, 0xB2);
            freq(t, 0xFF13, 0xFF14, midiHz(lead[i]), 0x80);
        } else if (lead[i] == 0) {
            at(t, 0xFF12, 0x00);                    // rest: DAC off, which holds the level
        }

        // CH2 arpeggio: chord tones cycling every step, 12.5% duty, quick decay.
        at(t + 0.001, 0xFF16, 0x00);
        at(t + 0.001, 0xFF17, 0x71);
        freq(t + 0.001, 0xFF18, 0xFF19, midiHz(chords[bar][pos % 3] + 12), 0x80);

        // CH3 bass: the wave channel, re-triggered every beat with the DAC
        // toggled -- the DMG way, click included.
        if (pos % 4 == 0) {
            at(t + 0.002, 0xFF1A, 0x00);
            at(t + 0.0022, 0xFF1A, 0x80);
            at(t + 0.0022, 0xFF1C, 0x20);
            freq(t + 0.0022, 0xFF1D, 0xFF1E, midiHz(roots[bar] + (pos == 8 ? 7 : 0)) * 2.0, 0x80);
        }

        // CH4 drums: kick on the beat, hat off it, snare on 2 and 4.
        if (pos % 4 == 0)       { at(t + 0.003, 0xFF21, 0xF2); at(t + 0.003, 0xFF22, 0x63); at(t + 0.003, 0xFF23, 0x80); }
        else if (pos % 8 == 4)  { at(t + 0.003, 0xFF21, 0xD3); at(t + 0.003, 0xFF22, 0x44); at(t + 0.003, 0xFF23, 0x80); }
        else if (pos % 2 == 1)  { at(t + 0.003, 0xFF21, 0x71); at(t + 0.003, 0xFF22, 0x1C); at(t + 0.003, 0xFF23, 0x80); }

        // Mixer: the lead wanders across the stereo field in bar 3, and the
        // master volume drops a notch for the last two beats.
        if (bar == 2 && pos == 0)  at(t + 0.005, 0xFF25, 0xEF);   // lead right only
        if (bar == 2 && pos == 8)  at(t + 0.005, 0xFF25, 0xFE);   // lead left only
        if (bar == 3 && pos == 0)  at(t + 0.005, 0xFF25, 0xFF);
        if (bar == 3 && pos == 8)  at(t + 0.005, 0xFF24, 0x44);
    }
    const double end = 0.02 + 64 * step;
    // Stop every channel by disabling its DAC: on this hardware that holds
    // the level, so it is the silent way out (writing NRx2 = $08 instead
    // would trigger the zombie-mode volume glitch).
    at(end, 0xFF12, 0x00); at(end, 0xFF17, 0x00); at(end, 0xFF21, 0x00);
    at(end + 0.3, 0xFF1A, 0x00);
    return s;
}

} // namespace

int main(int argc, char** argv)
{
    std::string out;
    bool cgb = false, noise = true;
    double rate = 48000.0;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--cgb")) cgb = true;
        else if (!std::strcmp(argv[i], "--no-noise")) noise = false;
        else if (!std::strcmp(argv[i], "--rate") && i + 1 < argc) rate = std::atof(argv[++i]);
        else if (argv[i][0] != '-') out = argv[i];
    }
    if (out.empty()) {
        std::fprintf(stderr, "usage: chipboy_demo out.wav [--cgb] [--no-noise] [--rate 48000]\n");
        return 2;
    }
    const auto model = cgb ? AnalogModel::cgb() : AnalogModel::dmg();
    const double seconds = 0.02 + 64 * 0.11 + 1.0;
    const uint64_t frames = uint64_t(seconds * rate);
    const auto pcm = renderScript(tune(), model, rate, frames, [](uint64_t) { return 512; }, noise);
    // Rail units: four aligned channels reach +-4. Leave headroom.
    if (!writeWav16(out, pcm, 2, int(rate), 0.18f)) {
        std::fprintf(stderr, "could not write %s\n", out.c_str());
        return 1;
    }
    std::printf("%s: %.2f s, %s, noise %s, %.0f Hz\n", out.c_str(), seconds, cgb ? "CGB" : "DMG",
                noise ? "on" : "off", rate);
    return 0;
}
