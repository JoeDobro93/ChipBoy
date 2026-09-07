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

juce::String bankToJson(const bank::Bank& b);
bool         bankFromJson(const juce::String& text, bank::Bank& out);
juce::String songToJson(const tracker::Song& s);
bool         songFromJson(const juce::String& text, tracker::Song& out);

} // namespace chipboy::plugin
