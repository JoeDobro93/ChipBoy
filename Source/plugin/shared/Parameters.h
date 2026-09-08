// ChipBoy -- the host-visible parameter layout (spec section 12).
//
// Every parameter is discrete with the hardware's range, except the output
// trim (C3). Per-channel parameters carry an extra "inst" position meaning
// "use the instrument's value", so automation overrides only when drawn.
#pragma once

#include "core/Bank/Bank.h"
#include "core/Driver/Driver.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <string>

namespace chipboy::plugin {

namespace ids {
constexpr const char* model = "model";
constexpr const char* masterL = "master_l";
constexpr const char* masterR = "master_r";
constexpr const char* trim = "trim";
constexpr const char* noise = "noise";   ///< the hiss and the frame hum (section 21)
constexpr const char* lcd = "lcd";       ///< the display's line, on its own
constexpr const char* bassMod = "bass_mod";
constexpr const char* volEdges = "vol_edges";
constexpr const char* declick = "declick";
constexpr const char* declickMs = "declick_ms";
constexpr const char* soften = "soften";
constexpr const char* tempoSource = "tempo_source";
constexpr const char* songTempo = "song_tempo";
constexpr const char* notesOnTick = "notes_on_tick";
constexpr const char* linkMode = "link";
constexpr const char* hexDisplay = "hex";
// per channel, prefixed "chN_" (N = 1..4) in the main plugin, "v_" in the Voice plugin
constexpr const char* source = "source";
constexpr const char* instrument = "instrument";
constexpr const char* table = "table";
constexpr const char* level = "level";
constexpr const char* pan = "pan";
constexpr const char* transpose = "transpose";
constexpr const char* cmd1Type = "cmd1_type";
constexpr const char* cmd1X = "cmd1_x";
constexpr const char* cmd1Y = "cmd1_y";
constexpr const char* cmd2Type = "cmd2_type";
constexpr const char* cmd2X = "cmd2_x";
constexpr const char* cmd2Y = "cmd2_y";
constexpr const char* liveFollow = "live_follow";
constexpr const char* velocityMode = "velocity";
constexpr const char* keyswitch = "keyswitch";
} // namespace ids

enum class ChannelKind { Pulse1, Pulse2, Wave, Noise, Any };

/// The command lane's choices: none, then the letters a channel can carry.
/// H is missing on purpose -- hop only means anything inside a table.
juce::StringArray commandChoices();
bank::Cmd cmdFromChoice(int index);
int       choiceFromCmd(bank::Cmd c);
/// "vol 12 . down 3": what the two arguments mean for this letter, for the
/// window and the tooltips. The letters whose speed the instrument owns (L, P
/// and V, docs/COMMANDS_AND_TEMPO.md section 7) say the amount, not the unit:
/// what a slide's duration or a bend's step is worth depends on the
/// instrument's pitch speed.
juce::String commandArgText(const bank::Command& c);

/// What a letter's arguments are: how many, the range each one takes and
/// what a fresh command starts at. One table for the lane's steppers, the
/// grids' typed entry and the palette (docs/COMMANDS_AND_TEMPO.md section 2).
struct CommandInfo {
    char        letter;
    const char* name;        ///< "Envelope"
    const char* args;        ///< "vol, 0-7 down / 8-15 up"
    int         nargs;       ///< 1 or 2
    int         lo[2], hi[2], def[2];
};
/// Every letter, H included; null for Cmd::None.
const CommandInfo* commandInfo(bank::Cmd c);
/// A fresh command of this letter, with the arguments a palette would give it.
bank::Command defaultCommand(bank::Cmd c);
/// Whether a letter does anything on this channel (the table in section 2).
/// Any -- the Voice plugin, whose channel moves -- takes every letter.
bool commandAppliesTo(bank::Cmd c, ChannelKind kind);

juce::String channelPrefix(int channel);   ///< "ch1_" .. "ch4_"
juce::String channelParamId(int channel, const char* id);

/// Adds one channel's parameter set to a layout. `kind` decides the ranges;
/// Any (the Voice plugin) includes everything.
void addChannelParameters(juce::AudioProcessorValueTreeState::ParameterLayout& layout,
                          const juce::String& prefix, ChannelKind kind, bool withSource);
/// Adds the main plugin's global parameters.
void addGlobalParameters(juce::AudioProcessorValueTreeState::ParameterLayout& layout);

/// Raw-value pointers for one channel, read on the audio thread.
struct ChannelParamCache {
    std::atomic<float>* source = nullptr;
    std::atomic<float>* instrument = nullptr;
    std::atomic<float>* table = nullptr;
    std::atomic<float>* level = nullptr;
    std::atomic<float>* pan = nullptr;
    std::atomic<float>* transpose = nullptr;
    std::atomic<float>* cmdType[2] = { nullptr, nullptr };
    std::atomic<float>* cmdX[2] = { nullptr, nullptr };
    std::atomic<float>* cmdY[2] = { nullptr, nullptr };
    std::atomic<float>* liveFollow = nullptr;
    std::atomic<float>* velocityMode = nullptr;
    std::atomic<float>* keyswitch = nullptr;

    void bind(juce::AudioProcessorValueTreeState& s, const juce::String& prefix);
    /// Translate to the driver's view. `kind` says which channel this is.
    driver::ChannelParams read(ChannelKind kind) const;
};

/// Value of a raw parameter as an int (choice index or int value).
inline int paramInt(const std::atomic<float>* p, int fallback = 0) { return p ? int(std::lround(p->load())) : fallback; }

/// Source choice index -> (omni, midi channel 1-16, off).
struct SourceChoice { bool omni = false; bool off = false; int midiChannel = 0; };
SourceChoice decodeSource(int choiceIndex);

} // namespace chipboy::plugin
