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

/// One project file (.lsdprj, .lsdsng; plan section 1a), already decompressed.
struct ProjectSong {
    juce::File file;
    juce::String name;                   ///< the 8-character name inside the file
    int formatVersion = -1;
    std::vector<uint8_t> song;           ///< 32 KB
};

/// What the import dialog lists: a save read into memory with its index, the
/// project files chosen beside it, and what the folder says.
struct SavePreview {
    juce::File file;                     ///< the save, or the first project when there is no save
    std::vector<uint8_t> bytes;          ///< the save; empty when only projects were chosen
    lsdj::SaveIndex index;
    std::vector<ProjectSong> projects;
    juce::String romVersion;             ///< "9.3.9" from a ROM beside the save, empty when none
    juce::File romFile;
    std::vector<lsdj::LsdjKit> kits;     ///< that ROM's kit banks, for the kit instruments
};

/// Reads the save and indexes it; false with a message when it is not one. A
/// project file is taken too: it becomes the preview's one project.
bool readSave(const juce::File& file, SavePreview& out, juce::String& error);
/// Adds a project file to a preview (the first one also sets its file, ROM
/// and kits); false with a message when the file is not one.
bool readProject(const juce::File& file, SavePreview& out, juce::String& error);
/// The files a chooser handed over, saves and projects together, into one
/// preview: the first save is read, every project added; false with a message
/// when none could be read.
bool readImportFiles(const juce::Array<juce::File>& files, SavePreview& out, juce::String& error);
/// The LSDj ROM (*.gb) in a folder to read kits and a version from: the one
/// whose version reads the working song's format when several are there,
/// else the newest. Returns its version, empty when there is none.
juce::String romVersionIn(const juce::File& folder, int preferFormat, juce::File& romOut);
/// The model a song takes on its own: its format's, else the ROM's, else the
/// newest (plan section 3).
const lsdj::LsdjModel& autoModel(int formatVersion, const juce::String& romVersion);

} // namespace chipboy::plugin
