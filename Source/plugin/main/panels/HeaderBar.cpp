#include "plugin/main/panels/HeaderBar.h"

#include "plugin/shared/BankFiles.h"
#include "plugin/shared/BankJson.h"

#include <cmath>
#include <iterator>

namespace chipboy::plugin {

using namespace juce;
using namespace chipboy::ui;

namespace {
const char* kStockTip = "STOCK: every audible setting is something a real unit does. MODIFIED: a departure in the Hardware tab is on.";
const char* kVisualizerTip = "Open the visualizer window: the five scopes, no chrome, made for screen capture";
const char* kHexTip = "Show values in hex, the LSDj habit. Display only.";
}

HeaderBar::HeaderBar(ChipBoyProcessor& p)
    : processor_(p),
      wordmark_("ChipBoy", Fonts::pixel(15.0f), colours::text),
      wordmarkSmall_("DMG APU " + String(CharPointer_UTF8("\xc2\xb7")) + " 4 voices", Fonts::caption(10.0f), colours::textDim),
      modelLabel_("Model", Fonts::caption(10.0f), colours::textDim),
      bankLabel_("Bank", Fonts::caption(10.0f), colours::textDim),
      model_({ "DMG", "CGB", "RAW" }),
      bankPrev_(String(CharPointer_UTF8("\xe2\x80\xb9"))), bankNext_(String(CharPointer_UTF8("\xe2\x80\xba"))), bankMenu_(String(CharPointer_UTF8("\xe2\x96\xbe"))),
      stockBox_(stock_),
      visualizer_("Visualizer"), hex_("Hex"), settings_(String(CharPointer_UTF8("\xe2\x9a\x99")))
{
    wordmarkSmall_.setUpperCase(true);
    modelLabel_.setUpperCase(true);
    bankLabel_.setUpperCase(true);
    for (auto* c : std::initializer_list<Component*>{ &wordmark_, &wordmarkSmall_, &modelLabel_, &model_, &bankLabel_, &bankPrev_, &bankName_, &bankNext_, &bankMenu_, &stockBox_, &visualizer_, &hex_, &settings_ })
        addAndMakeVisible(c);

    model_.setOptionColour(0, colours::wav);
    model_.setOptionColour(1, colours::cgb);
    model_.setOptionColour(2, colours::textMute);
    model_.setOptionTooltip(0, "The brick: measured DMG coupling at 25 Hz, its noise floor, its clicks");
    model_.setOptionTooltip(1, "Game Boy Color: live wave RAM, coupling at 338 Hz, the louder LCD line");
    model_.setOptionTooltip(2, "The DMG chip with no analog stage: the clean digital mix a gaming emulator makes");
    model_.attach(param(processor_, ids::model));

    bankPrev_.setTooltip("Previous bank");
    bankNext_.setTooltip("Next bank");
    bankMenu_.setTooltip("Load, save or reset the bank");
    bankPrev_.onClick = [this] { cycleBank(-1); };
    bankNext_.onClick = [this] { cycleBank(1); };
    bankMenu_.onClick = [this] { showBankMenu(); };
    bankName_.setText(processor_.bankName());
    bankName_.onChange = [this](const String& n) { processor_.setBankName(n.trim().isEmpty() ? String("Bank") : n.trim()); };

    stock_.set("STOCK", Pill::Tone::Ok);
    stockBox_.setTooltip(kStockTip);

    visualizer_.setTooltip(kVisualizerTip);
    visualizer_.onClick = [this] { if (onToggleVisualizer) onToggleVisualizer(); };
    hex_.setTooltip(kHexTip);
    hex_.setClickingTogglesState(true);
    hexAtt_ = std::make_unique<ButtonParameterAttachment>(param(processor_, ids::hexDisplay), hex_);
    settings_.setTooltip(processor_.wrapperType == AudioProcessor::wrapperType_Standalone
                             ? "Settings: scaling, tick source. Audio and MIDI devices are under the standalone's Options button."
                             : "Settings: scaling, tick source");
    settings_.onClick = [this] { showSettingsMenu(); };
}

HeaderBar::~HeaderBar() = default;

void HeaderBar::setVisualizerOpen(bool open)
{
    visualizer_.setToggleState(open, dontSendNotification);
    visualizer_.setButtonText(open ? "Visualizer (open)" : "Visualizer");
    resized();
}

void HeaderBar::tick()
{
    const bool modified = paramValue(processor_, ids::declick) != 0 || paramValue(processor_, ids::soften) != 0;
    if (modified != lastModified_ || lastModel_ < 0) {
        lastModified_ = modified;
        stock_.set(modified ? "MODIFIED" : "STOCK", modified ? Pill::Tone::Accent : Pill::Tone::Ok);
        resized();
    }
    lastModel_ = modelIndex(processor_);
    const String n = processor_.bankName();
    if (n != bankName_.text()) bankName_.setText(n);
}

/* ------------------------------------------------------------- banks */

StringArray HeaderBar::bankNames() const
{
    StringArray files;
    for (const auto& f : banksFolder().findChildFiles(File::findFiles, false, "*.chipboy")) files.add(f.getFileNameWithoutExtension());
    files.sortNatural();
    StringArray names { "Factory" };
    names.addArray(files);
    return names;
}

void HeaderBar::cycleBank(int direction)
{
    const StringArray names = bankNames();
    if (names.isEmpty()) return;
    int idx = names.indexOf(processor_.bankName());
    if (idx < 0) idx = direction > 0 ? -1 : 0;
    idx = (idx + direction + names.size()) % names.size();
    loadBankByName(names[idx]);
}

void HeaderBar::loadBankByName(const String& name)
{
    if (name == "Factory") { applyBank(bank::Bank::factory(), "Factory"); return; }
    const File f = banksFolder().getChildFile(name + ".chipboy");
    auto b = std::make_unique<bank::Bank>();
    if (f.existsAsFile() && bankFromJson(f.loadFileAsString(), *b)) { applyBank(*b, name); return; }
    AlertWindow::showMessageBoxAsync(MessageBoxIconType::WarningIcon, "ChipBoy", "Could not read the bank file " + f.getFileName());
}

void HeaderBar::applyBank(const bank::Bank& b, const String& name)
{
    processor_.loadBank(b);
    processor_.setBankName(name);
    bankName_.setText(name);
    if (onBankLoaded) onBankLoaded();
}

void HeaderBar::showBankMenu()
{
    PopupMenu m;
    m.addItem(1, "Load" + String(CharPointer_UTF8("\xe2\x80\xa6")));
    m.addItem(2, "Save as" + String(CharPointer_UTF8("\xe2\x80\xa6")));
    m.addSeparator();
    m.addItem(3, "Reset to factory");
    m.addSeparator();
    m.addItem(4, "Bank folder: " + banksFolder().getFullPathName(), false);
    Component::SafePointer<HeaderBar> safe(this);
    m.showMenuAsync(PopupMenu::Options().withTargetComponent(&bankMenu_), [safe](int r) {
        if (safe == nullptr || r == 0) return;
        HeaderBar& h = *safe;
        if (r == 1) {
            chipboy::plugin::loadBank(&h, [safe](std::unique_ptr<bank::Bank> b, File f) {
                if (safe == nullptr || !b) return;
                safe->applyBank(*b, f.getFileNameWithoutExtension());
            });
        } else if (r == 2) {
            auto snapshot = h.processor_.bank();
            if (!snapshot) return;
            saveBankAs(&h, *snapshot, h.processor_.bankName(), [safe, snapshot](bool ok, File f) {
                if (safe == nullptr || !ok) return;
                safe->processor_.setBankName(f.getFileNameWithoutExtension());
                safe->bankName_.setText(f.getFileNameWithoutExtension());
            });
        } else if (r == 3) {
            h.applyBank(bank::Bank::factory(), "Factory");
        }
    });
}

/* ---------------------------------------------------------- settings */

void HeaderBar::showSettingsMenu()
{
    PopupMenu m;
    PopupMenu scale;
    scale.addItem(101, "100 %", true, std::abs(scale_ - 1.0f) < 0.01f);
    scale.addItem(102, "125 %", true, std::abs(scale_ - 1.25f) < 0.01f);
    scale.addItem(103, "150 %", true, std::abs(scale_ - 1.5f) < 0.01f);
    m.addSubMenu("Window scale", scale);

    // Tempo lives on the Phrases tab now (docs/COMMANDS_AND_TEMPO.md section 6).

    if (processor_.wrapperType == AudioProcessor::wrapperType_Standalone) {
        m.addSeparator();
        m.addItem(900, "Audio and MIDI devices: the standalone's own Options button", false);
    }

    Component::SafePointer<HeaderBar> safe(this);
    m.showMenuAsync(PopupMenu::Options().withTargetComponent(&settings_), [safe](int r) {
        if (safe == nullptr || r == 0) return;
        HeaderBar& h = *safe;
        if (r >= 101 && r <= 103) { if (h.onScale) h.onScale(r == 101 ? 1.0f : r == 102 ? 1.25f : 1.5f); }
    });
}

/* ------------------------------------------------------------ layout */

void HeaderBar::paint(Graphics& g)
{
    g.fillAll(colours::panel2);
    g.setColour(colours::lineSoft);
    g.fillRect(0, getHeight() - 1, getWidth(), 1);
}

void HeaderBar::resized()
{
    const int h = getHeight();
    auto centred = [h](Component& c, int x, int w, int ch) { c.setBounds(x, (h - ch) / 2, w, ch); };
    int x = 14;
    centred(wordmark_, x, wordmark_.preferredWidth(), 20); x += wordmark_.preferredWidth() + 8;
    centred(wordmarkSmall_, x, wordmarkSmall_.preferredWidth(), 20); x += wordmarkSmall_.preferredWidth() + 18;
    centred(modelLabel_, x, modelLabel_.preferredWidth(), 20); x += modelLabel_.preferredWidth() + 8;
    const int modelW = std::max(model_.preferredWidth(), 120);
    centred(model_, x, modelW, 26); x += modelW + 18;
    centred(bankLabel_, x, bankLabel_.preferredWidth(), 20); x += bankLabel_.preferredWidth() + 8;
    centred(bankPrev_, x, 24, 22); x += 28;
    centred(bankName_, x, 150, 24); x += 154;
    centred(bankNext_, x, 24, 22); x += 28;
    centred(bankMenu_, x, 24, 22);

    int r = getWidth() - 14;
    centred(settings_, r - 30, 30, 24); r -= 38;
    centred(hex_, r - 46, 46, 24); r -= 54;
    const int vw = visualizer_.getToggleState() ? 118 : 86;
    centred(visualizer_, r - vw, vw, 24); r -= vw + 12;
    const int pw = std::max(stock_.preferredWidth(), 60);
    centred(stockBox_, r - pw, pw, 20);
}

} // namespace chipboy::plugin
