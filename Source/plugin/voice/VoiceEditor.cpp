#include "plugin/voice/VoiceEditor.h"

#include <algorithm>
#include <cmath>
#include <functional>

namespace chipboy::plugin {

using namespace juce;
using namespace chipboy::ui;

namespace {

// docs/mockups/chipboy_mockup.html, .win.voice
constexpr int kHeaderHeight = 46;      // .voice .bar
constexpr int kFooterHeight = 27;      // .status: 11 px mono, 6 px padding, 1 px rule
constexpr int kPad = 12;               // .voice .body padding
constexpr int kGap = 12;               // .voice .body gap
constexpr int kNoteHeight = 31;        // .audio-note: 12 px text, 6 px padding, 1 px border
constexpr int kScopeHeight = 96;
constexpr int kRegsHeight = 14;
constexpr int kLabelHeight = 12;       // .label: 10 px, upper case, tracked
constexpr int kControlHeight = 24;     // .sel
constexpr int kButtonHeight = 22;      // .btn.small
constexpr int kGridGapX = 6;           // .voice .params gap: 10px 6px
constexpr int kGridGapY = 10;
constexpr float kOffAlpha = 0.45f;     // .toggle-row.off

const Colour kBody = colours::bg;

String dot() { return String(CharPointer_UTF8(" \xc2\xb7 ")); }     // " · "
String arrow() { return String(CharPointer_UTF8("\xe2\x86\x92 ")); } // "→ "

Font labelFont() { return Fonts::caption(10.0f); }

void drawLabel(Graphics& g, const String& text, Rectangle<int> area) { draw::caption(g, text.toUpperCase(), area); }

/// The coupling corner of the linked instance's model: DMG's 6.44 ms and
/// CGB's 0.471 ms (docs/HARDWARE_REFERENCE.md); RAW has no capacitor.
double cornerHzForModel(int model) { return model == 0 ? 25.0 : model == 1 ? 338.0 : 0.0; }

Pill::Tone toneFor(LinkClient::Status s)
{
    using S = LinkClient::Status;
    switch (s) {
        case S::Linked: return Pill::Tone::Ok;
        case S::Busy:
        case S::Disconnected:
        case S::Incompatible: return Pill::Tone::Bad;
        case S::Waiting:
        case S::LinkOff:
        case S::Unlinked: break;
    }
    return Pill::Tone::Warn;
}

} // namespace

// --- the params grid ----------------------------------------------------------

/// A control with the mockup's .label above it.
struct VoiceEditor::Field : public Component {
    Field(const String& label, std::unique_ptr<Component> control, int controlHeight, std::function<int()> controlWidth)
        : label_(label), control_(std::move(control)), controlHeight_(controlHeight), controlWidth_(std::move(controlWidth))
    {
        addAndMakeVisible(*control_);
    }
    int preferredWidth() const
    {
        return std::max(controlWidth_(), GlyphArrangement::getStringWidthInt(labelFont(), label_.toUpperCase()) + 2);
    }
    int preferredHeight() const { return kLabelHeight + 4 + controlHeight_; }
    void paint(Graphics& g) override { drawLabel(g, label_, getLocalBounds().withHeight(kLabelHeight)); }
    void resized() override { control_->setBounds(0, kLabelHeight + 4, getWidth(), controlHeight_); }

    String label_;
    std::unique_ptr<Component> control_;
    int controlHeight_;
    std::function<int()> controlWidth_;
};

/// Items flow left to right and wrap, as the mockup's grid does; a row is as
/// tall as its tallest item. The groups remember which controls only apply
/// to some channel types, so the editor can grey them.
struct VoiceEditor::ParamsGrid : public Component {
    struct Item { std::unique_ptr<Component> comp; std::function<int()> width; int height; };
    std::vector<Item> items;
    std::vector<Component*> waveOnly, pulseOnly, pu1Only, noiseOnly, envelope;

    Component* add(std::unique_ptr<Component> c, std::function<int()> width, int height)
    {
        Component* raw = c.get();
        addAndMakeVisible(*raw);
        items.push_back({ std::move(c), std::move(width), height });
        return raw;
    }
    /// Places the children for `width` and returns the height they need.
    int layoutFor(int width)
    {
        int x = 0, y = 0, rowHeight = 0;
        for (auto& it : items) {
            const int w = it.width();
            if (x > 0 && x + w > width) { x = 0; y += rowHeight + kGridGapY; rowHeight = 0; }
            it.comp->setBounds(x, y, w, it.height);
            x += w + kGridGapX;
            rowHeight = std::max(rowHeight, it.height);
        }
        return y + rowHeight;
    }
    void resized() override { layoutFor(getWidth()); }
};

// --- the editor ---------------------------------------------------------------

VoiceEditor::VoiceEditor(VoiceProcessor& p)
    : AudioProcessorEditor(p), processor_(p), tooltips_(this, 700)
{
    setLookAndFeel(&lnf_);

    // header: status, instance, channel
    addAndMakeVisible(statusPill_);
    addAndMakeVisible(instanceBox_);
    instanceBox_.setTextWhenNothingSelected("no ChipBoy");
    instanceBox_.setTextWhenNoChoicesAvailable("no ChipBoy running");
    instanceBox_.setTooltip("The ChipBoy instance this track plays through");
    instanceBox_.onChange = [this] {
        const int i = instanceBox_.getSelectedId() - 1;
        if (i >= 0 && i < int(instanceUuids_.size())) processor_.setTarget(instanceUuids_[size_t(i)], processor_.channel());
        timerCallback();
    };
    addAndMakeVisible(channelBox_);
    channelBox_.setTooltip("The hardware channel this track drives");
    channelBox_.onChange = [this] {
        const int ch = channelBox_.getSelectedId() - 1;
        if (ch >= 0 && ch < 4) processor_.setTarget(processor_.link().targetUuid(), ch);
        timerCallback();
    };

    // left column: the channel's scope and registers, from the return path
    addAndMakeVisible(scope_);
    scope_.setLcdGround(true);
    scope_.setChrome(false);
    scope_.setPeriods(2);
    addAndMakeVisible(regs_);

    // right column: the instrument, where it comes from, and the bank exchange
    addAndMakeVisible(instrumentBox_);
    instrumentBox_.setTextWhenNothingSelected("none");
    instrumentBox_.setTooltip("The bank slot this channel plays: the Instrument parameter");
    instrumentAttachment_ = std::make_unique<ParameterAttachment>(param(ids::instrument), [this](float v) {
        instrumentBox_.setSelectedId(int(std::lround(v)) + 1, dontSendNotification);
    });
    instrumentBox_.onChange = [this] {
        const int id = instrumentBox_.getSelectedId();
        if (id >= 1) instrumentAttachment_->setValueAsCompleteGesture(float(id - 1));
    };
    refreshInstruments(true);
    instrumentAttachment_->sendInitialUpdate();

    addAndMakeVisible(sourceSeg_);
    sourceSeg_.setOptions({ "Linked", "Local" });
    sourceSeg_.setMini(true);
    sourceSeg_.setOptionTooltip(0, "The bank slot in the linked instance. Editing it changes every channel that uses it.");
    sourceSeg_.setOptionTooltip(1, "A private copy in this track's state, pushed to the instance on connect.");
    sourceSeg_.attach(*processor_.apvts.getParameter("inst_source"));

    auto button = [this](TextButton& b, const String& text, const String& tip, std::function<void()> fn) {
        addAndMakeVisible(b);
        b.setButtonText(text);
        b.setTooltip(tip);
        b.onClick = std::move(fn);
    };
    button(pushButton_, "Push", "Copy the local instrument into the bank slot", [this] {
        if (const int slot = instrumentSlot()) processor_.pushToSlot(slot); else hint("choose an instrument slot first");
        refreshStatus();
    });
    button(pullButton_, "Pull", "Copy the bank slot into the local instrument", [this] {
        if (const int slot = instrumentSlot()) processor_.pullFromSlot(slot); else hint("choose an instrument slot first");
        refreshStatus();
    });
    button(openButton_, "Open editor " + String(CharPointer_UTF8("\xe2\x86\x97")), "Show this channel's instrument in the ChipBoy window",
           [this] { processor_.requestFocus(); });
    openButton_.setColour(TextButton::buttonColourId, colours::accent);      // .btn.primary
    openButton_.setColour(TextButton::buttonOnColourId, colours::accentHi);
    openButton_.setColour(TextButton::textColourOffId, Colours::white);
    openButton_.setColour(TextButton::textColourOnId, Colours::white);

    // the params grid: more than fits at 560 x 420, so it scrolls
    buildParams();
    addAndMakeVisible(paramsViewport_);
    paramsViewport_.setViewedComponent(params_.get(), false);
    paramsViewport_.setScrollBarsShown(true, false);
    paramsViewport_.setScrollBarThickness(8);

    setResizable(false, false);
    setSize(kVoiceWidth, kVoiceHeight);
    timerCallback();
    startTimerHz(10);
}

VoiceEditor::~VoiceEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

RangedAudioParameter& VoiceEditor::param(const char* id) const
{
    auto* p = processor_.apvts.getParameter(String("v_") + id);
    jassert(p != nullptr);
    return *p;
}

int VoiceEditor::instrumentSlot() const
{
    const auto& p = param(ids::instrument);
    return int(std::lround(p.convertFrom0to1(p.getValue())));
}

const LinkClient::InstanceInfo* VoiceEditor::targetInfo() const
{
    const String target = processor_.link().targetUuid();
    for (const auto& i : instances_) if (i.uuid == target) return &i;
    return nullptr;
}

String VoiceEditor::instanceName() const
{
    const String n = processor_.link().targetName();
    if (n.isNotEmpty()) return n;
    if (const auto* i = targetInfo()) if (i->name.isNotEmpty()) return i->name;
    return "ChipBoy";
}

void VoiceEditor::hint(const String& message)
{
    editorMessage_ = message;
    lastRequest_ = processor_.lastRequestMessage();
}

void VoiceEditor::buildParams()
{
    params_ = std::make_unique<ParamsGrid>();
    auto& grid = *params_;

    auto knob = [&](const char* id, const String& label, const String& tip = {}) -> Component* {
        auto k = std::make_unique<Knob>(label);
        k->attach(param(id));
        if (tip.isNotEmpty()) k->setTooltip(tip);
        return grid.add(std::move(k), [] { return Knob::kWidth; }, Knob::kHeight);
    };
    auto field = [&](const String& label, std::unique_ptr<Component> control, int height, std::function<int()> width) -> Component* {
        auto f = std::make_unique<Field>(label, std::move(control), height, std::move(width));
        Field* raw = f.get();
        return grid.add(std::move(f), [raw] { return raw->preferredWidth(); }, raw->preferredHeight());
    };
    auto seg = [&](const char* id, const String& label, const StringArray& options) -> Component* {
        auto s = std::make_unique<Segmented>(options);
        s->setMini(true);
        s->attach(param(id));
        Segmented* raw = s.get();
        return field(label, std::move(s), raw->preferredHeight(), [raw] { return raw->preferredWidth(); });
    };
    auto toggle = [&](const char* id, const String& label, const String& tip = {}) -> Component* {
        auto t = std::make_unique<Toggle>("on");
        t->attach(param(id));
        if (tip.isNotEmpty()) t->setTooltip(tip);
        return field(label, std::move(t), Toggle::kHeight, [] { return 84; });
    };

    // the mockup's knobs first, then what the other channel types add
    knob(ids::level, "Level", "Automation lane: Level (0-15, or the instrument's)");
    grid.envelope.push_back(knob(ids::envVol, "Env vol", "Envelope start volume, or the instrument's"));
    grid.envelope.push_back(knob(ids::envRate, "Env rate", "Automation lane: Envelope rate"));
    knob(ids::transpose, "Transpose", "Semitones, applied to the period");
    knob(ids::detune, "Detune", "Raw period units");
    knob(ids::vibSpeed, "Vib speed", "Ticks per vibrato step, or the instrument's");
    knob(ids::vibDepth, "Vib depth", "Period units");
    knob(ids::table, "Table", "0 = the instrument's own");
    knob(ids::arp, "Arp", "A table slot used as an arpeggio");
    grid.waveOnly.push_back(knob(ids::wave, "Wave", "Wave slot (WAV), 0 = the instrument's"));
    grid.waveOnly.push_back(knob(ids::frame, "Frame", "Hold one frame (WAV), 0 = automatic"));
    grid.pu1Only.push_back(knob(ids::sweepRate, "Sweep rate", "PU1 only"));
    grid.pu1Only.push_back(knob(ids::sweepShift, "Sweep shift", "PU1 only"));

    grid.pulseOnly.push_back(seg(ids::duty, "Duty", { "12", "25", "50", "75", "inst" }));
    StringArray pan;
    pan.add(String(CharPointer_UTF8("\xe2\x80\x93")));   // the mockup shows "off" as a dash
    pan.addArray({ "L", "R", "LR", "inst" });
    seg(ids::pan, "Pan", pan);
    grid.envelope.push_back(seg(ids::envDir, "Env dir", { "down", "up", "inst" }));
    grid.pu1Only.push_back(seg(ids::sweepDir, "Sweep dir", { "up", "down", "inst" }));
    grid.noiseOnly.push_back(seg(ids::lfsr, "LFSR", { "15-bit", "7-bit", "inst" }));
    {
        auto c = std::make_unique<ComboBox>();
        StringArray modes;
        modes.add(arrow() + "start volume");
        modes.add(arrow() + "instrument bank");
        modes.add("ignored");
        c->addItemList(modes, 1);
        velocityAttachment_ = std::make_unique<ComboBoxParameterAttachment>(param(ids::velocityMode), *c);
        field("Velocity", std::move(c), kControlHeight, [] { return 150; });
    }
    toggle(ids::liveFollow, "Live follow", "Parameters apply to the sounding note instead of waiting for the next one");
    toggle(ids::keyswitch, "Keyswitches", "Notes below the playing range pick an instrument instead of sounding");
}

// --- refresh, 10 Hz ----------------------------------------------------------

void VoiceEditor::timerCallback()
{
    instances_ = processor_.link().instances();
    refreshInstances();
    refreshChannels();
    refreshInstruments(false);
    applyChannelKind();
    refreshScope();
    refreshStatus();
    refreshNote();
}

void VoiceEditor::refreshInstances()
{
    auto& client = processor_.link();
    const String target = client.targetUuid();

    String signature;
    for (const auto& i : instances_) signature << i.uuid << '|' << i.name << '|' << (i.alive ? '1' : '0') << ';';
    signature << '#' << target << '|' << client.targetName();
    if (signature == instanceSignature_ || instanceBox_.isPopupActive()) return;
    instanceSignature_ = signature;

    instanceBox_.clear(dontSendNotification);
    instanceUuids_.clear();
    auto* menu = instanceBox_.getRootMenu();
    int selected = 0;
    bool haveTarget = target.isEmpty();
    for (const auto& i : instances_) {
        instanceUuids_.push_back(i.uuid);
        const int id = int(instanceUuids_.size());
        const String name = i.name.isNotEmpty() ? i.name : String("ChipBoy");
        PopupMenu::Item item(i.alive ? name : name + " (offline)");
        item.itemID = id;
        if (!i.alive) item.setColour(colours::textDim);
        menu->addItem(std::move(item));
        if (i.uuid == target) { selected = id; haveTarget = true; }
    }
    if (!haveTarget) {
        // The saved target is not in the directory at all: keep it, say so (11.5).
        instanceUuids_.push_back(target);
        const int id = int(instanceUuids_.size());
        const String name = client.targetName().isNotEmpty() ? client.targetName() : String("saved ChipBoy");
        PopupMenu::Item item(name + " (offline)");
        item.itemID = id;
        item.setColour(colours::textDim);
        menu->addItem(std::move(item));
        selected = id;
    }
    instanceBox_.setSelectedId(selected, dontSendNotification);
}

void VoiceEditor::refreshChannels()
{
    const auto* info = targetInfo();
    StringArray names;
    String signature;
    for (int ch = 0; ch < 4; ++ch) {
        String text = colours::channelName(ch);
        if (info && info->claimed[ch])
            text << dot() << (info->claimNames[ch].isNotEmpty() ? info->claimNames[ch] : String("another Voice"));
        names.add(text);
        signature << text << ';';
    }
    signature << '#' << processor_.channel();
    if (signature == channelSignature_ || channelBox_.isPopupActive()) return;
    channelSignature_ = signature;

    channelBox_.clear(dontSendNotification);
    for (int ch = 0; ch < 4; ++ch) channelBox_.addItem(names[ch], ch + 1);
    channelBox_.setSelectedId(processor_.channel() + 1, dontSendNotification);
}

void VoiceEditor::refreshInstruments(bool force)
{
    auto& client = processor_.link();
    const uint32_t serial = client.bankSerial();
    const String target = client.targetUuid();
    const bool hex = ValueFormat::hex();
    const bool linked = client.region() != nullptr;
    if (!force && serial == instrumentSerial_ && target == instrumentTarget_ && hex == instrumentHex_ && linked == instrumentLinked_) return;
    if (instrumentBox_.isPopupActive()) return;   // next tick
    instrumentSerial_ = serial; instrumentTarget_ = target; instrumentHex_ = hex; instrumentLinked_ = linked;

    instrumentBox_.clear(dontSendNotification);
    auto* menu = instrumentBox_.getRootMenu();
    instrumentBox_.addItem("none", 1);
    for (int slot = 1; slot <= bank::kInstrumentSlots; ++slot) {
        const String name = client.instrumentName(slot);
        const bool empty = linked && client.instrumentType(slot) < 0;
        String text = ValueFormat::number(slot);
        if (name.isNotEmpty()) text << dot() << name;
        else if (empty) text << dot() << "empty";
        PopupMenu::Item item(text);
        item.itemID = slot + 1;
        if (empty) item.setColour(colours::textDim);
        menu->addItem(std::move(item));
    }
    instrumentBox_.setSelectedId(instrumentSlot() + 1, dontSendNotification);
}

void VoiceEditor::applyChannelKind()
{
    const int ch = processor_.channel();
    if (ch == channelShown_) return;
    channelShown_ = ch;
    scope_.setChannel(ch);
    regs_.setChannel(ch);

    // Everything stays visible; what the channel cannot do is greyed.
    auto enable = [](const std::vector<Component*>& group, bool on) {
        for (auto* c : group) { c->setEnabled(on); c->setAlpha(on ? 1.0f : kOffAlpha); }
    };
    enable(params_->waveOnly, ch == 2);
    enable(params_->pulseOnly, ch < 2);
    enable(params_->pu1Only, ch == 0);
    enable(params_->noiseOnly, ch == 3);
    enable(params_->envelope, ch != 2);
}

void VoiceEditor::refreshScope()
{
    auto& client = processor_.link();
    link::ChannelSlot* slot = client.slot();
    if (static_cast<const void*>(slot) != scopeSlot_) {
        scopeSlot_ = slot;
        ScopeView::Source source;
        if (slot) { source.ring = &slot->scope; source.state = &slot->state; }
        scope_.setSource(source);
    }

    int model = 0;
    if (auto* r = client.region()) model = int(r->model.load(std::memory_order_relaxed) & 3);
    else if (const auto* info = targetInfo()) model = info->model;
    if (model != modelShown_) { modelShown_ = model; scope_.setAnalogCornerHz(cornerHzForModel(model)); }

    regs_.setState(slot ? slot->state.load(std::memory_order_acquire) : 0);
    regs_.repaint();
}

void VoiceEditor::refreshStatus()
{
    auto& client = processor_.link();
    const auto status = client.status();
    const String ch = colours::channelName(processor_.channel());

    String text;
    if (status == LinkClient::Status::Linked) text = "Linked" + dot() + instanceName() + dot() + ch;
    else if (status == LinkClient::Status::Busy) text = "Channel busy" + dot() + ch;
    else text = LinkClient::statusText(status);
    const Pill::Tone tone = toneFor(status);
    if (text != statusPill_.text() || tone != statusTone_) {
        statusTone_ = tone;
        statusPill_.set(text, tone);
        layoutHeader();
        statusPill_.repaint();
    }

    // the footer: the link in plain words, then what the last request did
    const String request = processor_.lastRequestMessage();
    if (request != lastRequest_) { lastRequest_ = request; editorMessage_.clear(); }
    String footer = LinkClient::statusText(status);
    if (status == LinkClient::Status::Busy)
        if (const auto* info = targetInfo())
            if (info->claimNames[processor_.channel() & 3].isNotEmpty()) footer << ", held by " << info->claimNames[processor_.channel() & 3];
    const String message = editorMessage_.isNotEmpty() ? editorMessage_ : request;
    if (message.isNotEmpty()) footer << dot() << message;
    if (footer != footerText_) { footerText_ = footer; repaint(footerArea_); }
}

void VoiceEditor::refreshNote()
{
    const String name = instanceName();
    if (name != noteName_) { noteName_ = name; repaint(noteArea_); }
}

// --- layout and paint ---------------------------------------------------------

void VoiceEditor::layoutHeader()
{
    auto bar = headerArea_.reduced(14, 0);
    bar.removeFromLeft(wordmarkArea_.getWidth() + 14);
    channelBox_.setBounds(bar.removeFromRight(130).withSizeKeepingCentre(130, kControlHeight));
    bar.removeFromRight(8);
    instanceBox_.setBounds(bar.removeFromRight(112).withSizeKeepingCentre(112, kControlHeight));
    bar.removeFromRight(14);
    const int pillWidth = std::clamp(statusPill_.preferredWidth(), 0, bar.getWidth());
    statusPill_.setBounds(bar.removeFromLeft(pillWidth).withSizeKeepingCentre(pillWidth, 20));
}

void VoiceEditor::resized()
{
    auto r = getLocalBounds();
    headerArea_ = r.removeFromTop(kHeaderHeight);
    footerArea_ = r.removeFromBottom(kFooterHeight);

    // the wordmark: "Voice" in the pixel face, "CHIPBOY" small beside it
    const int wordmarkWidth = GlyphArrangement::getStringWidthInt(Fonts::pixel(15.0f), "Voice") + 8
                            + GlyphArrangement::getStringWidthInt(labelFont(), "CHIPBOY");
    wordmarkArea_ = headerArea_.reduced(14, 0).removeFromLeft(wordmarkWidth);
    layoutHeader();

    auto body = r.reduced(kPad);
    noteArea_ = body.removeFromTop(kNoteHeight);
    body.removeFromTop(kGap);

    auto middle = body.removeFromTop(kScopeHeight + 6 + kRegsHeight);
    body.removeFromTop(kGap);
    auto left = middle.removeFromLeft((middle.getWidth() - kGap) / 2);
    middle.removeFromLeft(kGap);
    scope_.setBounds(left.removeFromTop(kScopeHeight));
    left.removeFromTop(6);
    regs_.setBounds(left.removeFromTop(kRegsHeight));

    auto right = middle;
    instrumentLabelArea_ = right.removeFromTop(kLabelHeight);
    right.removeFromTop(4);
    auto row = right.removeFromTop(kControlHeight);
    const int segWidth = std::min(sourceSeg_.preferredWidth(), row.getWidth() / 2);
    sourceSeg_.setBounds(row.removeFromRight(segWidth).withSizeKeepingCentre(segWidth, sourceSeg_.preferredHeight()));
    row.removeFromRight(8);
    instrumentBox_.setBounds(row);
    right.removeFromTop(8);
    auto buttons = right.removeFromTop(kButtonHeight);
    const Font buttonFont = Fonts::sans(11.0f);
    auto place = [&](TextButton& b) {
        const int w = std::min(GlyphArrangement::getStringWidthInt(buttonFont, b.getButtonText()) + 16, buttons.getWidth());
        b.setBounds(buttons.removeFromLeft(w));
        buttons.removeFromLeft(8);
    };
    place(pushButton_);
    place(pullButton_);
    place(openButton_);

    paramsViewport_.setBounds(body);
    const int contentWidth = std::max(1, body.getWidth() - paramsViewport_.getScrollBarThickness());
    params_->setSize(contentWidth, std::max(1, params_->layoutFor(contentWidth)));
}

void VoiceEditor::paint(Graphics& g)
{
    g.fillAll(kBody);

    // header bar
    g.setColour(colours::panel2);
    g.fillRect(headerArea_);
    g.setColour(colours::lineSoft);
    g.fillRect(headerArea_.withTop(headerArea_.getBottom() - 1));
    {
        auto a = wordmarkArea_;
        const Font pixel = Fonts::pixel(15.0f);
        g.setColour(colours::text);
        g.setFont(pixel);
        g.drawText("Voice", a.removeFromLeft(GlyphArrangement::getStringWidthInt(pixel, "Voice")), Justification::centredLeft, false);
        a.removeFromLeft(8);
        drawLabel(g, "ChipBoy", a);
    }

    // the one line everyone needs: where the audio is
    g.setColour(colours::accentSoft);
    g.fillRoundedRectangle(noteArea_.toFloat(), 4.0f);
    g.setColour(colours::accent);
    g.drawRoundedRectangle(noteArea_.toFloat().reduced(0.5f), 4.0f, 1.0f);
    {
        AttributedString s;
        s.setJustification(Justification::centredLeft);
        s.setWordWrap(AttributedString::WordWrap::none);
        s.append("Audio comes out of the ", Fonts::sans(12.0f), colours::text);
        s.append(noteName_, Fonts::sans(12.0f, true), colours::text);
        s.append(" track. This track is silent on purpose.", Fonts::sans(12.0f), colours::text);
        s.draw(g, noteArea_.reduced(10, 0).toFloat());
    }

    drawLabel(g, "Instrument", instrumentLabelArea_);

    // footer
    g.setColour(colours::panel2);
    g.fillRect(footerArea_);
    g.setColour(colours::lineSoft);
    g.fillRect(footerArea_.withHeight(1));
    auto f = footerArea_.reduced(14, 0);
    const Font mono = Fonts::mono(11.0f);
    {
        AttributedString s;
        s.setJustification(Justification::centredRight);
        s.setWordWrap(AttributedString::WordWrap::none);
        s.append("latency ", mono, colours::textDim);
        s.append("1 block", mono, colours::textMute);
        s.append(", reported", mono, colours::textDim);
        s.draw(g, f.removeFromRight(GlyphArrangement::getStringWidthInt(mono, "latency 1 block, reported") + 4).toFloat());
        f.removeFromRight(16);
    }
    g.setColour(colours::textDim);
    g.setFont(mono);
    g.drawText(footerText_, f, Justification::centredLeft, true);
}

} // namespace chipboy::plugin
