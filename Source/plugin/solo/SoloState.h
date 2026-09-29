// ChipBoy Solo -- what an instance keeps beside its parameters and its bank
// (docs/plan-solo.md sections 2 and 3): the sixty-four sounds, the command
// library and the key map. Plain data and its JSON; nothing here runs on
// the audio thread but a read of a published copy.
#pragma once

#include "core/Bank/Bank.h"

#include <juce_core/juce_core.h>

#include <array>
#include <cstdint>
#include <string>

namespace chipboy::plugin {

constexpr int kSoloSounds = 64;
constexpr int kSoloCommands = 32;

/// A row without its note: what a sound recalls into the main window.
struct SoloSound {
    bool        used = false;
    std::string name;
    uint8_t     inst = 0, table = 0;   ///< 0 none / the instrument's, as a cell's columns
    bank::Command cmd1, cmd2;
};

/// One entry of the library: a command a musician keeps to hand.
struct SoloCommand {
    bool        used = false;
    std::string name;
    bank::Command cmd;
};

struct SoloState {
    std::array<SoloSound, kSoloSounds>     sounds;
    std::array<SoloCommand, kSoloCommands> commands;
    /// Per channel (PU1, PU2, WAV, NOI -- their ranges differ), the sound
    /// (1-64) a MIDI note outside the range selects; 0 none. Entries on
    /// playable notes are kept but never read.
    std::array<std::array<uint8_t, 128>, 4> keyMaps{};
};

/// The notes a channel (0-3) has a period for: pulse 36 up, wave 24 up, to
/// the highest note the period register holds; the noise channel's map is
/// 12-127 with nothing above.
void soloNoteRange(int channel, int& lo, int& hi);
bool soloKeyMappable(int channel, int note);
/// The default layout: the keys below the floor counting down are sounds
/// 1, 2, 3 ...; the keys above the ceiling counting up carry on from there.
void soloDefaultKeyMap(int channel, std::array<uint8_t, 128>& map);

juce::var soloStateToVar(const SoloState& s);
bool      soloStateFromVar(const juce::var& v, SoloState& out);

} // namespace chipboy::plugin
