// ChipBoy -- the LSDj import's file side (docs/plan-lsdj-import.md section 5).
#include "plugin/shared/LsdjImport.h"

namespace chipboy::plugin {

namespace {
/// A ROM's kits and pages into the preview, under the name it is shown as.
void applyRom(const uint8_t* rom, size_t size, const juce::File& shownAs, const juce::String& version, SavePreview& out)
{
    out.romFile = shownAs;
    out.romVersion = version;
    out.kits = lsdj::readKits(rom, size);
    out.rawPages = lsdj::lsdjRawPages(rom, size, version.toStdString());
}
/// The ROM beside `file`, its version and kits, into the preview.
void sniffRom(const juce::File& file, int preferFormat, SavePreview& out)
{
    out.romVersion = romVersionIn(file.getParentDirectory(), preferFormat, out.romFile);
    out.kits.clear();
    if (out.romFile != juce::File()) {
        juce::MemoryBlock rom;
        if (out.romFile.loadFileAsData(rom)) applyRom(static_cast<const uint8_t*>(rom.getData()), rom.getSize(), out.romFile, out.romVersion, out);
    }
}
/// The format the preview's songs are in, for choosing among several ROMs.
int previewFormat(const SavePreview& out)
{
    if (!out.bytes.empty()) return out.index.workingFormat;
    return out.projects.empty() ? -1 : out.projects.front().formatVersion;
}
} // namespace

bool useRomFile(const juce::File& chosen, SavePreview& out, juce::String& error)
{
    if (!chosen.existsAsFile()) { error = "could not read " + chosen.getFileName(); return false; }
    const int preferFormat = previewFormat(out);
    // Every candidate: the file itself, or each .gb inside a zip. The one
    // whose version reads the songs' format wins, then the newest, then any
    // with kit banks at all -- a ROM without a readable version still has kits.
    struct Candidate { juce::MemoryBlock data; juce::String name, version; bool fits = false; int kits = 0; };
    std::vector<Candidate> found;
    auto consider = [&](juce::MemoryBlock&& data, const juce::String& name) {
        if (data.getSize() < 0x8000) return;
        Candidate c; c.data = std::move(data); c.name = name;
        const auto* p = static_cast<const uint8_t*>(c.data.getData());
        c.version = juce::String(lsdj::romVersion(p, c.data.getSize()));
        c.kits = int(lsdj::readKits(p, c.data.getSize()).size());
        const auto* rm = c.version.isEmpty() ? nullptr : lsdj::lsdjModelForRomVersion(c.version.toRawUTF8());
        c.fits = rm != nullptr && preferFormat >= rm->formatMin && preferFormat <= rm->formatMax;
        if (c.kits > 0 || c.version.isNotEmpty()) found.push_back(std::move(c));
    };
    if (chosen.hasFileExtension(".zip")) {
        juce::ZipFile zip(chosen);
        for (int i = 0; i < zip.getNumEntries(); ++i) {
            const auto* e = zip.getEntry(i);
            if (e == nullptr || !e->filename.endsWithIgnoreCase(".gb")) continue;
            std::unique_ptr<juce::InputStream> in(zip.createStreamForEntry(i));
            if (in == nullptr) continue;
            juce::MemoryBlock data;
            in->readIntoMemoryBlock(data);
            consider(std::move(data), chosen.getFileName() + "/" + e->filename);
        }
    } else {
        juce::MemoryBlock data;
        if (chosen.loadFileAsData(data)) consider(std::move(data), chosen.getFileName());
    }
    if (found.empty()) { error = chosen.getFileName() + " holds no LSDj ROM with kits"; return false; }
    const Candidate* best = nullptr;
    for (const auto& c : found)
        if (best == nullptr || (c.fits && !best->fits) || (c.fits == best->fits && c.version.compareNatural(best->version) > 0)) best = &c;
    applyRom(static_cast<const uint8_t*>(best->data.getData()), best->data.getSize(), chosen, best->version, out);
    if (best->name != chosen.getFileName()) out.romFile = chosen.getParentDirectory().getChildFile(best->name);   // shown as zip/entry
    return true;
}

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
    p.kits = lsdj::projectKits(static_cast<const uint8_t*>(block.getData()), block.getSize(), p.song);   // section 218
    p.kitNumbers = lsdj::songKitNumbers(p.song.data(), p.song.size(), true);
    const bool first = out.bytes.empty() && out.projects.empty();
    out.projects.push_back(std::move(p));
    if (first) { out.file = file; sniffRom(file, out.projects.back().formatVersion, out); }
    return true;
}

bool ProjectSong::kitsInside() const
{
    for (const int k : kitNumbers) if (lsdj::lsdjKitByNumber(kits, k) == nullptr) return false;
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
        // A whole bank, not just the header: before 4.3 the cartridge title is
        // "LSDJ" with no version in it and the number sits in the welcome line
        // inside bank 0, so reading 0x150 bytes left every old ROM nameless --
        // no kits, and the format's default model instead of the ROM's own.
        std::vector<uint8_t> head(0x8000, 0);
        const int n = in.read(head.data(), int(head.size()));
        if (n < 0x150) continue;
        const std::string v = lsdj::romVersion(head.data(), size_t(n));
        if (v.empty()) continue;
        // "Fits" is whether this ROM's own version reads the song's format at
        // all, not whether it is the format's default model: the whole point of
        // a ROM beside the save is to settle the two formats the byte cannot
        // (docs/LSDJ_VERSIONS.md section 4), and there the default is wrong.
        const auto* rm = lsdj::lsdjModelForRomVersion(v.c_str());
        const bool fits = rm != nullptr && preferFormat >= rm->formatMin && preferFormat <= rm->formatMax;
        (void)want;
        // The ROM that reads the song's format wins; among equals, the newer.
        if (bestFile == juce::File() || (fits && !bestFits) || (fits == bestFits && juce::String(v).compareNatural(best) > 0)) { best = v; bestFile = f; bestFits = fits; }
    }
    romOut = bestFile;
    return best;
}

const lsdj::LsdjModel& autoModel(int formatVersion, const juce::String& romVersion)
{
    // The ROM beside the save comes first, whenever its version reads this
    // format: format 2 splits at 4.0.4 and format 3 at 4.8.0, and only the ROM
    // tells those apart (docs/LSDJ_VERSIONS.md section 4). Reading the format's
    // default model instead made the version-keyed table dead code here.
    if (const auto* m = lsdj::lsdjModelForRomVersion(romVersion.toRawUTF8()))
        if (formatVersion >= m->formatMin && formatVersion <= m->formatMax) return *m;
    if (const auto* m = lsdj::lsdjModelForFormat(formatVersion)) return *m;
    return lsdj::lsdjLatestModel();
}

} // namespace chipboy::plugin
