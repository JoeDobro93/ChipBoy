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
    /// The table run per channel (docs/COMMANDS_AND_TEMPO.md section 32),
    /// packed by packTableRun(): which table is running, the row it is on and
    /// a serial that counts the runs, so a window showing one table can
    /// highlight the row of the run that started last.
    std::array<std::atomic<uint32_t>, 4> tableRun{};
};

/// row + 1 (0 = no table running) | slot << 7 | run << 14.
inline uint32_t packTableRun(const driver::VoiceView& v)
{
    const uint32_t row = v.tableRow < 0 ? 0u : uint32_t(v.tableRow + 1) & 127u;
    return row | (uint32_t(v.tableSlot & 127) << 7) | (uint32_t(v.tableRun) << 14);
}
/// What packTableRun() packed: `row` is -1 when no table is running.
inline void unpackTableRun(uint32_t packed, int& slot, int& row, uint32_t& run)
{
    row = int(packed & 127u) - 1;
    slot = int((packed >> 7) & 127u);
    run = packed >> 14;
    if (row < 0) slot = 0;
}

} // namespace chipboy::plugin
