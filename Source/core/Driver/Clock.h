// ChipBoy -- the clock (docs/COMMANDS_AND_TEMPO.md section 4).
//
// One place that says where the ticks are and where the tracker is. Ticks are
// always 24 per beat. Two sources:
//
//   Host  the host's tempo. A tick sits at every multiple of 1/24 beat of the
//         host's beat position, so scrubbing is exact and the tracker's bars
//         are the host's bars.
//   Song  the song's own tempo: a base tempo plus T commands at known ticks.
//         The position at a host time is the integral of that map from the
//         song start, computed from the song alone -- a jump to bar 9 lands on
//         the step playing through would have reached, and a playback ROM
//         could reproduce it.
//
// With the transport stopped both sources free-run at the current tempo, so
// live playing still has tables and vibrato.
//
// Plain C++20: no allocation, nothing but the standard library.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace chipboy::driver {

constexpr int    kTicksPerBeat = 24;        ///< always, as LSDj (6 per sixteenth straight)
constexpr size_t kMaxTicksPerBlock = 512;
constexpr size_t kMaxTempoPoints = 64;      ///< T changes the clock integrates; the rest are ignored

enum class TempoSource : uint8_t { Host = 0, Song = 1 };

/// What the host says at the start of a block.
struct Transport {
    bool   valid = false;        ///< the host gave a tempo and a beat position
    bool   playing = false;
    double bpm = 120.0;
    double ppq = 0.0;            ///< at the block start
    double seconds = 0.0;        ///< host time at the block start
    bool   timeValid = false;    ///< `seconds` is real
    double beatsPerBar = 4.0;    ///< from the host's time signature
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
    double beatsPerBar = 4.0;        ///< Song source; Host takes the host's signature
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

    /// One block. Fills the tick list; nothing else about the block is kept.
    void process(const Transport& t, uint32_t numSamples, uint64_t frameAbs);

    const TickPoint* ticks() const { return ticks_.data(); }
    size_t   tickCount() const { return tickCount_; }
    int64_t  tickAtBlockStart() const { return blockStartTick_; }
    /// Beats per bar in force (the host's signature, or the song's).
    double   beatsPerBar() const { return barBeats_; }
    /// Ticks in a bar, the unit the Player counts in.
    int      barTicks() const;
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
    double  barBeats_ = 4.0;
    double  bpm_ = 120.0;

    // Song source: tick(s) = ticksAtSeconds(s) + offset_. The offset is zero
    // until the tempo changes while playing, when it holds the position the
    // integral would otherwise jump by (section 4: automate the parameter and
    // the plugin integrates; locate and it re-anchors).
    double  offset_ = 0.0;
    double  nextSeconds_ = 0.0;   ///< host time the next block should start at
    double  nextTick_ = 0.0;      ///< and the position it should start at
    bool    running_ = false;

    // free-running phase while the transport is stopped
    uint64_t lastTickFrame_ = 0;
    bool     haveFreeTick_ = false;
    int64_t  freeTick_ = 0;
};

} // namespace chipboy::driver
