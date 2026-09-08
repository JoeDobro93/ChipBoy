// ChipBoy -- bank and song serialisation (spec section 14.1). JSON via juce,
// which keeps the core free of it. Unknown fields are preserved on the var
// level by callers that round-trip whole objects; here every known field is
// written, and missing fields take defaults on read.
#pragma once

#include "core/Bank/Bank.h"
#include "core/Tracker/Song.h"

#include <juce_core/juce_core.h>

namespace chipboy::plugin {

juce::var bankToVar(const bank::Bank& b);
bool      bankFromVar(const juce::var& v, bank::Bank& out);
juce::var songToVar(const tracker::Song& s);
bool      songFromVar(const juce::var& v, tracker::Song& out);

/// One instrument on its own (the Voice plugin's local instrument, spec 12.5).
juce::var    instrumentToVar(const bank::Instrument& i);
bool         instrumentFromVar(const juce::var& v, bank::Instrument& out);

/// The bank's parts on their own, for an instrument preset file
/// (docs/COMMANDS_AND_TEMPO.md section 15): the same writers and readers the
/// bank file uses, so a preset and a bank can never drift apart. `slot` is
/// written into the object as the slot the part came from.
juce::var instrumentToVar(const bank::Instrument& i, int slot);
juce::var tableToVar(const bank::Table& t, int slot);
bool      tableFromVar(const juce::var& v, bank::Table& out);
juce::var waveToVar(const bank::Wave& w, int slot);
bool      waveFromVar(const juce::var& v, bank::Wave& out);
juce::var kitToVar(const bank::Kit& k, int slot);
bool      kitFromVar(const juce::var& v, bank::Kit& out);
/// The slot an object written by one of those carries, or 0.
int slotOfVar(const juce::var& v);
juce::String instrumentToJson(const bank::Instrument& i);
bool         instrumentFromJson(const juce::String& text, bank::Instrument& out);

juce::String bankToJson(const bank::Bank& b);
bool         bankFromJson(const juce::String& text, bank::Bank& out);
juce::String songToJson(const tracker::Song& s);
bool         songFromJson(const juce::String& text, tracker::Song& out);

} // namespace chipboy::plugin
