#include "plugin/solo/SoloEditor.h"

#include "plugin/main/panels/InstrumentPanel.h"
#include "plugin/main/panels/KitsPanel.h"
#include "plugin/main/panels/TablesPanel.h"
#include "plugin/main/panels/WavesPanel.h"
#include "plugin/shared/BankFiles.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace chipboy::plugin {

using namespace juce;
using namespace chipboy::ui;

namespace {
constexpr int kHeader = 30, kTabsHeight = 26, kStatus = 22, kPad = 8, kGap = 6;
constexpr int kMenuOpen = 1000;
const Identifier kViewNode("ui_view"), kTabProp("tab");
const char* kTabNames[] = { "Main", "Sounds", "Instrument", "Tables", "Waves", "Kits", "Commands", "Setup" };
static_assert(int(std::size(kTabNames)) == int(SoloEditor::kTabs), "a tab needs a name");
const String kDot = String(CharPointer_UTF8(" \xc2\xb7 "));
const String kDash = String(CharPointer_UTF8("\xe2\x80\x94"));

} // namespace

/* ------------------------------------------------------------- strip */

SoloStrip::SoloStrip(SoloProcessor& p)
    : processor_(p),
      name_("PU1", Fonts::pixel(12.0f), colours::pu1),
      soundLabel_("Sound", Fonts::caption(10.0f), colours::textDim), instLabel_("Instrument", Fonts::caption(10.0f), colours::textDim),
      tableLabel_("Table", Fonts::caption(10.0f), colours::textDim), transposeLabel_("Transpose", Fonts::caption(10.0f), colours::textDim),
      panLabel_("Pan", Fonts::caption(10.0f), colours::textDim), levelLabel_("Level", Fonts::caption(10.0f), colours::textDim),
      noteLine_({}, Fonts::mono(11.0f), colours::textMute),
      soundName_({}, Fonts::sans(12.0f), colours::textMute), instrumentName_({}, Fonts::sans(12.0f), colours::textMute),
      storeBtn_("Store"),
      pan_({ String(CharPointer_UTF8("\xe2\x80\x93")), "L", "LR", "R", "inst" })
{
    for (auto* l : { &soundLabel_, &instLabel_, &tableLabel_, &transposeLabel_, &panLabel_, &levelLabel_ }) l->setUpperCase(true);
    for (auto* c : std::initializer_list<Component*>{ &led_, &name_, &scope_, &regs_, &soundLabel_, &sound_, &soundName_, &storeBtn_, &instLabel_, &instrument_, &instrumentName_,
                                                     &tableLabel_, &table_, &transposeLabel_, &transpose_, &panLabel_, &pan_, &levelLabel_, &level_, &noteLine_ })
        addAndMakeVisible(c);
    led_.setInterceptsMouseClicks(false, false);

    ScopeView::Source src;
    src.ring = &processor_.scopes().channels[0];
    src.state = &processor_.scopes().state[0];
    src.latestCycle = &processor_.scopes().latestCycle;
    src.mix = &processor_.scopes().mix;
    scope_.setSource(src);
    scope_.setChrome(false);
    scope_.setStereo(false);
    scope_.setLcdGround(true);
    scope_.setPeriods(2);

    // The sound: a recall by hand is one undo step that carries the row and
    // the Sound parameter (plan section 2), so the stepper is not attached.
    sound_.setRange(0, kSoloSounds, 0);
    sound_.setSlotNumbering(true);
    sound_.setTooltip("The sound last recalled into the row: pick one to recall it. Right-click lists them; double-click opens the Sounds tab.");
    sound_.setTextFunction([](int v) { return v == 0 ? String(CharPointer_UTF8("\xe2\x80\x93")) : ValueFormat::slot(v); });
    sound_.onChange = [this](int v) {
        if (v >= 1) processor_.recallSound(v, true);
        else setParam(*this, param(processor_, solo::ids::sound), 0.0f);
    };
    sound_.onList = [this] { showSoundMenu(); };
    sound_.onOpen = [this] { if (onOpenSound) onOpenSound(std::max(1, sound_.value())); };
    storeBtn_.setTooltip("Store the row as it stands into the sound shown, or into the first empty slot when none is");
    storeBtn_.onClick = [this] {
        int slot = paramValue(processor_, solo::ids::sound);
        if (slot < 1) {
            const auto s = processor_.solo();
            for (int k = 1; k <= kSoloSounds && slot < 1; ++k) if (s && !s->sounds[size_t(k - 1)].used) slot = k;
            if (slot < 1) return;
            setParam(*this, param(processor_, solo::ids::sound), float(slot));
        }
        processor_.storeSound(slot);
    };

    instrument_.setTooltip("The instrument the row plays. Right-click lists the bank, double-click opens it.");
    instrument_.attach(param(processor_, processor_.channelParamId(0, ids::instrument)));
    instrument_.setSlotNumbering(true);
    instrument_.onList = [this] { showInstrumentMenu(); };
    instrument_.onOpen = [this] { if (onOpenSlot && instrument_.value() > 0) onOpenSlot(SlotKind::Instrument, instrument_.value()); else instrument_.beginTypedEntry(); };
    table_.setTooltip("Table override; inst = the instrument's own. Right-click lists the bank, double-click opens it.");
    table_.attach(param(processor_, processor_.channelParamId(0, ids::table)));
    table_.setSlotNumbering(true);
    table_.onList = [this] { showTableMenu(); };
    table_.onOpen = [this] { if (onOpenSlot && table_.value() > 0) onOpenSlot(SlotKind::Table, table_.value()); else table_.beginTypedEntry(); };
    transpose_.setTooltip("Semitones added to every note, before the period is worked out");
    transpose_.attach(param(processor_, processor_.channelParamId(0, ids::transpose)));
    transpose_.setTransposeNumbering(true);
    pan_.setMini(true);
    pan_.setTooltip("NR51: off, left, both, right, or the instrument's own");
    panParam_ = std::make_unique<SegmentedParam>(pan_, param(processor_, processor_.channelParamId(0, ids::pan)), std::vector<int>{ 0, 1, 3, 2, 4 });
    level_.setTooltip("The envelope's start volume, sixteen levels, inst = the instrument's; on WAV they fold to mute, 25, 50 and 100 %");
    level_.attach(param(processor_, processor_.channelParamId(0, ids::level)));
    channelChanged();
    refreshInstrumentName();
    refreshSoundName();
}

SoloStrip::~SoloStrip() = default;

void SoloStrip::channelChanged()
{
    const int ch = processor_.channel();
    if (ch == channel_) return;
    channel_ = ch;
    name_.setText(colours::channelName(ch));
    name_.setColour(colours::channel(ch));
    led_.setColour(colours::channel(ch));
    level_.setTextFunction(ch == 2 ? std::function<String(int)>([](int v) { return v >= 16 ? String("inst") : v == 0 ? String("mute") : v <= 5 ? String("25%") : v <= 10 ? String("50%") : String("100%"); })
                                   : std::function<String(int)>([](int v) { return v >= 16 ? String("inst") : ValueFormat::number(v); }));
    ScopeView::Source src;
    src.ring = &processor_.scopes().channels[size_t(ch)];
    src.state = &processor_.scopes().state[size_t(ch)];
    src.latestCycle = &processor_.scopes().latestCycle;
    src.mix = &processor_.scopes().mix;
    scope_.setSource(src);
    scope_.setChannel(ch);
    regs_.setChannel(ch);
    // The palettes grey the letters this channel cannot use.
    const ChannelKind kind = processor_.kind();
    if (!cmd1_) {
        cmd1_ = std::make_unique<CommandSlot>("CMD 1", kind);
        cmd2_ = std::make_unique<CommandSlot>("CMD 2", kind);
        cmd1_->attach(param(processor_, processor_.channelParamId(0, ids::cmd1Type)), param(processor_, processor_.channelParamId(0, ids::cmd1X)), param(processor_, processor_.channelParamId(0, ids::cmd1Y)));
        cmd2_->attach(param(processor_, processor_.channelParamId(0, ids::cmd2Type)), param(processor_, processor_.channelParamId(0, ids::cmd2X)), param(processor_, processor_.channelParamId(0, ids::cmd2Y)));
        addAndMakeVisible(*cmd1_);
        addAndMakeVisible(*cmd2_);
    } else {
        cmd1_->setChannelKind(kind);
        cmd2_->setChannelKind(kind);
    }
    namesFor_ = nullptr;
    refreshInstrumentName();
    resized();
    repaint();
}

void SoloStrip::refreshInstrumentName()
{
    const auto b = processor_.bank();
    driver::VoiceView v;
    link::unpackState(processor_.scopes().state[size_t(processor_.channel())].load(std::memory_order_relaxed), v);
    const int loaded = int(v.instrument);
    const int slot = loaded > 0 ? loaded : paramValue(processor_, processor_.channelParamId(0, ids::instrument));
    if (b.get() == namesFor_ && slot == instrumentShown_ && loaded == loadedInstrument_) return;
    namesFor_ = b.get(); instrumentShown_ = slot; loadedInstrument_ = loaded;
    if (slot <= 0) { instrumentName_.setText("no instrument"); instrumentName_.setColour(colours::textDim); return; }
    const bank::Instrument* inst = b ? b->instrument(slot) : nullptr;
    if (!inst) { instrumentName_.setText(kDash + " empty " + kDash); instrumentName_.setColour(colours::textDim); return; }
    const bool elsewhere = loaded > 0 && loaded != paramValue(processor_, processor_.channelParamId(0, ids::instrument));
    instrumentName_.setText(elsewhere ? ValueFormat::slot(slot) + " " + String(inst->name) : String(inst->name));
    instrumentName_.setColour(instrumentFitsChannel(inst->type, processor_.channel()) ? instrumentKindColour(int(inst->type)) : colours::warn);
}

void SoloStrip::refreshSoundName()
{
    const auto s = processor_.solo();
    const int slot = paramValue(processor_, solo::ids::sound);
    if (s.get() == soundsFor_ && slot == soundShown_) return;
    soundsFor_ = s.get(); soundShown_ = slot;
    if (sound_.value() != slot) sound_.setValue(slot, dontSendNotification);
    if (slot < 1) { soundName_.setText("none recalled"); soundName_.setColour(colours::textDim); return; }
    const auto& snd = s ? s->sounds[size_t(slot - 1)] : SoloSound{};
    soundName_.setText(snd.used ? String(CharPointer_UTF8(snd.name.c_str())) : kDash + " empty " + kDash);
    soundName_.setColour(snd.used ? colours::text : colours::textDim);
}

void SoloStrip::showSoundMenu()
{
    const auto s = processor_.solo();
    if (!s) return;
    PopupMenu m;
    m.addSectionHeader("Sound");
    const int current = sound_.value();
    m.addItem(kMenuOpen, "Open the Sounds tab");
    m.addSeparator();
    for (int k = 1; k <= kSoloSounds; ++k) {
        const auto& snd = s->sounds[size_t(k - 1)];
        if (!snd.used) continue;
        m.addItem(k + 1, ValueFormat::slot(k) + "  " + String(CharPointer_UTF8(snd.name.c_str())), true, k == current);
    }
    Component::SafePointer<SoloStrip> safe(this);
    m.showMenuAsync(PopupMenu::Options().withTargetComponent(&sound_), [safe, current](int r) {
        if (safe == nullptr || r < 1) return;
        if (r == kMenuOpen) { if (safe->onOpenSound) safe->onOpenSound(std::max(1, current)); return; }
        safe->processor_.recallSound(r - 1, true);
    });
}

void SoloStrip::showInstrumentMenu()
{
    const auto b = processor_.bank();
    if (!b) return;
    PopupMenu m;
    m.addSectionHeader("Instrument");
    const int current = instrument_.value();
    if (current > 0) {
        const auto* cur = b->instrument(current);
        m.addItem(kMenuOpen, "Open instrument " + (cur ? slotAndName(current, cur->name) : ValueFormat::slot(current)) + " in its tab");
        m.addSeparator();
    }
    m.addItem(1, String(CharPointer_UTF8("\xe2\x80\x93   none")), true, current == 0);
    for (int slot = 1; slot <= bank::kInstrumentSlots; ++slot) {
        const auto* inst = b->instrument(slot);
        if (inst == nullptr) continue;
        PopupMenu::Item item(slotAndName(slot, inst->name));
        item.itemID = slot + 1;
        item.isTicked = slot == current;
        item.isEnabled = instrumentFitsChannel(inst->type, processor_.channel());
        if (!item.isEnabled) item.shortcutKeyDescription = instrumentTypeName(inst->type);
        m.addItem(std::move(item));
    }
    Component::SafePointer<SoloStrip> safe(this);
    m.showMenuAsync(PopupMenu::Options().withTargetComponent(&instrument_), [safe, current](int r) {
        if (safe == nullptr || r < 1) return;
        if (r == kMenuOpen) { if (safe->onOpenSlot) safe->onOpenSlot(SlotKind::Instrument, current); return; }
        setParam(*safe, param(safe->processor_, safe->processor_.channelParamId(0, ids::instrument)), float(r - 1));
    });
}

void SoloStrip::showTableMenu()
{
    const auto b = processor_.bank();
    if (!b) return;
    PopupMenu m;
    m.addSectionHeader("Table");
    const int current = table_.value();
    if (current > 0) {
        const auto* cur = b->table(current);
        m.addItem(kMenuOpen, "Open table " + (cur ? slotAndName(current, cur->name) : ValueFormat::slot(current)) + " in its tab");
        m.addSeparator();
    }
    m.addItem(1, String(CharPointer_UTF8("\xe2\x80\x93   the instrument's")), true, current == 0);
    for (int slot = 1; slot <= bank::kTableSlots; ++slot)
        if (const auto* t = b->table(slot)) m.addItem(slot + 1, slotAndName(slot, t->name), true, slot == current);
    Component::SafePointer<SoloStrip> safe(this);
    m.showMenuAsync(PopupMenu::Options().withTargetComponent(&table_), [safe, current](int r) {
        if (safe == nullptr || r < 1) return;
        if (r == kMenuOpen) { if (safe->onOpenSlot) safe->onOpenSlot(SlotKind::Table, current); return; }
        setParam(*safe, param(safe->processor_, safe->processor_.channelParamId(0, ids::table)), float(r - 1));
    });
}

void SoloStrip::tick()
{
    const int ch = processor_.channel();
    driver::VoiceView v;
    link::unpackState(processor_.scopes().state[size_t(ch)].load(std::memory_order_acquire), v);
    led_.setOn(v.active && v.dacOn);
    regs_.setState(processor_.scopes().state[size_t(ch)].load(std::memory_order_acquire));
    refreshInstrumentName();
    refreshSoundName();
    if (ch == 2) {
        const auto b = processor_.bank();
        const bank::Instrument* inst = b ? b->instrument(v.instrument) : nullptr;
        scope_.setFixedWindow(inst != nullptr && inst->type == bank::InstrumentType::Kit);
    }
    const int note = processor_.lastNote();
    int lo = 0, hi = 127;
    processor_.noteRange(lo, hi);
    String line = "plays " + ValueFormat::noteValue(lo, ch == 3) + String(CharPointer_UTF8("\xe2\x80\x93")) + ValueFormat::noteValue(hi, ch == 3);
    line += kDot + "note " + (note >= 0 ? ValueFormat::noteValue(note, ch == 3) : kDash);
    if (const String r = processor_.lastRecall(); r.isNotEmpty()) line += kDot + r;
    if (line != noteShown_) { noteShown_ = line; noteLine_.setText(line); }
}

void SoloStrip::bankChanged() { refreshInstrumentName(); }
void SoloStrip::soloChanged() { refreshSoundName(); }
void SoloStrip::hexChanged()
{
    namesFor_ = nullptr; instrumentShown_ = -1; soundsFor_ = nullptr; soundShown_ = -1;
    refreshInstrumentName(); refreshSoundName();
    if (cmd1_) { cmd1_->hexChanged(); cmd2_->hexChanged(); }
    noteShown_.clear();
    repaint();
}

void SoloStrip::paint(Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(colours::panel2);
    g.fillRoundedRectangle(r, 5.0f);
    g.setColour(colours::lineSoft);
    g.drawRoundedRectangle(r, 5.0f, 1.0f);
}

void SoloStrip::resized()
{
    // The scope across the top with the channel's name over it and the
    // registers under it, then the row in three bands -- sound and
    // instrument; table, transpose, pan and level; the two slots -- and the
    // note line last.
    auto area = getLocalBounds().reduced(kPad);
    const int x = area.getX(), w = area.getWidth();
    int y = area.getY();
    led_.setBounds(x, y + 5, 7, 7);
    name_.setBounds(x + 13, y, 60, 16);
    y += 18;
    // The fields need a fixed height; the scope takes the rest.
    const int fields = 14 + kGap + (12 + Stepper::kHeight) + kGap + (12 + Stepper::kHeight) + kGap + CommandSlot::kHeight + kGap + 14 + 4;
    const int scopeH = std::max(60, area.getBottom() - y - fields);
    scope_.setBounds(x, y, w, scopeH);
    y += scopeH + 4;
    regs_.setBounds(x, y, w, 14);
    y += 14 + kGap;
    const int sw = sound_.preferredWidth();
    const int half = (w - kGap) / 2;
    soundLabel_.setBounds(x, y, half, 10);
    sound_.setBounds(x, y + 12, sw, Stepper::kHeight);
    storeBtn_.setBounds(x + sw + 4, y + 12, 44, Stepper::kHeight);
    soundName_.setBounds(x + sw + 52, y + 12, half - sw - 52, Stepper::kHeight);
    const int ix = x + half + kGap;
    instLabel_.setBounds(ix, y, half, 10);
    instrument_.setBounds(ix, y + 12, sw, Stepper::kHeight);
    instrumentName_.setBounds(ix + sw + 6, y + 12, half - sw - 6, Stepper::kHeight);
    y += 12 + Stepper::kHeight + kGap;
    const int col = (w - 3 * kGap) / 4;
    tableLabel_.setBounds(x, y, col, 10);
    table_.setBounds(x, y + 12, std::min(sw, col), Stepper::kHeight);
    transposeLabel_.setBounds(x + col + kGap, y, col, 10);
    transpose_.setBounds(x + col + kGap, y + 12, std::min(sw, col), Stepper::kHeight);
    panLabel_.setBounds(x + 2 * (col + kGap), y, col, 10);
    pan_.setBounds(x + 2 * (col + kGap), y + 12, std::min(pan_.preferredWidth(), col), 22);
    levelLabel_.setBounds(x + 3 * (col + kGap), y, col, 10);
    level_.setBounds(x + 3 * (col + kGap), y + 12, std::min(sw, col), Stepper::kHeight);
    y += 12 + Stepper::kHeight + kGap;
    if (cmd1_) {
        cmd1_->setBounds(x, y, half, CommandSlot::kHeight);
        cmd2_->setBounds(x + half + kGap, y, half, CommandSlot::kHeight);
    }
    y += CommandSlot::kHeight + kGap;
    noteLine_.setBounds(x, y, w, 14);
}

/* ------------------------------------------------------------ sounds */

/// One mappable key: its name and the sound it selects.
class SoundsPanel::KeyRow : public Block {
public:
    KeyRow(SoundsPanel& owner, int note, bool noise) : owner_(owner), note_(note), name_(ValueFormat::noteValue(note, noise), Fonts::mono(11.0f), colours::textMute)
    {
        addAndMakeVisible(name_);
        addAndMakeVisible(sound_);
        sound_.setRange(0, kSoloSounds, 0);
        sound_.setSlotNumbering(true);
        sound_.setTextFunction([this](int v) { return v == 0 ? String(CharPointer_UTF8("\xe2\x80\x93")) : ValueFormat::slot(v); });
        sound_.setTooltip("The sound this key selects; " + String(CharPointer_UTF8("\xe2\x80\x93")) + " for none");
        sound_.onChange = [this](int v) {
            const int ch = owner_.processor_.channel();
            owner_.processor_.editSolo("Key " + ValueFormat::noteName(note_), [this, v, ch](SoloState& s) { s.keyMaps[size_t(ch)][size_t(note_)] = uint8_t(std::clamp(v, 0, kSoloSounds)); });
        };
        addAndMakeVisible(label_);
        label_.setFont(Fonts::sans(11.0f));
        label_.setColour(colours::textDim);
    }
    int note() const { return note_; }
    void sync(int sound, const String& label) { if (sound_.value() != sound) sound_.setValue(sound, dontSendNotification); label_.setText(label); }
    int preferredHeight(int) override { return Stepper::kHeight + 2; }
    void resized() override
    {
        auto r = getLocalBounds().withHeight(Stepper::kHeight);
        name_.setBounds(r.removeFromLeft(44));
        sound_.setBounds(r.removeFromLeft(sound_.preferredWidth()));
        r.removeFromLeft(6);
        label_.setBounds(r);
    }
private:
    SoundsPanel& owner_;
    int note_;
    TextLine name_;
    Stepper sound_;
    TextLine label_;
};

SoundsPanel::SoundsPanel(SoloProcessor& p)
    : EditorPanel(p), processor_(p),
      listTitle_("Sounds", Fonts::caption(10.0f, true), colours::textDim), keysTitle_("Keys outside the range", Fonts::caption(10.0f, true), colours::textDim),
      keysHelp_({}, Fonts::sans(11.0f), colours::textDim),
      recallBtn_("Recall"), storeBtn_("Store here"), clearBtn_("Clear"), defaultKeysBtn_("Default"), clearKeysBtn_("Clear"),
      keyMap_("Key map on")
{
    listTitle_.setUpperCase(true); keysTitle_.setUpperCase(true);
    for (auto* c : std::initializer_list<Component*>{ &list_, &listTitle_, &keysTitle_, &keysHelp_, &recallBtn_, &storeBtn_, &clearBtn_, &defaultKeysBtn_, &clearKeysBtn_, &keyMap_, &keys_ })
        addAndMakeVisible(c);
    list_.setKindColours([](int) { return colours::text; });
    list_.onSelect = [this](int slot) { slot_ = slot; contextChanged(); };
    list_.onDoubleClick = [this](int slot) { slot_ = slot; processor_.recallSound(slot, true); };
    list_.onRename = [this](int slot, const String& name) {
        processor_.editSolo("Rename sound " + String(slot), [slot, name](SoloState& s) { auto& snd = s.sounds[size_t(slot - 1)]; snd.used = true; snd.name = name.toStdString(); });
    };
    recallBtn_.setTooltip("The sound's row into the main window (a double click on a row does the same)");
    recallBtn_.onClick = [this] { processor_.recallSound(slot_, true); };
    storeBtn_.setTooltip("The row as it stands -- instrument, table, the two commands -- into this slot");
    storeBtn_.onClick = [this] { processor_.storeSound(slot_); };
    clearBtn_.onClick = [this] { processor_.editSolo("Clear sound " + String(slot_), [this](SoloState& s) { s.sounds[size_t(slot_ - 1)] = SoloSound{}; }); };
    defaultKeysBtn_.setTooltip("The keys below the floor counting down are sounds 1, 2, 3 ...; the keys above the ceiling carry on from there");
    defaultKeysBtn_.onClick = [this] { const int ch = processor_.channel(); processor_.editSolo("Default key layout", [ch](SoloState& s) { soloDefaultKeyMap(ch, s.keyMaps[size_t(ch)]); }); };
    clearKeysBtn_.onClick = [this] { const int ch = processor_.channel(); processor_.editSolo("Clear the key map", [ch](SoloState& s) { s.keyMaps[size_t(ch)].fill(0); }); };
    keyMap_.attach(param(processor_, solo::ids::keyMap));
    keyMap_.setTooltip("Off, every key plays and none selects a sound");
    keys_.setReserveScrollbar(true);
    rebuildList();
    rebuildKeys();
}

SoundsPanel::~SoundsPanel() = default;

String SoundsPanel::soundText(int slot) const
{
    const auto s = processor_.solo();
    if (slot < 1 || !s || !s->sounds[size_t(slot - 1)].used) return {};
    return String(CharPointer_UTF8(s->sounds[size_t(slot - 1)].name.c_str()));
}

void SoundsPanel::rebuildList()
{
    const auto s = processor_.solo();
    const auto b = processor_.bank();
    if (s.get() == rowsFor_ && b.get() == rowsBank_) return;
    rowsFor_ = s.get(); rowsBank_ = b.get();
    std::vector<SlotRow> rows;
    rows.resize(size_t(kSoloSounds));
    for (int k = 0; k < kSoloSounds; ++k) {
        auto& r = rows[size_t(k)];
        r.slot = k + 1;
        const auto& snd = s ? s->sounds[size_t(k)] : SoloSound{};
        r.used = snd.used;
        r.name = snd.used ? String(CharPointer_UTF8(snd.name.c_str())) : String();
        if (snd.used) {
            const bank::Instrument* inst = b ? b->instrument(snd.inst) : nullptr;
            r.note = snd.inst == 0 ? "bare" : ValueFormat::slot(snd.inst) + (inst ? " " + String(inst->name) : String());
        }
    }
    list_.setRows(rows);
    list_.setSelected(slot_, dontSendNotification);
    syncKeys();
}

void SoundsPanel::rebuildKeys()
{
    const int ch = processor_.channel();
    if (ch == keysForChannel_) return;
    keysForChannel_ = ch;
    keyRows_.clear();
    auto stack = std::make_unique<Stack>(0);
    int lo = 0, hi = 127;
    processor_.noteRange(lo, hi);
    auto add = [&](int note) { auto row = std::make_unique<KeyRow>(*this, note, ch == 3); keyRows_.push_back(row.get()); stack->add(std::move(row)); };
    for (int n = lo - 1; n >= 0; --n) add(n);
    for (int n = hi + 1; n < 128; ++n) add(n);
    keys_.setContent(std::move(stack));
    keysTitle_.setText(String("Keys outside ") + colours::channelName(ch) + "'s range");
    keysHelp_.setText(ValueFormat::noteValue(lo, ch == 3) + " to " + ValueFormat::noteValue(hi, ch == 3) + " play; " + String(keyRows_.size()) + " keys can select a sound");
    syncKeys();
}

void SoundsPanel::syncKeys()
{
    const auto s = processor_.solo();
    for (auto* row : keyRows_) {
        const int sound = s ? int(s->keyMaps[size_t(processor_.channel())][size_t(row->note())]) : 0;
        row->sync(sound, soundText(sound));
    }
}

void SoundsPanel::selectSlot(int slot)
{
    slot_ = std::clamp(slot, 1, kSoloSounds);
    list_.setSelected(slot_, dontSendNotification);
    contextChanged();
}

void SoundsPanel::soloChanged() { rebuildList(); }
void SoundsPanel::channelChanged() { rebuildKeys(); }
void SoundsPanel::hexChanged() { rowsFor_ = nullptr; rebuildList(); keysForChannel_ = -1; rebuildKeys(); repaint(); }

void SoundsPanel::tick()
{
    if (processor_.recallSerial() != lastSerial_) { lastSerial_ = processor_.recallSerial(); message(processor_.lastRecall()); }
}

RichText SoundsPanel::contextLine() const
{
    RichText t;
    t.plain("Sound ").bold(ValueFormat::slot(slot_));
    const String n = soundText(slot_);
    if (n.isNotEmpty()) t.plain(kDot).bold(n);
    else t.plain(kDot + "empty");
    return t;
}

void SoundsPanel::resized()
{
    auto area = getLocalBounds();
    auto left = area.removeFromLeft(area.getWidth() / 2 - 8);
    listTitle_.setBounds(left.removeFromTop(16));
    auto buttons = left.removeFromBottom(26);
    const int bw = (buttons.getWidth() - 8) / 3;
    recallBtn_.setBounds(buttons.removeFromLeft(bw)); buttons.removeFromLeft(4);
    storeBtn_.setBounds(buttons.removeFromLeft(bw)); buttons.removeFromLeft(4);
    clearBtn_.setBounds(buttons);
    left.removeFromBottom(6);
    list_.setBounds(left);
    area.removeFromLeft(16);
    keysTitle_.setBounds(area.removeFromTop(16));
    keysHelp_.setBounds(area.removeFromTop(16));
    auto kb = area.removeFromBottom(26);
    keyMap_.setBounds(kb.removeFromLeft(110)); kb.removeFromLeft(6);
    const int kw = (kb.getWidth() - 4) / 2;
    defaultKeysBtn_.setBounds(kb.removeFromLeft(kw)); kb.removeFromLeft(4);
    clearKeysBtn_.setBounds(kb);
    area.removeFromBottom(6);
    keys_.setBounds(area);
}

/* ---------------------------------------------------------- commands */

CommandsPanel::CommandsPanel(SoloProcessor& p)
    : EditorPanel(p), processor_(p),
      listTitle_("Library", Fonts::caption(10.0f, true), colours::textDim), refTitle_("The letters on this channel", Fonts::caption(10.0f, true), colours::textDim),
      use1Btn_(String(CharPointer_UTF8("\xe2\x86\x92 CMD 1"))), use2Btn_(String(CharPointer_UTF8("\xe2\x86\x92 CMD 2"))),
      store1Btn_("Store CMD 1"), store2Btn_("Store CMD 2"), clearBtn_("Clear")
{
    listTitle_.setUpperCase(true); refTitle_.setUpperCase(true);
    for (auto* c : std::initializer_list<Component*>{ &list_, &listTitle_, &refTitle_, &use1Btn_, &use2Btn_, &store1Btn_, &store2Btn_, &clearBtn_, &reference_ })
        addAndMakeVisible(c);
    list_.setKindColours([](int) { return colours::text; });
    list_.onSelect = [this](int slot) { entry_ = slot; contextChanged(); };
    list_.onDoubleClick = [this](int slot) { entry_ = slot; processor_.useLibraryCommand(slot, 0); };
    list_.onRename = [this](int slot, const String& name) {
        processor_.editSolo("Rename command " + String(slot), [slot, name](SoloState& s) { auto& c = s.commands[size_t(slot - 1)]; c.used = true; c.name = name.toStdString(); });
    };
    use1Btn_.setTooltip("Put this entry into the row's first command slot (a double click does the same)");
    use1Btn_.onClick = [this] { processor_.useLibraryCommand(entry_, 0); };
    use2Btn_.setTooltip("Put this entry into the row's second command slot");
    use2Btn_.onClick = [this] { processor_.useLibraryCommand(entry_, 1); };
    store1Btn_.setTooltip("Keep the row's first command here");
    store1Btn_.onClick = [this] { processor_.storeLibraryCommand(entry_, 0); };
    store2Btn_.setTooltip("Keep the row's second command here");
    store2Btn_.onClick = [this] { processor_.storeLibraryCommand(entry_, 1); };
    clearBtn_.onClick = [this] { processor_.editSolo("Clear command " + String(entry_), [this](SoloState& s) { s.commands[size_t(entry_ - 1)] = SoloCommand{}; }); };
    reference_.setReserveScrollbar(true);
    rebuildList();
    rebuildReference();
}

CommandsPanel::~CommandsPanel() = default;

void CommandsPanel::rebuildList()
{
    const auto s = processor_.solo();
    if (s.get() == rowsFor_) return;
    rowsFor_ = s.get();
    std::vector<SlotRow> rows;
    rows.resize(size_t(kSoloCommands));
    for (int k = 0; k < kSoloCommands; ++k) {
        auto& r = rows[size_t(k)];
        r.slot = k + 1;
        const auto& c = s ? s->commands[size_t(k)] : SoloCommand{};
        r.used = c.used;
        r.name = c.used ? String(CharPointer_UTF8(c.name.c_str())) : String();
        if (c.used && c.cmd.cmd != bank::Cmd::None) r.note = String(bank::cmdLetter(c.cmd.cmd)) + " " + commandValueText(c.cmd, ValueFormat::hex());
    }
    list_.setRows(rows);
    list_.setSelected(entry_, dontSendNotification);
}

void CommandsPanel::rebuildReference()
{
    const int ch = processor_.channel();
    if (ch == refForChannel_) return;
    refForChannel_ = ch;
    auto stack = std::make_unique<Stack>(4);
    const ChannelKind kind = processor_.kind();
    for (int i = 1; i <= bank::kCmdCount; ++i) {
        const auto c = bank::Cmd(i);
        if (c == bank::Cmd::H) continue;
        const auto* info = commandInfo(c);
        if (info == nullptr) continue;
        RichText t;
        t.bold(String::charToString(juce_wchar(info->letter)) + "  " + String(info->name));
        t.plain(kDot + String(info->args));
        if (!commandAppliesTo(c, kind)) t.plain(kDot + "does nothing on " + colours::channelName(ch));
        stack->add(std::make_unique<HelpText>(t, 11.0f, commandAppliesTo(c, kind) ? colours::textMute : colours::textDim));
    }
    reference_.setContent(std::move(stack));
}

void CommandsPanel::soloChanged() { rebuildList(); }
void CommandsPanel::channelChanged() { rebuildReference(); }
void CommandsPanel::hexChanged() { rowsFor_ = nullptr; rebuildList(); repaint(); }

RichText CommandsPanel::contextLine() const
{
    RichText t;
    t.plain("Library entry ").bold(ValueFormat::slot(entry_));
    const auto s = processor_.solo();
    if (s && s->commands[size_t(entry_ - 1)].used) t.plain(kDot).bold(String(CharPointer_UTF8(s->commands[size_t(entry_ - 1)].name.c_str())));
    return t;
}

void CommandsPanel::resized()
{
    auto area = getLocalBounds();
    auto left = area.removeFromLeft(area.getWidth() / 2 - 8);
    listTitle_.setBounds(left.removeFromTop(16));
    auto row2 = left.removeFromBottom(26);
    left.removeFromBottom(4);
    auto row1 = left.removeFromBottom(26);
    left.removeFromBottom(6);
    const int bw = (row1.getWidth() - 4) / 2;
    use1Btn_.setBounds(row1.removeFromLeft(bw)); row1.removeFromLeft(4); use2Btn_.setBounds(row1);
    const int cw = (row2.getWidth() - 8) / 3;
    store1Btn_.setBounds(row2.removeFromLeft(cw)); row2.removeFromLeft(4);
    store2Btn_.setBounds(row2.removeFromLeft(cw)); row2.removeFromLeft(4);
    clearBtn_.setBounds(row2);
    list_.setBounds(left);
    area.removeFromLeft(16);
    refTitle_.setBounds(area.removeFromTop(16));
    reference_.setBounds(area);
}

/* ------------------------------------------------------------- setup */

SetupPanel::SetupPanel(SoloProcessor& p) : EditorPanel(p), processor_(p)
{
    addAndMakeVisible(scroll_);
    auto stack = std::make_unique<Stack>(14);
    auto play = std::make_unique<FormGroup>("Playing", 130);
    {
        auto seg = std::make_unique<Segmented>(StringArray{ "start volume", "instrument bank", "ignored" });
        seg->setMini(true);
        seg->attach(param(processor_, processor_.channelParamId(0, ids::velocityMode)));
        play->add("Velocity", std::move(seg), 300, 22, "What a note's velocity does: the start volume, which instrument bank slot it picks, or nothing");
    }
    {
        auto t = std::make_unique<Toggle>("Live follow");
        t->attach(param(processor_, processor_.channelParamId(0, ids::liveFollow)));
        play->add("Changes", std::move(t), 300, Toggle::kHeight, "On, a change of instrument, table, level, pan or transpose reaches the sounding note; off, the next one");
    }
    {
        auto t = std::make_unique<Toggle>("Quantize notes to ticks");
        t->attach(param(processor_, solo::ids::notesOnTick));
        play->add("Timing", std::move(t), 300, Toggle::kHeight, "A note waits for the next tick, as a tracker's would; off, it plays where it lands");
    }
    {
        auto t = std::make_unique<Toggle>("Keys outside the range select sounds");
        t->attach(param(processor_, solo::ids::keyMap));
        play->add("Key map", std::move(t), 300, Toggle::kHeight, "The Sounds tab lays out which key selects which sound");
    }
    stack->add(std::move(play));
    auto time = std::make_unique<FormGroup>("Ticks", 130);
    {
        auto seg = std::make_unique<Segmented>(StringArray{ "Host", "Own" });
        seg->setMini(true);
        seg->attach(param(processor_, solo::ids::tempoSource));
        time->add("Tempo source", std::move(seg), 160, 22, "Host follows the DAW's tempo; Own runs the tempo below. Either way the ticks free-run while the transport is stopped");
    }
    {
        auto st = std::make_unique<Stepper>();
        st->setRange(40, 295, 120);
        st->attach(param(processor_, solo::ids::tempo));
        st->setTextFunction([](int v) { return String(v) + " BPM"; });
        time->add("Own tempo", std::move(st), 110, Stepper::kHeight, "24 ticks a beat: tables, vibrato and commands run on them");
    }
    stack->add(std::move(time));
    auto out = std::make_unique<FormGroup>("Output", 130);
    {
        auto st = std::make_unique<Stepper>();
        st->setRange(0, 7, 7);
        st->attach(param(processor_, solo::ids::volume));
        out->add("Volume", std::move(st), 80, Stepper::kHeight, "NR50, both sides: the master volume the chip mixes at");
    }
    {
        auto st = std::make_unique<Stepper>();
        st->setRange(-40, 6, -6);
        st->setTextFunction([](int v) { return String(v) + " dB"; });
        auto* raw = st.get();
        raw->onChange = [this](int v) { setParam(*this, param(processor_, solo::ids::trim), float(v)); };
        raw->setValue(int(std::lround(paramFloat(processor_, solo::ids::trim))), dontSendNotification);
        out->add("Trim", std::move(st), 80, Stepper::kHeight, "The output level after the chip, in dB");
    }
    {
        auto t = std::make_unique<Toggle>("Hex values");
        t->attach(param(processor_, solo::ids::hexDisplay));
        out->add("Display", std::move(t), 300, Toggle::kHeight, "Values in hex, counting like LSDj, or in decimal");
    }
    stack->add(std::move(out));
    auto files = std::make_unique<FormGroup>("Files", 130);
    {
        auto b = std::make_unique<TextButton>("Save Solo file" + String(CharPointer_UTF8("\xe2\x80\xa6")));
        b->onClick = [this] { saveFile(); };
        files->add("This instance", std::move(b), 160, 24, "The bank, the sounds, the library, the key map, the channel and the row as a .cbsolo file");
    }
    {
        auto b = std::make_unique<TextButton>("Load Solo file" + String(CharPointer_UTF8("\xe2\x80\xa6")));
        b->onClick = [this] { loadFile(); };
        files->add("", std::move(b), 160, 24, "A .cbsolo file replaces all of that, as one undo step");
    }
    {
        auto b = std::make_unique<TextButton>("Load bank" + String(CharPointer_UTF8("\xe2\x80\xa6")));
        b->onClick = [this] { loadBankFile(); };
        files->add("The bank", std::move(b), 160, 24, "A ChipBoy bank file (.chipboy) replaces the instruments, tables, waves and kits; the Instrument tab loads a single .cbi preset");
    }
    stack->add(std::move(files));
    scroll_.setContent(std::move(stack));
}

SetupPanel::~SetupPanel() = default;

RichText SetupPanel::contextLine() const
{
    RichText t;
    t.plain("ChipBoy Solo").plain(kDot).bold(colours::channelName(processor_.channel()));
    t.plain(kDot + "ticks at " + String(int(std::lround(processor_.effectiveTempo()))) + " BPM, " + (processor_.hostTempo() ? "the host's" : "its own"));
    return t;
}

void SetupPanel::resized() { scroll_.setBounds(getLocalBounds()); }

void SetupPanel::saveFile()
{
    chooser_ = std::make_unique<FileChooser>("Save the Solo file", SoloProcessor::soloFolder().getChildFile("Solo.cbsolo"), "*.cbsolo");
    Component::SafePointer<SetupPanel> safe(this);
    chooser_->launchAsync(FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::warnAboutOverwriting, [safe](const FileChooser& fc) {
        if (safe == nullptr) return;
        File f = fc.getResult();
        if (f == File()) return;
        if (!f.hasFileExtension(".cbsolo")) f = f.withFileExtension(".cbsolo");
        safe->message(safe->processor_.saveSoloFile(f) ? "Saved " + f.getFileName() : "Could not write " + f.getFileName());
    });
}

void SetupPanel::loadFile()
{
    chooser_ = std::make_unique<FileChooser>("Load a Solo file", SoloProcessor::soloFolder(), "*.cbsolo");
    Component::SafePointer<SetupPanel> safe(this);
    chooser_->launchAsync(FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [safe](const FileChooser& fc) {
        if (safe == nullptr) return;
        const File f = fc.getResult();
        if (f == File()) return;
        String report;
        safe->processor_.loadSoloFile(f, report);
        safe->message(report);
    });
}

void SetupPanel::loadBankFile()
{
    Component::SafePointer<SetupPanel> safe(this);
    plugin::loadBank(this, [safe](std::unique_ptr<bank::Bank> b, File f) {
        if (safe == nullptr || !b) return;
        safe->processor_.loadBankEdit("Load bank " + f.getFileNameWithoutExtension(), *b);
        safe->message("Loaded bank " + f.getFileName());
    });
}

/* -------------------------------------------------------------- tabs */

class SoloEditor::TabBar : public Component {
public:
    TabBar() { bar_.onChange = [this](int i) { if (onChange && i >= 0 && i < int(panelOf_.size())) onChange(panelOf_[size_t(i)]); }; addAndMakeVisible(bar_); }
    std::function<void(int)> onChange;
    /// The tabs on show, as panel indices; Waves and Kits come and go with the channel.
    void setTabs(const std::vector<int>& panels)
    {
        if (panels == panelOf_) return;
        const int current = currentPanel();
        panelOf_ = panels;
        bar_.clearTabs();
        for (int p : panels) bar_.addTab(kTabNames[p], Colours::transparentBlack, -1);
        setCurrent(current);
    }
    int currentPanel() const { const int i = bar_.getCurrentTabIndex(); return i >= 0 && i < int(panelOf_.size()) ? panelOf_[size_t(i)] : 0; }
    void setCurrent(int panel)
    {
        for (int i = 0; i < int(panelOf_.size()); ++i)
            if (panelOf_[size_t(i)] == panel) { if (bar_.getCurrentTabIndex() != i) bar_.setCurrentTabIndex(i, false); return; }
        if (bar_.getNumTabs() > 0 && bar_.getCurrentTabIndex() < 0) bar_.setCurrentTabIndex(0, false);
    }
    void paint(Graphics& g) override
    {
        g.fillAll(colours::panel);
        g.setColour(colours::lineSoft);
        g.fillRect(0, getHeight() - 1, getWidth(), 1);
    }
    void resized() override { bar_.setBounds(8, 4, getWidth() - 16, getHeight() - 4); }
private:
    struct Bar : TabbedButtonBar {
        Bar() : TabbedButtonBar(TabbedButtonBar::TabsAtTop) {}
        std::function<void(int)> onChange;
        void currentTabChanged(int i, const String&) override { if (onChange) onChange(i); }
    } bar_;
    std::vector<int> panelOf_;
};

/* ------------------------------------------------------------ editor */

SoloEditor::SoloEditor(SoloProcessor& p)
    : AudioProcessorEditor(p), processor_(p), tooltips_(this, 600),
      wordmark_("CHIPBOY", Fonts::pixel(13.0f), colours::text), product_("SOLO", Fonts::pixel(13.0f), colours::accentHi),
      tempoLabel_("Tempo", Fonts::caption(10.0f), colours::textDim), tempo_({}, Fonts::mono(12.0f), colours::text, Justification::centredRight),
      channel_({ "PU1", "PU2", "WAV", "NOI" }), model_({ "DMG", "CGB", "RAW" }),
      undoBtn_("Undo"), redoBtn_("Redo"), strip_(p), status_({}, Fonts::mono(10.5f), colours::textMute)
{
    setLookAndFeel(&lookAndFeel_);
    setWantsKeyboardFocus(true);
    tempoLabel_.setUpperCase(true);
    for (auto* c : std::initializer_list<Component*>{ &wordmark_, &product_, &tempoLabel_, &tempo_, &channel_, &model_, &undoBtn_, &redoBtn_, &status_ })
        addAndMakeVisible(c);
    addChildComponent(strip_);
    channel_.setMini(true);
    channel_.attach(param(processor_, solo::ids::channel));
    channel_.setTooltip("Which of the four voices this plugin is");
    for (int i = 0; i < 4; ++i) channel_.setOptionColour(i, colours::channel(i));
    model_.setMini(true);
    model_.attach(param(processor_, solo::ids::model));
    model_.setTooltip("Which real machine is emulated; RAW is the clean digital mix");
    model_.setOptionColour(1, colours::cgb);
    undoBtn_.onClick = [this] { undo(); };
    redoBtn_.onClick = [this] { redo(); };
    tabs_ = std::make_unique<TabBar>();
    addAndMakeVisible(*tabs_);

    panels_[Sounds] = std::make_unique<SoundsPanel>(processor_);
    panels_[Instrument] = std::make_unique<InstrumentPanel>(processor_);
    panels_[Tables] = std::make_unique<TablesPanel>(processor_);
    panels_[Waves] = std::make_unique<WavesPanel>(processor_);
    panels_[Kits] = std::make_unique<KitsPanel>(processor_);
    panels_[Commands] = std::make_unique<CommandsPanel>(processor_);
    panels_[Setup] = std::make_unique<SetupPanel>(processor_);
    for (auto& panel : panels_) {
        if (!panel) continue;                      // Main is the strip, not a panel
        panel->setCompact(true);
        panel->onContextChanged = [this] { refreshContext(); };
        panel->onMessage = [this](const String& text) { showMessage(text); };
        panel->onOpenSlot = [this](SlotKind kind, int slot) { openSlot(kind, slot); };
        addChildComponent(*panel);
    }
    strip_.onOpenSlot = [this](SlotKind kind, int slot) { openSlot(kind, slot); };
    strip_.onOpenSound = [this](int slot) { showTab(Sounds); panels_[Sounds]->selectSlot(slot); };
    tabs_->onChange = [this](int panel) { showTab(panel); };

    hexWatch_ = std::make_unique<ParamWatch>(param(processor_, solo::ids::hexDisplay), [this](float v) {
        ValueFormat::setHex(v > 0.5f);
        strip_.hexChanged();
        for (auto& panel : panels_) if (panel) panel->hexChanged();
        refreshContext();
        repaint();
    });
    channelWatch_ = std::make_unique<ParamWatch>(param(processor_, solo::ids::channel), [this](float) { channelChanged(); });

    lastBank_ = processor_.bank().get();
    lastSolo_ = processor_.solo().get();
    setSize(kSoloWidth, kSoloHeight);
    channelChanged();
    showTab(Main);
    restoreView();
    startTimerHz(30);
}

SoloEditor::~SoloEditor()
{
    stopTimer();
    saveView();
    setLookAndFeel(nullptr);
}

void SoloEditor::channelChanged()
{
    const int ch = processor_.channel();
    if (ch == channelShown_) return;
    channelShown_ = ch;
    strip_.channelChanged();
    for (auto& panel : panels_) if (panel) panel->setChannel(ch);
    static_cast<SoundsPanel&>(*panels_[Sounds]).channelChanged();
    static_cast<CommandsPanel&>(*panels_[Commands]).channelChanged();
    std::vector<int> tabs{ Main, Sounds, Instrument, Tables };
    if (ch == 2) { tabs.push_back(Waves); tabs.push_back(Kits); }
    tabs.push_back(Commands); tabs.push_back(Setup);
    tabs_->setTabs(tabs);
    if (ch != 2 && (tab_ == Waves || tab_ == Kits)) showTab(Instrument);
    else showTab(tabs_->currentPanel());
    product_.setColour(colours::channel(ch));
    refreshContext();
    repaint();
}

void SoloEditor::showTab(int tab)
{
    const int t = std::clamp(tab, 0, int(kTabs) - 1);
    for (int i = 0; i < int(kTabs); ++i) {
        if (!panels_[size_t(i)]) continue;
        const bool show = i == t;
        if (panels_[size_t(i)]->isVisible() != show) { panels_[size_t(i)]->setVisible(show); panels_[size_t(i)]->shown(show); }
    }
    strip_.setVisible(t == Main);
    tab_ = t;
    tabs_->setCurrent(t);
    refreshContext();
    if (isTimerRunning()) saveView();
}

void SoloEditor::openSlot(SlotKind kind, int slot)
{
    const int tab = kind == SlotKind::Instrument ? Instrument : kind == SlotKind::Table ? Tables : kind == SlotKind::Wave ? Waves : kind == SlotKind::Kit ? Kits : Sounds;
    if ((tab == Waves || tab == Kits) && processor_.channel() != 2) return;
    showTab(tab);
    if (panels_[size_t(tab)]) panels_[size_t(tab)]->selectSlot(slot);
    refreshContext();
}

void SoloEditor::saveView()
{
    auto view = processor_.apvts.state.getOrCreateChildWithName(kViewNode, nullptr);
    view.setProperty(kTabProp, tab_, nullptr);
    for (int i = 0; i < int(kTabs); ++i) {
        if (!panels_[size_t(i)]) continue;
        auto node = view.getOrCreateChildWithName(Identifier("panel" + String(i)), nullptr);
        panels_[size_t(i)]->saveView(node);
    }
}

void SoloEditor::restoreView()
{
    const auto view = processor_.apvts.state.getChildWithName(kViewNode);
    if (!view.isValid()) return;
    for (int i = 0; i < int(kTabs); ++i) {
        if (!panels_[size_t(i)]) continue;
        const auto node = view.getChildWithName(Identifier("panel" + String(i)));
        if (node.isValid()) panels_[size_t(i)]->restoreView(node);
    }
    if (view.hasProperty(kTabProp)) {
        const int t = int(view[kTabProp]);
        if ((t == Waves || t == Kits) && processor_.channel() != 2) showTab(Instrument); else showTab(t);
    }
}

void SoloEditor::showMessage(const String& text)
{
    message_ = text;
    messageUntil_ = Time::getMillisecondCounter() + 5000;
    lastContext_ = RichText();
    refreshContext();
}

void SoloEditor::refreshContext()
{
    RichText c;
    if (message_.isNotEmpty() && Time::getMillisecondCounter() < messageUntil_) c.plain(message_);
    else if (panels_[size_t(tab_)]) c = panels_[size_t(tab_)]->contextLine();
    else {
        const auto b = processor_.bank();
        const int slot = paramValue(processor_, processor_.channelParamId(0, ids::instrument));
        const bank::Instrument* inst = b && slot > 0 ? b->instrument(slot) : nullptr;
        c.bold(inst ? String(inst->name) : slot > 0 ? kDash + " empty " + kDash : String("no instrument"));
    }
    if (c != lastContext_) { lastContext_ = c; status_.setText(c.toString()); }
}

void SoloEditor::refreshHeader()
{
    const String t = String(int(std::lround(processor_.effectiveTempo()))) + " BPM " + (processor_.hostTempo() ? "HOST" : "OWN");
    if (t != tempoShown_) { tempoShown_ = t; tempo_.setText(t); }
    undoBtn_.setEnabled(processor_.history().canUndo());
    redoBtn_.setEnabled(processor_.history().canRedo());
}

void SoloEditor::timerCallback()
{
    if (processor_.channel() != channelShown_) channelChanged();   // a change the watch saw before the value landed
    const auto b = processor_.bank();
    const auto s = processor_.solo();
    if (b.get() != lastBank_) {
        lastBank_ = b.get();
        strip_.bankChanged();
        for (auto& panel : panels_) if (panel) panel->bankChanged();
    }
    if (s.get() != lastSolo_) {
        lastSolo_ = s.get();
        strip_.soloChanged();
        static_cast<SoundsPanel&>(*panels_[Sounds]).soloChanged();
        static_cast<CommandsPanel&>(*panels_[Commands]).soloChanged();
    }
    if (processor_.recallSerial() != lastRecall_) { lastRecall_ = processor_.recallSerial(); showMessage(processor_.lastRecall()); }
    strip_.tick();
    refreshHeader();
    if (panels_[size_t(tab_)]) panels_[size_t(tab_)]->tick();
    refreshContext();
}

void SoloEditor::paint(Graphics& g)
{
    g.fillAll(colours::bg);
    g.setColour(colours::panel);
    g.fillRect(0, 0, getWidth(), kHeader);
    g.setColour(colours::lineSoft);
    g.fillRect(0, kHeader - 1, getWidth(), 1);
    g.fillRect(0, getHeight() - kStatus, getWidth(), 1);
}


void SoloEditor::resized()
{
    auto area = getLocalBounds();
    auto head = area.removeFromTop(kHeader).reduced(kPad, 0);
    wordmark_.setBounds(head.removeFromLeft(62));
    product_.setBounds(head.removeFromLeft(38));
    head.removeFromLeft(8);
    channel_.setBounds(head.removeFromLeft(channel_.preferredWidth()).withSizeKeepingCentre(channel_.preferredWidth(), 22));
    head.removeFromLeft(8);
    model_.setBounds(head.removeFromLeft(model_.preferredWidth()).withSizeKeepingCentre(model_.preferredWidth(), 22));
    tempo_.setBounds(head.removeFromRight(92));
    tempoLabel_.setBounds(head.removeFromRight(44));
    auto foot = area.removeFromBottom(kStatus).reduced(kPad, 0);
    redoBtn_.setBounds(foot.removeFromRight(44).withSizeKeepingCentre(44, 18));
    foot.removeFromRight(4);
    undoBtn_.setBounds(foot.removeFromRight(44).withSizeKeepingCentre(44, 18));
    foot.removeFromRight(8);
    status_.setBounds(foot);
    tabs_->setBounds(area.removeFromTop(kTabsHeight));
    const auto page = area.reduced(kPad);
    strip_.setBounds(page);
    for (auto& panel : panels_) if (panel) panel->setBounds(page);
}

bool SoloEditor::typing() const
{
    auto* focused = Component::getCurrentlyFocusedComponent();
    return focused != nullptr && dynamic_cast<TextEditor*>(focused) != nullptr;
}

void SoloEditor::undo()
{
    auto& history = processor_.history();
    const String what = history.undoName();
    if (!history.undo()) return;
    showMessage(what.isEmpty() ? String("Undone") : "Undone: " + what);
}

void SoloEditor::redo()
{
    auto& history = processor_.history();
    const String what = history.redoName();
    if (!history.redo()) return;
    showMessage(what.isEmpty() ? String("Redone") : "Redone: " + what);
}

bool SoloEditor::keyPressed(const KeyPress& key)
{
    if (key == KeyPress::escapeKey) { grabKeyboardFocus(); return true; }
    const auto mods = key.getModifiers();
    if ((mods.isCtrlDown() || mods.isCommandDown()) && !typing()) {
        const auto ch = key.getTextCharacter();
        const int code = key.getKeyCode();
        const bool isZ = code == 'Z' || code == 'z' || ch == 'z' || ch == 'Z';
        const bool isY = code == 'Y' || code == 'y' || ch == 'y' || ch == 'Y';
        if (isZ) { if (mods.isShiftDown()) redo(); else undo(); return true; }
        if (isY) { redo(); return true; }
    }
    return false;
}

} // namespace chipboy::plugin
