#include "tools/harness/Wav.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>

namespace chipboy::harness {

namespace {
void put16(std::ofstream& f, uint16_t v) { char b[2] = { char(v & 0xFF), char(v >> 8) }; f.write(b, 2); }
void put32(std::ofstream& f, uint32_t v) { char b[4] = { char(v & 0xFF), char((v >> 8) & 0xFF), char((v >> 16) & 0xFF), char(v >> 24) }; f.write(b, 4); }
} // namespace

bool writeWav16(const std::string& path, const std::vector<float>& x, int channels, int sampleRate, float gain)
{
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    const uint32_t dataBytes = uint32_t(x.size() * 2);
    f.write("RIFF", 4); put32(f, 36 + dataBytes); f.write("WAVE", 4);
    f.write("fmt ", 4); put32(f, 16); put16(f, 1); put16(f, uint16_t(channels));
    put32(f, uint32_t(sampleRate)); put32(f, uint32_t(sampleRate * channels * 2));
    put16(f, uint16_t(channels * 2)); put16(f, 16);
    f.write("data", 4); put32(f, dataBytes);
    for (float v : x) {
        const float c = std::clamp(v * gain, -1.0f, 1.0f);
        put16(f, uint16_t(int16_t(std::lround(c * 32767.0f))));
    }
    return bool(f);
}

} // namespace chipboy::harness
