#include "core/Tracker/Player.h"

#include <algorithm>
#include <cmath>

namespace chipboy::tracker {

using driver::NoteEvent;
using driver::TickPoint;

/* ------------------------------------------------------------- the grid */

void stepStartTicks(const Song& s, const Phrase* p, uint8_t groove, int* start)
{
    // A bar is beatsPerBar x 24 ticks; a straight step is its share; the groove
    // scales alternate steps by a/6 and b/6, the way LSDj's grooves do.
    const int steps = s.steps();
    const double barTicks = s.barTicks();
    const double base = barTicks / steps;
    uint8_t slot = groove;
    if (slot == kGrooveNone) slot = p ? p->groove : 0;
    Groove g;
    if (slot >= 1 && slot <= 16) g = s.grooves[size_t(slot - 1)];
    double acc = 0.0;
    for (int i = 0; i < steps && i < kSteps; ++i) {
        start[i] = int(std::lround(acc));
        acc += base * ((i & 1) ? g.b : g.a) / 6.0;
    }
    for (int i = std::min(steps, kSteps); i <= kSteps; ++i) start[i] = int(std::lround(barTicks));
}

void buildTempoMap(Song& s)
{
    // Every T cell, at the tick its step starts on. The chains are short and
    // this runs on the message thread when a song is published.
    s.tempoMap.clear();
    s.tempoMap.push_back({ 0, std::clamp(s.tempoBpm, 40.0, 255.0) });
    int bars = 0;
    for (const auto& c : s.chain) bars = std::max(bars, int(c.size()));
    const int barTicks = s.barTicks();
    int starts[kSteps + 1];
    for (int bar = 0; bar < bars; ++bar)
        for (int step = 0; step < s.steps(); ++step)
            for (int ch = 0; ch < 4; ++ch) {
                const Phrase* p = s.phrase(s.phraseAt(ch, bar));
                if (!p) continue;
                const Cell& cell = p->steps[size_t(step)];
                const bank::Command* t = cell.cmd1.cmd == bank::Cmd::T ? &cell.cmd1 : cell.cmd2.cmd == bank::Cmd::T ? &cell.cmd2 : nullptr;
                if (!t) continue;
                stepStartTicks(s, p, kGrooveNone, starts);
                const int64_t tick = int64_t(bar) * barTicks + starts[step];
                if (s.tempoMap.back().tick == tick && tick != 0) break;   // one T per tick: the first channel wins
                if (tick == 0) s.tempoMap[0].bpm = std::clamp(double(t->a), 40.0, 255.0);
                else s.tempoMap.push_back({ tick, std::clamp(double(t->a), 40.0, 255.0) });
                break;
            }
}

/* ---------------------------------------------------------------- player */

void Player::prepare(double sampleRate)
{
    sampleRate_ = sampleRate;
    playing_ = false;
    for (auto& p : pos_) p = Position{};
    for (auto& n : lastNote_) n = 0;
}

void Player::setGrooveOverride(int ch, uint8_t slot)
{
    const size_t c = size_t(ch & 3);
    if (slot == grooveParam_[c]) return;      // a slot applies when it changes
    grooveParam_[c] = slot;
    groove_[c] = slot;
}

void Player::stepTicks(const Phrase* p, int* start, uint8_t grooveSlot) const
{
    if (!song_) { for (int i = 0; i <= kSteps; ++i) start[i] = i * 6; return; }
    stepStartTicks(*song_, p, grooveSlot, start);
}

void Player::fireStep(int ch, int bar, int step, uint8_t slot, uint32_t offset, std::vector<NoteEvent>& out)
{
    const Phrase* ph = song_->phrase(slot);
    pos_[size_t(ch)] = { bar, step, slot };
    if (!ph) {
        // A bar with no phrase is silence: end the note at its first step.
        if (step == 0 && lastNote_[ch]) {
            NoteEvent e; e.offset = offset; e.channel = uint8_t(ch); e.source = NoteEvent::Tracker; e.kind = NoteEvent::NoteOff; e.a = lastNote_[ch];
            out.push_back(e); lastNote_[ch] = 0;
        }
        return;
    }
    const Cell& c = ph->steps[size_t(step)];
    // G and T belong to the timeline, not to the channel: G sets this channel's
    // groove from here on, T is already in the song's tempo map.
    for (const bank::Command* cmd : { &c.cmd1, &c.cmd2 })
        if (cmd->cmd == bank::Cmd::G) groove_[size_t(ch)] = uint8_t(std::clamp<int>(cmd->a, 0, 16));
    if (c.note == 0 && c.inst == 0 && c.table == 0 && c.cmd1.cmd == bank::Cmd::None && c.cmd2.cmd == bank::Cmd::None) return;
    NoteEvent e;
    e.offset = offset; e.channel = uint8_t(ch); e.source = NoteEvent::Tracker;
    e.inst = c.inst; e.table = c.table; e.cmd1 = c.cmd1; e.cmd2 = c.cmd2;
    if (c.note == kNoteOff) { e.kind = NoteEvent::NoteOff; e.a = lastNote_[ch]; lastNote_[ch] = 0; }
    else if (c.note) { e.kind = NoteEvent::NoteOn; e.a = c.note; e.b = 100; lastNote_[ch] = c.note; }
    else e.kind = NoteEvent::Command;
    out.push_back(e);
}

void Player::process(const TickPoint* ticks, size_t nTicks, bool playing, std::vector<NoteEvent>& out)
{
    if (!song_ || !playing) {
        if (playing_)
            for (int ch = 0; ch < 4; ++ch)
                if (lastNote_[ch]) { NoteEvent e; e.kind = NoteEvent::NoteOff; e.source = NoteEvent::Tracker; e.channel = uint8_t(ch); e.a = lastNote_[ch]; out.push_back(e); lastNote_[ch] = 0; }
        playing_ = false;
        for (auto& p : pos_) p = Position{};
        return;
    }
    playing_ = true;
    const int steps = song_->steps();

    for (int ch = 0; ch < 4; ++ch) {
        const bool lane = song_->noteSource[size_t(ch)] == NoteSource::Tracker && !(muteMask_ & (1u << ch));
        if (!lane) {
            // Leaving the lane (source switched to the piano roll, or muted for
            // recording) must not leave its last note ringing. A note-off from
            // the tracker would be filtered on a piano-roll channel, so this
            // is an all-notes-off, which every channel accepts.
            if (lastNote_[ch]) { NoteEvent e; e.kind = NoteEvent::AllNotesOff; e.source = NoteEvent::Tracker; e.channel = uint8_t(ch); out.push_back(e); lastNote_[ch] = 0; }
            continue;
        }
        int starts[kSteps + 1];
        int builtFor = -1;
        for (size_t k = 0; k < nTicks; ++k) {
            const int64_t tick = ticks[k].tick;
            if (tick < 0) continue;
            const int bar = int(tick / barTicks_);
            const int inBar = int(tick % barTicks_);
            const uint8_t slot = song_->phraseAt(ch, bar);
            if (builtFor != bar) { stepTicks(song_->phrase(slot), starts, groove_[size_t(ch)]); builtFor = bar; }
            for (int s = 0; s < steps; ++s)
                if (starts[s] == inBar) { fireStep(ch, bar, s, slot, ticks[k].offset, out); break; }
        }
    }
}

bool Player::quantise(double tick, int& bar, int& step, int64_t& stepTick) const
{
    if (!song_) return false;
    const int steps = song_->steps();
    bar = int(std::floor(tick / barTicks_));
    if (bar < 0) return false;
    const double inBar = tick - double(bar) * barTicks_;
    int starts[kSteps + 1];
    stepTicks(song_->phrase(song_->phraseAt(0, bar)), starts, kGrooveNone);
    int best = 0; double bestD = 1e18;
    for (int s = 0; s <= steps; ++s) { const double d = std::fabs(starts[s] - inBar); if (d < bestD) { bestD = d; best = s; } }
    if (best >= steps) {
        ++bar; best = 0;
        stepTicks(song_->phrase(song_->phraseAt(0, bar)), starts, kGrooveNone);
    }
    step = best;
    stepTick = int64_t(bar) * barTicks_ + starts[best];
    return true;
}

void Player::resetRecord()
{
    for (int ch = 0; ch < 4; ++ch) { recInst_[ch] = 0; recSlots_[ch][0] = {}; recSlots_[ch][1] = {}; recStep_[ch] = -1; }
}

bool Player::recordNote(int ch, double tick, uint8_t note, bool noteOff, uint8_t instrument, uint8_t table,
                        const bank::Command& c1, const bank::Command& c2, RecordMessage& out)
{
    int bar = 0, step = 0; int64_t at = 0;
    if (!quantise(tick, bar, step, at)) return false;
    out = RecordMessage{};
    out.channel = uint8_t(ch & 3);
    out.bar = uint16_t(std::clamp(bar, 0, 65535));
    out.step = uint8_t(std::clamp(step, 0, kSteps - 1));
    if (noteOff) { out.cell.note = kNoteOff; return true; }
    out.cell.note = note;
    if (instrument != recInst_[ch & 3]) { out.cell.inst = instrument; recInst_[ch & 3] = instrument; }
    out.cell.table = table;
    out.cell.cmd1 = c1; out.cell.cmd2 = c2;
    recSlots_[ch & 3][0] = c1; recSlots_[ch & 3][1] = c2;
    recStep_[ch & 3] = at;
    return true;
}

bool Player::recordSlots(int ch, double tick, const bank::Command& c1, const bank::Command& c2, RecordMessage& out)
{
    const size_t c = size_t(ch & 3);
    if (bank::sameCmd(c1, recSlots_[c][0]) && bank::sameCmd(c2, recSlots_[c][1])) return false;
    int bar = 0, step = 0; int64_t at = 0;
    if (!quantise(tick, bar, step, at)) return false;
    recSlots_[c][0] = c1; recSlots_[c][1] = c2;
    if (at == recStep_[c]) return false;                  // that step already carries them
    recStep_[c] = at;
    out = RecordMessage{};
    out.channel = uint8_t(c);
    out.bar = uint16_t(std::clamp(bar, 0, 65535));
    out.step = uint8_t(std::clamp(step, 0, kSteps - 1));
    out.slotsOnly = true;
    out.cell.cmd1 = c1; out.cell.cmd2 = c2;
    return true;
}

} // namespace chipboy::tracker
