#include "plugin/main/panels/MixerRow.h"

#include <algorithm>
#include <cmath>

namespace chipboy::plugin {

using namespace juce;
using namespace chipboy::ui;

namespace {
constexpr int kPad = 8, kGap = 6, kTightGap = 4;
constexpr int kHead = 20, kScope = 60, kRegs = 14, kRow = 24, kQuick = 70, kSeg = 22, kCaption = 10;
constexpr int kButton = 26, kButtonH = 20;
const char* kMuteTip = "NR51 gate off. Pops like the hardware.";
const char* kSoloTip = "Gates the other three off (NR51). Pops like the hardware.";
const char* kPanTip = "NR51: off, left, both, right, or the instrument's own. There is no pan law.";
const char* kTableTip = "Table override for this channel; inst = the instrument's own. Right-click lists the bank, double-click opens it.";
const char* kInstTip = "The instrument this channel's notes latch at note-on. Right-click lists the bank, double-click opens it.";
const char* kTransposeTip = "Semitones added to every note on this channel, before the period is worked out";
const String kDot = String(CharPointer_UTF8(" \xc2\xb7 "));
/// Why Instrument, Table and the two slots are greyed on a Hybrid channel
/// (docs/COMMANDS_AND_TEMPO.md section 20).
const char* kHybridTip = "The tracker's cells drive this channel: it is on Hybrid.";
/// The reserved octave is one below the channel's playable floor.
juce::String keyswitchTip(int ch)
{
    const int base = ch < 2 ? 24 : 12;
    return "Keyswitches: notes " + juce::String(base) + juce::String(CharPointer_UTF8("\xe2\x80\x93")) + juce::String(base + 11)
         + " select instrument slots 1" + juce::String(CharPointer_UTF8("\xe2\x80\x93")) + "12 and never sound";
}

}

/* ------------------------------------------------------ ChannelStrip */

ChannelStrip::ChannelStrip(ChipBoyProcessor& p, int ch)
    : processor_(p), ch_(ch),
      name_(colours::channelName(ch), Fonts::pixel(12.0f), colours::channel(ch)),
      hybridBox_(hybrid_), sourceBox_(source_),
      instrumentName_({}, Fonts::sans(12.0f), colours::textMute),
      tableLabel_("Table", Fonts::caption(10.0f), colours::textDim),
      transposeLabel_("Transpose", Fonts::caption(10.0f), colours::textDim),
      pan_({ String(CharPointer_UTF8("\xe2\x80\x93")), "L", "LR", "R", "inst" }),
      mute_("M"), solo_("S"), keyswitch_("KS"),
      cmd1_("CMD1", ChipBoyProcessor::kindOf(ch)), cmd2_("CMD2", ChipBoyProcessor::kindOf(ch))
{
    led_.setColour(colours::channel(ch));
    led_.setInterceptsMouseClicks(false, false);
    tableLabel_.setUpperCase(true);
    transposeLabel_.setUpperCase(true);
    for (auto* c : std::initializer_list<Component*>{ &led_, &name_, &sourceBox_, &scope_, &regs_, &instrument_, &instrumentName_,
                                                     &tableLabel_, &table_, &transposeLabel_, &transpose_, &pan_,
                                                     &mute_, &solo_, &keyswitch_, &cmd1_, &cmd2_ })
        addAndMakeVisible(c);
    // Only while the song has this channel on Hybrid (section 20).
    addChildComponent(hybridBox_);
    hybrid_.set("HYBRID", Pill::Tone::Accent);
    hybridBox_.setTooltip(kHybridTip);

    // source badge: a menu bound to the channel's source parameter
    sourceBox_.onClick = [this] { showSourceMenu(); };
    sourceAtt_ = std::make_unique<ParameterAttachment>(param(processor_, channelParamId(ch_, ids::source)), [this](float v) { sourceValue_ = int(std::lround(v)); });
    sourceAtt_->sendInitialUpdate();

    // scope and registers
    ScopeView::Source src;
    src.ring = &processor_.scopes().channels[size_t(ch_)];
    src.state = &processor_.scopes().state[size_t(ch_)];
    src.latestCycle = &processor_.scopes().latestCycle;
    scope_.setSource(src);
    scope_.setChannel(ch_);
    scope_.setChrome(false);
    scope_.setLcdGround(true);
    scope_.setAnalogCornerHz(analogCornerHz(processor_));
    regs_.setChannel(ch_);

    // the tracker row: instrument, table, transpose, then the two slots
    // The one selector convention (UI_DESIGN section 2.1): the click focuses
    // the field for typing, a right click lists the bank's slots by name and
    // a double click opens that item's own tab.
    instrument_.setTooltip(kInstTip);
    instrument_.attach(param(processor_, channelParamId(ch_, ids::instrument)));
    instrument_.onList = [this] { showInstrumentMenu(); };
    instrument_.onOpen = [this] { if (onOpenSlot && instrument_.value() > 0) onOpenSlot(SlotKind::Instrument, instrument_.value()); };
    table_.setTooltip(kTableTip);
    table_.attach(param(processor_, channelParamId(ch_, ids::table)));
    table_.onList = [this] { showTableMenu(); };
    table_.onOpen = [this] { if (onOpenSlot && table_.value() > 0) onOpenSlot(SlotKind::Table, table_.value()); };
    transpose_.setTooltip(kTransposeTip);
    transpose_.attach(param(processor_, channelParamId(ch_, ids::transpose)));
    transpose_.setTextFunction([](int v) { return (v > 0 ? "+" : "") + String(v); });
    cmd1_.attach(param(processor_, channelParamId(ch_, ids::cmd1Type)), param(processor_, channelParamId(ch_, ids::cmd1X)), param(processor_, channelParamId(ch_, ids::cmd1Y)));
    cmd2_.attach(param(processor_, channelParamId(ch_, ids::cmd2Type)), param(processor_, channelParamId(ch_, ids::cmd2X)), param(processor_, channelParamId(ch_, ids::cmd2Y)));

    // quick controls by channel type
    const bool wave = ch_ == 2;
    if (!wave) {
        const String nr = ch_ == 3 ? "NR42" : ch_ == 0 ? "NR12" : "NR22";
        level_ = std::make_unique<Knob>("Level");
        level_->setTooltip(nr + " bits 7-4: envelope start volume, 16 levels; inst = the instrument's");
        level_->attach(param(processor_, channelParamId(ch_, ids::level)));
        level_->setAccent(colours::channel(ch_));
        addAndMakeVisible(*level_);
    } else {
        waveLevelLabel_.reset(TextLine::label("Level"));
        waveLevel_ = std::make_unique<Segmented>(StringArray{ "mute", "25", "50", "100", "inst" });
        waveLevel_->setMini(true);
        waveLevel_->setTooltip("NR32: two bits, no envelope on this channel; inst = the instrument's");
        waveLevel_->attach(param(processor_, channelParamId(ch_, ids::level)));
        addAndMakeVisible(*waveLevelLabel_);
        addAndMakeVisible(*waveLevel_);
    }

    // mute / solo: NR51 gates
    mute_.setTooltip(kMuteTip);
    mute_.setClickingTogglesState(true);
    mute_.onClick = [this] { processor_.setChannelMute(ch_, mute_.getToggleState()); };
    solo_.setTooltip(kSoloTip);
    solo_.setClickingTogglesState(true);
    solo_.onClick = [this] { processor_.setChannelSolo(ch_, solo_.getToggleState()); };

    // keyswitches: the reserved octave picks an instrument instead of sounding
    keyswitch_.setTooltip(keyswitchTip(ch_));
    keyswitch_.setClickingTogglesState(true);
    keyswitchAtt_ = std::make_unique<ToggleParam>(keyswitch_, param(processor_, channelParamId(ch_, ids::keyswitch)));

    // pan: the parameter's order is off, L, R, both, inst; the display's is the hardware's
    pan_.setMini(true);
    pan_.setTooltip(kPanTip);
    panParam_ = std::make_unique<SegmentedParam>(pan_, param(processor_, channelParamId(ch_, ids::pan)), std::vector<int>{ 0, 1, 3, 2, 4 });

    refreshInstrumentName();
    refreshHybrid();
}

ChannelStrip::~ChannelStrip() = default;

void ChannelStrip::setSelected(bool on)
{
    if (on == selected_) return;
    selected_ = on;
    repaint();
}

void ChannelStrip::setScopeSettings(ScopeView::Trace trace, int periods)
{
    scope_.setTrace(trace);
    scope_.setPeriods(periods);
}

void ChannelStrip::setAnalogCornerHz(double hz) { scope_.setAnalogCornerHz(hz); }

void ChannelStrip::showSourceMenu()
{
    PopupMenu m;
    bool owned = false;
    const String voice = channelSourceText(processor_, ch_, &owned);
    if (owned) { m.addItem(100, "Owned by a Voice plugin: " + voice.fromFirstOccurrenceOf(": ", false, false), false); m.addSeparator(); }
    m.addSectionHeader("Where this voice's notes come from");
    m.addItem(1, "Omni", true, sourceValue_ == 0);
    PopupMenu midi;
    for (int i = 1; i <= 16; ++i) midi.addItem(1 + i, "MIDI " + String(i), true, sourceValue_ == i);
    m.addSubMenu("MIDI channel", midi, true, nullptr, sourceValue_ >= 1 && sourceValue_ <= 16);
    m.addItem(18, "Off (the tracker lane and tables only)", true, sourceValue_ == 17);
    Component::SafePointer<ChannelStrip> safe(this);
    m.showMenuAsync(PopupMenu::Options().withTargetComponent(&sourceBox_), [safe](int r) {
        if (safe == nullptr || r < 1 || r > 18) return;
        setParam(*safe, param(safe->processor_, channelParamId(safe->ch_, ids::source)), float(r - 1));
    });
}

/// The name beside the stepper is the instrument the **driver** last loaded,
/// not the one the parameter names (docs/COMMANDS_AND_TEMPO.md section 30):
/// a cell's INS column, an A, a Hybrid channel or a keyswitch all change what
/// is really playing, and the strip follows it.
void ChannelStrip::refreshInstrumentName()
{
    const auto b = processor_.bank();
    driver::VoiceView v;
    link::unpackState(processor_.scopes().state[size_t(ch_)].load(std::memory_order_relaxed), v);
    const int loaded = int(v.instrument);
    const int slot = loaded > 0 ? loaded : paramValue(processor_, channelParamId(ch_, ids::instrument));
    if (b.get() == namesFor_ && slot == instrumentShown_ && loaded == loadedInstrument_) return;
    namesFor_ = b.get();
    instrumentShown_ = slot;
    loadedInstrument_ = loaded;
    if (slot <= 0) { instrumentName_.setText("no instrument"); instrumentName_.setColour(colours::textDim); return; }
    const bank::Instrument* inst = b ? b->instrument(slot) : nullptr;
    if (!inst) { instrumentName_.setText(String(CharPointer_UTF8("\xe2\x80\x94 empty \xe2\x80\x94"))); instrumentName_.setColour(colours::textDim); return; }
    // The slot is named when the driver is playing something the stepper does
    // not say, so the two never disagree silently.
    const bool elsewhere = loaded > 0 && loaded != paramValue(processor_, channelParamId(ch_, ids::instrument));
    instrumentName_.setText(elsewhere ? ValueFormat::number(slot) + " " + String(inst->name) : String(inst->name));
    const Colour c = instrumentFitsChannel(inst->type, ch_) ? instrumentKindColour(int(inst->type)) : colours::warn;
    instrumentName_.setColour(hybridShown_ == 1 && !elsewhere ? c.withMultipliedAlpha(0.45f) : c);
}

/// The bank's slots by name, the list every slot field's right click opens.
void ChannelStrip::showInstrumentMenu()
{
    const auto b = processor_.bank();
    if (!b) return;
    PopupMenu m;
    m.addSectionHeader("Instrument");
    const int current = instrument_.value();
    m.addItem(1, String(CharPointer_UTF8("\xe2\x80\x93   none")), true, current == 0);
    for (int slot = 1; slot <= bank::kInstrumentSlots; ++slot) {
        const auto* inst = b->instrument(slot);
        if (inst == nullptr) continue;
        PopupMenu::Item item(slotAndName(slot, inst->name));
        item.itemID = slot + 1;
        item.isTicked = slot == current;
        item.isEnabled = instrumentFitsChannel(inst->type, ch_);
        if (!item.isEnabled) item.shortcutKeyDescription = instrumentTypeName(inst->type);
        m.addItem(std::move(item));
    }
    Component::SafePointer<ChannelStrip> safe(this);
    m.showMenuAsync(PopupMenu::Options().withTargetComponent(&instrument_), [safe](int r) {
        if (safe == nullptr || r < 1) return;
        setParam(*safe, param(safe->processor_, channelParamId(safe->ch_, ids::instrument)), float(r - 1));
    });
}

void ChannelStrip::showTableMenu()
{
    const auto b = processor_.bank();
    if (!b) return;
    PopupMenu m;
    m.addSectionHeader("Table");
    const int current = table_.value();
    m.addItem(1, String(CharPointer_UTF8("\xe2\x80\x93   the instrument's")), true, current == 0);
    for (int slot = 1; slot <= bank::kTableSlots; ++slot)
        if (const auto* t = b->table(slot)) m.addItem(slot + 1, slotAndName(slot, t->name), true, slot == current);
    Component::SafePointer<ChannelStrip> safe(this);
    m.showMenuAsync(PopupMenu::Options().withTargetComponent(&table_), [safe](int r) {
        if (safe == nullptr || r < 1) return;
        setParam(*safe, param(safe->processor_, channelParamId(safe->ch_, ids::table)), float(r - 1));
    });
}

void ChannelStrip::tick()
{
    led_.setOn(processor_.channelLevels[size_t(ch_)].load() >= 0);
    regs_.setState(processor_.scopes().state[size_t(ch_)].load(std::memory_order_acquire));

    bool owned = false;
    const String text = channelSourceText(processor_, ch_, &owned);
    if (text != source_.text()) {
        source_.set(text, owned ? Pill::Tone::Ok : Pill::Tone::Neutral);
        sourceBox_.setTooltip(owned ? "A ChipBoy Voice plugin on another track owns this channel. Click to set the MIDI source it falls back to."
                                    : "Where this voice's notes come from. Click to change.");
        resized();
    }
    const bool mute = processor_.channelMute(ch_), solo = processor_.channelSolo(ch_);
    if (mute != mute_.getToggleState()) mute_.setToggleState(mute, dontSendNotification);
    if (solo != solo_.getToggleState()) solo_.setToggleState(solo, dontSendNotification);
    refreshInstrumentName();
    refreshHybrid();
    // A kit is a sample, not a repeating wave, so the wave channel's scope
    // keeps a fixed window while one plays (section 22).
    if (ch_ == 2) {
        driver::VoiceView v;
        link::unpackState(processor_.scopes().state[2].load(std::memory_order_relaxed), v);
        const auto b = processor_.bank();
        const bank::Instrument* inst = b ? b->instrument(v.instrument) : nullptr;
        scope_.setFixedWindow(inst != nullptr && inst->type == bank::InstrumentType::Kit);
    }
}

/// A Hybrid channel reads none of the strip's Instrument, Table or command
/// slots (section 20), so they grey out, the head carries a HYBRID tag and
/// every one of them says why.
void ChannelStrip::refreshHybrid()
{
    const auto song = processor_.song();
    const int on = song && song->noteSource[size_t(ch_)] == tracker::NoteSource::Hybrid ? 1 : 0;
    if (on == hybridShown_) return;
    hybridShown_ = on;
    namesFor_ = nullptr;                 // the instrument name greys with the stepper
    refreshInstrumentName();
    hybridBox_.setVisible(on != 0);
    instrument_.setEnabled(on == 0);
    table_.setEnabled(on == 0);
    instrument_.setTooltip(on != 0 ? String(kHybridTip) : String(kInstTip));
    table_.setTooltip(on != 0 ? String(kHybridTip) : String(kTableTip));
    cmd1_.setInert(on != 0, "from the cells");
    cmd2_.setInert(on != 0, "from the cells");
    resized();
    repaint();
}

void ChannelStrip::bankChanged() { refreshInstrumentName(); }

void ChannelStrip::hexChanged()
{
    namesFor_ = nullptr;
    instrumentShown_ = -1;
    refreshInstrumentName();
    // The two slots show the arguments in Decimal and the byte in Hex
    // (docs/COMMANDS_AND_TEMPO.md section 34).
    cmd1_.hexChanged();
    cmd2_.hexChanged();
    repaint();
}

void ChannelStrip::mouseDown(const MouseEvent& e)
{
    if (e.y < kPad + kHead + kGap && onSelect) onSelect(ch_);
}

void ChannelStrip::paint(Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(colours::panel2);
    g.fillRoundedRectangle(r, 5.0f);
    g.setColour(selected_ ? colours::channel(ch_) : colours::lineSoft);
    g.drawRoundedRectangle(r, 5.0f, 1.0f);
    if (selected_) g.drawRoundedRectangle(r.reduced(1.0f), 4.0f, 1.0f);
}

void ChannelStrip::resized()
{
    const int x = kPad, w = getWidth() - 2 * kPad;
    int y = kPad;
    // head
    led_.setBounds(x, y + (kHead - 7) / 2, 7, 7);
    name_.setBounds(x + 13, y, 44, kHead);
    const int pw = std::min(source_.preferredWidth(), w - 60);
    sourceBox_.setBounds(x + w - pw, y + 1, pw, 18);
    if (hybridBox_.isVisible()) {
        const int hw = std::min(hybrid_.preferredWidth(), std::max(0, sourceBox_.getX() - (x + 46) - 6));
        hybridBox_.setBounds(x + 46, y + 1, hw, 18);
    }
    y += kHead + kGap;
    scope_.setBounds(x, y, w, kScope);
    y += kScope + kGap;
    regs_.setBounds(x, y, w, kRegs);
    y += kRegs + kGap;
    instrument_.setBounds(x, y, instrument_.preferredWidth(), kRow);
    instrumentName_.setBounds(x + instrument_.preferredWidth() + 6, y, w - instrument_.preferredWidth() - 6, kRow);
    y += kRow + kGap;
    // The level band. PU and NOI get the knob, with table and transpose in a
    // column beside it; WAV's four steps are too wide for a column, so it
    // takes the band's first row and the two steppers share the second.
    if (level_) {
        level_->setBounds(x, y, Knob::kWidth, Knob::kHeight);
        const int cx = x + Knob::kWidth + kGap, cw = w - Knob::kWidth - kGap;
        tableLabel_.setBounds(cx, y, cw, kCaption);
        table_.setBounds(cx, y + kCaption, cw, kRow);
        transposeLabel_.setBounds(cx, y + kQuick - kCaption - kRow, cw, kCaption);
        transpose_.setBounds(cx, y + kQuick - kRow, cw, kRow);
    } else {
        waveLevelLabel_->setBounds(x, y, w, kCaption);
        waveLevel_->setBounds(x, y + kCaption, std::min(waveLevel_->preferredWidth(), w), waveLevel_->preferredHeight());
        const int half = (w - kGap) / 2, ly = y + kQuick - kCaption - kRow;
        tableLabel_.setBounds(x, ly, half, kCaption);
        table_.setBounds(x, ly + kCaption, half, kRow);
        transposeLabel_.setBounds(x + half + kGap, ly, w - half - kGap, kCaption);
        transpose_.setBounds(x + half + kGap, ly + kCaption, w - half - kGap, kRow);
    }
    y += kQuick + kGap;
    // pan, then the three gates at the right edge
    const int buttons = 3 * kButton + 2 * 3;
    const int panW = std::min(pan_.preferredWidth(), w - buttons - kGap);
    pan_.setBounds(x, y, panW, kSeg);
    int bx = x + w - buttons;
    for (auto* b : { &mute_, &solo_, &keyswitch_ }) { b->setBounds(bx, y + (kSeg - kButtonH) / 2, kButton, kButtonH); bx += kButton + 3; }
    // the two slots read as one block, so they sit on the tighter gap
    y += kSeg + kTightGap;
    cmd1_.setBounds(x, y, w, ui::CommandSlot::kHeight);
    y += ui::CommandSlot::kHeight + kTightGap;
    cmd2_.setBounds(x, y, w, ui::CommandSlot::kHeight);
}

/* ------------------------------------------------------- MasterStrip */

MasterStrip::MasterStrip(ChipBoyProcessor& p)
    : processor_(p),
      name_("MASTER", Fonts::pixel(12.0f), colours::lcdTrace),
      modelBox_(model_),
      volLabel_("Vol", Fonts::caption(10.0f), colours::textDim),
      trimLabel_("Trim", Fonts::caption(10.0f), colours::textDim, Justification::centred),
      mix_({}, Fonts::mono(10.5f), colours::textMute), mixBox_(mix_),
      noise_("Headphone Noise"), lcd_("LCD Whine"), declick_("De-click")
{
    volLabel_.setUpperCase(true); trimLabel_.setUpperCase(true);
    for (auto* c : std::initializer_list<Component*>{ &name_, &modelBox_, &scope_, &mixBox_, &volLabel_, &trimLabel_, &vol_, &noise_, &lcd_, &declick_, &trim_ })
        addAndMakeVisible(c);
    mixBox_.setTooltip("The two mixer registers: NR50 is the master volume per side, NR51 the four gates. Mute and solo write NR51.");
    modelBox_.setTooltip("Which real machine is emulated. RAW is the clean digital mix of a gaming emulator.");
    scope_.setSource(&processor_.scopes().master);
    scope_.setWindowMs(12.0);
    scope_.setLcdGround(true);

    // One VOL for both NR50 sides (section 21). The two parameters remain --
    // the M command and existing automation address left and right -- so the
    // control writes both as one undo step, shows the left value, and shows
    // both when something has moved them apart.
    vol_.setRange(0, 7, 7);
    vol_.setTyped(true);
    vol_.setTextFunction([this](int v) { return volR_ == v ? String(v) : String(v) + String(CharPointer_UTF8("\xc2\xb7")) + String(volR_); });
    vol_.onChange = [this](int v) {
        auto* history = ui::historyFor(*this);
        if (history != nullptr) history->beginGesture("Master volume " + String(v));
        setParam(*this, param(processor_, ids::masterL), float(v));
        setParam(*this, param(processor_, ids::masterR), float(v));
        if (history != nullptr) history->endGesture();
    };
    volLAtt_ = std::make_unique<ParameterAttachment>(param(processor_, ids::masterL), [this](float v) { volL_ = roundToInt(v); refreshVol(); });
    volRAtt_ = std::make_unique<ParameterAttachment>(param(processor_, ids::masterR), [this](float v) { volR_ = roundToInt(v); refreshVol(); });
    volLAtt_->sendInitialUpdate();
    volRAtt_->sendInitialUpdate();

    noise_.setTooltip("The hiss and the frame hum, as measured.");
    noise_.attach(param(processor_, ids::noise));
    lcd_.setTooltip("The 9198 Hz line the display puts into the headphones, on its own switch.");
    lcd_.attach(param(processor_, ids::lcd));
    declick_.setTooltip("Crossfades each DAC-on step. A departure: the header reads MODIFIED while it is on.");
    declick_.attach(param(processor_, ids::declick));
    trim_.setTooltip("The one continuous control in the product: a fader after the analog stage, outside the chip");
    trim_.attach(param(processor_, ids::trim));
    refreshVol();
}

MasterStrip::~MasterStrip() = default;

/// The readout is the left side; when the two are apart -- automation, or an
/// M command -- it shows both, left before right, and the tooltip says so.
void MasterStrip::refreshVol()
{
    vol_.setValue(volL_, dontSendNotification);
    vol_.setTooltip(volL_ == volR_
                        ? String("NR50: the master volume both sides. 0 is 1/8, not mute.")
                        : "NR50: left " + String(volL_) + ", right " + String(volR_) + " " + String(CharPointer_UTF8("\xe2\x80\x94")) + " a step here sets both.");
    vol_.repaint();
}

void MasterStrip::tick()
{
    const int m = modelIndex(processor_);
    if (m != lastModel_) {
        lastModel_ = m;
        model_.set(modelName(m), Pill::Tone::Neutral);
        resized();
    }
    // The mixer's own registers, next to the picture of what they do.
    const uint32_t mix = processor_.scopes().mix.load(std::memory_order_relaxed);
    if (mix != mixShown_) {
        mixShown_ = mix;
        mix_.setText("NR50 " + ValueFormat::byte(int(mix & 0xFF)) + kDot + "NR51 " + ValueFormat::byte(int((mix >> 8) & 0xFF))
                     + kDot + ((mix >> 16) & 1 ? "on" : "off"));
    }
}

void MasterStrip::paint(Graphics& g)
{
    const auto r = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(colours::panel2);
    g.fillRoundedRectangle(r, 5.0f);
    g.setColour(colours::lineSoft);
    g.drawRoundedRectangle(r, 5.0f, 1.0f);
}

void MasterStrip::resized()
{
    const int x = kPad, w = getWidth() - 2 * kPad;
    const int bottom = getHeight() - kPad;
    int y = kPad;
    name_.setBounds(x, y, 80, kHead);
    const int pw = std::min(model_.preferredWidth(), 60);
    modelBox_.setBounds(x + w - pw, y + 1, pw, 18);
    y += kHead + kGap;
    // The output's picture takes whatever the controls under it leave: the
    // master strip is as tall as a channel strip and this is what it is for.
    // One VOL row and three switches, where two volumes and two switches
    // stood: the same height, so the scope keeps its picture (section 21).
    const int controls = kRegs + kGap + (kRow + kGap) + 3 * (Toggle::kHeight + kGap) - kGap;
    scope_.setBounds(x, y, w, std::max(kScope, bottom - y - kGap - controls));
    y = scope_.getBottom() + kGap;
    mixBox_.setBounds(x, y, w, kRegs);
    y += kRegs + kGap;
    const int faderW = 54;
    const int leftW = w - faderW - 6;
    // left column: the one master volume and the three switches
    const int fy = y;
    volLabel_.setBounds(x, y, 32, kRow);
    vol_.setBounds(x + 34, y, vol_.preferredWidth(), kRow);
    y += kRow + kGap;
    for (auto* sw : { &noise_, &lcd_, &declick_ }) {
        sw->setBounds(x, y, leftW, Toggle::kHeight);
        y += Toggle::kHeight + kGap;
    }
    // right column: the trim fader, beside them
    trimLabel_.setBounds(x + leftW + 6, fy, faderW, 12);
    trim_.setBounds(x + leftW + 6, fy + 14, faderW, bottom - fy - 14);
}

/* ---------------------------------------------------------- MixerRow */

MixerRow::MixerRow(ChipBoyProcessor& p) : master_(p)
{
    for (int ch = 0; ch < 4; ++ch) {
        strips_[size_t(ch)] = std::make_unique<ChannelStrip>(p, ch);
        strips_[size_t(ch)]->onSelect = [this](int c) { if (onSelect) onSelect(c); };
        strips_[size_t(ch)]->onOpenSlot = [this](SlotKind kind, int slot) { if (onOpenSlot) onOpenSlot(kind, slot); };
        addAndMakeVisible(*strips_[size_t(ch)]);
    }
    addAndMakeVisible(master_);
    strips_[0]->setSelected(true);
}

MixerRow::~MixerRow() = default;

void MixerRow::setSelected(int ch)
{
    selected_ = std::clamp(ch, 0, 3);
    for (int i = 0; i < 4; ++i) strips_[size_t(i)]->setSelected(i == selected_);
}

void MixerRow::tick()
{
    for (auto& s : strips_) s->tick();
    master_.tick();
}

void MixerRow::bankChanged() { for (auto& s : strips_) s->bankChanged(); }
void MixerRow::hexChanged() { for (auto& s : strips_) s->hexChanged(); master_.repaint(); }
void MixerRow::setScopeSettings(ScopeView::Trace trace, int periods) { for (auto& s : strips_) s->setScopeSettings(trace, periods); }
void MixerRow::setAnalogCornerHz(double hz) { for (auto& s : strips_) s->setAnalogCornerHz(hz); }

void MixerRow::paint(Graphics& g)
{
    g.fillAll(colours::panel);
    g.setColour(colours::lineSoft);
    g.fillRect(0, getHeight() - 1, getWidth(), 1);
}

void MixerRow::resized()
{
    // grid-template-columns: repeat(4, minmax(0, 1fr)) 224px; gap 8; padding 10 12 12
    const int gap = 8, left = 12, top = 10;
    const int stripW = (getWidth() - 2 * left - MasterStrip::kWidth - 4 * gap) / 4;
    int x = left;
    for (auto& s : strips_) { s->setBounds(x, top, stripW, ChannelStrip::kHeight); x += stripW + gap; }
    master_.setBounds(x, top, MasterStrip::kWidth, ChannelStrip::kHeight);
}

} // namespace chipboy::plugin
