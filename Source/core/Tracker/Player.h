// ChipBoy -- tracker playback and record (UI_DESIGN section 7,
// docs/COMMANDS_AND_TEMPO.md section 4).
//
// The song is counted in ticks, not beats: a bar is beatsPerBar x 24 ticks, a
// straight step is its share of the bar, and a groove stretches alternate
// steps. The Clock says which ticks fall in this block and what their absolute
// tick numbers are; a step fires on the tick it starts on, so the Host and Song
// tempo sources are one code path. Record turns incoming notes into cells,
// quantised to the step grid, as messages for the message thread to apply.
#pragma once

#include "core/Driver/Driver.h"
#include "core/Tracker/Song.h"

#include <cstdint>
#include <vector>

namespace chipboy::tracker {

/// A cell write produced by recording, applied by the message thread.
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
    /// A G slot on the host lane: the groove this channel plays in. It follows
    /// the command-slot rule -- it applies when it changes, and kGrooveNone
    /// gives the phrase its own groove back.
    void setGrooveOverride(int ch, uint8_t slot);
    /// The groove in force on a channel, for the running-state line.
    uint8_t groove(int ch) const { return groove_[size_t(ch & 3)]; }

    /// Emit this block's tracker events, from the Clock's ticks. Steps sit on
    /// ticks, so nothing here needs the sample rate or the tempo.
    void process(const driver::TickPoint* ticks, size_t nTicks, bool playing, std::vector<driver::NoteEvent>& out);

    /// Recording: the step nearest an absolute tick, and where that step is.
    bool quantise(double tick, int& bar, int& step, int64_t& stepTick) const;
    /// The cell a note writes at that tick (section 5): the note, the
    /// instrument when it differs from the last cell written on this channel,
    /// the table override and the two slots in force.
    bool recordNote(int ch, double tick, uint8_t note, bool noteOff, uint8_t instrument, uint8_t table,
                    const bank::Command& c1, const bank::Command& c2, RecordMessage& out);
    /// A slot that changed since the last step written, with no note.
    bool recordSlots(int ch, double tick, const bank::Command& c1, const bank::Command& c2, RecordMessage& out);
    void resetRecord();

    struct Position { int bar = -1; int step = -1; uint8_t phrase = 0; };
    const Position& position(int ch) const { return pos_[size_t(ch & 3)]; }
    bool playing() const { return playing_; }

    /// Step boundaries of a phrase in ticks from the bar start (kSteps + 1).
    void stepTicks(const Phrase* p, int* startTicks, uint8_t grooveSlot = kGrooveNone) const;

private:
    void fireStep(int ch, int bar, int step, uint8_t phraseSlot, uint32_t offset, std::vector<driver::NoteEvent>& out);

    const Song* song_ = nullptr;
    double sampleRate_ = 48000.0;
    int barTicks_ = driver::kTicksPerBeat * 4;
    bool playing_ = false;
    uint32_t muteMask_ = 0;
    Position pos_[4];
    uint8_t lastNote_[4] = { 0, 0, 0, 0 };
    uint8_t groove_[4] = { kGrooveNone, kGrooveNone, kGrooveNone, kGrooveNone };       ///< in force
    uint8_t grooveParam_[4] = { kGrooveNone, kGrooveNone, kGrooveNone, kGrooveNone };  ///< last from a G slot
    // what the recorder last wrote on each channel
    uint8_t recInst_[4] = { 0, 0, 0, 0 };
    bank::Command recSlots_[4][2]{};
    int64_t recStep_[4] = { -1, -1, -1, -1 };
};

} // namespace chipboy::tracker
