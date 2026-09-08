#include "core/Tracker/Player.h"

#include <algorithm>
#include <cmath>

namespace chipboy::tracker {

using driver::NoteEvent;
using driver::TickPoint;

/* ------------------------------------------------------------- the grid */

Groove grooveFor(const Song& s, const Phrase* p, uint8_t slot)
{
    uint8_t g = slot;
    if (g == kGrooveNone) g = p ? p->groove : 0;
    if (g >= 1 && g <= 16) return s.grooves[size_t(g - 1)];
    return Groove{};                        // slot 0 is straight and not editable
}

void stepStartTicks(const Song& s, const Phrase* p, uint8_t groove, int* start)
{
    // The groove holds the ticks a step lasts, so the grid is the running sum
    // of its entries (section 9.2). It is not scaled to the bar: a groove that
    // adds up to less leaves the last note sustaining, and steps that start at
    // or past the bar's end never fire.
    const int steps = s.steps();
    const Groove g = grooveFor(s, p, groove);
    const int scale = steps <= 8 ? 2 : 1;   // eight steps per bar: entries doubled
    int acc = 0;
    for (int i = 0; i < steps && i < kSteps; ++i) { start[i] = acc; acc += g.at(i) * scale; }
    for (int i = std::min(steps, kSteps); i <= kSteps; ++i) start[i] = acc;
}

void buildTempoMap(Song& s)
{
    // Every T cell, at the tick its step starts on: the song's own bar ticks
    // and the phrase's own groove (section 9.3). The chains are short and this
    // runs on the message thread when a song is published.
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
                if (starts[step] >= barTicks) break;                          // that step never plays
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
    haveTick_ = false;
    lastTick_ = -1;
    for (auto& p : pos_) p = Position{};
    for (auto& n : lastNote_) n = 0;
    for (auto& l : laneOn_) l = false;
    for (auto& g : grooveCell_) g = kGrooveNone;
    for (auto& b : firedBar_) b = -1;
}

uint8_t Player::groove(int ch) const
{
    // The slot's G wins, then the last G cell that played, then the phrase's
    // own (section 9.2). Nothing is latched, so a cell and a slot cannot
    // disagree about which groove is in force.
    const size_t c = size_t(ch & 3);
    return grooveParam_[c] != kGrooveNone ? grooveParam_[c] : grooveCell_[c];
}

void Player::stepTicks(const Phrase* p, int* start, uint8_t grooveSlot) const
{
    if (!song_) { for (int i = 0; i <= kSteps; ++i) start[i] = i * 6; return; }
    stepStartTicks(*song_, p, grooveSlot, start);
}

void Player::allNotesOff(int ch, uint32_t offset, std::vector<NoteEvent>& out)
{
    NoteEvent e;
    e.offset = offset; e.channel = uint8_t(ch); e.source = NoteEvent::Tracker; e.kind = NoteEvent::AllNotesOff;
    out.push_back(e);
    lastNote_[size_t(ch)] = 0;
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
    // G and T belong to the timeline, not to the channel: G is this channel's
    // groove from here on, T is already in the song's tempo map.
    for (const bank::Command* cmd : { &c.cmd1, &c.cmd2 })
        if (cmd->cmd == bank::Cmd::G) grooveCell_[size_t(ch)] = uint8_t(std::clamp<int>(cmd->a, 0, 16));
    if (c.note == 0 && c.inst == 0 && c.table == 0 && c.cmd1.cmd == bank::Cmd::None && c.cmd2.cmd == bank::Cmd::None) return;
    NoteEvent e;
    e.offset = offset; e.channel = uint8_t(ch); e.source = NoteEvent::Tracker;
    e.inst = c.inst; e.table = c.table; e.cmd1 = c.cmd1; e.cmd2 = c.cmd2;
    if (c.note == kNoteOff) { e.kind = NoteEvent::NoteOff; e.a = lastNote_[ch]; lastNote_[ch] = 0; }
    else if (c.note) { e.kind = NoteEvent::NoteOn; e.a = c.note; e.b = velocityOf(c); lastNote_[ch] = c.note; }
    else e.kind = NoteEvent::Command;
    out.push_back(e);
}

void Player::process(const TickPoint* ticks, size_t nTicks, bool playing, std::vector<NoteEvent>& out)
{
    if (!song_ || !playing) {
        // The transport stopped: nothing is left ringing (section 9.1).
        if (playing_)
            for (int ch = 0; ch < 4; ++ch)
                if (laneOn_[size_t(ch)] || lastNote_[size_t(ch)]) allNotesOff(ch, 0, out);
        playing_ = false;
        haveTick_ = false;
        for (auto& p : pos_) p = Position{};
        for (auto& l : laneOn_) l = false;
        for (auto& g : grooveCell_) g = kGrooveNone;
        for (auto& b : firedBar_) b = -1;
        return;
    }
    playing_ = true;
    const int steps = song_->steps();

    bool lane[4];
    for (int ch = 0; ch < 4; ++ch) {
        lane[ch] = song_->noteSource[size_t(ch)] == NoteSource::Tracker && !(muteMask_ & (1u << ch));
        // Leaving the lane (the source switched to the piano roll, or the
        // channel was muted for recording) must not leave a note ringing.
        if (!lane[ch] && laneOn_[size_t(ch)]) { allNotesOff(ch, 0, out); pos_[size_t(ch)] = Position{}; }
        laneOn_[size_t(ch)] = lane[ch];
    }

    int starts[4][kSteps + 1];
    int builtBar[4] = { -1, -1, -1, -1 };
    uint8_t builtGroove[4] = { kGrooveNone, kGrooveNone, kGrooveNone, kGrooveNone };
    for (size_t k = 0; k < nTicks; ++k) {
        const int64_t tick = ticks[k].tick;
        // A tick that is not the one after the last means the transport jumped
        // (a locate, a loop wrap): what was sounding has no note-off coming.
        if (haveTick_ && tick != lastTick_ + 1) {
            for (int ch = 0; ch < 4; ++ch) if (lane[ch]) allNotesOff(ch, ticks[k].offset, out);
            for (auto& g : grooveCell_) g = kGrooveNone;   // the groove starts again from the song
            for (auto& b : firedBar_) b = -1;
            for (auto& b : builtBar) b = -1;
        }
        lastTick_ = tick;
        haveTick_ = true;
        if (tick < 0) continue;
        const int bar = int(tick / barTicks_);
        const int inBar = int(tick % barTicks_);
        for (int ch = 0; ch < 4; ++ch) {
            if (!lane[ch]) continue;
            const uint8_t slot = song_->phraseAt(ch, bar);
            const uint8_t g = groove(ch);
            if (builtBar[ch] != bar || builtGroove[ch] != g) {
                stepTicks(song_->phrase(slot), starts[ch], g);
                builtBar[ch] = bar; builtGroove[ch] = g;
            }
            for (int s = 0; s < steps; ++s)
                if (starts[ch][s] == inBar) {
                    // A groove that changes mid-bar re-lays the steps after
                    // it; a step already played in this bar is not played
                    // again because the new grid puts it later.
                    if (bar != firedBar_[ch] || s > firedStep_[ch]) {
                        firedBar_[ch] = bar; firedStep_[ch] = s;
                        fireStep(ch, bar, s, slot, ticks[k].offset, out);
                    }
                    break;
                }
        }
    }
}

/* -------------------------------------------------------------- record */

bool Player::quantise(int ch, double tick, int& bar, int& step, int64_t& stepTick) const
{
    // The channel's own grid: its phrase this bar, and its groove in force.
    if (!song_) return false;
    const int steps = song_->steps();
    bar = int(std::floor(tick / barTicks_));
    if (bar < 0) return false;
    const double inBar = tick - double(bar) * barTicks_;
    int starts[kSteps + 1];
    stepTicks(song_->phrase(song_->phraseAt(ch, bar)), starts, groove(ch));
    int best = -1; double bestD = 1e18;
    for (int s = 0; s < steps; ++s) {
        if (starts[s] >= barTicks_) break;              // that step never fires
        const double d = std::fabs(double(starts[s]) - inBar);
        if (d < bestD) { bestD = d; best = s; }
    }
    if (best < 0 || double(barTicks_) - inBar < bestD) {
        // Nearer the bar's end: that is the next bar's first step.
        ++bar;
        step = 0;
        stepTicks(song_->phrase(song_->phraseAt(ch, bar)), starts, groove(ch));
        stepTick = int64_t(bar) * barTicks_ + starts[0];
        return true;
    }
    step = best;
    stepTick = int64_t(bar) * barTicks_ + starts[best];
    return true;
}

bool Player::stepAt(int ch, int64_t tick, int& bar, int& step) const
{
    if (!song_ || tick < 0) return false;
    bar = int(tick / barTicks_);
    const int inBar = int(tick % barTicks_);
    int starts[kSteps + 1];
    stepTicks(song_->phrase(song_->phraseAt(ch, bar)), starts, groove(ch));
    for (int s = 0; s < song_->steps(); ++s)
        if (starts[s] == inBar) { step = s; return true; }
    return false;
}

bool Player::nextStep(int ch, int& bar, int& step, int64_t& stepTick) const
{
    if (!song_) return false;
    int starts[kSteps + 1];
    if (step + 1 < song_->steps()) {
        stepTicks(song_->phrase(song_->phraseAt(ch, bar)), starts, groove(ch));
        if (starts[step + 1] < barTicks_) {
            ++step;
            stepTick = int64_t(bar) * barTicks_ + starts[step];
            return true;
        }
    }
    ++bar;
    step = 0;
    stepTicks(song_->phrase(song_->phraseAt(ch, bar)), starts, groove(ch));
    stepTick = int64_t(bar) * barTicks_ + starts[0];
    return true;
}

bool Player::stepHasNote(int ch, int bar, int step) const
{
    if (!song_ || step < 0 || step >= kSteps) return false;
    const Phrase* p = song_->phrase(song_->phraseAt(ch, bar));
    if (!p) return false;
    const uint8_t n = p->steps[size_t(step)].note;
    return n >= 1 && n <= 127;
}

bank::Command revertCommand(bank::Cmd letter, const SlotRevert& r)
{
    using bank::Cmd;
    switch (letter) {
        case Cmd::A: return { Cmd::A, 0, 0, 0 };
        case Cmd::E: return { Cmd::E, r.e[0], r.e[1], 0 };
        case Cmd::F: return { Cmd::F, r.f, 0, 0 };
        case Cmd::G: return { Cmd::G, 0, 0, 0 };
        case Cmd::M: return { Cmd::M, r.m[0], r.m[1], 0 };
        case Cmd::O: return { Cmd::O, r.o, 0, 0 };
        case Cmd::P: return { Cmd::P, 128, 0, 0 };
        case Cmd::S: return { Cmd::S, r.s[0], r.s[1], 0 };
        case Cmd::T: return { Cmd::T, r.t, 0, 0 };
        case Cmd::V: return { Cmd::V, r.v[0], r.v[1], 0 };
        case Cmd::W: return { Cmd::W, r.w, 0, 0 };
        default: return {};                     // C D H K L R Z leave nothing behind
    }
}

void Player::slotCells(int ch, const bank::Command& c1, const bank::Command& c2, const SlotRevert& rev,
                       bool plainNote, bank::Command& o1, bank::Command& o2)
{
    const size_t c = size_t(ch & 3);
    const bank::Command* in[2] = { &c1, &c2 };
    bank::Command* out[2] = { &o1, &o2 };
    for (size_t i = 0; i < 2; ++i) {
        bank::Command& last = recSlots_[c][i];
        *out[i] = {};
        if (bank::sameCmd(*in[i], last)) {
            // A plain note reloads the instrument and fires the slots after
            // it, so its cell carries them even when nothing has changed.
            if (plainNote && in[i]->cmd != bank::Cmd::None) *out[i] = *in[i];
            continue;
        }
        // A slot going to none is written as the letter it reverts to.
        *out[i] = in[i]->cmd != bank::Cmd::None ? *in[i] : revertCommand(last.cmd, rev);
        last = *in[i];
    }
}

void Player::resetRecord()
{
    for (int ch = 0; ch < 4; ++ch) {
        recSlots_[ch][0] = {}; recSlots_[ch][1] = {};
        recNoteStep_[ch] = -1; recNote_[ch] = 0;
    }
}

bool Player::recordNote(int ch, double tick, uint8_t note, uint8_t velocity, bool noteOff, bool plain,
                        uint8_t instrument, uint8_t table, const bank::Command& c1, const bank::Command& c2,
                        const SlotRevert& rev, RecordMessage& out)
{
    const size_t c = size_t(ch & 3);
    int bar = 0, step = 0; int64_t at = 0;
    if (!quantise(ch, tick, bar, step, at)) return false;
    out = RecordMessage{};
    out.channel = uint8_t(c);
    if (noteOff) {
        if (at == recNoteStep_[c]) {
            // Its own step: a note shorter than a step becomes one step long.
            // A different note-on there ends this note by itself.
            if (recNote_[c] != note) return false;
            if (!nextStep(int(c), bar, step, at)) return false;
        }
        if (stepHasNote(int(c), bar, step)) return false;   // occupied: the note there ends it
        out.bar = uint16_t(std::clamp(bar, 0, 65535));
        out.step = uint8_t(std::clamp(step, 0, kSteps - 1));
        out.cell.note = kNoteOff;
        return true;
    }
    out.bar = uint16_t(std::clamp(bar, 0, 65535));
    out.step = uint8_t(std::clamp(step, 0, kSteps - 1));
    out.cell.note = note;
    out.cell.vel = uint8_t(std::clamp<int>(velocity, 0, 127));
    // The instrument column is what the note loaded, and blank when the note
    // was bare -- so an overlap records as a bare cell and plays back bare.
    out.cell.inst = plain ? instrument : 0;
    out.cell.table = table;
    slotCells(int(c), c1, c2, rev, plain, out.cell.cmd1, out.cell.cmd2);
    recNoteStep_[c] = at;
    recNote_[c] = note;
    return true;
}

bool Player::recordSlots(int ch, double tick, const bank::Command& c1, const bank::Command& c2,
                         const SlotRevert& rev, RecordMessage& out)
{
    const size_t c = size_t(ch & 3);
    int bar = 0, step = 0; int64_t at = 0;
    if (!quantise(ch, tick, bar, step, at)) return false;
    bank::Command o1, o2;
    slotCells(int(c), c1, c2, rev, false, o1, o2);
    if (o1.cmd == bank::Cmd::None && o2.cmd == bank::Cmd::None) return false;
    out = RecordMessage{};
    out.channel = uint8_t(c);
    out.bar = uint16_t(std::clamp(bar, 0, 65535));
    out.step = uint8_t(std::clamp(step, 0, kSteps - 1));
    out.slotsOnly = true;
    out.cell.cmd1 = o1; out.cell.cmd2 = o2;
    return true;
}

} // namespace chipboy::tracker
