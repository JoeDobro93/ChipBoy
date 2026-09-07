// ChipBoy tools -- minimal WAV writer (16-bit PCM). Not part of the core.
#pragma once

#include <string>
#include <vector>

namespace chipboy::harness {

/// Write interleaved float samples as 16-bit PCM. Values are scaled by `gain`
/// and clipped to +-1 before conversion. Returns false if the file could not
/// be written.
bool writeWav16(const std::string& path, const std::vector<float>& interleaved,
                int channels, int sampleRate, float gain = 1.0f);

} // namespace chipboy::harness
