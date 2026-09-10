// ChipBoy -- the LSDj import dialog (UI_DESIGN D-UI-22).
#include "plugin/main/panels/LsdjImportDialog.h"

#include "core/Import/LsdjSong.h"
#include "plugin/ui/Theme.h"

namespace chipboy::plugin {

using namespace juce;
using namespace chipboy::ui;

namespace {
constexpr int kPad = 16, kRowH = 26, kHead = 40, kFoot = 44, kVersionRow = 30;
}

/// One song of the save: a checkbox with the name, its format and the model
/// it will take under Auto.
struct LsdjImportDialog::Row {
    ToggleButton check;
    Label detail;
    int file = -1;                 ///< -1 the working song
    int formatVersion = -1;
    String name;
};

LsdjImportDialog::LsdjImportDialog(ChipBoyProcessor& processor, SavePreview preview, std::function<void(const Outcome&)> done)
    : processor_(processor), preview_(std::move(preview)), done_(std::move(done)),
      import_("Import"), cancel_("Cancel")
{
    const auto& idx = preview_.index;
    title_.setText("Songs in " + preview_.file.getFileName(), dontSendNotification);
    title_.setFont(Fonts::sans(15.0f, true));
    title_.setColour(Label::textColourId, colours::text);
    addAndMakeVisible(title_);

    // The working song first: it is what the machine was playing, saved or not.
    auto addRow = [this](int file, const String& name, int format, bool checked) {
        auto r = std::make_unique<Row>();
        r->file = file; r->formatVersion = format; r->name = name;
        r->check.setButtonText(name);
        r->check.setToggleState(checked, dontSendNotification);
        r->check.setColour(ToggleButton::textColourId, colours::text);
        r->check.setColour(ToggleButton::tickColourId, colours::accentHi);
        r->detail.setFont(Fonts::mono(11.0f));
        r->detail.setColour(Label::textColourId, colours::textDim);
        r->detail.setJustificationType(Justification::centredRight);
        addAndMakeVisible(r->check);
        addAndMakeVisible(r->detail);
        rows_.push_back(std::move(r));
    };
    {
        String workingName = "Working song";
        for (const auto& e : idx.files) if (e.active) workingName = e.name + " (working copy)";
        if (idx.workingUsed) addRow(-1, workingName, idx.workingFormat, idx.activeFile < 0);
    }
    for (const auto& e : idx.files) addRow(e.file, e.name, e.formatVersion, e.active);

    // The version: Auto, then every model, newest first.
    versionLabel_.setText("Version", dontSendNotification);
    versionLabel_.setFont(Fonts::caption(10.0f));
    versionLabel_.setColour(Label::textColourId, colours::textDim);
    addAndMakeVisible(versionLabel_);
    version_.addItem("Auto (by each song's format)", 1);
    int count = 0; const auto* const* models = lsdj::lsdjModels(count);
    for (int i = 0; i < count; ++i) version_.addItem(String(models[i]->name) + (models[i]->measured ? String() : String(" (assumed)")), 2 + i);
    version_.setSelectedId(1, dontSendNotification);
    version_.setTooltip("Which LSDj's rules read the bytes: Auto picks by each song's format version, a ROM beside the save decides an unknown format, and the newest version is the last resort. Pick a version to read every song with it.");
    version_.onChange = [this] { for (auto& r : rows_) r->detail.setText(String("format ") + (r->formatVersion >= 0 ? String(r->formatVersion) : String("?")) + String(CharPointer_UTF8("  \xc2\xb7  ")) + modelFor(r->formatVersion).name, dontSendNotification); };
    addAndMakeVisible(version_);
    version_.onChange();

    romLine_.setFont(Fonts::sans(11.0f));
    romLine_.setColour(Label::textColourId, colours::textDim);
    romLine_.setText(preview_.romVersion.isNotEmpty() ? "ROM beside the save: LSDj " + preview_.romVersion + " (" + preview_.romFile.getFileName() + "), " + String(int(preview_.kits.size())) + " kits for the kit instruments"
                                                      : "No LSDj ROM beside the save: kit instruments cannot be read, and an unknown format takes the newest version.", dontSendNotification);
    addAndMakeVisible(romLine_);

    import_.onClick = [this] { runImport(); };
    cancel_.onClick = [this] { close(); };
    addAndMakeVisible(import_);
    addAndMakeVisible(cancel_);
    setSize(kWidth, preferredHeight());
}

LsdjImportDialog::~LsdjImportDialog() = default;

int LsdjImportDialog::preferredHeight() const { return kHead + kVersionRow + 22 + int(rows_.size()) * kRowH + kFoot + 2 * kPad; }

const lsdj::LsdjModel& LsdjImportDialog::modelFor(int formatVersion) const
{
    const int id = version_.getSelectedId();
    if (id >= 2) { int count = 0; const auto* const* models = lsdj::lsdjModels(count); if (id - 2 < count) return *models[id - 2]; }
    return autoModel(formatVersion, preview_.romVersion);
}

void LsdjImportDialog::paint(Graphics& g)
{
    g.fillAll(colours::panel);
    g.setColour(colours::lineSoft);
    g.fillRect(kPad, kHead - 6, getWidth() - 2 * kPad, 1);
}

void LsdjImportDialog::resized()
{
    auto area = getLocalBounds().reduced(kPad);
    title_.setBounds(area.removeFromTop(kHead - kPad));
    area.removeFromTop(4);
    auto vrow = area.removeFromTop(kVersionRow);
    versionLabel_.setBounds(vrow.removeFromLeft(56));
    version_.setBounds(vrow.removeFromLeft(280).withSizeKeepingCentre(280, 24));
    romLine_.setBounds(area.removeFromTop(22));
    for (auto& r : rows_) {
        auto row = area.removeFromTop(kRowH);
        r->detail.setBounds(row.removeFromRight(250));
        r->check.setBounds(row);
    }
    auto foot = area.removeFromBottom(28);
    import_.setBounds(foot.removeFromRight(96));
    foot.removeFromRight(8);
    cancel_.setBounds(foot.removeFromRight(96));
}

void LsdjImportDialog::runImport()
{
    Outcome out;
    lsdj::ImportNotes allNotes;
    int lastTab = -1;
    for (const auto& r : rows_) {
        if (!r->check.getToggleState()) continue;
        std::vector<uint8_t> song; std::string err;
        const bool ok = r->file < 0 ? lsdj::workingSong(preview_.bytes.data(), preview_.bytes.size(), song)
                                    : lsdj::decompressFile(preview_.bytes.data(), preview_.bytes.size(), r->file, song, err);
        if (!ok) { allNotes.add(r->name.toStdString() + ": " + (err.empty() ? std::string("could not be read") : err)); continue; }
        const auto& model = modelFor(r->formatVersion);
        // A bank is 41 KB and a song 300 KB: both on the heap (CLAUDE.md).
        auto bank = std::make_shared<bank::Bank>();
        auto tune = std::make_shared<tracker::Song>();
        lsdj::ImportSummary sum; lsdj::ImportNotes notes;
        if (!lsdj::importSong(song.data(), song.size(), model, *bank, *tune, sum, notes, preview_.kits.empty() ? nullptr : &preview_.kits)) { allNotes.add(r->name.toStdString() + ": the song could not be read"); continue; }
        for (const auto& l : notes.lines) allNotes.add(r->name.toStdString() + ": " + l);
        lastTab = processor_.addTab(std::shared_ptr<const tracker::Song>(std::move(tune)), std::shared_ptr<const bank::Bank>(std::move(bank)),
                                    r->name, "LSDj " + String(CharPointer_UTF8(" \xc2\xb7 ")) + r->name);
        ++out.songs;
    }
    if (lastTab >= 0) processor_.setActiveTab(lastTab);
    for (const auto& l : allNotes.lines) out.notes.add(String(l));
    if (!out.notes.isEmpty()) {
        String text = String(out.notes.size()) + (out.notes.size() == 1 ? " note" : " notes") + " on what could not be carried over exactly:\n\n";
        for (int i = 0; i < out.notes.size() && i < 40; ++i) text += String(CharPointer_UTF8("\xe2\x80\xa2 ")) + out.notes[i] + "\n";
        if (out.notes.size() > 40) text += "... and " + String(out.notes.size() - 40) + " more";
        AlertWindow::showMessageBoxAsync(MessageBoxIconType::InfoIcon, "Imported from LSDj", text);
    }
    if (done_) done_(out);
    close();
}

void LsdjImportDialog::close()
{
    if (auto* w = findParentComponentOfClass<DialogWindow>()) w->exitModalState(0);
}

void LsdjImportDialog::show(ChipBoyProcessor& processor, SavePreview preview, Component* parent, std::function<void(const Outcome&)> done)
{
    DialogWindow::LaunchOptions o;
    o.content.setOwned(new LsdjImportDialog(processor, std::move(preview), std::move(done)));
    o.dialogTitle = "Import from LSDj";
    o.dialogBackgroundColour = colours::panel;
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.componentToCentreAround = parent != nullptr ? parent->getTopLevelComponent() : nullptr;
    o.launchAsync();
}

} // namespace chipboy::plugin
