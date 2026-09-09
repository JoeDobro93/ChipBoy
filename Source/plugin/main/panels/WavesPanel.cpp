#include "plugin/main/panels/WavesPanel.h"

#include <cmath>

namespace chipboy::plugin {

using namespace juce;
using namespace chipboy::ui;

namespace {
constexpr int kListWidth = 220, kGap = 14, kListHeader = 28, kTopRow = 30, kGridHeight = 200;
constexpr int kThumbW = 40, kThumbH = 30, kThumbGap = 4;
/// The synth takes the column to the right of the drawing grid
/// (docs/COMMANDS_AND_TEMPO.md section 33).
constexpr int kSynthWidth = 356, kSynthGap = 18, kSynthLabel = 78;
constexpr int kPreviewHeight = 58, kPartialsHeight = 44;
String middot() { return String(CharPointer_UTF8(" \xc2\xb7 ")); }

/// A handful of controls side by side in one form row, laid out by a
/// function the caller gives: FormRow holds one control, and some of these
/// rows are three.
class RowOf : public juce::Component {
public:
    void add(std::unique_ptr<juce::Component> c) { addAndMakeVisible(*c); parts_.push_back(std::move(c)); }
    juce::Component& part(int i) { return *parts_[size_t(i)]; }
    std::function<void(RowOf&, juce::Rectangle<int>)> layout;
    void resized() override { if (layout) layout(*this, getLocalBounds()); }
private:
    std::vector<std::unique_ptr<juce::Component>> parts_;
};

/// One frame drawn small, on the LCD ground the wave grid uses.
void paintFrame(Graphics& g, Rectangle<int> r, const bank::Frame& f, Colour trace)
{
    g.setColour(colours::lcd);
    g.fillRect(r);
    const float cw = float(r.getWidth() - 4) / 32.0f;
    g.setColour(trace);
    for (int i = 0; i < 32; ++i) {
        const float h = float(r.getHeight() - 6) * float(f.s[size_t(i)] + 1) / 16.0f;
        g.fillRect(float(r.getX()) + 2.0f + cw * float(i), float(r.getBottom()) - 3.0f - h, std::max(1.0f, cw - 0.5f), h);
    }
    g.setColour(colours::scopeBorder);
    g.drawRect(r, 1);
}
}

/// The frames strip: a thumbnail per frame, click selects, an empty slot
/// extends the wave, + copies the current frame, - removes it.
class WavesPanel::FrameStrip : public Block, public SettableTooltipClient {
public:
    FrameStrip() { setTooltip("Frames: click to select, click an empty slot to extend, + copies the current frame, - removes it"); }
    void set(const std::vector<bank::Frame>& frames, int selected)
    {
        frames_ = frames;
        selected_ = selected;
        repaint();
    }
    std::function<void(int)> onSelect, onExtendTo;
    std::function<void()> onAdd, onRemove;

    int perRow() const { return std::max(1, (getWidth() + kThumbGap) / (kThumbW + kThumbGap)); }
    int preferredHeight(int width) override
    {
        const int per = std::max(1, (width + kThumbGap) / (kThumbW + kThumbGap));
        const int rows = (bank::kMaxFrames + 2 + per - 1) / per;
        return rows * (kThumbH + kThumbGap) - kThumbGap;
    }
    Rectangle<int> cell(int index) const
    {
        const int per = perRow();
        return { (index % per) * (kThumbW + kThumbGap), (index / per) * (kThumbH + kThumbGap), kThumbW, kThumbH };
    }
    void paint(Graphics& g) override
    {
        const int n = int(frames_.size());
        for (int k = 0; k < bank::kMaxFrames; ++k) {
            const auto r = cell(k).toFloat().reduced(0.5f);
            const bool has = k < n, sel = k == selected_;
            g.setColour(colours::panel2);
            g.fillRoundedRectangle(r, 3.0f);
            if (has) {
                g.setColour(colours::wav.withAlpha(sel ? 0.9f : 0.55f));
                const auto& f = frames_[size_t(k)];
                const float cw = (r.getWidth() - 6.0f) / 32.0f;
                for (int i = 0; i < 32; ++i) {
                    const float h = (r.getHeight() - 8.0f) * (f.s[size_t(i)] + 1) / 16.0f;
                    g.fillRect(r.getX() + 3.0f + cw * float(i), r.getBottom() - 4.0f - h, std::max(1.0f, cw - 0.5f), h);
                }
            } else {
                g.setColour(colours::textDim);
                g.setFont(Fonts::mono(11.0f));
                g.drawText(String(k + 1), cell(k), Justification::centred, false);
            }
            g.setColour(sel ? colours::wav : colours::lineSoft);
            if (has) g.drawRoundedRectangle(r, 3.0f, 1.0f);
            else { const float d[] = { 3.0f, 3.0f }; Path p; p.addRoundedRectangle(r, 3.0f); Path dashed; PathStrokeType(1.0f).createDashedStroke(dashed, p, d, 2); g.fillPath(dashed); }
        }
        g.setFont(Fonts::sans(14.0f));
        for (int b = 0; b < 2; ++b) {
            const auto r = cell(bank::kMaxFrames + b);
            const bool enabled = b == 0 ? n < bank::kMaxFrames : n > 1;
            g.setColour(colours::raised.withAlpha(enabled ? 1.0f : 0.5f));
            g.fillRoundedRectangle(r.toFloat().reduced(0.5f), 3.0f);
            g.setColour(colours::line);
            g.drawRoundedRectangle(r.toFloat().reduced(0.5f), 3.0f, 1.0f);
            g.setColour(enabled ? colours::text : colours::textDim);
            g.drawText(b == 0 ? "+" : String(CharPointer_UTF8("\xe2\x88\x92")), r, Justification::centred, false);
        }
    }
    void mouseDown(const MouseEvent& e) override
    {
        for (int k = 0; k < bank::kMaxFrames + 2; ++k) {
            if (!cell(k).contains(e.getPosition())) continue;
            const int n = int(frames_.size());
            if (k == bank::kMaxFrames) { if (onAdd) onAdd(); }
            else if (k == bank::kMaxFrames + 1) { if (onRemove) onRemove(); }
            else if (k < n) { if (onSelect) onSelect(k); }
            else if (onExtendTo) onExtendTo(k);
            return;
        }
    }
private:
    std::vector<bank::Frame> frames_;
    int selected_ = 0;
};

/// One frame as a picture: the start and the end of the morph.
class WavesPanel::MiniWave : public Component, public SettableTooltipClient {
public:
    void set(const bank::Frame& f) { frame_ = f; repaint(); }
    void paint(Graphics& g) override { paintFrame(g, getLocalBounds(), frame_, colours::wav.withAlpha(0.9f)); }
private:
    bank::Frame frame_;
};

/// The run the synth would write: every frame, side by side.
class WavesPanel::RunStrip : public Component, public SettableTooltipClient {
public:
    void set(std::vector<bank::Frame> frames) { frames_ = std::move(frames); repaint(); }
    void paint(Graphics& g) override
    {
        const int n = std::max(1, int(frames_.size()));
        const int w = std::max(6, (getWidth() - (n - 1) * 2) / n);
        for (int k = 0; k < int(frames_.size()); ++k)
            paintFrame(g, { k * (w + 2), 0, w, getHeight() }, frames_[size_t(k)], colours::wav.withAlpha(k == 0 || k == n - 1 ? 0.9f : 0.6f));
    }
private:
    std::vector<bank::Frame> frames_;
};

/// The additive source's eight partials, drawn and dragged like the wave
/// grid: a column each, 0-15.
class WavesPanel::PartialsBar : public Component, public SettableTooltipClient {
public:
    PartialsBar() { setMouseCursor(MouseCursor::CrosshairCursor); }
    void set(const std::array<uint8_t, bank::kSynthPartials>& p) { partials_ = p; repaint(); }
    std::function<void(int index, int level)> onChange;
    void paint(Graphics& g) override
    {
        g.setColour(colours::lcd);
        g.fillRect(getLocalBounds());
        const float cw = float(getWidth() - 4) / float(bank::kSynthPartials);
        for (int k = 0; k < bank::kSynthPartials; ++k) {
            const float h = float(getHeight() - 6) * float(partials_[size_t(k)]) / 15.0f;
            g.setColour(partials_[size_t(k)] > 0 ? colours::wav.withAlpha(0.9f) : colours::lcdGrid);
            g.fillRect(2.0f + cw * float(k), float(getHeight()) - 3.0f - std::max(1.0f, h), cw - 2.0f, std::max(1.0f, h));
        }
        g.setColour(colours::scopeBorder);
        g.drawRect(getLocalBounds(), 1);
    }
    void mouseDown(const MouseEvent& e) override { drag(e); }
    void mouseDrag(const MouseEvent& e) override { drag(e); }
private:
    void drag(const MouseEvent& e)
    {
        const float cw = float(getWidth() - 4) / float(bank::kSynthPartials);
        const int k = std::clamp(int(std::floor((float(e.x) - 2.0f) / std::max(1.0f, cw))), 0, bank::kSynthPartials - 1);
        const int v = std::clamp(15 - int(std::floor(float(e.y - 3) / std::max(1.0f, float(getHeight() - 6) / 16.0f))), 0, 15);
        if (partials_[size_t(k)] == uint8_t(v)) return;
        partials_[size_t(k)] = uint8_t(v);
        repaint();
        if (onChange) onChange(k, v);
    }
    std::array<uint8_t, bank::kSynthPartials> partials_{};
};

/// Everything the synth section holds, so the values can be read back.
struct WavesPanel::SynthWidgets {
    Segmented* which = nullptr;               ///< which state the fields write
    ComboBox* source = nullptr;
    Stepper* width = nullptr;
    PartialsBar* partials = nullptr;
    ComboBox* shaper[bank::kSynthStages] = { nullptr, nullptr, nullptr, nullptr };
    Stepper* amount[bank::kSynthStages] = { nullptr, nullptr, nullptr, nullptr };
    Stepper* resonance[bank::kSynthStages] = { nullptr, nullptr, nullptr, nullptr };
    Stepper* frames = nullptr;
    Stepper* seed = nullptr;
    juce::TextButton* generate = nullptr;
    MiniWave* start = nullptr;
    MiniWave* end = nullptr;
    RunStrip* run = nullptr;
};

WavesPanel::WavesPanel(ChipBoyProcessor& p)
    : EditorPanel(p),
      listTitle_("Waves" + middot() + "64 slots", Fonts::sans(11.0f), colours::textMute),
      newBtn_("New"),
      frameLabel_("Frame", Fonts::caption(10.0f), colours::textDim),
      frameText_({}, Fonts::mono(12.0f), colours::text),
      shapeLabel_("Shape", Fonts::caption(10.0f), colours::textDim),
      shape_({ "Sine", "Triangle", "Saw", "Pulse" }),
      interp_("Interpolate")
{
    frameLabel_.setUpperCase(true);
    shapeLabel_.setUpperCase(true);
    sw_ = std::make_unique<SynthWidgets>();
    for (auto* c : std::initializer_list<Component*>{ &list_, &listTitle_, &newBtn_, &name_, &frameLabel_, &frameText_, &shapeLabel_, &shape_, &interp_, &scroll_, &synthScroll_ }) addAndMakeVisible(c);
    list_.onSelect = [this](int slot) { showSlot(slot); };
    list_.onRename = [this](int slot, const String& n) {
        const int s = std::clamp(slot, 1, bank::kWaveSlots);
        processor.editBank("Wave " + ValueFormat::number(s) + " named " + n, [s, n](bank::Bank& b) { auto& w = b.waves[size_t(s - 1)]; w.used = true; if (w.frames.empty()) w.frames.push_back(bank::Frame{}); w.name = n.toStdString(); });
        selfBank_ = processor.bank().get();
        if (s == slot_) name_.setText(n);
        rebuildList();
        contextChanged();
    };
    newBtn_.setTooltip("New wave in the first empty slot: one triangle frame");
    newBtn_.onClick = [this] {
        const auto b = processor.bank();
        if (!b) return;
        int slot = 0;
        for (int k = 0; k < bank::kWaveSlots; ++k) if (!b->waves[size_t(k)].used) { slot = k + 1; break; }
        if (slot == 0) return;
        processor.editBank("New wave " + ValueFormat::number(slot), [slot](bank::Bank& bk) { auto& w = bk.waves[size_t(slot - 1)]; w.used = true; w.name = ("Wave " + String(slot)).toStdString(); w.frames = { bank::frameTriangle() }; });
        selfBank_ = processor.bank().get();
        rebuildList();
        showSlot(slot);
    };
    name_.onChange = [this](const String& n) { editWave("named " + n, [n](bank::Wave& w) { w.name = n.toStdString(); }, false); };
    shape_.setMini(true);
    shape_.setTooltip("Draw a shape into the frame on show, quantized to 4 bits.");
    shape_.onChange = [this](int i) { generate(i); };
    interp_.setTooltip("Every frame between the first and the last becomes a blend of the two.");
    interp_.onClick = [this] { interpolate(); };

    auto stack = std::make_unique<Stack>(8);
    stack->add(std::make_unique<Hold>(grid_, kGridHeight));
    auto frames = std::make_unique<FrameStrip>();
    frames_ = frames.get();
    frames->onSelect = [this](int k) { frame_ = k; syncFromBank(true); contextChanged(); };
    frames->onExtendTo = [this](int k) {
        editWave("frame count", [k](bank::Wave& w) { const bank::Frame last = w.frames.empty() ? bank::Frame{} : w.frames.back(); while (int(w.frames.size()) <= k && int(w.frames.size()) < bank::kMaxFrames) w.frames.push_back(last); }, false);
        frame_ = k;
        syncFromBank(true);
    };
    frames->onAdd = [this] {
        const int cur = frame_;
        editWave("frame inserted", [cur](bank::Wave& w) { if (int(w.frames.size()) >= bank::kMaxFrames) return; const bank::Frame f = w.frames.empty() ? bank::Frame{} : w.frames[size_t(std::clamp(cur, 0, int(w.frames.size()) - 1))]; w.frames.insert(w.frames.begin() + std::min<long>(long(cur) + 1, long(w.frames.size())), f); }, false);
        const auto b = processor.bank();
        if (b) frame_ = std::min(frame_ + 1, int(b->waves[size_t(slot_ - 1)].frames.size()) - 1);
        syncFromBank(true);
    };
    frames->onRemove = [this] {
        const int cur = frame_;
        editWave("frame deleted", [cur](bank::Wave& w) { if (w.frames.size() > 1 && cur >= 0 && cur < int(w.frames.size())) w.frames.erase(w.frames.begin() + cur); }, false);
        const auto b = processor.bank();
        if (b) frame_ = std::clamp(frame_, 0, std::max(0, int(b->waves[size_t(slot_ - 1)].frames.size()) - 1));
        syncFromBank(true);
    };
    stack->add(std::move(frames));
    RichText help;
    help.plain("Each frame is one load of wave RAM. ").bold("A frame change costs a click on a DMG").plain(", not on a CGB.");
    stack->add(std::make_unique<HelpText>(help));
    scroll_.setContent(std::move(stack));
    buildSynth();
    grid_.onChange = [this](const bank::Frame& f) {
        const int k = frame_;
        editWave("frame " + String(k + 1), [f, k](bank::Wave& w) { if (w.frames.empty()) w.frames.push_back(bank::Frame{}); w.frames[size_t(std::clamp(k, 0, int(w.frames.size()) - 1))] = f; }, false);
    };

    rebuildList();
    syncFromBank(true);
}

WavesPanel::~WavesPanel() = default;

RichText WavesPanel::contextLine() const
{
    RichText r;
    const auto b = processor.bank();
    const bank::Wave* w = b ? b->wave(slot_) : nullptr;
    r.plain("Wave ").bold(w ? slotAndName(slot_, w->name) : slotAndName(slot_, "empty"));
    if (w) r.plain(middot() + String(int(w->frames.size())) + (w->frames.size() == 1 ? " frame" : " frames") + middot() + "frame " + String(frame_ + 1));
    return r;
}

void WavesPanel::rebuildList()
{
    const auto b = processor.bank();
    if (!b) return;
    std::vector<SlotRow> rows;
    rows.resize(size_t(bank::kWaveSlots));
    for (int k = 0; k < bank::kWaveSlots; ++k) {
        const auto& w = b->waves[size_t(k)];
        auto& r = rows[size_t(k)];
        r.slot = k + 1;
        r.used = w.used;
        r.name = w.used ? String(w.name) : String();
        r.note = w.used ? String(int(w.frames.size())) + " fr" : String();
    }
    bool same = rows.size() == lastRows_.size();
    for (size_t k = 0; same && k < rows.size(); ++k)
        same = rows[k].used == lastRows_[k].used && rows[k].name == lastRows_[k].name && rows[k].note == lastRows_[k].note;
    if (!same) { lastRows_ = rows; list_.setRows(rows); }
    if (list_.selected() != slot_) list_.setSelected(slot_, dontSendNotification);
}

void WavesPanel::showSlot(int slot)
{
    slot_ = std::clamp(slot, 1, bank::kWaveSlots);
    frame_ = 0;
    if (list_.selected() != slot_) list_.setSelected(slot_, dontSendNotification);
    buildSynth();
    syncFromBank(true);
    contextChanged();
}

void WavesPanel::syncFromBank(bool pushToGrid)
{
    const auto b = processor.bank();
    if (!b) return;
    const bank::Wave& w = b->waves[size_t(slot_ - 1)];
    const String n = w.used ? String(w.name) : String();
    if (name_.text() != n) name_.setText(n);
    const int count = std::max(1, int(w.frames.size()));
    frame_ = std::clamp(frame_, 0, count - 1);
    frameText_.setText(String(frame_ + 1) + " of " + String(count));
    const bank::Frame f = frame_ < int(w.frames.size()) ? w.frames[size_t(frame_)] : bank::Frame{};
    if (pushToGrid) grid_.setFrame(f);
    if (frames_) {
        frames_->set(w.frames.empty() ? std::vector<bank::Frame>{ f } : w.frames, frame_);
        scroll_.relayout();
    }
    interp_.setEnabled(w.frames.size() >= 3);
    syncSynth();
}

void WavesPanel::editWave(const String& what, const std::function<void(bank::Wave&)>& fn, bool pushToGrid)
{
    const int slot = slot_;
    processor.editBank("Wave " + ValueFormat::number(slot) + " " + what, [&fn, slot](bank::Bank& b) {
        auto& w = b.waves[size_t(slot - 1)];
        if (!w.used) { w.used = true; if (w.name.empty()) w.name = ("Wave " + String(slot)).toStdString(); }
        if (w.frames.empty()) w.frames.push_back(bank::Frame{});
        fn(w);
        if (w.frames.empty()) w.frames.push_back(bank::Frame{});
    });
    selfBank_ = processor.bank().get();
    syncFromBank(pushToGrid);
    rebuildList();
    contextChanged();
}

void WavesPanel::generate(int shape)
{
    const bank::Frame f = shape == 0 ? bank::frameSine() : shape == 1 ? bank::frameTriangle() : shape == 2 ? bank::frameSaw() : bank::framePulse(16);
    const int k = frame_;
    editWave("frame " + String(k + 1), [f, k](bank::Wave& w) { w.frames[size_t(std::clamp(k, 0, int(w.frames.size()) - 1))] = f; }, true);
}

void WavesPanel::interpolate()
{
    editWave("frames interpolated", [](bank::Wave& w) {
        const int n = int(w.frames.size());
        if (n < 3) return;
        const bank::Frame a = w.frames.front(), b = w.frames.back();
        for (int k = 1; k < n - 1; ++k) w.frames[size_t(k)] = bank::frameInterpolate(a, b, double(k) / double(n - 1));
    }, true);
}

void WavesPanel::bankChanged()
{
    const auto b = processor.bank();
    rebuildList();
    // The source decides which of the synth's own fields the section holds,
    // so it is the one change that rebuilds it (section 33).
    if (b && int(b->waves[size_t(slot_ - 1)].synth.source) != builtSource_) buildSynth();
    if (b && b.get() != selfBank_) syncFromBank(true);
    else syncSynth();
    contextChanged();
}

void WavesPanel::hexChanged()
{
    lastRows_.clear();
    rebuildList();
    repaint();
}

/* ------------------------------------------------------------ synth */

bank::Synth WavesPanel::currentSynth() const
{
    const auto b = processor.bank();
    return b ? b->waves[size_t(slot_ - 1)].synth : bank::Synth{};
}

bank::Frame WavesPanel::drawnFrame() const
{
    const auto b = processor.bank();
    if (!b) return {};
    const auto& w = b->waves[size_t(slot_ - 1)];
    if (w.frames.empty()) return {};
    return w.frames[size_t(std::clamp(frame_, 0, int(w.frames.size()) - 1))];
}

void WavesPanel::editSynth(const String& what, const std::function<void(bank::Synth&)>& fn)
{
    editWave("synth " + what, [&fn](bank::Wave& w) { w.synth.used = true; fn(w.synth); }, false);
}

void WavesPanel::editState(const String& what, const std::function<void(bank::SynthState&)>& fn)
{
    const int end = editEnd_;
    editSynth(what, [&fn, end](bank::Synth& sy) { fn(end == 1 ? sy.end : sy.start); });
}

void WavesPanel::buildSynth()
{
    const auto b = processor.bank();
    sw_ = std::make_unique<SynthWidgets>();
    auto stack = std::make_unique<Stack>(10);
    if (!b) { synthScroll_.setContent(std::move(stack)); return; }
    const bank::Synth sy = b->waves[size_t(slot_ - 1)].synth;
    builtSource_ = int(sy.source);

    auto group = std::make_unique<FormGroup>("Synth", kSynthLabel);

    // Which end of the morph the fields below write. The kinds -- the source
    // and the chain's shapers -- belong to both (section 33).
    {
        auto which = std::make_unique<Segmented>(StringArray{ "Start", "End" });
        which->setMini(true);
        which->setTooltip("Which end of the morph these values belong to. The source and the shapers are shared; the numbers are not.");
        which->setSelected(editEnd_, dontSendNotification);
        which->onChange = [this](int v) { editEnd_ = v == 1 ? 1 : 0; syncSynth(); };
        sw_->which = which.get();
        const int w = which->preferredWidth(), h = which->preferredHeight();
        group->add("Editing", std::move(which), w, h, "Which end of the morph these values belong to.");
    }
    {
        auto src = std::make_unique<ComboBox>();
        src->setScrollWheelEnabled(false);
        for (int k = 0; k < bank::kSynthSourceCount; ++k) src->addItem(bank::synthSourceName(bank::SynthSource(k)), k + 1);
        src->setSelectedId(int(sy.source) + 1, dontSendNotification);
        src->setTooltip("What the frame starts as, before the shapers. Drawn reads the frame on show.");
        src->onChange = [this] {
            if (sw_ == nullptr || sw_->source == nullptr) return;
            const int id = sw_->source->getSelectedId();
            if (id < 1) return;
            const auto v = bank::SynthSource(id - 1);
            editSynth(String("source ") + bank::synthSourceName(v), [v](bank::Synth& sy2) { sy2.source = v; });
            buildSynth();
            resized();
            syncSynth();
        };
        sw_->source = src.get();
        group->add("Source", std::move(src), 148, 24, "What the frame starts as, before the shapers.");
    }
    if (bank::synthSourceHasWidth(sy.source)) {
        auto width = std::make_unique<Stepper>();
        width->setRange(1, 31, 16);
        width->setTooltip("The square's pulse width, in samples of the 32.");
        width->onChange = [this](int v) { editState("width " + String(v), [v](bank::SynthState& st) { st.width = uint8_t(std::clamp(v, 1, 31)); }); };
        sw_->width = width.get();
        group->add("Width", std::move(width), 90, Stepper::kHeight, "The square's pulse width, in samples of the 32.");
    }
    if (bank::synthSourceHasPartials(sy.source)) {
        auto bar = std::make_unique<PartialsBar>();
        bar->setTooltip("Eight harmonics, 0-15 each, drawn with the mouse. The sum is scaled to the rails.");
        bar->onChange = [this](int k, int v) {
            editState("partial " + String(k + 1) + " " + String(v), [k, v](bank::SynthState& st) { st.partials[size_t(k)] = uint8_t(std::clamp(v, 0, 15)); });
        };
        sw_->partials = bar.get();
        group->add("Partials", std::move(bar), 190, kPartialsHeight, "Eight harmonics, 0-15 each, drawn with the mouse.");
    }
    // The chain: four shapers in order, each with its amount and, on the
    // filters, its resonance. An amount of 0 is a no-op.
    for (int k = 0; k < bank::kSynthStages; ++k) {
        auto kind = std::make_unique<ComboBox>();
        kind->setScrollWheelEnabled(false);
        for (int i = 0; i < bank::kSynthShaperCount; ++i) kind->addItem(bank::synthShaperName(bank::SynthShaper(i)), i + 1);
        kind->setSelectedId(int(sy.chain[size_t(k)]) + 1, dontSendNotification);
        kind->onChange = [this, k] {
            if (sw_ == nullptr || sw_->shaper[k] == nullptr) return;
            const int id = sw_->shaper[k]->getSelectedId();
            if (id < 1) return;
            const auto v = bank::SynthShaper(id - 1);
            editSynth("shaper " + String(k + 1) + " " + bank::synthShaperName(v), [k, v](bank::Synth& sy2) { sy2.chain[size_t(k)] = v; });
            syncSynth();
        };
        auto amount = std::make_unique<Stepper>();
        amount->setRange(-15, 15, 0);
        amount->setTextFunction([](int v) { return ValueFormat::signedNumber(v); });
        amount->onChange = [this, k](int v) {
            editState("shaper " + String(k + 1) + " amount " + String(v), [k, v](bank::SynthState& st) { st.amount[size_t(k)] = int8_t(std::clamp(v, -15, 15)); });
        };
        auto res = std::make_unique<Stepper>();
        res->setRange(0, 15, 0);
        res->onChange = [this, k](int v) {
            editState("shaper " + String(k + 1) + " resonance " + String(v), [k, v](bank::SynthState& st) { st.resonance[size_t(k)] = uint8_t(std::clamp(v, 0, 15)); });
        };
        sw_->shaper[k] = kind.get();
        sw_->amount[k] = amount.get();
        sw_->resonance[k] = res.get();
        auto row = std::make_unique<RowOf>();
        row->add(std::move(kind));
        row->add(std::move(amount));
        row->add(std::move(res));
        row->layout = [](RowOf& r, Rectangle<int> area) {
            r.part(0).setBounds(area.removeFromLeft(112).withSizeKeepingCentre(112, 24));
            area.removeFromLeft(4);
            r.part(2).setBounds(area.removeFromRight(76));
            area.removeFromRight(4);
            r.part(1).setBounds(area);
        };
        group->add("Shaper " + String(k + 1), std::move(row), 0, Stepper::kHeight,
                   String(bank::synthShaperHelp(sy.chain[size_t(k)])) + "  The amount is 0-15 either way, 0 a no-op; the last field is the filters' resonance.");
    }
    {
        auto frames = std::make_unique<Stepper>();
        frames->setRange(1, bank::kMaxFrames, 1);
        frames->setTooltip("How many frames the run holds. They morph from the start state to the end state.");
        frames->onChange = [this](int v) { editSynth("frames " + String(v), [v](bank::Synth& sy2) { sy2.frames = uint8_t(std::clamp(v, 1, bank::kMaxFrames)); }); };
        sw_->frames = frames.get();
        group->add("Frames", std::move(frames), 90, Stepper::kHeight, "How many frames the run holds; they morph from the start state to the end state.");
        auto seed = std::make_unique<Stepper>();
        seed->setRange(0, 255, 1);
        seed->setTooltip("The Noise source's seed, so a run is repeatable.");
        seed->onChange = [this](int v) { editSynth("seed " + String(v), [v](bank::Synth& sy2) { sy2.seed = uint8_t(std::clamp(v, 0, 255)); }); };
        sw_->seed = seed.get();
        group->add("Seed", std::move(seed), 90, Stepper::kHeight, "The Noise source's seed, so a run is repeatable.");
    }
    {
        auto go = std::make_unique<juce::TextButton>("Generate");
        go->setTooltip("Write the run into this slot's frames. One undo; the parameters stay, so the run can be made again.");
        go->onClick = [this] { runSynth(); };
        sw_->generate = go.get();
        group->add("", std::move(go), 110, 24, "Write the run into this slot's frames.");
    }
    stack->add(std::move(group));

    // The two ends and the run they morph through.
    auto previews = std::make_unique<FormGroup>("Preview", kSynthLabel);
    {
        auto a = std::make_unique<MiniWave>();
        auto z = std::make_unique<MiniWave>();
        a->setTooltip("The start state's wave.");
        z->setTooltip("The end state's wave.");
        sw_->start = a.get();
        sw_->end = z.get();
        auto ends = std::make_unique<RowOf>();
        ends->add(std::move(a));
        ends->add(std::move(z));
        ends->layout = [](RowOf& r, Rectangle<int> area) {
            r.part(0).setBounds(area.removeFromLeft((area.getWidth() - 8) / 2));
            area.removeFromLeft(8);
            r.part(1).setBounds(area);
        };
        previews->add("Start / end", std::move(ends), 0, kPreviewHeight, "The two ends of the morph.");
        auto run = std::make_unique<RunStrip>();
        run->setTooltip("The frames Generate would write, oldest first.");
        sw_->run = run.get();
        previews->add("Run", std::move(run), 0, kPreviewHeight, "The frames Generate would write.");
    }
    stack->add(std::move(previews));

    synthScroll_.setContent(std::move(stack));
    syncSynth();
}

void WavesPanel::syncSynth()
{
    if (!sw_) return;
    const auto b = processor.bank();
    if (!b) return;
    const bank::Synth sy = b->waves[size_t(slot_ - 1)].synth;
    const bank::SynthState& st = editEnd_ == 1 ? sy.end : sy.start;
    if (sw_->which) sw_->which->setSelected(editEnd_, dontSendNotification);
    if (sw_->source) sw_->source->setSelectedId(int(sy.source) + 1, dontSendNotification);
    if (sw_->width) sw_->width->setValue(st.width, dontSendNotification);
    if (sw_->partials) sw_->partials->set(st.partials);
    for (int k = 0; k < bank::kSynthStages; ++k) {
        if (sw_->shaper[k]) sw_->shaper[k]->setSelectedId(int(sy.chain[size_t(k)]) + 1, dontSendNotification);
        if (sw_->amount[k]) sw_->amount[k]->setValue(st.amount[size_t(k)], dontSendNotification);
        if (sw_->resonance[k]) {
            sw_->resonance[k]->setValue(st.resonance[size_t(k)], dontSendNotification);
            sw_->resonance[k]->setEnabled(bank::synthShaperHasResonance(sy.chain[size_t(k)]));
        }
        if (sw_->amount[k]) sw_->amount[k]->setEnabled(sy.chain[size_t(k)] != bank::SynthShaper::None);
    }
    if (sw_->frames) sw_->frames->setValue(std::clamp<int>(sy.frames, 1, bank::kMaxFrames), dontSendNotification);
    if (sw_->seed) sw_->seed->setValue(sy.seed, dontSendNotification);
    // The pictures are the synth rendered, so they say exactly what Generate
    // would write (section 33).
    const bank::Frame drawn = drawnFrame();
    bank::Synth one = sy;
    one.frames = 1;
    if (sw_->start) { bank::Synth s0 = one; s0.end = s0.start; sw_->start->set(bank::synthesizeFrame(s0, drawn, 0)); }
    if (sw_->end) { bank::Synth s1 = one; s1.start = s1.end; sw_->end->set(bank::synthesizeFrame(s1, drawn, 0)); }
    if (sw_->run) {
        std::vector<bank::Frame> run;
        bank::synthesize(sy, drawn, run);
        sw_->run->set(std::move(run));
    }
}

void WavesPanel::runSynth()
{
    const bank::Synth sy = currentSynth();
    const bank::Frame drawn = drawnFrame();
    std::vector<bank::Frame> run;
    bank::synthesize(sy, drawn, run);
    if (run.empty()) return;
    editWave("generated " + String(int(run.size())) + (run.size() == 1 ? " frame" : " frames"),
             [&run](bank::Wave& w) { w.frames = run; w.synth.used = true; }, true);
    frame_ = 0;
    syncFromBank(true);
    message("Generated " + String(int(run.size())) + (run.size() == 1 ? " frame" : " frames") + " into wave " + ValueFormat::number(slot_));
}

void WavesPanel::resized()
{
    auto area = getLocalBounds();
    auto left = area.removeFromLeft(kListWidth);
    auto head = left.removeFromTop(kListHeader);
    newBtn_.setBounds(head.removeFromRight(44).reduced(2, 3));
    listTitle_.setBounds(head.withTrimmedLeft(8));
    list_.setBounds(left);
    area.removeFromLeft(kGap);
    // The synth takes the right column, the drawing grid and its frames the
    // rest (docs/COMMANDS_AND_TEMPO.md section 33).
    auto synth = area.removeFromRight(std::min(kSynthWidth, std::max(0, area.getWidth() / 2)));
    area.removeFromRight(kSynthGap);
    auto top = area.removeFromTop(kTopRow);
    name_.setBounds(top.removeFromLeft(200).withHeight(NameField::kHeight));
    top.removeFromLeft(12);
    frameLabel_.setBounds(top.removeFromLeft(44));
    frameText_.setBounds(top.removeFromLeft(64));
    interp_.setBounds(top.removeFromRight(88).reduced(0, 3));
    top.removeFromRight(8);
    shape_.setBounds(top.removeFromRight(shape_.preferredWidth()).withSizeKeepingCentre(shape_.preferredWidth(), shape_.preferredHeight()));
    top.removeFromRight(6);
    shapeLabel_.setBounds(top.removeFromRight(42));
    area.removeFromTop(6);
    scroll_.setBounds(area);
    synthScroll_.setBounds(synth);
}

} // namespace chipboy::plugin
