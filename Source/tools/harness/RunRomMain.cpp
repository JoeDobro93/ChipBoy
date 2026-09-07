// chipboy_runrom -- run one ROM and print what it reports, or render its
// audio through the analog model to a WAV file.
//
//   chipboy_runrom rom.gb [seconds]
//   chipboy_runrom rom.gb seconds --wav out.wav [--cgb] [--no-noise] [--rate 48000]
#include "core/Analog/AnalogModel.h"
#include "core/Render/Renderer.h"
#include "tools/harness/Machine.h"
#include "tools/harness/Wav.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

int main(int argc, char** argv)
{
    std::string romPath, wavPath;
    double secs = 60.0, rate = 48000.0;
    bool cgb = false, noise = true, haveSecs = false;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--wav") && i + 1 < argc) wavPath = argv[++i];
        else if (!std::strcmp(argv[i], "--rate") && i + 1 < argc) rate = std::stod(argv[++i]);
        else if (!std::strcmp(argv[i], "--cgb")) cgb = true;
        else if (!std::strcmp(argv[i], "--no-noise")) noise = false;
        else if (romPath.empty()) romPath = argv[i];
        else if (!haveSecs) { secs = std::stod(argv[i]); haveSecs = true; }
    }
    if (romPath.empty()) {
        std::fprintf(stderr, "usage: chipboy_runrom rom.gb [seconds] [--wav out.wav] [--cgb] [--no-noise] [--rate hz]\n");
        return 2;
    }
    auto rom = chipboy::harness::Machine::loadRom(romPath);
    if (rom.empty()) { std::fprintf(stderr, "cannot read %s\n", romPath.c_str()); return 2; }
    chipboy::harness::Machine m(std::move(rom));

    if (!wavPath.empty()) {
        if (!haveSecs) secs = 10.0;
        chipboy::render::Renderer r;
        r.prepare(rate, cgb ? chipboy::AnalogModel::cgb() : chipboy::AnalogModel::dmg(), 512);
        r.setNoise(noise);
        const uint64_t frames = uint64_t(secs * rate);
        std::vector<float> pcm(size_t(frames) * 2), L(512), R(512);
        for (uint64_t f = 0; f < frames; f += 512) {
            const int n = int(std::min<uint64_t>(512, frames - f));
            m.stepUntil(r.cycleForFrame(f + uint64_t(n)));
            r.render(m.bus().apu(), L.data(), R.data(), n);
            for (int k = 0; k < n; ++k) { pcm[(size_t(f) + size_t(k)) * 2] = L[size_t(k)]; pcm[(size_t(f) + size_t(k)) * 2 + 1] = R[size_t(k)]; }
        }
        if (!chipboy::harness::writeWav16(wavPath, pcm, 2, int(rate), 0.22f)) {
            std::fprintf(stderr, "could not write %s\n", wavPath.c_str());
            return 1;
        }
        std::printf("%s: %.1f s of %s rendered as %s (%s, noise %s)\n", wavPath.c_str(), secs, romPath.c_str(),
                    cgb ? "CGB" : "DMG", noise ? "on" : "off", noise ? "on" : "off");
        return 0;
    }

    auto r = m.run(uint64_t(secs * chipboy::kCpuHz));
    std::printf("%s\n", romPath.c_str());
    std::printf("  finished: %s   code: %u   cycles: %llu (%.2f s)\n",
                r.finished ? "yes" : "NO (timed out)", unsigned(r.code),
                (unsigned long long)r.cycles, double(r.cycles) / chipboy::kCpuHz);
    if (!r.text.empty())   std::printf("  text:\n%s\n", r.text.c_str());
    if (!r.serial.empty()) std::printf("  serial:\n%s\n", r.serial.c_str());
    return (r.finished && r.code == 0) ? 0 : 1;
}
