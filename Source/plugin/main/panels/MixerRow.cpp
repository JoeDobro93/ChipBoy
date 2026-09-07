#include "plugin/main/panels/MixerRow.h"

#include <cmath>

namespace chipboy::plugin {

using namespace juce;
using namespace chipboy::ui;

namespace {
constexpr int kPad = 8, kGap = 6;
constexpr int kHead = 20, kScope = 78, kRegs = 14, kRow = 24, kQuick = 70;
const char* kMuteTip = "NR51 gate off. Pops like the hardware.";
const char* kSoloTip = "Gates the other three off (NR51). Pops like the hardware.";
const char* kPanTip = "NR51: off, left, both, right, or the instrument's own. There is no pan law.";
const char* kTableTip = "Table override for this channel; inst = the instrument's own table";
const char* kInstTip = "The instrument this channel's notes latch at note-on; automate it to switch per note";
}

/* ------------------------------------------------------ ChannelStrip */

ChannelStrip::ChannelStrip(ChipBoyProcessor& p, int ch)
    : processor_(p), ch_(ch),
      name_(colours::channelName(ch), Fonts::pixel(12.0f), colours::channel(ch)),
      sourceBox_(source_),
      instrumentName_({}, Fonts::sans(12.0f), colours::textMute),
      mute_("M"), solo_("S"),
      pan_({ String(CharPointer_UTF8("\xe2\x80\x93")), "L", "LR", "R", "inst" })
{
    led_.setColour(colours::channel(ch));
    led_.setInterceptsMouseClicks(false, false);
    for (auto* c : std::initializer_list<Component*>{ &led_, &name_, &sourceBox_, &scope_, &regs_, &instrument_, &instrumentName_, &mute_, &solo_, &table_, &pan_ })
        addAndMakeVisible(c);

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

    // instrument and table
    instrument_.setTooltip(kInstTip);
    instrument_.attach(param(processor_, channelParamId(ch_, ids::instrument)));
    table_.setTooltip(kTableTip);
    table_.attach(param(processor_, channelParamId(ch_, ids::table)));

    // quick controls by channel type
    const bool wave = ch_ == 2;
    if (!wave) {
        const String nr = ch_ == 3 ? "NR42" : ch_ == 0 ? "NR12" : "NR22";
        level_ = std::make_unique<Knob>("Level");
        level_->setTooltip(nr + " bits 7-4: envelope start volume, 16 levels; inst = the instrument's");
        level_->attach(param(processor_, channelParamId(ch_, ids::level)));
        level_->setAccent(colours::channel(ch_));
        envRate_ = std::make_unique<Knob>("Env");
        envRate_->setTooltip(nr + " bits 2-0: envelope rate, 0 = off, 1-7 = n x 15.6 ms per step; inst = the instrument's");
        envRate_->attach(param(processor_, channelParamId(ch_, ids::envRate)));
        envRate_->setAccent(colours::channel(ch_));
        addAndMakeVisible(*level_);
        addAndMakeVisible(*envRate_);
    } else {
        waveLevelLabel_.reset(TextLine::label("Level"));
        frameLabel_.reset(TextLine::label("Frame"));
        waveLevel_ = std::make_unique<Segmented>(StringArray{ "mute", "25", "50", "100", "inst" });
        waveLevel_->setMini(true);
        waveLevel_->setTooltip("NR32: two bits, no envelope on this channel; inst = the instrument's");
        waveLevel_->attach(param(processor_, channelParamId(ch_, ids::level)));
        frame_ = std::make_unique<Stepper>();
        frame_->setTooltip("Which of the wave's frames is loaded; auto follows the instrument. On a DMG a change costs a click.");
        frame_->attach(param(processor_, channelParamId(ch_, ids::frame)));
        addAndMakeVisible(*waveLevelLabel_);
        addAndMakeVisible(*frameLabel_);
        addAndMakeVisible(*waveLevel_);
        addAndMakeVisible(*frame_);
    }

    // mute / solo: NR51 gates
    mute_.setTooltip(kMuteTip);
    mute_.setClickingTogglesState(true);
    mute_.onClick = [this] { processor_.setChannelMute(ch_, mute_.getToggleState()); };
    solo_.setTooltip(kSoloTip);
    solo_.setClickingTogglesState(true);
    solo_.onClick = [this] { processor_.setChannelSolo(ch_, solo_.getToggleState()); };

    // pan: the parameter's order is off, L, R, both, inst; the display's is the hardware's
    pan_.setMini(true);
    pan_.setTooltip(kPanTip);
    panParam_ = std::make_unique<SegmentedParam>(pan_, param(processor_, channelParamId(ch_, ids::pan)), std::vector<int>{ 0, 1, 3, 2, 4 });

    refreshInstrumentName();
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
    m.addItem(18, "Off (Phrases lane and tables only)", true, sourceValue_ == 17);
    Component::SafePointer<ChannelStrip> safe(this);
    m.showMenuAsync(PopupMenu::Options().withTargetComponent(&sourceBox_), [safe](int r) {
        if (safe == nullptr || r < 1 || r > 18) return;
        safe->sourceAtt_->setValueAsCompleteGesture(float(r - 1));
    });
}

void ChannelStrip::refreshInstrumentName()
{
    const auto b = processor_.bank();
    const int slot = paramValue(processor_, channelParamId(ch_, ids::instrument));
    if (b.get() == namesFor_ && slot == instrumentShown_) return;
    namesFor_ = b.get();
    instrumentShown_ = slot;
    if (slot <= 0) { instrumentName_.setText("no instrument"); instrumentName_.setColour(colours::textDim); return; }
    const bank::Instrument* inst = b ? b->instrument(slot) : nullptr;
    if (!inst) { instrumentName_.setText(String(CharPointer_UTF8("\xe2\x80\x94 empty \xe2\x80\x94"))); instrumentName_.setColour(colours::textDim); return; }
    instrumentName_.setText(String(inst->name));
    instrumentName_.setColour(instrumentFitsChannel(inst->type, ch_) ? instrumentKindColour(int(inst->type)) : colours::warn);
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
}

void ChannelStrip::bankChanged() { refreshInstrumentName(); }

void ChannelStrip::hexChanged()
{
    namesFor_ = nullptr;
    refreshInstrumentName();
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
    y += kHead + kGap;
    scope_.setBounds(x, y, w, kScope);
    y += kScope + kGap;
    regs_.setBounds(x, y, w, kRegs);
    y += kRegs + kGap;
    instrument_.setBounds(x, y, instrument_.preferredWidth(), kRow);
    instrumentName_.setBounds(x + instrument_.preferredWidth() + 6, y, w - instrument_.preferredWidth() - 6, kRow);
    y += kRow + kGap;
    // quick controls, with mute / solo stacked at the right edge
    mute_.setBounds(x + w - 24, y + 2, 24, 22);
    solo_.setBounds(x + w - 24, y + 30, 24, 22);
    if (level_ && envRate_) {
        level_->setBounds(x, y, Knob::kWidth, Knob::kHeight);
        envRate_->setBounds(x + Knob::kWidth + 6, y, Knob::kWidth, Knob::kHeight);
    }
    if (waveLevel_ && frame_) {
        waveLevelLabel_->setBounds(x, y, 80, 12);
        waveLevel_->setBounds(x, y + 12, std::min(waveLevel_->preferredWidth(), w - 32), waveLevel_->preferredHeight());
        frameLabel_->setBounds(x, y + 40, 44, kRow);
        frame_->setBounds(x + 46, y + 40, frame_->preferredWidth(), kRow);
    }
    y += kQuick + kGap;
    table_.setBounds(x, y, table_.preferredWidth(), kRow);
    const int panW = std::min(pan_.preferredWidth(), w - table_.preferredWidth() - 6);
    pan_.setBounds(x + table_.preferredWidth() + 6, y + (kRow - pan_.preferredHeight()) / 2, panW, pan_.preferredHeight());
}

/* ------------------------------------------------------- MasterStrip */

MasterStrip::MasterStrip(ChipBoyProcessor& p)
    : processor_(p),
      name_("MASTER", Fonts::pixel(12.0f), colours::lcdTrace),
      modelBox_(model_),
      volLLabel_("Vol L", Fonts::caption(10.0f), colours::textDim), volRLabel_("Vol R", Fonts::caption(10.0f), colours::textDim),
      trimLabel_("Trim", Fonts::caption(10.0f), colours::textDim, Justification::centred),
      noise_("Headphone Noise"), declick_("De-click")
{
    volLLabel_.setUpperCase(true); volRLabel_.setUpperCase(true); trimLabel_.setUpperCase(true);
    for (auto* c : std::initializer_list<Component*>{ &name_, &modelBox_, &scope_, &volLLabel_, &volRLabel_, &trimLabel_, &volL_, &volR_, &noise_, &declick_, &trim_ })
        addAndMakeVisible(c);
    modelBox_.setTooltip("Which real machine is emulated. RAW is the clean digital mix of a gaming emulator.");
    scope_.setSource(&processor_.scopes().master);
    scope_.setWindowMs(12.0);
    scope_.setLcdGround(true);
    volL_.setTooltip("NR50 bits 6-4: left master volume, 0 = 1/8, not mute");
    volR_.setTooltip("NR50 bits 2-0: right master volume, 0 = 1/8, not mute");
    volL_.attach(param(processor_, ids::masterL));
    volR_.attach(param(processor_, ids::masterR));
    noise_.setTooltip("The original switch. Hiss, the LCD line and the frame hum, together, as measured.");
    noise_.attach(param(processor_, ids::noise));
    declick_.setTooltip("Crossfades each DAC-on step over a few milliseconds. Not what a Game Boy does: the header reads MODIFIED while it is on. Also in the Hardware tab.");
    declick_.attach(param(processor_, ids::declick));
    trim_.setTooltip("The one continuous control in the product: a fader after the analog stage, outside the chip");
    trim_.attach(param(processor_, ids::trim));
}

MasterStrip::~MasterStrip() = default;

void MasterStrip::tick()
{
    const int m = modelIndex(processor_);
    if (m != lastModel_) {
        lastModel_ = m;
        model_.set(modelName(m), Pill::Tone::Neutral);
        resized();
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
    int y = kPad;
    name_.setBounds(x, y, 80, kHead);
    const int pw = std::min(model_.preferredWidth(), 60);
    modelBox_.setBounds(x + w - pw, y + 1, pw, 18);
    y += kHead + kGap;
    scope_.setBounds(x, y, w, kScope);
    y += kScope + kGap;
    const int faderW = 54;
    const int leftW = w - faderW - 6;
    const int bottom = getHeight() - kPad;
    // left column: master volumes and the two switches
    volLLabel_.setBounds(x, y, 40, kRow);
    volL_.setBounds(x + 42, y, volL_.preferredWidth(), kRow);
    y += kRow + kGap;
    volRLabel_.setBounds(x, y, 40, kRow);
    volR_.setBounds(x + 42, y, volR_.preferredWidth(), kRow);
    y += kRow + kGap;
    noise_.setBounds(x, y, leftW, Toggle::kHeight);
    y += Toggle::kHeight + kGap;
    declick_.setBounds(x, y, leftW, Toggle::kHeight);
    // right column: the trim fader
    const int fy = kPad + kHead + kGap + kScope + kGap;
    trimLabel_.setBounds(x + leftW + 6, fy, faderW, 12);
    trim_.setBounds(x + leftW + 6, fy + 14, faderW, bottom - fy - 14);
}

/* ---------------------------------------------------------- MixerRow */

MixerRow::MixerRow(ChipBoyProcessor& p) : master_(p)
{
    for (int ch = 0; ch < 4; ++ch) {
        strips_[size_t(ch)] = std::make_unique<ChannelStrip>(p, ch);
        strips_[size_t(ch)]->onSelect = [this](int c) { if (onSelect) onSelect(c); };
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
