// ChipBoy -- instrument preset files (docs/COMMANDS_AND_TEMPO.md section 15).
//
// A `.cbi` is one instrument with every table, wave and kit it references,
// transitively, written with the bank file's own writers -- so a preset and a
// bank can never disagree about what an instrument is. The walk and the
// placement are core code (core/Bank/Preset.h); this is the file.
#pragma once

#include "core/Bank/Preset.h"

#include <juce_core/juce_core.h>

namespace chipboy::plugin {

juce::String presetToJson(const bank::Preset& p);
bool         presetFromJson(const juce::String& text, bank::Preset& out);

bool savePreset(const bank::Preset& p, const juce::File& file);
bool loadPreset(const juce::File& file, bank::Preset& out);

/// Documents/ChipBoy/Instruments, beside the banks folder.
juce::File presetsFolder();
constexpr const char* kPresetExtension = ".cbi";

} // namespace chipboy::plugin
