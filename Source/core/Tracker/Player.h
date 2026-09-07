// ChipBoy -- tracker playback and record (UI_DESIGN section 7).
//
// Turns the song into note events against the host transport: a step fires
// when its ppq position falls inside the block, grooves stretching steps by
// their tick counts. Record turns incoming notes into cells, quantised to
// the step grid, as messages for the message thread to apply.
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
    Cell cell;
};

class Player {
public:
    void prepare(double sampleRate);
    void setSong(const Song* s) { song_ = s; }
    void setTicksPerBeat(int tpb) { ticksPerBeat_ = tpb > 0 ? tpb : 24; }
    void setBeatsPerBar(double b) { beatsPerBar_ = b > 0.0 ? b : 4.0; }

    /// Emit this block's tracker events. Only while the transport plays.
    void process(const driver::Transport& t, uint32_t numSamples, std::vector<driver::NoteEvent>& out);

    /// Recording: quantise an incoming note to the nearest step of the bar
    /// the transport is in, and describe the cell to write.
    bool quantise(const driver::Transport& t, uint32_t offset, uint32_t numSamples, int& bar, int& step, double& ppqOfStep) const;

    struct Position { int bar = -1; int step = -1; uint8_t phrase = 0; };
    const Position& position(int ch) const { return pos_[size_t(ch & 3)]; }
    bool playing() const { return playing_; }

    /// Step boundaries of a phrase in ticks from the bar start, per the groove.
    void stepTicks(const Phrase* p, int stepsPerBar, int* startTicks /*[17]*/) const;

private:
    const Song* song_ = nullptr;
    double sampleRate_ = 48000.0;
    int ticksPerBeat_ = 24;
    double beatsPerBar_ = 4.0;
    bool playing_ = false;
    double lastPpq_ = -1.0;
    Position pos_[4];
    uint8_t lastNote_[4] = { 0, 0, 0, 0 };
};

} // namespace chipboy::tracker
