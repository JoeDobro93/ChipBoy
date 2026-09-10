// ChipBoy -- the LSDj import's file side (docs/plan-lsdj-import.md section 5).
#include "plugin/shared/LsdjImport.h"

namespace chipboy::plugin {

namespace {
/// The ROM beside `file`, its version and kits, into the preview.
void sniffRom(const juce::File& file, int preferFormat, SavePreview& out)
{
    out.romVersion = romVersionIn(file.getParentDirectory(), preferFormat, out.romFile);
    out.kits.clear();
    if (out.romFile != juce::File()) {
        juce::MemoryBlock rom;
        if (out.romFile.loadFileAsData(rom)) out.kits = lsdj::readKits(static_cast<const uint8_t*>(rom.getData()), rom.getSize());
    }
}
} // namespace

bool readSave(const juce::File& file, SavePreview& out, juce::String& error)
{
    out = SavePreview{};
    out.file = file;
    juce::MemoryBlock block;
    if (!file.existsAsFile() || !file.loadFileAsData(block)) { error = "could not read " + file.getFileName(); return false; }
    const auto* data = static_cast<const uint8_t*>(block.getData());
    if (lsdj::looksLikeProject(data, block.getSize())) return readProject(file, out, error);
    out.bytes.assign(data, data + block.getSize());
    std::string err;
    if (!lsdj::indexSave(out.bytes.data(), out.bytes.size(), out.index, err)) { error = juce::String(err); return false; }
    sniffRom(file, out.index.workingFormat, out);
    return true;
}

bool readProject(const juce::File& file, SavePreview& out, juce::String& error)
{
    juce::MemoryBlock block;
    if (!file.existsAsFile() || !file.loadFileAsData(block)) { error = "could not read " + file.getFileName(); return false; }
    ProjectSong p;
    p.file = file;
    std::string name, err; int version = -1;
    if (!lsdj::decompressProject(static_cast<const uint8_t*>(block.getData()), block.getSize(), name, version, p.song, err)) { error = file.getFileName() + ": " + juce::String(err); return false; }
    p.name = name.empty() ? file.getFileNameWithoutExtension().toUpperCase() : juce::String(name);
    p.formatVersion = lsdj::formatVersionOf(p.song.data(), p.song.size());
    const bool first = out.bytes.empty() && out.projects.empty();
    out.projects.push_back(std::move(p));
    if (first) { out.file = file; sniffRom(file, out.projects.back().formatVersion, out); }
    return true;
}

bool readImportFiles(const juce::Array<juce::File>& files, SavePreview& out, juce::String& error)
{
    out = SavePreview{};
    juce::StringArray errors;
    bool haveSave = false;
    for (const auto& f : files) {
        juce::MemoryBlock head;
        juce::FileInputStream in(f);
        if (!in.openedOk()) { errors.add("could not read " + f.getFileName()); continue; }
        const bool isSave = in.getTotalLength() == juce::int64(lsdj::kSaveSize);
        juce::String err;
        if (isSave) {
            if (haveSave) { errors.add(f.getFileName() + ": one save at a time; skipped"); continue; }
            SavePreview save;
            if (!readSave(f, save, err)) { errors.add(err); continue; }
            // The save's index, file, ROM and kits lead; the projects read so far come along.
            save.projects.insert(save.projects.begin(), out.projects.begin(), out.projects.end());
            out = std::move(save);
            haveSave = true;
        } else if (!readProject(f, out, err)) errors.add(err);
    }
    error = errors.joinIntoString("; ");
    return haveSave || !out.projects.empty();
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
