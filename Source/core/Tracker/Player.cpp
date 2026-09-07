#include "core/Tracker/Player.h"

#include <algorithm>
#include <cmath>

namespace chipboy::tracker {

using driver::NoteEvent;

void Player::prepare(double sampleRate) { sampleRate_ = sampleRate; lastPpq_ = -1.0; playing_ = false; for (auto& p : pos_) p = Position{}; }

void Player::stepTicks(const Phrase* p, int stepsPerBar, int* start) const
{
    // Base: a bar is beatsPerBar * ticksPerBeat ticks; a straight step is its
    // share; the groove scales alternate steps by a/6 and b/6.
    const double barTicks = beatsPerBar_ * ticksPerBeat_;
    const double base = barTicks / std::max(1, stepsPerBar);
    Groove g;
    if (p && song_ && p->groove > 0 && p->groove <= 16) g = song_->grooves[size_t(p->groove - 1)];
    double acc = 0.0;
    for (int s = 0; s < stepsPerBar && s < kSteps; ++s) {
        start[s] = int(std::lround(acc));
        acc += base * ((s & 1) ? g.b : g.a) / 6.0;
    }
    for (int s = std::min(stepsPerBar, kSteps); s <= kSteps; ++s) start[s] = int(std::lround(barTicks));
}

void Player::process(const driver::Transport& t, uint32_t numSamples, std::vector<NoteEvent>& out)
{
    if (!song_ || !t.valid || !t.playing) {
        if (playing_) { for (int ch = 0; ch < 4; ++ch) if (lastNote_[ch]) { NoteEvent e; e.kind = NoteEvent::NoteOff; e.source = NoteEvent::Tracker; e.channel = uint8_t(ch); e.a = lastNote_[ch]; out.push_back(e); lastNote_[ch] = 0; } }
        playing_ = false; lastPpq_ = -1.0;
        for (auto& p : pos_) p = Position{};
        return;
    }
    playing_ = true;
    const double ppqPerFrame = t.bpm / 60.0 / sampleRate_;
    const double ppqStart = t.ppq, ppqEnd = t.ppq + numSamples * ppqPerFrame;
    const double ticksPerPpq = ticksPerBeat_;
    const int stepsPerBar = std::clamp<int>(song_->stepsPerBar, 4, kSteps);

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
        // bars touching this block
        const int barFirst = int(std::floor((ppqStart - ppqPerFrame) / beatsPerBar_));
        const int barLast = int(std::floor((ppqEnd + ppqPerFrame) / beatsPerBar_));
        for (int bar = std::max(0, barFirst); bar <= barLast; ++bar) {
            const uint8_t slot = song_->phraseAt(ch, bar);
            const Phrase* ph = song_->phrase(slot);
            int starts[kSteps + 1];
            stepTicks(ph, stepsPerBar, starts);
            const double barPpq = bar * beatsPerBar_;
            for (int s = 0; s < stepsPerBar; ++s) {
                const double stepPpq = barPpq + starts[s] / ticksPerPpq;
                // Round to a frame first so a step on a block boundary lands
                // in exactly one block whatever the block size.
                const long long offFrames = std::llround((stepPpq - ppqStart) / ppqPerFrame);
                if (offFrames < 0 || offFrames >= (long long)numSamples) continue;
                pos_[size_t(ch)] = { bar, s, slot };
                const uint32_t off = uint32_t(offFrames);
                if (!ph) {
                    // A bar with no phrase is silence: end the note at its first step.
                    if (s == 0 && lastNote_[ch]) { NoteEvent e; e.offset = off; e.channel = uint8_t(ch); e.source = NoteEvent::Tracker; e.kind = NoteEvent::NoteOff; e.a = lastNote_[ch]; out.push_back(e); lastNote_[ch] = 0; }
                    continue;
                }
                const Cell& c = ph->steps[size_t(s)];
                if (c.note == 0 && c.inst == 0 && c.table == 0 && c.cmd1.cmd == bank::Cmd::None && c.cmd2.cmd == bank::Cmd::None) continue;
                NoteEvent e;
                e.offset = off; e.channel = uint8_t(ch); e.source = NoteEvent::Tracker;
                e.inst = c.inst; e.table = c.table; e.cmd1 = c.cmd1; e.cmd2 = c.cmd2;
                if (c.note == kNoteOff) { e.kind = NoteEvent::NoteOff; e.a = lastNote_[ch]; lastNote_[ch] = 0; }
                else if (c.note) { e.kind = NoteEvent::NoteOn; e.a = c.note; e.b = 100; lastNote_[ch] = c.note; }
                else e.kind = NoteEvent::Command;
                out.push_back(e);
            }
        }
    }
    lastPpq_ = ppqEnd;
}

bool Player::quantise(const driver::Transport& t, uint32_t offset, uint32_t numSamples, int& bar, int& step, double& ppqOfStep) const
{
    (void)numSamples;
    if (!song_ || !t.valid) return false;
    const double ppq = t.ppq + offset * (t.bpm / 60.0 / sampleRate_);
    bar = int(std::floor(ppq / beatsPerBar_));
    const int stepsPerBar = std::clamp<int>(song_->stepsPerBar, 4, kSteps);
    int starts[kSteps + 1];
    stepTicks(nullptr, stepsPerBar, starts);
    const double inBarTicks = (ppq - bar * beatsPerBar_) * ticksPerBeat_;
    int best = 0; double bestD = 1e9;
    for (int s = 0; s <= stepsPerBar; ++s) { const double d = std::fabs(starts[s] - inBarTicks); if (d < bestD) { bestD = d; best = s; } }
    if (best >= stepsPerBar) { ++bar; best = 0; }
    step = best;
    ppqOfStep = bar * beatsPerBar_ + starts[best] / double(ticksPerBeat_);
    return bar >= 0;
}

} // namespace chipboy::tracker
