// ChipBoy -- the brute-force reference renderer (spec section 7, 16.3).
//
// Ticks every CPU cycle: builds each side's analog value per cycle, then
// integrates it against the kernel's step response for every working sample.
// Nothing here is shared with the fast path except the kernel table, the
// clip function and the decimator taps, so a null between the two proves the
// step placement, block handling and mixer arithmetic of Renderer. Tests and
// offline bounces only: it is thousands of times slower than the fast path.
#pragma once

#include "core/Analog/AnalogModel.h"
#include "core/Apu/Apu.h"

#include <cstdint>
#include <vector>

namespace chipboy::render {

struct Reference {
    /// Render `cycles` cycles of the two event streams (cycle order) at a
    /// host rate; noise floor off. Output length is the number of host
    /// frames the cycles cover.
    static void render(const std::vector<ApuEvent>& events, const std::vector<MixEvent>& mix,
                       uint64_t cycles, const AnalogModel& model, double hostSampleRate,
                       std::vector<float>& outL, std::vector<float>& outR);
};

} // namespace chipboy::render
