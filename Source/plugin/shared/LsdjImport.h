// ChipBoy -- the LSDj import's file side (docs/plan-lsdj-import.md section
// 5): reading a .sav, sniffing the ROM beside it, choosing a model. The bytes
// themselves are read by core/Import.
#pragma once

#include "core/Import/LsdjKits.h"
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
    std::vector<lsdj::LsdjKit> kits;     ///< that ROM's kit banks, for the kit instruments
};

/// Reads the save and indexes it; false with a message when it is not one.
bool readSave(const juce::File& file, SavePreview& out, juce::String& error);
/// The LSDj ROM (*.gb) in a folder to read kits and a version from: the one
/// whose version reads the working song's format when several are there,
/// else the newest. Returns its version, empty when there is none.
juce::String romVersionIn(const juce::File& folder, int preferFormat, juce::File& romOut);
/// The model a song takes on its own: its format's, else the ROM's, else the
/// newest (plan section 3).
const lsdj::LsdjModel& autoModel(int formatVersion, const juce::String& romVersion);

} // namespace chipboy::plugin
