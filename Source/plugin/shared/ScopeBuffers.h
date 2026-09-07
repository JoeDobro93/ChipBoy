// ChipBoy -- what the audio thread leaves for the scopes (UI_DESIGN
// section 3). Single writer, readers take snapshots; no locks.
#pragma once

#include "core/Link/LinkLayout.h"

#include <array>
#include <atomic>
#include <cstdint>

namespace chipboy::plugin {

constexpr uint32_t kAudioRing = 16384;   ///< frames of rendered output kept for the master scope

/// Rendered output, stereo, in rail units (before the trim).
struct AudioRing {
    std::atomic<uint32_t> head{ 0 };
    std::array<float, kAudioRing> l{}, r{};
    double sampleRate = 48000.0;

    void push(const float* L, const float* R, uint32_t n)
    {
        uint32_t h = head.load(std::memory_order_relaxed);
        for (uint32_t i = 0; i < n; ++i) { const uint32_t k = (h + i) & (kAudioRing - 1); l[k] = L[i]; r[k] = R ? R[i] : L[i]; }
        head.store(h + n, std::memory_order_release);
    }
    /// Copy the newest `count` frames (oldest first). Returns the count copied.
    uint32_t snapshot(float* outL, float* outR, uint32_t count) const
    {
        for (int attempt = 0; attempt < 4; ++attempt) {
            const uint32_t h1 = head.load(std::memory_order_acquire);
            const uint32_t n = count < kAudioRing - 1024 ? count : kAudioRing - 1024;
            const uint32_t avail = h1 < n ? h1 : n;
            for (uint32_t i = 0; i < avail; ++i) { const uint32_t k = (h1 - avail + i) & (kAudioRing - 1); outL[i] = l[k]; if (outR) outR[i] = r[k]; }
            if (head.load(std::memory_order_acquire) - h1 < kAudioRing - n) return avail;
        }
        return 0;
    }
};

/// Everything the editor and the visualizer read.
struct ScopeBuffers {
    std::array<link::ScopeRing, 4> channels;     ///< digital traces, (cycle, level)
    AudioRing master;
    std::atomic<uint64_t> latestCycle{ 0 };       ///< APU cycle at the end of the last block
    std::atomic<uint64_t> latestFrame{ 0 };       ///< absolute frame at the end of the last block
    std::array<std::atomic<uint64_t>, 4> state{};  ///< link::packState per channel
    std::array<std::atomic<uint64_t>, 4> state2{}; ///< link::packState2: the running state
    std::atomic<uint32_t> mix{ 0 };               ///< NR50 | NR51 << 8 | powered << 16
};

} // namespace chipboy::plugin
