// ChipBoy -- the LSDj import's file side (docs/plan-lsdj-import.md section
// 5): reading a .sav, sniffing the ROM beside it, choosing a model. The bytes
// themselves are read by core/Import.
#pragma once

#include "core/Import/LsdjModel.h"
#include "core/Import/LsdjSave.h"

#include <juce_core/juce_core.h>

#include <vector>

namespace chipboy::plugin {

/// A save read into memory with its index and what the folder says.
struct SavePreview {
    juce::File file;
    std::vector<uint8_t> bytes;
    lsdj::SaveIndex index;
    juce::String romVersion;             ///< "9.3.9" from a ROM beside the save, empty when none
    juce::File romFile;
};

/// Reads the save and indexes it; false with a message when it is not one.
bool readSave(const juce::File& file, SavePreview& out, juce::String& error);
/// The version of the first LSDj ROM (*.gb) in a folder, and which file it was.
juce::String romVersionIn(const juce::File& folder, juce::File& romOut);
/// The model a song takes on its own: its format's, else the ROM's, else the
/// newest (plan section 3).
const lsdj::LsdjModel& autoModel(int formatVersion, const juce::String& romVersion);

} // namespace chipboy::plugin
