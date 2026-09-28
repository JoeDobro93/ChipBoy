#include "core/Export/MidiExport.h"

#include "core/Driver/Clock.h"
#include "core/Apu/Apu.h"
#include "core/Driver/Driver.h"
#include "core/Tracker/Player.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>

namespace chipboy::midi {

namespace {

constexpr double   kRate = 48000.0;
constexpr uint32_t kBlock = 256;
constexpr uint64_t kMaxFrames = uint64_t(kRate) * 3600 * 4;   ///< four hours: a runaway render stops here

/// One event of a track, before the deltas are taken: at one tick, metas
/// first, then note-offs, then note-ons, so a note that ends where the next
/// begins is off before it is on again.
struct Ev {
    int64_t tick = 0;
    int order = 0;
    std::vector<uint8_t> bytes;
};

void be16(std::vector<uint8_t>& o, uint32_t v) { o.push_back(uint8_t(v >> 8)); o.push_back(uint8_t(v)); }
void be32(std::vector<uint8_t>& o, uint32_t v) { o.push_back(uint8_t(v >> 24)); o.push_back(uint8_t(v >> 16)); o.push_back(uint8_t(v >> 8)); o.push_back(uint8_t(v)); }
void vlq(std::vector<uint8_t>& o, uint32_t v)
{
    uint8_t buf[5]; int n = 0;
    do { buf[n++] = uint8_t(v & 0x7f); v >>= 7; } while (v != 0 && n < 5);
    while (n > 0) { --n; o.push_back(uint8_t(buf[n] | (n > 0 ? 0x80 : 0))); }
}
void meta(std::vector<Ev>& evs, int64_t tick, uint8_t type, const std::vector<uint8_t>& data)
{
    Ev e; e.tick = tick; e.order = 0;
    e.bytes.push_back(0xFF); e.bytes.push_back(type); vlq(e.bytes, uint32_t(data.size()));
    e.bytes.insert(e.bytes.end(), data.begin(), data.end());
    evs.push_back(std::move(e));
}
void text(std::vector<Ev>& evs, int64_t tick, uint8_t type, const std::string& s)
{
    meta(evs, tick, type, std::vector<uint8_t>(s.begin(), s.end()));
}
void writeTrack(std::vector<uint8_t>& out, std::vector<Ev>& evs, int64_t endTick)
{
    std::stable_sort(evs.begin(), evs.end(), [](const Ev& a, const Ev& b) { return a.tick != b.tick ? a.tick < b.tick : a.order < b.order; });
    std::vector<uint8_t> body;
    int64_t at = 0;
    for (const auto& e : evs) {
        const int64_t t = std::max(at, e.tick);
        vlq(body, uint32_t(std::min<int64_t>(t - at, 0x0FFFFFFF)));
        at = t;
        body.insert(body.end(), e.bytes.begin(), e.bytes.end());
    }
    vlq(body, uint32_t(std::min<int64_t>(std::max<int64_t>(0, endTick - at), 0x0FFFFFFF)));
    body.push_back(0xFF); body.push_back(0x2F); body.push_back(0x00);
    out.push_back('M'); out.push_back('T'); out.push_back('r'); out.push_back('k');
    be32(out, uint32_t(body.size()));
    out.insert(out.end(), body.begin(), body.end());
}

const char* channelName(int ch) { return ch == 0 ? "PU1" : ch == 1 ? "PU2" : ch == 2 ? "WAV" : "NOI"; }

} // namespace

std::vector<uint8_t> exportSong(const tracker::Song& in, const bank::Bank& bank, const Options& options, Report* report)
{
    Report local;
    Report& rep = report != nullptr ? *report : local;
    rep = Report{};
    // A copy of the song with its tables built here, so the export never
    // reads what the audio thread is playing and never changes it.
    auto song = std::make_unique<tracker::Song>(in);
    // The file carries the song's chains whatever the tab's note source is
    // set to: every channel plays its tracker here (section 224).
    for (auto& n : song->noteSource) n = tracker::NoteSource::Tracker;
    tracker::normalizeSignatures(*song);
    tracker::buildRowTables(*song);
    tracker::buildTempoMap(*song, song->tempoBpm);
    const int64_t end = tracker::songTicks(*song);
    if (song->rows() == 0 || end <= 0) { rep.notes_.push_back("the song has no rows"); return {}; }
    rep.ticks = end;

    // PPQ from the first signature's beat unit (section 224): 24 for 4/4 at
    // 24 and for 11/8 at 12; anything else gets its own and a note.
    const auto& sig0 = song->signatures.front();
    const int ppq = std::clamp(int(std::lround(double(sig0.ticksPerBeat) * double(sig0.unit) / 4.0)), 1, 960);
    rep.ppq = ppq;
    if (ppq != driver::kTicksPerBeat) rep.notes_.push_back("the first signature's beat unit is not 24 ticks a quarter: the file counts " + std::to_string(ppq) + " ticks a quarter and its tempo is scaled to keep time");
    const double scale = double(ppq) / double(driver::kTicksPerBeat);
    auto mt = [scale](int64_t t) { return int64_t(std::llround(double(t) * scale)); };

    // --- render: the engine offline, a sub-block per tick ------------------
    auto clock = std::make_unique<driver::Clock>();
    auto player = std::make_unique<tracker::Player>();
    auto drv = std::make_unique<driver::Driver>();
    clock->prepare(kRate);
    player->prepare(kRate);
    drv->prepare(kRate, &bank, song.get(), Console::DMG);
    player->setSong(song.get());
    driver::ClockConfig cc;
    cc.source = driver::TempoSource::Song;
    cc.songTempo = std::clamp(song->tempoBpm, driver::kMinSongBpm, driver::kMaxSongBpm);
    cc.lsdjTempo = song->lsdjTempo;
    clock->setConfig(cc);
    if (!song->tempoMap.empty()) clock->setTempoMap(song->tempoMap.data(), song->tempoMap.size());
    clock->setOwnsTransport(true);
    clock->ownPlay();
    for (int ch = 0; ch < 4; ++ch) drv->setParams(ch, driver::ChannelParams{});
    const std::function<uint64_t(uint64_t)> cycleAt = [](uint64_t f) { return uint64_t(double(f) * double(chipboy::kCpuHz) / kRate); };

    std::vector<Ev> track[4];
    int sounding[4] = { 0, 0, 0, 0 };
    auto noteOff = [&](int ch, int64_t tick, int n) { Ev e; e.tick = mt(tick); e.order = 1; e.bytes = { uint8_t(0x80 | ch), uint8_t(n), 0 }; track[ch].push_back(std::move(e)); };
    auto noteOn = [&](int ch, int64_t tick, int n) { Ev e; e.tick = mt(tick); e.order = 2; e.bytes = { uint8_t(0x90 | ch), uint8_t(n), options.velocity }; track[ch].push_back(std::move(e)); ++rep.notes[ch]; };
    auto sample = [&](int64_t tick) {
        for (int ch = 0; ch < 4; ++ch) {
            const int n = drv->soundingNote(ch);
            if (n == sounding[ch]) continue;
            if (sounding[ch]) noteOff(ch, tick, sounding[ch]);
            if (n) noteOn(ch, tick, n);
            sounding[ch] = n;
        }
    };

    std::vector<driver::NoteEvent> events, sub;
    std::vector<driver::RegWrite> writes;
    uint64_t frame = 0;
    bool done = false;
    while (!done && frame < kMaxFrames) {
        driver::Transport t;
        clock->process(t, kBlock, frame);
        for (int ch = 0; ch < 4; ++ch) {
            const int slot = drv->tableGrooveSlot(ch);
            drv->setTableGroove(ch, slot >= 1 && slot <= tracker::kGrooveSlots ? song->grooves[size_t(slot - 1)].ticks.data() : nullptr);
            drv->setViewGroove(ch, player->groove(ch));
        }
        events.clear();
        player->process(clock->ticks(), clock->tickCount(), clock->playing(), events);
        drv->setTickRate(clock->bpm() * double(driver::kTicksPerBeat) / 60.0);
        // Sub-blocks that end on each tick, so the driver's state is read
        // after that tick's work; the block's events go to their sub-block.
        uint32_t a = 0;
        const size_t nTicks = clock->tickCount();
        for (size_t i = 0; i < nTicks; ++i) {
            const auto tp = clock->ticks()[i];
            if (tp.tick >= end) { done = true; break; }
            const uint32_t b = std::min<uint32_t>(kBlock, tp.offset + 1);
            sub.clear();
            for (const auto& e : events) if (e.offset >= a && e.offset < b) { sub.push_back(e); sub.back().offset = e.offset - a; }
            driver::TickPoint one{ tp.offset - a, tp.tick };
            writes.clear();
            drv->process(sub.data(), sub.size(), b - a, frame + a, &one, 1, cycleAt, writes);
            sample(tp.tick);
            a = b;
        }
        if (done) break;
        if (a < kBlock) {
            sub.clear();
            for (const auto& e : events) if (e.offset >= a) { sub.push_back(e); sub.back().offset = e.offset - a; }
            driver::TickPoint none{};
            writes.clear();
            drv->process(sub.data(), sub.size(), kBlock - a, frame + a, &none, 0, cycleAt, writes);
        }
        frame += kBlock;
    }
    for (int ch = 0; ch < 4; ++ch) if (sounding[ch]) { noteOff(ch, end, sounding[ch]); sounding[ch] = 0; }

    // --- the file ------------------------------------------------------------
    const int channels = options.noise ? 4 : 3;
    std::vector<uint8_t> out;
    out.push_back('M'); out.push_back('T'); out.push_back('h'); out.push_back('d');
    be32(out, 6); be16(out, 1); be16(out, uint32_t(1 + channels)); be16(out, uint32_t(ppq));
    // Track 0: the name, the tempo and its T points, the signatures.
    std::vector<Ev> zero;
    text(zero, 0, 0x03, "ChipBoy");
    auto tempoAt = [&](int64_t tick, double bpm) {
        const double us = driver::tickSeconds(std::clamp(bpm, driver::kMinSongBpm, driver::kMaxSongBpm), song->lsdjTempo) * double(ppq) * 1.0e6;
        const uint32_t v = uint32_t(std::clamp(std::llround(us), 1LL, 0xFFFFFFLL));
        meta(zero, mt(tick), 0x51, { uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v) });
    };
    tempoAt(0, song->tempoBpm);
    for (const auto& p : song->tempoMap) if (p.tick > 0) tempoAt(p.tick, p.bpm);
    for (const auto& s : song->signatures) {
        int dd = 0; int unit = 1;
        while (unit * 2 <= int(s.unit)) { unit *= 2; ++dd; }
        if (unit != int(s.unit) && (int(s.unit) - unit) > (unit * 2 - int(s.unit))) { unit *= 2; ++dd; }
        if (unit != int(s.unit)) rep.notes_.push_back("the signature at tick " + std::to_string(s.tick) + " has a beat unit of " + std::to_string(int(s.unit)) + ", which MIDI cannot name; " + std::to_string(unit) + " is written");
        const uint8_t cc2 = uint8_t(std::clamp(96 / std::max(1, unit), 1, 255));
        meta(zero, mt(s.tick), 0x58, { uint8_t(s.beats), uint8_t(dd), cc2, 8 });
    }
    writeTrack(out, zero, mt(end));
    for (int ch = 0; ch < channels; ++ch) {
        text(track[ch], 0, 0x03, channelName(ch));
        writeTrack(out, track[ch], mt(end));
    }
    return out;
}

} // namespace chipboy::midi
