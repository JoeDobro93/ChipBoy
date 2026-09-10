// ChipBoy -- the LSDj import's file side (docs/plan-lsdj-import.md section 5).
#include "plugin/shared/LsdjImport.h"

namespace chipboy::plugin {

bool readSave(const juce::File& file, SavePreview& out, juce::String& error)
{
    out = SavePreview{};
    out.file = file;
    juce::MemoryBlock block;
    if (!file.existsAsFile() || !file.loadFileAsData(block)) { error = "could not read " + file.getFileName(); return false; }
    out.bytes.assign(static_cast<const uint8_t*>(block.getData()), static_cast<const uint8_t*>(block.getData()) + block.getSize());
    std::string err;
    if (!lsdj::indexSave(out.bytes.data(), out.bytes.size(), out.index, err)) { error = juce::String(err); return false; }
    out.romVersion = romVersionIn(file.getParentDirectory(), out.index.workingFormat, out.romFile);
    if (out.romFile != juce::File()) {
        juce::MemoryBlock rom;
        if (out.romFile.loadFileAsData(rom)) out.kits = lsdj::readKits(static_cast<const uint8_t*>(rom.getData()), rom.getSize());
    }
    return true;
}

juce::String romVersionIn(const juce::File& folder, int preferFormat, juce::File& romOut)
{
    romOut = juce::File();
    if (!folder.isDirectory()) return {};
    juce::String best; juce::File bestFile; bool bestFits = false;
    const auto* want = lsdj::lsdjModelForFormat(preferFormat);
    for (const auto& f : folder.findChildFiles(juce::File::findFiles, false, "*.gb")) {
        juce::FileInputStream in(f);
        if (!in.openedOk()) continue;
        std::vector<uint8_t> head(0x150, 0);
        const int n = in.read(head.data(), int(head.size()));
        if (n < 0x150) continue;
        const std::string v = lsdj::romVersion(head.data(), head.size());
        if (v.empty()) continue;
        const bool fits = want != nullptr && lsdj::lsdjModelForRomVersion(v.c_str()) == want;
        // The ROM that reads the song's format wins; among equals, the newer.
        if (bestFile == juce::File() || (fits && !bestFits) || (fits == bestFits && juce::String(v).compareNatural(best) > 0)) { best = v; bestFile = f; bestFits = fits; }
    }
    romOut = bestFile;
    return best;
}

const lsdj::LsdjModel& autoModel(int formatVersion, const juce::String& romVersion)
{
    if (const auto* m = lsdj::lsdjModelForFormat(formatVersion)) return *m;
    if (const auto* m = lsdj::lsdjModelForRomVersion(romVersion.toRawUTF8())) return *m;
    return lsdj::lsdjLatestModel();
}

} // namespace chipboy::plugin
