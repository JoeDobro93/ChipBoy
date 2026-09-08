#include "core/Driver/Clock.h"

#include <algorithm>
#include <cmath>

namespace chipboy::driver {

namespace {
constexpr double kEps = 1e-9;
constexpr double kTickEps = 1e-6;   ///< a tick boundary is the same number in both blocks
double clampBpm(double b) { return std::clamp(b, 1.0, 400.0); }
}

void Clock::prepare(double sampleRate)
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    reset();
}

void Clock::reset()
{
    tickCount_ = 0; blockStartTick_ = 0;
    offset_ = 0.0; nextSeconds_ = 0.0; nextTick_ = 0.0; running_ = false;
    lastTickFrame_ = 0; haveFreeTick_ = false; freeTick_ = 0;
    mapDirty_ = true;
}

void Clock::setConfig(const ClockConfig& c)
{
    if (std::fabs(c.songTempo - cfg_.songTempo) > kEps || std::fabs(c.songStartSeconds - cfg_.songStartSeconds) > kEps) mapDirty_ = true;
    cfg_ = c;
}

void Clock::setTempoMap(const TempoPoint* pts, size_t n)
{
    // Called every block: rebuild only when the list actually differs.
    const size_t take = pts ? std::min(n, kMaxTempoPoints - 1) : 0;
    bool same = !mapDirty_ && take == rawCount_;
    for (size_t i = 0; same && i < take; ++i)
        if (raw_[i].tick != pts[i].tick || std::fabs(raw_[i].bpm - pts[i].bpm) > kEps) same = false;
    if (same) return;

    rawCount_ = take;
    for (size_t i = 0; i < take; ++i) raw_[i] = pts[i];
    map_[0] = { 0, clampBpm(cfg_.songTempo) };
    mapCount_ = 1;
    baseIsCell_ = false;
    for (size_t i = 0; i < take; ++i) {
        const TempoPoint p { raw_[i].tick, clampBpm(raw_[i].bpm) };
        if (p.tick <= 0) { map_[0].bpm = p.bpm; baseIsCell_ = true; continue; }   // a T cell at tick 0 owns the base
        if (p.tick <= map_[mapCount_ - 1].tick) continue;                 // unsorted or repeated: the first wins
        map_[mapCount_++] = p;
    }
    rebuild();
}

void Clock::rebuild()
{
    sec_[0] = 0.0;
    for (size_t i = 1; i < mapCount_; ++i)
        sec_[i] = sec_[i - 1] + double(map_[i].tick - map_[i - 1].tick) / rateAt(i - 1);
    mapDirty_ = false;
}

double Clock::rateAt(size_t i) const { return map_[std::min(i, mapCount_ - 1)].bpm * kTicksPerBeat / 60.0; }

size_t Clock::segmentForTick(double tick) const
{
    size_t i = 0;
    while (i + 1 < mapCount_ && double(map_[i + 1].tick) <= tick) ++i;
    return i;
}

size_t Clock::segmentForSeconds(double s) const
{
    size_t i = 0;
    while (i + 1 < mapCount_ && sec_[i + 1] <= s) ++i;
    return i;
}

double Clock::ticksAtSeconds(double seconds) const
{
    const double r = seconds - cfg_.songStartSeconds;
    if (r <= 0.0) return r * rateAt(0);                  // before the song start: the base tempo, backwards
    const size_t i = segmentForSeconds(r);
    return double(map_[i].tick) + (r - sec_[i]) * rateAt(i);
}

double Clock::secondsAtTicks(double tick) const
{
    if (tick <= 0.0) return cfg_.songStartSeconds + tick / rateAt(0);
    const size_t i = segmentForTick(tick);
    return cfg_.songStartSeconds + sec_[i] + (tick - double(map_[i].tick)) / rateAt(i);
}

double Clock::bpmAtTick(int64_t tick) const { return map_[segmentForTick(double(tick))].bpm; }

int Clock::barTicks() const { return std::max(1, int(std::lround(barBeats_ * kTicksPerBeat))); }

void Clock::pushTick(uint32_t offset, int64_t tick)
{
    if (tickCount_ >= kMaxTicksPerBlock) return;
    ticks_[tickCount_++] = { offset, tick };
}

void Clock::freeRun(double framesPerTick, uint32_t numSamples, uint64_t frameAbs)
{
    // Stopped: ticks come from the absolute frame count at the current tempo,
    // never accumulated per block (spec section 7.1).
    const uint64_t blockEnd = frameAbs + numSamples;
    double k = haveFreeTick_ ? std::floor((double(lastTickFrame_) + framesPerTick) / framesPerTick)
                             : std::ceil(double(frameAbs) / framesPerTick);
    for (; tickCount_ < kMaxTicksPerBlock; k += 1.0) {
        const uint64_t f = uint64_t(std::llround(k * framesPerTick));
        if (f >= blockEnd) break;
        if (f >= frameAbs) { pushTick(uint32_t(f - frameAbs), freeTick_++); lastTickFrame_ = f; haveFreeTick_ = true; }
    }
}

void Clock::process(const Transport& t, uint32_t numSamples, uint64_t frameAbs)
{
    tickCount_ = 0;
    if (mapDirty_) { if (!baseIsCell_) map_[0].bpm = clampBpm(cfg_.songTempo); rebuild(); }

    const bool song = cfg_.source == TempoSource::Song;
    barBeats_ = song ? std::max(0.25, cfg_.beatsPerBar) : (t.valid ? std::max(0.25, t.beatsPerBar) : 4.0);
    const double hostBpm = t.valid && t.bpm > 1.0 ? t.bpm : bpm_;
    const bool playing = t.valid && t.playing && (!song || t.timeValid);

    if (!playing) {
        // Free-running, so a note played with the transport stopped still has
        // tables and vibrato -- the tempo is the host's, or the song's.
        bpm_ = clampBpm(song ? cfg_.songTempo : hostBpm);
        running_ = false;
        freeRun(sampleRate_ * 60.0 / (bpm_ * kTicksPerBeat), numSamples, frameAbs);
        blockStartTick_ = tickCount_ ? ticks_[0].tick : freeTick_;
        return;
    }
    haveFreeTick_ = false;      // the free-running phase restarts when the transport stops

    if (!song) {
        // Host: tick k sits at ppq k/24, exactly.
        bpm_ = clampBpm(hostBpm);
        const double ppqPerFrame = bpm_ / 60.0 / sampleRate_;
        const double ppqStart = t.ppq, ppqEnd = ppqStart + numSamples * ppqPerFrame;
        blockStartTick_ = int64_t(std::floor(ppqStart * kTicksPerBeat + kTickEps));
        const double tickEnd = ppqEnd * kTicksPerBeat;
        for (double k = std::ceil(ppqStart * kTicksPerBeat - kTickEps); k < tickEnd - kTickEps && tickCount_ < kMaxTicksPerBlock; k += 1.0) {
            const double f = (k / kTicksPerBeat - ppqStart) / ppqPerFrame;
            const uint32_t off = uint32_t(std::max(0.0, std::floor(f + 1e-6)));
            if (off >= numSamples) break;
            pushTick(off, int64_t(std::llround(k)));
        }
        freeTick_ = blockStartTick_;
        running_ = true;
        return;
    }

    // Song: the position is the tempo map's integral from the song start.
    // A locate lands where the map says; a tempo change while playing keeps
    // the position continuous and integrates on from there (section 4).
    const double s0 = t.seconds;
    const double s1 = s0 + double(numSamples) / sampleRate_;
    const double mapped = ticksAtSeconds(s0);
    const double jumpTol = std::max(0.001, 0.5 * double(numSamples) / sampleRate_);
    offset_ = (!running_ || std::fabs(s0 - nextSeconds_) > jumpTol) ? 0.0 : nextTick_ - mapped;
    running_ = true;

    const double tickStart = mapped + offset_;
    const double tickEnd = ticksAtSeconds(s1) + offset_;
    blockStartTick_ = int64_t(std::floor(tickStart));
    bpm_ = bpmAtTick(int64_t(std::floor(std::max(0.0, mapped))));
    // The block owns the ticks in [tickStart, tickEnd), compared in the tick
    // domain: the next block starts from the same number, so a tick on a block
    // boundary is emitted once and never twice.
    for (double k = std::ceil(tickStart - kTickEps); k < tickEnd - kTickEps && tickCount_ < kMaxTicksPerBlock; k += 1.0) {
        const double f = (secondsAtTicks(k - offset_) - s0) * sampleRate_;
        const uint32_t off = uint32_t(std::clamp(std::floor(f + 1e-6), 0.0, double(numSamples ? numSamples - 1 : 0)));
        pushTick(off, int64_t(std::llround(k)));
    }
    nextSeconds_ = s1;
    nextTick_ = tickEnd;
    freeTick_ = blockStartTick_;
}

} // namespace chipboy::driver
