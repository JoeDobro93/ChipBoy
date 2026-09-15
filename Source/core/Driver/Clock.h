// ChipBoy -- the clock (docs/COMMANDS_AND_TEMPO.md section 4).
//
// One place that says where the ticks are and where the tracker is. Ticks are
// always 24 per beat. Two sources:
//
//   Host  the host's tempo. A tick sits at every multiple of 1/24 beat of the
//         host's beat position, so scrubbing is exact. The host contributes
//         the tempo and nothing else -- not its signature: the song's own
//         time is its phrases' lengths and grooves, per channel, and the
//         host's bars are a ruler beside it (sections 19 and 25).
//   Song  the song's own tempo: a base tempo plus T commands at known ticks.
//         The position at a host time is the integral of that map from the
//         song start, computed from the song alone -- a jump into the middle
//         lands on the step playing through would have reached, and a
//         playback ROM could reproduce it.
//
// With the transport stopped both sources free-run at the current tempo, so
// live playing still has tables and vibrato.
//
// Plain C++20: no allocation, nothing but the standard library.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace chipboy::driver {

constexpr int    kTicksPerBeat = 24;        ///< always, as LSDj (6 per sixteenth straight)
constexpr size_t kMaxTicksPerBlock = 512;
constexpr size_t kMaxTempoPoints = 64;      ///< T changes the clock integrates; the rest are ignored

// --- the ROM's 358 Hz grid (docs/COMMANDS_AND_TEMPO.md section 160) ---------
//
// Six timer interrupts a video frame, 11712 cycles apart and the sixth to the
// next frame's first 11664: instant n sits at gridCycle(n) from the timeline's
// cycle 0, averaging 11704 cycles. The driver's pitch clock runs on it, and
// every tick lands on the first instant strictly after its nominal frame.
constexpr uint64_t kGridCpuHz = 4194304;
constexpr uint64_t kGridFrameCycles = 70224;
constexpr uint64_t kGridStepCycles = 11712;
constexpr uint64_t kGridPerFrame = 6;
constexpr uint64_t kGridMeanCycles = kGridFrameCycles / kGridPerFrame;   ///< 11704
constexpr uint64_t gridCycle(uint64_t n) { return kGridFrameCycles * (n / kGridPerFrame) + kGridStepCycles * (n % kGridPerFrame); }
/// The first instant strictly after cycle c.
constexpr uint64_t gridAfter(uint64_t c)
{
    const uint64_t f = c / kGridFrameCycles, i = (c % kGridFrameCycles) / kGridStepCycles + 1;
    return i >= kGridPerFrame ? (f + 1) * kGridPerFrame : f * kGridPerFrame + i;
}
/// The first frame at or after instant n, at a sample rate.
inline uint64_t gridFrame(uint64_t n, double sampleRate)
{
    return uint64_t(std::ceil(double(gridCycle(n)) * sampleRate / double(kGridCpuHz) - 1e-9));
}
/// Where a tick due at a nominal frame fires: the frame of the first instant after it.
inline uint64_t gridTickFrame(double nominalFrame, double sampleRate)
{
    const double c = std::max(0.0, nominalFrame) * double(kGridCpuHz) / sampleRate;
    return gridFrame(gridAfter(uint64_t(std::floor(c))), sampleRate);
}
/// Section 165: the ROM's own placement. Its accumulator fires song tick s at
/// the last instant at or before the nominal **end** of the tick (the start of
/// s + 1): the row after play is a tick late, and which ticks are the long
/// ones follows floor((s + 1) * T), not floor(s * T) + 1. `periodFrames` is
/// the tick's own length.
inline uint64_t gridRomTickFrame(double nominalFrame, double periodFrames, double sampleRate)
{
    const double c = std::max(0.0, nominalFrame + periodFrames) * double(kGridCpuHz) / sampleRate;
    const uint64_t n = gridAfter(uint64_t(std::floor(c)));
    return gridFrame(n > 0 ? n - 1 : 0, sampleRate);
}
/// The tick period at a tempo. With `rom` (section 160, `Song::lsdjTempo`) a
/// whole-number BPM in 40..295 takes the ROM's tempo word, round(1834828.8 /
/// BPM), and a tick is that many 2048ths of the grid's mean step. Otherwise,
/// and for any other tempo -- the host's, a fraction -- it is 60 / (24 * BPM).
inline double tickSeconds(double bpm, bool rom)
{
    if (bpm <= 0.0) bpm = 120.0;
    const double r = std::round(bpm);
    if (!rom || std::fabs(bpm - r) > 1e-9 || r < 40.0 || r > 295.0) return 60.0 / (bpm * kTicksPerBeat);
    const double word = std::floor(2048.0 * 2.5 * double(kGridCpuHz) / double(kGridMeanCycles) / r + 0.5);
    return word / 2048.0 * double(kGridMeanCycles) / double(kGridCpuHz);
}

enum class TempoSource : uint8_t { Host = 0, Song = 1 };

/// What the host says at the start of a block.
struct Transport {
    bool   valid = false;        ///< the host gave a tempo and a beat position
    bool   playing = false;
    double bpm = 120.0;
    double ppq = 0.0;            ///< at the block start
    double seconds = 0.0;        ///< host time at the block start
    bool   timeValid = false;    ///< `seconds` is real
    // The host's time signature is not here on purpose: it contributes the
    // tempo only, and a song's time is its phrases' lengths and grooves, per
    // channel (sections 19 and 25).
};

/// The tempo from an absolute tick on: one per T cell, the base being the
/// Song tempo parameter until the first of them (docs/COMMANDS_AND_TEMPO.md
/// sections 2 and 4). A point at tick 0 is a T cell there, and does override
/// the parameter from the song's start.
struct TempoPoint { int64_t tick = 0; double bpm = 120.0; };

/// A tick inside a block: where it lands and which tick it is.
struct TickPoint { uint32_t offset = 0; int64_t tick = 0; };

struct ClockConfig {
    TempoSource source = TempoSource::Host;
    double songTempo = 120.0;        ///< Song source: the base tempo, the Song tempo parameter
    double songStartSeconds = 0.0;   ///< host time where song tick 0 sits
    bool   lsdjTempo = false;        ///< Song source: the tick is the ROM's tempo word (section 160)
};

class Clock {
public:
    void prepare(double sampleRate);
    void reset();

    void setConfig(const ClockConfig& c);
    const ClockConfig& config() const { return cfg_; }
    /// The song's T cells, sorted by tick. Copied into a fixed buffer, so
    /// the caller's list may go away and this stays allocation-free. The base
    /// tempo is ClockConfig::songTempo, not part of this list.
    void setTempoMap(const TempoPoint* pts, size_t n);

    // --- the plugin's own transport (docs/COMMANDS_AND_TEMPO.md section 16)
    //
    // With no host play head -- the Standalone, or a host that offers no
    // position -- the plugin runs the song itself: the clock makes the
    // transport at the Song tempo, from the song start, and loops between two
    // ticks the caller names (the song knows where its rows are, the clock
    // does not). The tempo source is Song while it owns the transport.
    void setOwnsTransport(bool on);
    bool ownsTransport() const { return owns_; }
    void ownPlay();                       ///< from the loop start, or tick 0
    void ownStop();
    bool ownPlaying() const { return ownPlaying_; }
    void setLoop(bool on, int64_t startTick, int64_t endTick);
    bool loopOn() const { return loop_; }
    int64_t loopStart() const { return loopStart_; }
    int64_t loopEnd() const { return loopEnd_; }

    /// One block. Fills the tick list; nothing else about the block is kept.
    void process(const Transport& host, uint32_t numSamples, uint64_t frameAbs);
    /// Whether the transport was running this block, whoever owns it.
    bool playing() const { return isPlaying_; }

    const TickPoint* ticks() const { return ticks_.data(); }
    size_t   tickCount() const { return tickCount_; }
    int64_t  tickAtBlockStart() const { return blockStartTick_; }
    /// The tempo in force at the block start.
    double   bpm() const { return bpm_; }

    /// Song source: the tracker position at a host time, straight from the
    /// tempo map. This is what a locate lands on.
    double ticksAtSeconds(double seconds) const;
    /// Its inverse.
    double secondsAtTicks(double tick) const;
    /// The tempo in force at an absolute tick.
    double bpmAtTick(int64_t tick) const;

private:
    void rebuild();
    double rateAt(size_t i) const;    ///< ticks per second in segment i
    size_t segmentForTick(double tick) const;
    size_t segmentForSeconds(double sec) const;
    void   pushTick(uint32_t offset, int64_t tick);
    /// A tick due at a nominal absolute frame lands on the grid (section 160):
    /// pushed if its frame is in the block, carried into the next one if not.
    void   place(double nominalFrame, int64_t tick, uint64_t frameAbs, uint64_t blockEnd, double periodFrames = -1.0);
    void   takeCarried(uint64_t frameAbs, uint64_t blockEnd);
    void   freeRun(double framesPerTick, uint32_t numSamples, uint64_t frameAbs);

    double sampleRate_ = 48000.0;
    ClockConfig cfg_;
    std::array<TempoPoint, kMaxTempoPoints> map_{};   ///< tick 0 first, then one per T
    std::array<double, kMaxTempoPoints> sec_{};       ///< seconds from the song start to map_[i].tick
    std::array<TempoPoint, kMaxTempoPoints> raw_{};   ///< the caller's list, to spot a real change
    size_t mapCount_ = 1, rawCount_ = 0;
    bool   mapDirty_ = true;
    bool   baseIsCell_ = false;   ///< a T cell sits at tick 0, so it owns the base, not the parameter

    std::array<TickPoint, kMaxTicksPerBlock> ticks_{};
    size_t  tickCount_ = 0;
    int64_t blockStartTick_ = 0;
    double  bpm_ = 120.0;

    // Song source: tick(s) = ticksAtSeconds(s) + offset_. The offset is zero
    // until the tempo changes while playing, when it holds the position the
    // integral would otherwise jump by (section 4: automate the parameter and
    // the plugin integrates; locate and it re-anchors).
    double  offset_ = 0.0;
    double  nextSeconds_ = 0.0;   ///< host time the next block should start at
    double  nextTick_ = 0.0;      ///< and the position it should start at
    bool    running_ = false;

    // a tick the grid pushed past the block's end, for the next block if it
    // follows on (section 160)
    bool     haveCarried_ = false;
    uint64_t carriedFrame_ = 0, carriedEnd_ = 0;
    int64_t  carriedTick_ = 0;

    // free-running phase while the transport is stopped
    uint64_t lastTickFrame_ = 0;
    bool     haveFreeTick_ = false;
    int64_t  freeTick_ = 0;

    // the plugin's own transport
    bool     owns_ = false, ownPlaying_ = false, isPlaying_ = false;
    double   ownSeconds_ = 0.0;      ///< its own clock, in the same units as a host's
    bool     loop_ = false;
    int64_t  loopStart_ = 0, loopEnd_ = 0;
};

} // namespace chipboy::driver
