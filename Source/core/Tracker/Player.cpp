#include "core/Tracker/Player.h"

#include <algorithm>
#include <cmath>

namespace chipboy::tracker {

using driver::NoteEvent;
using driver::TickPoint;

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
    for (auto& o : ownedNotes_) o = false;
    for (auto& g : grooveCell_) g = kGrooveNone;
    for (auto& b : firedRow_) b = -1;
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
    if (!song_) { for (int i = 0; i <= kMaxSteps; ++i) start[i] = i * kTicksPerStep; return; }
    stepStartTicks(*song_, p, grooveSlot, start);
}

void Player::allNotesOff(int ch, uint32_t offset, std::vector<NoteEvent>& out)
{
    NoteEvent e;
    e.offset = offset; e.channel = uint8_t(ch); e.source = NoteEvent::Tracker; e.kind = NoteEvent::AllNotesOff;
    out.push_back(e);
    lastNote_[size_t(ch)] = 0;
}

void Player::fireStep(int ch, int row, int step, uint8_t slot, uint32_t offset, std::vector<NoteEvent>& out)
{
    const Phrase* ph = song_->phrase(slot);
    // Hybrid: the notes are the incoming MIDI's and only the other columns
    // come from the cell (section 20), so nothing here ends a note.
    const bool notes = cellNotes(song_->noteSource[size_t(ch)]);
    pos_[size_t(ch)] = { row, step, slot };
    if (!ph) {
        // A row with no phrase is silence: end the note at its first step.
        if (notes && step == 0 && lastNote_[ch]) {
            NoteEvent e; e.offset = offset; e.channel = uint8_t(ch); e.source = NoteEvent::Tracker; e.kind = NoteEvent::NoteOff; e.a = lastNote_[ch];
            out.push_back(e); lastNote_[ch] = 0;
        }
        return;
    }
    const Cell& c = ph->cells[size_t(step)];
    // G and T belong to the timeline, not to the channel: G is this channel's
    // groove from here on, T is already in the song's tempo map.
    for (const bank::Command* cmd : { &c.cmd1, &c.cmd2 })
        if (cmd->cmd == bank::Cmd::G)
            // G reverting is the phrase's own groove back (section 3).
            grooveCell_[size_t(ch)] = bank::isRevert(*cmd) ? kGrooveNone : uint8_t(std::clamp<int>(cmd->a, 0, 16));
    if (c.note == 0 && c.inst == 0 && c.table == 0 && c.cmd1.cmd == bank::Cmd::None && c.cmd2.cmd == bank::Cmd::None) return;
    // A Hybrid cell that holds nothing but a note has nothing to say: its
    // note and its VEL are the MIDI's business (section 20).
    if (!notes && c.inst == 0 && c.table == 0 && c.cmd1.cmd == bank::Cmd::None && c.cmd2.cmd == bank::Cmd::None) return;
    NoteEvent e;
    e.offset = offset; e.channel = uint8_t(ch); e.source = NoteEvent::Tracker;
    e.inst = c.inst; e.table = c.table; e.cmd1 = c.cmd1; e.cmd2 = c.cmd2;
    e.transpose = song_->transposeAt(ch, row);       // the chain row's (section 48)
    if (!notes) { e.kind = NoteEvent::Command; e.hybrid = true; }
    else if (c.note == kNoteOff) { e.kind = NoteEvent::NoteOff; e.a = lastNote_[ch]; lastNote_[ch] = 0; }
    else if (c.note) { e.kind = NoteEvent::NoteOn; e.a = c.note; e.b = velocityOf(c); e.velSet = c.vel != 0; lastNote_[ch] = c.note; }
    else e.kind = NoteEvent::Command;
    out.push_back(e);
}

void Player::process(const TickPoint* ticks, size_t nTicks, bool playing, std::vector<NoteEvent>& out)
{
    if (!song_ || !playing) {
        // The transport stopped: nothing the lane started is left ringing
        // (section 9.1). A Hybrid channel's notes are the player's, not the
        // song's, so they are not touched (section 20).
        if (playing_)
            for (int ch = 0; ch < 4; ++ch)
                if ((laneOn_[size_t(ch)] && ownedNotes_[size_t(ch)]) || lastNote_[size_t(ch)]) allNotesOff(ch, 0, out);
        playing_ = false;
        haveTick_ = false;
        for (auto& p : pos_) p = Position{};
        for (auto& l : laneOn_) l = false;
        for (auto& o : ownedNotes_) o = false;
        for (auto& g : grooveCell_) g = kGrooveNone;
        for (auto& b : firedRow_) b = -1;
        return;
    }
    playing_ = true;

    bool lane[4];
    for (int ch = 0; ch < 4; ++ch) {
        // Trkr and Hybrid both play their cells; only Trkr's notes are the
        // song's, and only those can be left ringing (section 20).
        const NoteSource src = song_->noteSource[size_t(ch)];
        lane[ch] = cellsPlay(src) && !(muteMask_ & (1u << ch));
        const bool owned = cellNotes(src);
        // Leaving the lane (the source switched to the piano roll, or the
        // channel was muted for recording) must not leave a note ringing.
        if (!lane[ch] && laneOn_[size_t(ch)]) {
            if (ownedNotes_[size_t(ch)]) allNotesOff(ch, 0, out);
            pos_[size_t(ch)] = Position{};
        }
        laneOn_[size_t(ch)] = lane[ch];
        ownedNotes_[size_t(ch)] = owned;
    }

    int starts[4][kMaxSteps + 1];
    int builtRow[4] = { -1, -1, -1, -1 };
    uint8_t builtGroove[4] = { kGrooveNone, kGrooveNone, kGrooveNone, kGrooveNone };
    for (size_t k = 0; k < nTicks; ++k) {
        const int64_t tick = ticks[k].tick;
        // A tick that is not the one after the last means the transport jumped
        // (a locate, a loop wrap), and so does the first tick after Play. What
        // was sounding is left alone -- only a stop, a pause or the lane going
        // silence a channel (section 47) -- the groove starts again from the
        // song, and this one tick is allowed to land inside a step: the host
        // wraps mid-block, so the tick a step sits on was in the block before
        // and the step would otherwise never fire.
        const bool late = !haveTick_ || tick != lastTick_ + 1;
        if (late) {
            for (auto& g : grooveCell_) g = kGrooveNone;
            for (auto& b : firedRow_) b = -1;
            for (auto& b : builtRow) b = -1;
        }
        lastTick_ = tick;
        haveTick_ = true;
        if (tick < 0) continue;
        // Each channel is somewhere else: its own rows lie end to end from
        // tick 0 on its own prefix table, so a phrase of another length moves
        // that channel alone (section 25).
        for (int ch = 0; ch < 4; ++ch) {
            if (!lane[ch]) continue;
            int row = 0, inRow = 0;
            rowAtTick(*song_, ch, tick, row, inRow);
            const uint8_t slot = song_->phraseAt(ch, row);
            const Phrase* ph = song_->phrase(slot);
            const int length = phraseTicks(*song_, ph);
            if (!ph) {
                // A row with no phrase is one note-off at its start.
                if (inRow == 0 && (row != firedRow_[ch] || firedStep_[ch] < 0)) {
                    firedRow_[ch] = row; firedStep_[ch] = 0;
                    fireStep(ch, row, 0, slot, ticks[k].offset, out);
                }
                continue;
            }
            const uint8_t g = groove(ch);
            if (builtRow[ch] != row || builtGroove[ch] != g) {
                stepTicks(ph, starts[ch], g);
                builtRow[ch] = row; builtGroove[ch] = g;
            }
            const int steps = ph->length();
            for (int s = 0; s < steps; ++s) {
                if (starts[ch][s] >= length) break;     // a groove that ends early: that step never fires
                // The step at this tick; after a jump, the latest step at or
                // before it (section 47).
                const bool lastBefore = late && starts[ch][s] < inRow && (s + 1 >= steps || starts[ch][s + 1] >= length || starts[ch][s + 1] > inRow);
                if (starts[ch][s] == inRow || lastBefore) {
                    // A groove that changes mid-row re-lays the steps after
                    // it; a step already played in this row is not played
                    // again because the new grid puts it later.
                    if (row != firedRow_[ch] || s > firedStep_[ch]) {
                        firedRow_[ch] = row; firedStep_[ch] = s;
                        fireStep(ch, row, s, slot, ticks[k].offset, out);
                    }
                    break;
                }
            }
        }
    }
}

/* -------------------------------------------------------------- record */

bool Player::quantise(int ch, double tick, int& row, int& step, int64_t& stepTick) const
{
    // The channel's own grid: its phrase on its own row, and its groove in
    // force (section 25) -- not channel 0's.
    if (!song_ || tick < 0.0) return false;
    int inRowInt = 0;
    rowAtTick(*song_, ch, int64_t(std::floor(tick)), row, inRowInt);
    const Phrase* p = song_->phrase(song_->phraseAt(ch, row));
    const double rowStart = double(rowStartTick(*song_, ch, row));
    const int length = phraseTicks(*song_, p);
    const int steps = p ? p->length() : kEmptyRowTicks / kTicksPerStep;
    const double inRow = tick - rowStart;
    int starts[kMaxSteps + 1];
    stepTicks(p, starts, groove(ch));
    int best = -1; double bestD = 1e18;
    for (int s = 0; s < steps; ++s) {
        if (starts[s] >= length) break;                 // that step never fires
        const double d = std::fabs(double(starts[s]) - inRow);
        if (d < bestD) { bestD = d; best = s; }
    }
    if (best < 0 || double(length) - inRow < bestD) {
        // Nearer the row's end: that is the next row's first step.
        ++row;
        step = 0;
        stepTicks(song_->phrase(song_->phraseAt(ch, row)), starts, groove(ch));
        stepTick = rowStartTick(*song_, ch, row) + starts[0];
        return true;
    }
    step = best;
    stepTick = rowStartTick(*song_, ch, row) + starts[best];
    return true;
}

bool Player::stepAt(int ch, int64_t tick, int& row, int& step) const
{
    if (!song_ || tick < 0) return false;
    int inRow = 0;
    rowAtTick(*song_, ch, tick, row, inRow);
    const Phrase* p = song_->phrase(song_->phraseAt(ch, row));
    const int length = phraseTicks(*song_, p);
    const int steps = p ? p->length() : kEmptyRowTicks / kTicksPerStep;
    int starts[kMaxSteps + 1];
    stepTicks(p, starts, groove(ch));
    for (int s = 0; s < steps; ++s) {
        if (starts[s] >= length) break;
        if (starts[s] == inRow) { step = s; return true; }
    }
    return false;
}

bool Player::nextStep(int ch, int& row, int& step, int64_t& stepTick) const
{
    if (!song_) return false;
    int starts[kMaxSteps + 1];
    const Phrase* p = song_->phrase(song_->phraseAt(ch, row));
    const int steps = p ? p->length() : kEmptyRowTicks / kTicksPerStep;
    if (step + 1 < steps) {
        stepTicks(p, starts, groove(ch));
        if (starts[step + 1] < phraseTicks(*song_, p)) {
            ++step;
            stepTick = rowStartTick(*song_, ch, row) + starts[step];
            return true;
        }
    }
    ++row;
    step = 0;
    stepTicks(song_->phrase(song_->phraseAt(ch, row)), starts, groove(ch));
    stepTick = rowStartTick(*song_, ch, row) + starts[0];
    return true;
}

bool Player::stepHasNote(int ch, int row, int step) const
{
    if (!song_ || step < 0 || step >= kMaxSteps) return false;
    const Phrase* p = song_->phrase(song_->phraseAt(ch, row));
    if (!p) return false;
    const uint8_t n = p->cells[size_t(step)].note;
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
    int row = 0, step = 0; int64_t at = 0;
    if (!quantise(ch, tick, row, step, at)) return false;
    out = RecordMessage{};
    out.channel = uint8_t(c);
    if (noteOff) {
        if (at == recNoteStep_[c]) {
            // Its own step: a note shorter than a step becomes one step long.
            // A different note-on there ends this note by itself.
            if (recNote_[c] != note) return false;
            if (!nextStep(int(c), row, step, at)) return false;
        }
        if (stepHasNote(int(c), row, step)) return false;   // occupied: the note there ends it
        out.row = uint16_t(std::clamp(row, 0, 65535));
        out.step = uint8_t(std::clamp(step, 0, kMaxSteps - 1));
        out.cell.note = kNoteOff;
        return true;
    }
    out.row = uint16_t(std::clamp(row, 0, 65535));
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
    int row = 0, step = 0; int64_t at = 0;
    if (!quantise(ch, tick, row, step, at)) return false;
    (void)at;
    bank::Command o1, o2;
    slotCells(int(c), c1, c2, force ? SlotWrite::All : SlotWrite::Changed, o1, o2);
    if (o1.cmd == bank::Cmd::None && o2.cmd == bank::Cmd::None) return false;
    out = RecordMessage{};
    out.channel = uint8_t(c);
    out.row = uint16_t(std::clamp(row, 0, 65535));
    out.step = uint8_t(std::clamp(step, 0, kMaxSteps - 1));
    out.slotsOnly = true;
    out.cell.cmd1 = o1; out.cell.cmd2 = o2;
    return true;
}

} // namespace chipboy::tracker
