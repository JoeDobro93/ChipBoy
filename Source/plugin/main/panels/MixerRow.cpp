#include "plugin/main/panels/MixerRow.h"

#include <cmath>

namespace chipboy::plugin {

using namespace juce;
using namespace chipboy::ui;

namespace {
constexpr int kPad = 8, kGap = 6;
constexpr int kHead = 20, kScope = 66, kRegs = 14, kRow = 24, kQuick = 70, kSeg = 22, kCaption = 10, kState = 14;
constexpr int kButton = 26, kButtonH = 20;
const char* kMuteTip = "NR51 gate off. Pops like the hardware.";
const char* kSoloTip = "Gates the other three off (NR51). Pops like the hardware.";
const char* kPanTip = "NR51: off, left, both, right, or the instrument's own. There is no pan law.";
const char* kTableTip = "Table override for this channel; inst = the instrument's own table";
const char* kInstTip = "The instrument this channel's notes latch at note-on; automate it to switch per note";
const char* kTransposeTip = "Semitones added to every note on this channel, before the period is worked out";
const char* kStateTip = "What the driver is doing right now: the instrument, its table and the two command slots, resolved.";
/// The reserved octave is one below the channel's playable floor.
juce::String keyswitchTip(int ch)
{
    const int base = ch < 2 ? 24 : 12;
    return "Keyswitches: notes " + juce::String(base) + juce::String(CharPointer_UTF8("\xe2\x80\x93")) + juce::String(base + 11)
         + " select instrument slots 1" + juce::String(CharPointer_UTF8("\xe2\x80\x93")) + "12 and never sound";
}

const String kDot = String(CharPointer_UTF8(" \xc2\xb7 "));

/// "6/6": the groove in force on this channel, resolved through the song --
/// a G slot, else the phrase's own, else straight.
String grooveText(const tracker::Song* s, int ch, int bar, uint8_t slot)
{
    if (slot == tracker::kGrooveNone) {
        const tracker::Phrase* p = s != nullptr ? s->phrase(s->phraseAt(ch, bar)) : nullptr;
        slot = p != nullptr ? p->groove : 0;
    }
    if (s != nullptr && slot >= 1 && slot <= 16) {
        const auto g = s->grooves[size_t(slot - 1)];
        return String(int(g.a)) + "/" + String(int(g.b));
    }
    return "6/6";
}

/// The line under the two slots: what the driver has in force, in the
/// fields that mean something on this channel (section 3).
String stateText(int ch, const driver::VoiceView& v, const tracker::Song* song, int bar)
{
    const bool pulse = ch == 0 || ch == 1, wave = ch == 2;
    String s;
    auto add = [&s](const String& t) { if (s.isNotEmpty()) s += kDot; s += t; };
    if (pulse) { static const char* duty[] = { "12.5%", "25%", "50%", "75%" }; add("duty " + String(duty[v.duty & 3])); }
    else if (wave) { static const char* lvl[] = { "mute", "25%", "50%", "100%" }; add("lvl " + String(lvl[v.volume & 3])); add("frm " + String(v.frame)); }
    if (!wave) add("env " + String(v.envVol) + String::charToString(v.envDir ? 0x2191 : 0x2193) + String(v.envRate));
    if (pulse || wave) {
        if (v.vibDepth) add("vib " + String(v.vibSpeed) + "/" + String(v.vibDepth));
        if (v.pitchOffset) add("P " + String(v.pitchOffset > 0 ? "+" : "") + String(v.pitchOffset));
    }
    { static const char* pan[] = { "off", "L", "R", "LR" }; add(String(pan[v.pan & 3])); }
    if (v.tableSlot) add("tbl " + ValueFormat::number(v.tableSlot) + ":" + ValueFormat::number(v.tableStep + 1));
    add("grv " + grooveText(song, ch, bar, v.groove));
    return s;
}
}

/* ------------------------------------------------------ ChannelStrip */

ChannelStrip::ChannelStrip(ChipBoyProcessor& p, int ch)
    : processor_(p), ch_(ch),
      name_(colours::channelName(ch), Fonts::pixel(12.0f), colours::channel(ch)),
      sourceBox_(source_),
      instrumentName_({}, Fonts::sans(12.0f), colours::textMute),
      tableLabel_("Table", Fonts::caption(10.0f), colours::textDim),
      transposeLabel_("Transpose", Fonts::caption(10.0f), colours::textDim),
      pan_({ String(CharPointer_UTF8("\xe2\x80\x93")), "L", "LR", "R", "inst" }),
      mute_("M"), solo_("S"), keyswitch_("KS"),
      cmd1_("CMD1", ChipBoyProcessor::kindOf(ch)), cmd2_("CMD2", ChipBoyProcessor::kindOf(ch)),
      state_({}, Fonts::mono(10.0f), colours::textDim), stateBox_(state_)
{
    led_.setColour(colours::channel(ch));
    led_.setInterceptsMouseClicks(false, false);
    tableLabel_.setUpperCase(true);
    transposeLabel_.setUpperCase(true);
    for (auto* c : std::initializer_list<Component*>{ &led_, &name_, &sourceBox_, &scope_, &regs_, &instrument_, &instrumentName_,
                                                     &tableLabel_, &table_, &transposeLabel_, &transpose_, &pan_,
                                                     &mute_, &solo_, &keyswitch_, &cmd1_, &cmd2_, &stateBox_ })
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

    // the tracker row: instrument, table, transpose, then the two slots
    instrument_.setTooltip(kInstTip);
    instrument_.attach(param(processor_, channelParamId(ch_, ids::instrument)));
    table_.setTooltip(kTableTip);
    table_.attach(param(processor_, channelParamId(ch_, ids::table)));
    transpose_.setTooltip(kTransposeTip);
    transpose_.attach(param(processor_, channelParamId(ch_, ids::transpose)));
    transpose_.setTextFunction([](int v) { return (v > 0 ? "+" : "") + String(v); });
    cmd1_.attach(param(processor_, channelParamId(ch_, ids::cmd1Type)), param(processor_, channelParamId(ch_, ids::cmd1X)), param(processor_, channelParamId(ch_, ids::cmd1Y)));
    cmd2_.attach(param(processor_, channelParamId(ch_, ids::cmd2Type)), param(processor_, channelParamId(ch_, ids::cmd2X)), param(processor_, channelParamId(ch_, ids::cmd2Y)));
    stateBox_.setTooltip(kStateTip);

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
    keyswitchAtt_ = std::make_unique<ButtonParameterAttachment>(param(processor_, channelParamId(ch_, ids::keyswitch)), keyswitch_);

    // pan: the parameter's order is off, L, R, both, inst; the display's is the hardware's
    pan_.setMini(true);
    pan_.setTooltip(kPanTip);
    panParam_ = std::make_unique<SegmentedParam>(pan_, param(processor_, channelParamId(ch_, ids::pan)), std::vector<int>{ 0, 1, 3, 2, 4 });

    refreshInstrumentName();
    refreshState();
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

/// The running state the driver publishes next to the registers: rebuilt
/// only when the packed word (or the groove it resolves through) changes, so
/// the timer costs a comparison in the common case.
void ChannelStrip::refreshState()
{
    const uint64_t packed = processor_.scopes().state2[size_t(ch_)].load(std::memory_order_acquire);
    const auto song = processor_.song();
    const int barTicks = std::max(1, processor_.barTicks());
    const int bar = int(std::max<int64_t>(0, processor_.trackerTick()) / barTicks);
    const int width = stateBox_.getWidth();
    if (packed == stateShown_ && bar == stateBar_ && width == stateWidth_) return;
    stateShown_ = packed;
    stateBar_ = bar;
    stateWidth_ = width;
    driver::VoiceView v;
    link::unpackState(processor_.scopes().state[size_t(ch_)].load(std::memory_order_acquire), v);
    link::unpackState2(packed, v);
    const String text = stateText(ch_, v, song.get(), bar);
    // Six fields do not fit a strip at 10 px, so the face shrinks rather
    // than the line being cut -- the register line above does the same.
    const float avail = float(std::max(1, width));
    float px = 10.0f;
    const float wide = draw::textWidth(Fonts::mono(px), text);
    if (wide > avail) px = std::max(7.5f, px * avail / wide);
    state_.setFont(Fonts::mono(px));
    state_.setText(text);
}

void ChannelStrip::tick()
{
    led_.setOn(processor_.channelLevels[size_t(ch_)].load() >= 0);
    regs_.setState(processor_.scopes().state[size_t(ch_)].load(std::memory_order_acquire));
    refreshState();

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
    stateShown_ = ~uint64_t(0);
    refreshInstrumentName();
    refreshState();
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
    y += kSeg + kGap;
    cmd1_.setBounds(x, y, w, ui::CommandSlot::kHeight);
    y += ui::CommandSlot::kHeight + 4;
    cmd2_.setBounds(x, y, w, ui::CommandSlot::kHeight);
    y += ui::CommandSlot::kHeight + 4;
    stateBox_.setBounds(x, y, w, kState);
}

/* ------------------------------------------------------- MasterStrip */

MasterStrip::MasterStrip(ChipBoyProcessor& p)
    : processor_(p),
      name_("MASTER", Fonts::pixel(12.0f), colours::lcdTrace),
      modelBox_(model_),
      volLLabel_("Vol L", Fonts::caption(10.0f), colours::textDim), volRLabel_("Vol R", Fonts::caption(10.0f), colours::textDim),
      trimLabel_("Trim", Fonts::caption(10.0f), colours::textDim, Justification::centred),
      mix_({}, Fonts::mono(10.5f), colours::textMute), mixBox_(mix_),
      noise_("Headphone Noise"), declick_("De-click")
{
    volLLabel_.setUpperCase(true); volRLabel_.setUpperCase(true); trimLabel_.setUpperCase(true);
    for (auto* c : std::initializer_list<Component*>{ &name_, &modelBox_, &scope_, &mixBox_, &volLLabel_, &volRLabel_, &trimLabel_, &volL_, &volR_, &noise_, &declick_, &trim_ })
        addAndMakeVisible(c);
    mixBox_.setTooltip("The two mixer registers: NR50 is the master volume per side, NR51 the four gates. Mute and solo write NR51.");
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
    const int controls = kRegs + kGap + 2 * (kRow + kGap) + 2 * (Toggle::kHeight + kGap) - kGap;
    scope_.setBounds(x, y, w, std::max(kScope, bottom - y - kGap - controls));
    y = scope_.getBottom() + kGap;
    mixBox_.setBounds(x, y, w, kRegs);
    y += kRegs + kGap;
    const int faderW = 54;
    const int leftW = w - faderW - 6;
    // left column: master volumes and the two switches
    const int fy = y;
    volLLabel_.setBounds(x, y, 40, kRow);
    volL_.setBounds(x + 42, y, volL_.preferredWidth(), kRow);
    y += kRow + kGap;
    volRLabel_.setBounds(x, y, 40, kRow);
    volR_.setBounds(x + 42, y, volR_.preferredWidth(), kRow);
    y += kRow + kGap;
    noise_.setBounds(x, y, leftW, Toggle::kHeight);
    y += Toggle::kHeight + kGap;
    declick_.setBounds(x, y, leftW, Toggle::kHeight);
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
