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

void stepStartTicks(const Song& s, const Phrase* p, uint8_t groove, int* start, int barTicks, int barSteps)
{
    // The bar's ticks divided by the song's steps per bar is a step; the
    // groove says how many sixths of one each step really lasts (section 11).
    // Step i therefore starts at (the groove's ticks so far) x bar ticks
    // / (6 x steps), which is floor(i x bar ticks / steps) for the straight
    // groove and the plain running sum at sixteen steps in a 4/4 bar -- the
    // grid every song had before bars could hold another step count.
    const int ticks = barTicks > 0 ? barTicks : s.barTicks();
    const int perBar = s.steps();
    const int steps = std::clamp(barSteps > 0 ? barSteps : perBar, 1, kMaxSteps);
    const Groove g = grooveFor(s, p, groove);
    int64_t acc = 0;                        // the groove's ticks so far, at six to a step
    for (int i = 0; i < steps; ++i) {
        start[i] = int(acc * int64_t(ticks) / (6 * int64_t(perBar)));
        acc += g.at(i);
    }
    const int end = int(acc * int64_t(ticks) / (6 * int64_t(perBar)));
    for (int i = steps; i <= kMaxSteps; ++i) start[i] = end;
}

/* ------------------------------------------------------------- the bars */

void buildBarTable(Song& s)
{
    // One entry per bar plus the end: how many steps the song holds before
    // that bar. Everything else about a bar's place on the timeline follows
    // from this and the bar ticks in force (section 11).
    const int n = s.bars();
    s.barStartSteps.clear();
    s.barStartSteps.reserve(size_t(n) + 1);
    int32_t acc = 0;
    for (int bar = 0; bar <= n; ++bar) {
        s.barStartSteps.push_back(acc);
        if (bar < n) acc = int32_t(std::min<int64_t>(int64_t(acc) + s.stepsOfBar(bar), INT32_MAX / 2));
    }
}

int64_t barStartStep(const Song& s, int bar)
{
    if (bar <= 0) return 0;
    const auto& t = s.barStartSteps;
    if (t.empty()) return int64_t(bar) * s.steps();                 // no table: every bar is the default
    if (size_t(bar) < t.size()) return t[size_t(bar)];
    // Past the last bar the song describes: default bars, end to end.
    return int64_t(t.back()) + int64_t(bar - int(t.size()) + 1) * s.steps();
}

int64_t barStartTick(const Song& s, int bar, int barTicks)
{
    const int ticks = barTicks > 0 ? barTicks : s.barTicks();
    return barStartStep(s, bar) * int64_t(ticks) / int64_t(s.steps());
}

int barLengthTicks(const Song& s, int bar, int barTicks)
{
    return int(std::max<int64_t>(1, barStartTick(s, bar + 1, barTicks) - barStartTick(s, bar, barTicks)));
}

void barAtTick(const Song& s, int64_t tick, int barTicks, int& bar, int& inBar)
{
    const int ticks = barTicks > 0 ? barTicks : s.barTicks();
    const int64_t t = std::max<int64_t>(0, tick);
    const int last = s.barStartSteps.empty() ? 0 : int(s.barStartSteps.size()) - 1;
    if (t >= barStartTick(s, last, ticks)) {
        // Past the song's own bars: they are the default length from here, so
        // the rest is arithmetic (the floor is exact over whole bars).
        bar = last + int(std::min<int64_t>((t - barStartTick(s, last, ticks)) / std::max(1, ticks), 1 << 20));
    } else {
        // The last bar that starts at or before this tick; a bar so short it
        // starts on the same tick as the next is stepped over, never played.
        int lo = 0, hi = last;
        while (lo < hi) {
            const int mid = lo + (hi - lo + 1) / 2;
            if (barStartTick(s, mid, ticks) <= t) lo = mid; else hi = mid - 1;
        }
        bar = lo;
    }
    inBar = int(t - barStartTick(s, bar, ticks));
}

void buildTempoMap(Song& s, double baseBpm)
{
    // Every T cell, at the tick its step starts on: the song's own bars and
    // the phrase's own groove (section 9.3). The chains are short and this
    // runs on the message thread when a song is published.
    //
    // The base is not in the map: it is the Song tempo parameter, which the
    // clock holds and a host can automate (section 4). Only T cells are here,
    // and a T reverting is the base again from its tick.
    buildBarTable(s);
    s.tempoMap.clear();
    const double base = std::clamp(baseBpm, 40.0, 255.0);
    const int bars = s.bars();
    const int barTicks = s.barTicks();
    std::vector<int> starts(size_t(kMaxSteps) + 1, 0);
    for (int bar = 0; bar < bars; ++bar) {
        const int steps = s.stepsOfBar(bar);
        const int length = barLengthTicks(s, bar, barTicks);
        for (int step = 0; step < steps; ++step)
            for (int ch = 0; ch < 4; ++ch) {
                const Phrase* p = s.phrase(s.phraseAt(ch, bar));
                if (!p) continue;
                const Cell& cell = p->steps[size_t(step)];
                const bank::Command* t = cell.cmd1.cmd == bank::Cmd::T ? &cell.cmd1 : cell.cmd2.cmd == bank::Cmd::T ? &cell.cmd2 : nullptr;
                if (!t) continue;
                stepStartTicks(s, p, kGrooveNone, starts.data(), barTicks, steps);
                if (starts[size_t(step)] >= length) break;                     // that step never plays
                const int64_t tick = barStartTick(s, bar, barTicks) + starts[size_t(step)];
                if (!s.tempoMap.empty() && s.tempoMap.back().tick == tick) break;   // one T per tick: the first channel wins
                s.tempoMap.push_back({ tick, bank::isRevert(*t) ? base : std::clamp(double(t->a), 40.0, 255.0) });
                break;
            }
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

void Player::stepTicks(const Phrase* p, int* start, uint8_t grooveSlot, int barSteps) const
{
    if (!song_) { for (int i = 0; i <= kMaxSteps; ++i) start[i] = i * 6; return; }
    stepStartTicks(*song_, p, grooveSlot, start, barTicks_, barSteps);
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
        if (cmd->cmd == bank::Cmd::G)
            // G reverting is the phrase's own groove back (section 3).
            grooveCell_[size_t(ch)] = bank::isRevert(*cmd) ? kGrooveNone : uint8_t(std::clamp<int>(cmd->a, 0, 16));
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

    bool lane[4];
    for (int ch = 0; ch < 4; ++ch) {
        lane[ch] = song_->noteSource[size_t(ch)] == NoteSource::Tracker && !(muteMask_ & (1u << ch));
        // Leaving the lane (the source switched to the piano roll, or the
        // channel was muted for recording) must not leave a note ringing.
        if (!lane[ch] && laneOn_[size_t(ch)]) { allNotesOff(ch, 0, out); pos_[size_t(ch)] = Position{}; }
        laneOn_[size_t(ch)] = lane[ch];
    }

    int starts[4][kMaxSteps + 1];
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
        // The bars are the song's own, laid end to end from tick 0 through
        // the prefix table, so a bar with its own step count moves the ones
        // after it (section 11).
        int bar = 0, inBar = 0;
        barAtTick(*song_, tick, barTicks_, bar, inBar);
        const int steps = song_->stepsOfBar(bar);
        for (int ch = 0; ch < 4; ++ch) {
            if (!lane[ch]) continue;
            const uint8_t slot = song_->phraseAt(ch, bar);
            const uint8_t g = groove(ch);
            if (builtBar[ch] != bar || builtGroove[ch] != g) {
                stepTicks(song_->phrase(slot), starts[ch], g, steps);
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
    if (!song_ || tick < 0.0) return false;
    barAtTick(*song_, int64_t(std::floor(tick)), barTicks_, bar, step);
    const double barStart = double(barStartTick(*song_, bar, barTicks_));
    const int length = barLengthTicks(*song_, bar, barTicks_);
    const int steps = song_->stepsOfBar(bar);
    const double inBar = tick - barStart;
    int starts[kMaxSteps + 1];
    stepTicks(song_->phrase(song_->phraseAt(ch, bar)), starts, groove(ch), steps);
    int best = -1; double bestD = 1e18;
    for (int s = 0; s < steps; ++s) {
        if (starts[s] >= length) break;                 // that step never fires
        const double d = std::fabs(double(starts[s]) - inBar);
        if (d < bestD) { bestD = d; best = s; }
    }
    if (best < 0 || double(length) - inBar < bestD) {
        // Nearer the bar's end: that is the next bar's first step.
        ++bar;
        step = 0;
        stepTicks(song_->phrase(song_->phraseAt(ch, bar)), starts, groove(ch), song_->stepsOfBar(bar));
        stepTick = barStartTick(*song_, bar, barTicks_) + starts[0];
        return true;
    }
    step = best;
    stepTick = barStartTick(*song_, bar, barTicks_) + starts[best];
    return true;
}

bool Player::stepAt(int ch, int64_t tick, int& bar, int& step) const
{
    if (!song_ || tick < 0) return false;
    int inBar = 0;
    barAtTick(*song_, tick, barTicks_, bar, inBar);
    const int steps = song_->stepsOfBar(bar);
    int starts[kMaxSteps + 1];
    stepTicks(song_->phrase(song_->phraseAt(ch, bar)), starts, groove(ch), steps);
    for (int s = 0; s < steps; ++s)
        if (starts[s] == inBar) { step = s; return true; }
    return false;
}

bool Player::nextStep(int ch, int& bar, int& step, int64_t& stepTick) const
{
    if (!song_) return false;
    int starts[kMaxSteps + 1];
    if (step + 1 < song_->stepsOfBar(bar)) {
        stepTicks(song_->phrase(song_->phraseAt(ch, bar)), starts, groove(ch), song_->stepsOfBar(bar));
        if (starts[step + 1] < barLengthTicks(*song_, bar, barTicks_)) {
            ++step;
            stepTick = barStartTick(*song_, bar, barTicks_) + starts[step];
            return true;
        }
    }
    ++bar;
    step = 0;
    stepTicks(song_->phrase(song_->phraseAt(ch, bar)), starts, groove(ch), song_->stepsOfBar(bar));
    stepTick = barStartTick(*song_, bar, barTicks_) + starts[0];
    return true;
}

bool Player::stepHasNote(int ch, int bar, int step) const
{
    if (!song_ || step < 0 || step >= kMaxSteps) return false;
    const Phrase* p = song_->phrase(song_->phraseAt(ch, bar));
    if (!p) return false;
    const uint8_t n = p->steps[size_t(step)].note;
    return n >= 1 && n <= 127;
}

void Player::slotCells(int ch, const bank::Command& c1, const bank::Command& c2,
                       SlotWrite mode, bank::Command& o1, bank::Command& o2)
{
    const size_t c = size_t(ch & 3);
    const bank::Command* in[2] = { &c1, &c2 };
    bank::Command* out[2] = { &o1, &o2 };
    for (size_t i = 0; i < 2; ++i) {
        bank::Command& last = recSlots_[c][i];
        *out[i] = {};
        if (bank::sameCmd(*in[i], last)) {
            // The slot has not moved, so the cell carries it only where the
            // driver would fire it again: at a plain note, which reloads the
            // instrument; at a bare note, the per-note letters it re-fires;
            // and at a note in the command octave, which fires both slots.
            const bool wanted = mode == SlotWrite::Plain || mode == SlotWrite::All
                                || (mode == SlotWrite::Bare && !bank::cmdPersists(in[i]->cmd));
            if (wanted && in[i]->cmd != bank::Cmd::None) *out[i] = *in[i];
            continue;
        }
        // A slot going to none is written as the letter's revert form, which
        // says "put this letter back" rather than naming a value that would
        // then stay in force (section 9.4).
        *out[i] = in[i]->cmd != bank::Cmd::None ? *in[i] : bank::revertOf(last.cmd);
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
                        RecordMessage& out)
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
        out.step = uint8_t(std::clamp(step, 0, kMaxSteps - 1));
        out.cell.note = kNoteOff;
        return true;
    }
    out.bar = uint16_t(std::clamp(bar, 0, 65535));
    out.step = uint8_t(std::clamp(step, 0, kMaxSteps - 1));
    out.cell.note = note;
    out.cell.vel = uint8_t(std::clamp<int>(velocity, 0, 127));
    // The instrument column is what the note loaded, and blank when the note
    // was bare -- so an overlap records as a bare cell and plays back bare.
    out.cell.inst = plain ? instrument : 0;
    out.cell.table = table;
    // A plain note's cell carries both slots in force; a bare note's carries
    // the per-note letters only, since the persistent ones are already in the
    // running state and re-writing E on a sounding pulse would restart its
    // envelope (section 12). A slot that moved is written either way.
    slotCells(int(c), c1, c2, plain ? SlotWrite::Plain : SlotWrite::Bare, out.cell.cmd1, out.cell.cmd2);
    recNoteStep_[c] = at;
    recNote_[c] = note;
    return true;
}

bool Player::recordSlots(int ch, double tick, const bank::Command& c1, const bank::Command& c2, RecordMessage& out, bool force)
{
    const size_t c = size_t(ch & 3);
    int bar = 0, step = 0; int64_t at = 0;
    if (!quantise(ch, tick, bar, step, at)) return false;
    bank::Command o1, o2;
    slotCells(int(c), c1, c2, force ? SlotWrite::All : SlotWrite::Changed, o1, o2);
    if (o1.cmd == bank::Cmd::None && o2.cmd == bank::Cmd::None) return false;
    out = RecordMessage{};
    out.channel = uint8_t(c);
    out.bar = uint16_t(std::clamp(bar, 0, 65535));
    out.step = uint8_t(std::clamp(step, 0, kMaxSteps - 1));
    out.slotsOnly = true;
    out.cell.cmd1 = o1; out.cell.cmd2 = o2;
    return true;
}

} // namespace chipboy::tracker
