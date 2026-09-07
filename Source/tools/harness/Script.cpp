#include "tools/harness/Script.h"

#include <algorithm>

namespace chipboy::harness {

std::vector<float> renderScript(const std::vector<Write>& script, const AnalogModel& model,
                                double sampleRate, uint64_t frames,
                                const std::function<int(uint64_t)>& blockSize, bool noise,
                                std::vector<ApuEvent>* events, std::vector<MixEvent>* mix)
{
    Apu apu;
    render::Renderer r;
    r.prepare(sampleRate, model, 8192);
    r.setNoise(noise);
    std::vector<float> out(size_t(frames) * 2, 0.0f);
    std::vector<float> L, R;
    size_t w = 0;
    uint64_t done = 0, block = 0;
    while (done < frames) {
        const int n = int(std::min<uint64_t>(uint64_t(std::max(1, blockSize(block++))), frames - done));
        const uint64_t cEnd = r.cycleForFrame(done + uint64_t(n));
        while (w < script.size() && script[w].cycle <= cEnd) {
            apu.runTo(script[w].cycle);
            apu.write(script[w].addr, script[w].value);
            ++w;
        }
        apu.runTo(cEnd);
        if (L.size() < size_t(n)) { L.resize(size_t(n)); R.resize(size_t(n)); }
        if (events) events->insert(events->end(), apu.events().begin(), apu.events().end());
        if (mix) mix->insert(mix->end(), apu.mixEvents().begin(), apu.mixEvents().end());
        r.render(apu, L.data(), R.data(), n);
        for (int k = 0; k < n; ++k) { out[(size_t(done) + size_t(k)) * 2] = L[size_t(k)]; out[(size_t(done) + size_t(k)) * 2 + 1] = R[size_t(k)]; }
        done += uint64_t(n);
    }
    return out;
}

std::vector<Write> testScript()
{
    std::vector<Write> s;
    auto at = [&](double seconds, uint16_t addr, uint8_t v) {
        s.push_back({uint64_t(seconds * kCpuHz), addr, v});
    };
    auto freq = [&](double seconds, uint16_t lo, uint16_t hi, uint16_t f, uint8_t hiBits) {
        at(seconds, lo, uint8_t(f & 0xFF));
        at(seconds, hi, uint8_t(hiBits | (f >> 8)));
    };
    // Wave RAM: a ramp, written while the channel is off.
    for (int i = 0; i < 16; ++i)
        at(0.0, uint16_t(0xFF30 + i), uint8_t((((2 * i) & 15) << 4) | ((2 * i + 1) & 15)));
    at(0.0, 0xFF1A, 0x80);
    at(0.0, 0xFF1C, 0x20);

    double t = 0.001;
    const uint16_t notes[] = { 1046, 1253, 1379, 1546, 1650, 1750, 1798, 1899 };
    for (int step = 0; step < 24; ++step, t += 0.011) {
        const uint16_t f = notes[step % 8];
        // CH1: envelope, alternating duty, sweep on every fourth note
        at(t, 0xFF10, uint8_t(step % 4 == 0 ? 0x23 : 0x00));
        at(t, 0xFF11, uint8_t(((step & 3) << 6) | 0x10));
        at(t, 0xFF12, uint8_t(0xF0 | (1 + step % 3)));
        freq(t, 0xFF13, 0xFF14, f, 0x80);
        // CH2: a fifth below, length-limited, quieter
        at(t + 0.002, 0xFF16, 0xB8);
        at(t + 0.002, 0xFF17, 0xA2);
        freq(t + 0.002, 0xFF18, 0xFF19, uint16_t(f - 300), 0xC0);
        // CH3: re-trigger with a DAC off/on around a wave RAM change every other step
        if (step % 2 == 0) {
            at(t + 0.004, 0xFF1A, 0x00);
            at(t + 0.0041, uint16_t(0xFF30 + (step / 2) % 16), uint8_t(0xF0 >> (step % 4)));
            at(t + 0.0042, 0xFF1A, 0x80);
            at(t + 0.0042, 0xFF1C, uint8_t((1 + (step / 2) % 3) << 5));
        }
        freq(t + 0.0043, 0xFF1D, 0xFF1E, uint16_t(f - 600), 0x80);
        // CH4: alternating short bursts, one at the fastest clock
        at(t + 0.006, 0xFF21, uint8_t(step % 3 == 0 ? 0xF1 : 0x91));
        at(t + 0.006, 0xFF22, uint8_t(step % 4 == 0 ? 0x00 : step % 4 == 1 ? 0x38 : step % 4 == 2 ? 0x51 : 0x2C));
        at(t + 0.006, 0xFF23, 0x80);
        // Mixer: pan and master volume moves
        if (step % 6 == 3) at(t + 0.008, 0xFF25, uint8_t(step % 12 == 3 ? 0x1E : 0xF3));
        if (step % 8 == 5) at(t + 0.009, 0xFF24, uint8_t(step % 16 == 5 ? 0x34 : 0x77));
    }
    // A DAC-off (hold), a DAC-on at the held level (silent) and at another level.
    at(t, 0xFF12, 0x08);
    at(t + 0.01, 0xFF12, 0xF0);
    at(t + 0.01, 0xFF14, 0x87);
    std::stable_sort(s.begin(), s.end(), [](const Write& a, const Write& b) { return a.cycle < b.cycle; });
    return s;
}

} // namespace chipboy::harness
