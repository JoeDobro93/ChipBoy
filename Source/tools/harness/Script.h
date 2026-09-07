// ChipBoy tools -- register scripts: a list of cycle-stamped writes, and a
// block-by-block render of one through the APU and the fast renderer. Used
// by the tests (block-size determinism, golden files) and by chipboy_demo.
#pragma once

#include "core/Analog/AnalogModel.h"
#include "core/Apu/Apu.h"
#include "core/Render/Renderer.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace chipboy::harness {

struct Write {
    uint64_t cycle;
    uint16_t addr;
    uint8_t  value;
};

/// Apply `script` (sorted by cycle) while rendering `frames` host frames in
/// blocks whose sizes come from `blockSize(blockIndex)`. Returns interleaved
/// stereo. Also returns the APU's complete event streams through `events`
/// and `mix` when those pointers are given, for the reference path.
std::vector<float> renderScript(const std::vector<Write>& script, const AnalogModel& model,
                                double sampleRate, uint64_t frames,
                                const std::function<int(uint64_t)>& blockSize, bool noise,
                                std::vector<ApuEvent>* events = nullptr,
                                std::vector<MixEvent>* mix = nullptr);

/// A dense, deterministic script exercising all four channels, DAC toggles,
/// mixer changes and the noise channel at its fastest -- the test material.
std::vector<Write> testScript();

} // namespace chipboy::harness
