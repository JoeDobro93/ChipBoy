// ChipBoy -- the tracker's song (spec section 13.1, UI_DESIGN section 7).
//
// Phrases of sixteen steps, chained by bar per channel, following the host
// transport. Self-contained with the bank on purpose: a song must be able to
// leave for a playback ROM one day (spec section 15.3).
#pragma once

#include "core/Bank/Bank.h"

#include <array>
#include <cstdint>
#include <vector>

namespace chipboy::tracker {

constexpr int kPhraseSlots = 255;
constexpr int kSteps = 16;
constexpr uint8_t kNoteOff = 255;

struct Cell {
    uint8_t note = 0;            ///< 0 empty, 1-127 MIDI note, 255 note off
    uint8_t inst = 0;            ///< 0 keep, else instrument slot
    uint8_t table = 0;           ///< 0 keep, else table slot
    bank::Command cmd1, cmd2;
};
struct Phrase {
    bool used = false;
    std::array<Cell, kSteps> steps{};
    uint8_t groove = 0;          ///< groove slot, 0 = straight
};
struct Groove { uint8_t a = 6, b = 6; };   ///< ticks per step, alternating
enum class NoteSource : uint8_t { PianoRoll = 0, Tracker = 1 };

struct Song {
    std::array<Phrase, kPhraseSlots> phrases;         ///< slot n is phrases[n-1]
    std::array<std::vector<uint8_t>, 4> chain;        ///< per channel: bar -> phrase slot (0 none)
    std::array<NoteSource, 4> noteSource{ NoteSource::PianoRoll, NoteSource::PianoRoll, NoteSource::PianoRoll, NoteSource::PianoRoll };
    std::array<Groove, 16> grooves{};
    uint8_t stepsPerBar = 16;

    const Phrase* phrase(int slot) const { return slot >= 1 && slot <= kPhraseSlots && phrases[size_t(slot - 1)].used ? &phrases[size_t(slot - 1)] : nullptr; }
    uint8_t phraseAt(int ch, int bar) const { const auto& c = chain[size_t(ch & 3)]; return bar >= 0 && size_t(bar) < c.size() ? c[size_t(bar)] : 0; }
};

} // namespace chipboy::tracker
