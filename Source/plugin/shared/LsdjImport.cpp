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
    out.romVersion = romVersionIn(file.getParentDirectory(), out.romFile);
    return true;
}

juce::String romVersionIn(const juce::File& folder, juce::File& romOut)
{
    romOut = juce::File();
    if (!folder.isDirectory()) return {};
    for (const auto& f : folder.findChildFiles(juce::File::findFiles, false, "*.gb")) {
        juce::FileInputStream in(f);
        if (!in.openedOk()) continue;
        std::vector<uint8_t> head(0x150, 0);
        const int n = in.read(head.data(), int(head.size()));
        if (n < 0x150) continue;
        const std::string v = lsdj::romVersion(head.data(), head.size());
        if (!v.empty()) { romOut = f; return juce::String(v); }
    }
    return {};
}

const lsdj::LsdjModel& autoModel(int formatVersion, const juce::String& romVersion)
{
    if (const auto* m = lsdj::lsdjModelForFormat(formatVersion)) return *m;
    if (const auto* m = lsdj::lsdjModelForRomVersion(romVersion.toRawUTF8())) return *m;
    return lsdj::lsdjLatestModel();
}

} // namespace chipboy::plugin
