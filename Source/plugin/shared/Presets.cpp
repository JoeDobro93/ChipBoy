#include "plugin/shared/Presets.h"

#include "plugin/shared/BankJson.h"

namespace chipboy::plugin {

using namespace juce;

File presetsFolder()
{
    const File folder = File::getSpecialLocation(File::userDocumentsDirectory).getChildFile("ChipBoy").getChildFile("Instruments");
    folder.createDirectory();
    return folder;
}

String presetToJson(const bank::Preset& p)
{
    auto* o = new DynamicObject();
    o->setProperty("format", "chipboy-instrument");
    o->setProperty("version", 1);
    o->setProperty("instrument", instrumentToVar(p.instrument, 0));
    Array<var> tables, waves, kits;
    for (const auto& t : p.tables) tables.add(tableToVar(t.second, t.first));
    for (const auto& w : p.waves) waves.add(waveToVar(w.second, w.first));
    for (const auto& k : p.kits) kits.add(kitToVar(k.second, k.first));
    o->setProperty("tables", tables); o->setProperty("waves", waves); o->setProperty("kits", kits);
    return JSON::toString(var(o), false);
}

bool presetFromJson(const String& text, bank::Preset& out)
{
    out = bank::Preset{};
    const var parsed = JSON::parse(text);
    auto* o = parsed.getDynamicObject();
    if (!o || o->getProperty("format").toString() != "chipboy-instrument") return false;
    if (!instrumentFromVar(o->getProperty("instrument"), out.instrument)) return false;
    if (auto* a = o->getProperty("tables").getArray())
        for (const auto& v : *a) { bank::Table t; if (tableFromVar(v, t)) out.tables.emplace_back(slotOfVar(v), std::move(t)); }
    if (auto* a = o->getProperty("waves").getArray())
        for (const auto& v : *a) { bank::Wave w; if (waveFromVar(v, w)) out.waves.emplace_back(slotOfVar(v), std::move(w)); }
    if (auto* a = o->getProperty("kits").getArray())
        for (const auto& v : *a) { bank::Kit k; if (kitFromVar(v, k)) out.kits.emplace_back(slotOfVar(v), std::move(k)); }
    return true;
}

bool savePreset(const bank::Preset& p, const File& file) { return file.replaceWithText(presetToJson(p)); }

bool loadPreset(const File& file, bank::Preset& out)
{
    if (!file.existsAsFile()) return false;
    return presetFromJson(file.loadFileAsString(), out);
}

} // namespace chipboy::plugin
