// ChipBoy -- tracker playback and record (UI_DESIGN section 7,
// docs/COMMANDS_AND_TEMPO.md sections 4 and 9).
//
// The song is counted in ticks, not beats: a bar is beatsPerBar x 24 ticks and
// the groove says how many ticks each step lasts. The Clock says which ticks
// fall in this block and what their absolute tick numbers are; a step fires on
// the tick it starts on, so the Host and Song tempo sources are one code path.
// Record turns incoming notes and command slots into cells, quantised to the
// channel's own step grid, as messages for the message thread to apply.
#pragma once

#include "core/Driver/Driver.h"
#include "core/Tracker/Song.h"

#include <cstdint>
#include <vector>

namespace chipboy::tracker {

/// A cell write produced by recording, applied by the message thread. A
/// command column is written only when it carries a letter, so two messages
/// for one step (a note and a slot change) merge whichever order they arrive.
struct RecordMessage {
    uint8_t channel = 0;
    uint16_t phraseSlot = 0;     ///< 0 = allocate the next free phrase and chain it
    uint16_t bar = 0;
    uint8_t step = 0;
    bool    slotsOnly = false;   ///< a slot changed with no note: merge, keep the cell's note
    Cell cell;
};

class Player {
public:
    void prepare(double sampleRate);
    void setSong(const Song* s) { song_ = s; }
    /// Ticks in a bar, from the Clock (the host's signature, or the song's).
    void setBarTicks(int t) { barTicks_ = t > 0 ? t : driver::kTicksPerBeat * 4; }
    /// Channels whose lane is silenced (bit per channel), e.g. while recording.
    void setMuteMask(uint32_t m) { muteMask_ = m; }
    /// The G in this channel's command slots, or kGrooveNone when neither slot
    /// holds one. It is read, not latched: the groove in force is worked out
    /// again at every tick (section 9.2).
    void setGrooveSlot(int ch, uint8_t slot) { grooveParam_[size_t(ch & 3)] = slot; }
    /// The groove slot in force on a channel: the G slot, else the last G cell
    /// that played, else kGrooveNone for the phrase's own.
    uint8_t groove(int ch) const;

    /// Emit this block's tracker events, from the Clock's ticks. Steps sit on
    /// ticks, so nothing here needs the sample rate or the tempo.
    void process(const driver::TickPoint* ticks, size_t nTicks, bool playing, std::vector<driver::NoteEvent>& out);

    /// Recording: the step of this channel's own grid nearest an absolute
    /// tick, and where that step is.
    bool quantise(int ch, double tick, int& bar, int& step, int64_t& stepTick) const;
    /// Does a step of this channel's grid start on that tick? The recorder
    /// reads the command slots at the step, not at the block start (9.4).
    bool stepAt(int ch, int64_t tick, int& bar, int& step) const;

    /// The cell a note writes (section 9.4): the note and its velocity, the
    /// instrument column filled when the note was plain and blank when it was
    /// bare, the table override, and the slots the step must carry. A note-off
    /// writes OFF at its step, or at the next one when that is the note's own.
    bool recordNote(int ch, double tick, uint8_t note, uint8_t velocity, bool noteOff, bool plain,
                    uint8_t instrument, uint8_t table, const bank::Command& c1, const bank::Command& c2,
                    RecordMessage& out);
    /// A step whose slots differ from the last written on this channel, with
    /// no note of its own.
    bool recordSlots(int ch, double tick, const bank::Command& c1, const bank::Command& c2, RecordMessage& out);
    void resetRecord();

    struct Position { int bar = -1; int step = -1; uint8_t phrase = 0; };
    const Position& position(int ch) const { return pos_[size_t(ch & 3)]; }
    bool playing() const { return playing_; }

    /// Step boundaries of a phrase in ticks from the bar start (kSteps + 1).
    void stepTicks(const Phrase* p, int* startTicks, uint8_t grooveSlot = kGrooveNone) const;

private:
    void fireStep(int ch, int bar, int step, uint8_t phraseSlot, uint32_t offset, std::vector<driver::NoteEvent>& out);
    /// Silence a channel unconditionally (section 9.1): a Tracker note-off is
    /// dropped by the driver's source gate once the lane has gone.
    void allNotesOff(int ch, uint32_t offset, std::vector<driver::NoteEvent>& out);
    /// What the two command columns at a step hold, and what that leaves as
    /// the last written on the channel.
    void slotCells(int ch, const bank::Command& c1, const bank::Command& c2,
                   bool plainNote, bank::Command& o1, bank::Command& o2);
    bool stepHasNote(int ch, int bar, int step) const;
    /// The step after this one, wrapping into the next bar.
    bool nextStep(int ch, int& bar, int& step, int64_t& stepTick) const;

    const Song* song_ = nullptr;
    double sampleRate_ = 48000.0;
    int barTicks_ = driver::kTicksPerBeat * 4;
    bool playing_ = false;
    uint32_t muteMask_ = 0;
    Position pos_[4];
    uint8_t lastNote_[4] = { 0, 0, 0, 0 };
    bool    laneOn_[4] = { false, false, false, false };   ///< the lane played last block
    int64_t lastTick_ = -1;
    bool    haveTick_ = false;
    // The last step each channel fired, so a groove change mid-bar moves the
    // steps that follow without playing one twice.
    int     firedBar_[4] = { -1, -1, -1, -1 };
    int     firedStep_[4] = { -1, -1, -1, -1 };
    uint8_t grooveParam_[4] = { kGrooveNone, kGrooveNone, kGrooveNone, kGrooveNone };  ///< from a G slot
    uint8_t grooveCell_[4] = { kGrooveNone, kGrooveNone, kGrooveNone, kGrooveNone };   ///< from the last G cell
    // what the recorder last wrote on each channel: the slots in force, and
    // the note-on it last placed, which is where a note-off may not go.
    bank::Command recSlots_[4][2]{};
    int64_t recNoteStep_[4] = { -1, -1, -1, -1 };
    uint8_t recNote_[4] = { 0, 0, 0, 0 };
};

} // namespace chipboy::tracker
