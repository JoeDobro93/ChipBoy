#include "plugin/shared/SongFiles.h"

#include "plugin/shared/BankJson.h"

#include <algorithm>
#include <memory>

namespace chipboy::plugin {

using namespace juce;

File songsFolder()
{
    const File folder = File::getSpecialLocation(File::userDocumentsDirectory).getChildFile("ChipBoy").getChildFile("Songs");
    folder.createDirectory();
    return folder;
}

std::vector<int> instrumentsUsedBy(const tracker::Song& song)
{
    std::vector<int> used;
    for (int ch = 0; ch < 4; ++ch)
        for (auto slot : song.chain[size_t(ch)])
            if (const auto* p = song.phrase(slot))
                for (const auto& c : p->cells)
                    if (c.inst >= 1 && c.inst <= bank::kInstrumentSlots
                        && std::find(used.begin(), used.end(), int(c.inst)) == used.end())
                        used.push_back(int(c.inst));
    std::sort(used.begin(), used.end());
    return used;
}

String songFileText(const tracker::Song& song, const bank::Bank& bank, const String& bankName)
{
    // Format 6 (sections 18 and 25): the song JSON as the plugin state writes
    // it -- phrases with their own lengths -- the whole bank it plays through,
    // and what it takes to say whether a bank it meets later is the bank it
    // was written with.
    auto* o = new DynamicObject();
    o->setProperty("format", "chipboy-song-file");
    o->setProperty("version", 6);
    o->setProperty("bank", bankName);
    auto* names = new DynamicObject();
    for (int slot : instrumentsUsedBy(song)) {
        const auto* i = bank.instrument(slot);
        names->setProperty(Identifier(String(slot)), i ? String(i->name) : String());
    }
    o->setProperty("instruments", var(names));
    o->setProperty("bankData", bankToVar(bank));
    o->setProperty("song", songToVar(song));
    return JSON::toString(var(o), false);
}

bool saveSong(const tracker::Song& song, const bank::Bank& bank, const File& file, const String& bankName)
{
    return file.replaceWithText(songFileText(song, bank, bankName));
}

bool loadSongText(const String& text, tracker::Song& out, SongReport& report, const bank::Bank* current, bank::Bank* bankOut)
{
    report = SongReport{};
    const var parsed = JSON::parse(text);
    auto* o = parsed.getDynamicObject();
    if (!o) return false;
    // A bare song document (the plugin state's own form) reads too, so a song
    // saved by hand out of a project file still opens.
    const bool wrapped = o->getProperty("format").toString() == "chipboy-song-file";
    if (!wrapped && o->getProperty("format").toString() != "chipboy-song") return false;
    if (!songFromVar(wrapped ? o->getProperty("song") : parsed, out)) return false;

    // Formats 5 and 6 carry the bank with them; the song then plays through that
    // bank and there is nothing to report (section 18).
    if (bankOut != nullptr && o->hasProperty("bankData") && bankFromVar(o->getProperty("bankData"), *bankOut)) {
        report.hasBank = true;
        current = bankOut;
    }
    report.bankName = o->getProperty("bank").toString();
    const auto used = instrumentsUsedBy(out);
    report.instrumentsUsed = int(used.size());
    auto* names = o->getProperty("instruments").getDynamicObject();
    for (int slot : used) {
        const String was = names && names->hasProperty(Identifier(String(slot)))
                               ? names->getProperty(Identifier(String(slot))).toString() : String();
        const auto* i = current ? current->instrument(slot) : nullptr;
        if (!current) continue;
        if (!i) { report.missing.add("slot " + String(slot) + " was " + (was.isEmpty() ? String("empty") : was) + "; this bank has nothing there"); continue; }
        const String now(i->name);
        if (was.isNotEmpty() && was != now)
            report.differences.add("slot " + String(slot) + " was " + was + "; this bank has " + now);
    }
    return true;
}

bool loadSong(const File& file, tracker::Song& out, SongReport& report, const bank::Bank* current, bank::Bank* bankOut)
{
    if (!file.existsAsFile()) { report = SongReport{}; return false; }
    return loadSongText(file.loadFileAsString(), out, report, current, bankOut);
}

} // namespace chipboy::plugin
