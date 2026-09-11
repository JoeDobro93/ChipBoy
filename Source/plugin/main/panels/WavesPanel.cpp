#include "plugin/main/panels/WavesPanel.h"

#include "plugin/shared/KitImport.h"

#include <cmath>

namespace chipboy::plugin {

using namespace juce;
using namespace chipboy::ui;

namespace {
constexpr int kListWidth = 220, kGap = 14, kListHeader = 28, kTopRow = 30, kGridHeight = 200;
/// The frames strip: eight thumbnails to a row, stretched to the strip
/// (docs/COMMANDS_AND_TEMPO.md section 36), with + and - after the sixteenth.
constexpr int kThumbsPerRow = 8, kThumbH = 34, kThumbGap = 4;
/// The synth takes the column to the right of the drawing grid (section 33).
constexpr int kSynthWidth = 356, kSynthGap = 18, kSynthLabel = 78;
constexpr int kPreviewHeight = 58, kPartialsHeight = 44;
String middot() { return String(CharPointer_UTF8(" \xc2\xb7 ")); }

/// A handful of controls side by side in one form row, laid out by a
/// function the caller gives.
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
    std::function<void(int)> onSelect;

    // Section 103: a wave is sixteen frames, always, so the strip is those
    // sixteen -- there is nothing to add or remove.
    static constexpr int kCells = bank::kMaxFrames;
    static int rows() { return (kCells + kThumbsPerRow - 1) / kThumbsPerRow; }
    int preferredHeight(int) override { return rows() * (kThumbH + kThumbGap) - kThumbGap; }
    int thumbW() const { return std::max(24, (getWidth() - (kThumbsPerRow - 1) * kThumbGap) / kThumbsPerRow); }
    Rectangle<int> cell(int index) const
    {
        const int w = thumbW();
        return { (index % kThumbsPerRow) * (w + kThumbGap), (index / kThumbsPerRow) * (kThumbH + kThumbGap), w, kThumbH };
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
                // The frame's number in its corner, so the strip reads as the run
                // it is and From / To can be read off it (section 36).
                g.setColour(sel ? colours::text : colours::textDim);
                g.setFont(Fonts::mono(9.0f));
                g.drawText(String(k + 1), cell(k).reduced(4, 2), Justification::topLeft, false);
            } else {
                g.setColour(colours::textDim);
                g.setFont(Fonts::mono(11.0f));
                g.drawText(String(k + 1), cell(k), Justification::centred, false);
            }
            g.setColour(sel ? colours::wav : colours::lineSoft);
            if (has) g.drawRoundedRectangle(r, 3.0f, 1.0f);
            else { const float d[] = { 3.0f, 3.0f }; Path p; p.addRoundedRectangle(r, 3.0f); Path dashed; PathStrokeType(1.0f).createDashedStroke(dashed, p, d, 2); g.fillPath(dashed); }
        }
    }
    void mouseDown(const MouseEvent& e) override
    {
        for (int k = 0; k < kCells; ++k) {
            if (!cell(k).contains(e.getPosition())) continue;
            if (onSelect) onSelect(k);
            return;
        }
    }
private:
    std::vector<bank::Frame> frames_;
    int selected_ = 0;
};

/// The tools under the drawing grid (section 36): draw a shape into the
/// frame, interpolate the frames between the first and the last, and the
/// grid's view. Holds the panel's own widgets, so it owns nothing.
class WavesPanel::ToolsRow : public Block {
public:
    ToolsRow(Segmented& shape, TextButton& interp, TextButton& import, Segmented& view)
        : drawLabel_("Draw", Fonts::caption(10.0f), colours::textDim), viewLabel_("View", Fonts::caption(10.0f), colours::textDim),
          shape_(shape), interp_(interp), import_(import), view_(view)
    {
        drawLabel_.setUpperCase(true);
        viewLabel_.setUpperCase(true);
        for (auto* c : std::initializer_list<Component*>{ &drawLabel_, &shape_, &interp_, &import_, &viewLabel_, &view_ }) addAndMakeVisible(c);
    }
    int preferredHeight(int) override { return 26; }
    void resized() override
    {
        auto area = getLocalBounds();
        drawLabel_.setBounds(area.removeFromLeft(38));
        shape_.setBounds(area.removeFromLeft(shape_.preferredWidth()).withSizeKeepingCentre(shape_.preferredWidth(), shape_.preferredHeight()));
        area.removeFromLeft(8);
        interp_.setBounds(area.removeFromLeft(88).reduced(0, 2));
        area.removeFromLeft(6);
        import_.setBounds(area.removeFromLeft(72).reduced(0, 2));
        view_.setBounds(area.removeFromRight(view_.preferredWidth()).withSizeKeepingCentre(view_.preferredWidth(), view_.preferredHeight()));
        area.removeFromRight(6);
        viewLabel_.setBounds(area.removeFromRight(34));
    }
private:
    TextLine drawLabel_, viewLabel_;
    Segmented& shape_;
    TextButton& interp_;
    TextButton& import_;
    Segmented& view_;
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

/// The additive shape's eight partials, drawn and dragged like the wave
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

/// One link of the chain (section 36): the shaper, its amount and -- on
/// the filters only -- its resonance, with a dim line underneath that says
/// what this shaper's amount does. The line is there only while a shaper is
/// chosen, so an empty stage is one row.
class WavesPanel::ShaperRow : public Block, public SettableTooltipClient {
public:
    explicit ShaperRow(const String& label) : label_(label)
    {
        kind.setScrollWheelEnabled(false);
        for (int i = 0; i < bank::kSynthShaperCount; ++i) kind.addItem(bank::synthShaperName(bank::SynthShaper(i)), i + 1);
        amount.setRange(-15, 15, 0);
        amount.setTextFunction([](int v) { return ValueFormat::signedNumber(v); });
        amount.setTooltip("How much, -15 to 15: 0 is a no-op, and the sign is the direction where the shaper has one.");
        resonance.setRange(0, 15, 0);
        resonance.setTooltip("Resonance, 0-15: the peak at the filter's corner.");
        addAndMakeVisible(kind);
        addAndMakeVisible(amount);
        addChildComponent(resonance);
    }
    static constexpr int kControls = 26, kHelp = 28;
    /// What the row shows for a shaper: its help line, and the resonance
    /// where the shaper reads one. True when the row's height changed.
    bool setShaper(bank::SynthShaper s)
    {
        const bool was = shown_;
        shown_ = s != bank::SynthShaper::None;
        help_ = shown_ ? String(bank::synthShaperHelp(s)) : String();
        resonance.setVisible(bank::synthShaperHasResonance(s));
        amount.setEnabled(shown_);
        repaint();
        return was != shown_;
    }
    int preferredHeight(int) override { return kControls + (shown_ ? kHelp : 0); }
    void resized() override
    {
        auto row = getLocalBounds().removeFromTop(kControls).withTrimmedLeft(kSynthLabel);
        kind.setBounds(row.removeFromLeft(112).withSizeKeepingCentre(112, 24));
        row.removeFromLeft(4);
        if (resonance.isVisible()) { resonance.setBounds(row.removeFromRight(76).withSizeKeepingCentre(76, Stepper::kHeight)); row.removeFromRight(4); }
        amount.setBounds(row.withSizeKeepingCentre(row.getWidth(), Stepper::kHeight));
    }
    void paint(Graphics& g) override
    {
        g.setFont(Fonts::sans(12.0f));
        g.setColour(colours::textMute);
        g.drawText(label_, 0, 0, kSynthLabel - 8, kControls, Justification::centredLeft, false);
        if (!shown_) return;
        g.setFont(Fonts::sans(10.5f));
        g.setColour(colours::textDim);
        g.drawFittedText(help_, Rectangle<int>(kSynthLabel, kControls - 2, getWidth() - kSynthLabel, kHelp - 2), Justification::topLeft, 2, 0.9f);
    }
    ComboBox kind;
    Stepper amount, resonance;
private:
    String label_, help_;
    bool shown_ = false;
};

/// Everything the synth section holds, so the values can be read back.
struct WavesPanel::SynthWidgets {
    Segmented* which = nullptr;               ///< which state the fields write
    ComboBox* source = nullptr;               ///< the edited end's shape
    Stepper* width = nullptr;
    PartialsBar* partials = nullptr;
    Stepper* seed = nullptr;
    ShaperRow* stage[bank::kSynthStages] = { nullptr, nullptr, nullptr, nullptr };
    Stepper* from = nullptr;
    Stepper* to = nullptr;
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
      shape_({ "Sine", "Triangle", "Saw", "Pulse" }),
      view_({ "Points", "Bars" }),
      interp_("Interpolate"),
      import_("Import" + String(CharPointer_UTF8("\xe2\x80\xa6")))
{
    frameLabel_.setUpperCase(true);
    sw_ = std::make_unique<SynthWidgets>();
    for (auto* c : std::initializer_list<Component*>{ &list_, &listTitle_, &newBtn_, &name_, &frameLabel_, &frameText_, &scroll_, &synthScroll_ }) addAndMakeVisible(c);
    list_.onSelect = [this](int slot) { showSlot(slot); };
    list_.onRename = [this](int slot, const String& n) {
        const int s = std::clamp(slot, 1, bank::kWaveSlots);
        processor.editBank("Wave " + ValueFormat::slot(s) + " named " + n, [s, n](bank::Bank& b) { auto& w = b.waves[size_t(s - 1)]; w.used = true; if (w.frames.empty()) w.frames.push_back(bank::Frame{}); w.name = n.toStdString(); });
        selfBank_ = processor.bank().get();
        if (s == slot_) name_.setText(n);
        rebuildList();
        contextChanged();
    };
    newBtn_.setTooltip("New wave in the first empty slot: sixteen triangle frames");
    newBtn_.onClick = [this] {
        const auto b = processor.bank();
        if (!b) return;
        int slot = 0;
        for (int k = 0; k < bank::kWaveSlots; ++k) if (!b->waves[size_t(k)].used) { slot = k + 1; break; }
        if (slot == 0) return;
        processor.editBank("New wave " + ValueFormat::slot(slot), [slot](bank::Bank& bk) { auto& w = bk.waves[size_t(slot - 1)]; w.used = true; w.name = ("Wave " + String(slot)).toStdString(); w.frames.assign(size_t(bank::kMaxFrames), bank::frameTriangle()); });
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
    import_.setTooltip("An audio file -- a single-cycle waveform -- read as one cycle into the frame on show: mono, 32 samples, 16 levels.");
    import_.onClick = [this] { importWave(); };
    view_.setMini(true);
    view_.setTooltip("Each sample as a point on the 32 by 16 grid, or bars. Either way the pointer's column and row are lit and the corner reads the coordinates.");
    view_.setSelected(0, dontSendNotification);   // Points, the default
    grid_.setView(WaveGrid::View::Points);
    view_.onChange = [this](int i) { grid_.setView(i == 1 ? WaveGrid::View::Bars : WaveGrid::View::Points); };

    auto stack = std::make_unique<Stack>(8);
    stack->add(std::make_unique<Hold>(grid_, kGridHeight));
    stack->add(std::make_unique<ToolsRow>(shape_, interp_, import_, view_));
    auto frames = std::make_unique<FrameStrip>();
    frames_ = frames.get();
    frames->onSelect = [this](int k) { frame_ = k; syncFromBank(true); contextChanged(); };
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

void WavesPanel::saveView(juce::ValueTree& v) const
{
    v.setProperty("slot", slot_, nullptr);
    v.setProperty("frame", frame_, nullptr);
    v.setProperty("end", editEnd_, nullptr);
}

void WavesPanel::restoreView(const juce::ValueTree& v)
{
    if (v.hasProperty("end")) editEnd_ = int(v["end"]) == 1 ? 1 : 0;
    if (v.hasProperty("slot")) showSlot(int(v["slot"]));
    if (v.hasProperty("frame")) { frame_ = std::max(0, int(v["frame"])); syncFromBank(true); contextChanged(); }
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
        // Section 103: every slot is the same sixteen frames, so what is worth
        // reading off the list is where they sit in the flat table.
        r.note = String(bank::waveFlatOf(k + 1, 0)) + "-" + String(bank::waveFlatOf(k + 1, bank::kMaxFrames - 1));
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
    frame_ = std::clamp(frame_, 0, bank::kMaxFrames - 1);
    frameText_.setText(String(frame_ + 1) + " of " + String(bank::kMaxFrames));
    const bank::Frame f = frame_ < int(w.frames.size()) ? w.frames[size_t(frame_)] : bank::Frame{};
    if (pushToGrid) grid_.setFrame(f);
    if (frames_) {
        frames_->set(w.frames, frame_);
        scroll_.relayout();
    }
    syncSynth();
}

void WavesPanel::editWave(const String& what, const std::function<void(bank::Wave&)>& fn, bool pushToGrid)
{
    const int slot = slot_;
    processor.editBank("Wave " + ValueFormat::slot(slot) + " " + what, [&fn, slot](bank::Bank& b) {
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

void WavesPanel::importWave()
{
    Component::SafePointer<WavesPanel> safe(this);
    const int k = frame_;
    chooseAndImportWave(this, [safe, k](WaveImportResult r) {
        if (safe == nullptr) return;
        if (!r.ok) { AlertWindow::showMessageBoxAsync(MessageBoxIconType::WarningIcon, "ChipBoy", r.error.isEmpty() ? String("The file could not be imported.") : r.error); return; }
        const bank::Frame f = r.frame;
        safe->frame_ = k;
        safe->editWave("frame " + String(k + 1) + " imported from " + r.name, [f, k](bank::Wave& w) { w.frames[size_t(std::clamp(k, 0, int(w.frames.size()) - 1))] = f; }, true);
        safe->message("Imported " + r.name + " (" + String(int(r.length)) + " samples) as one cycle into frame " + String(k + 1));
    });
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
    // The edited end's shape decides which of the synth's own fields the
    // section holds, so it is the one change that rebuilds it (section 36).
    if (b) {
        const auto& sy = b->waves[size_t(slot_ - 1)].synth;
        if (int((editEnd_ == 1 ? sy.end : sy.start).source) != builtSource_) buildSynth();
    }
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

/// The section is rebuilt from inside one of its own widgets' callbacks (the
/// Start / End switch, the shape combo), so the rebuild waits for the
/// message loop: the widget that asked for it is gone by then.
void WavesPanel::rebuildSynthLater()
{
    Component::SafePointer<WavesPanel> safe(this);
    MessageManager::callAsync([safe] {
        if (safe == nullptr) return;
        safe->buildSynth();
        safe->resized();
    });
}

void WavesPanel::buildSynth()
{
    const auto b = processor.bank();
    sw_ = std::make_unique<SynthWidgets>();
    auto stack = std::make_unique<Stack>(10);
    if (!b) { synthScroll_.setContent(std::move(stack)); return; }
    const bank::Synth sy = b->waves[size_t(slot_ - 1)].synth;
    const bank::SynthState& st = editEnd_ == 1 ? sy.end : sy.start;
    builtSource_ = int(st.source);

    // --- SHAPE: which end is being edited, what it starts as, and the
    // shape's own numbers (section 36).
    auto shape = std::make_unique<FormGroup>("Shape", kSynthLabel);
    {
        auto which = std::make_unique<Segmented>(StringArray{ "Start", "End" });
        which->setMini(true);
        which->setTooltip("Which end of the morph the shape and the numbers below belong to. The chain's shapers are shared; their amounts are not.");
        which->setSelected(editEnd_, dontSendNotification);
        which->onChange = [this](int v) { editEnd_ = v == 1 ? 1 : 0; rebuildSynthLater(); };
        sw_->which = which.get();
        const int w = which->preferredWidth(), h = which->preferredHeight();
        shape->add("Editing", std::move(which), w, h, "Which end of the morph these values belong to.");
    }
    {
        auto src = std::make_unique<ComboBox>();
        src->setScrollWheelEnabled(false);
        for (int k = 0; k < bank::kSynthSourceCount; ++k) src->addItem(bank::synthSourceName(bank::SynthSource(k)), k + 1);
        src->setSelectedId(int(st.source) + 1, dontSendNotification);
        src->setTooltip(String("What this end of the morph starts as, before the chain. ") + bank::synthSourceHelp(st.source));
        src->onChange = [this] {
            if (sw_ == nullptr || sw_->source == nullptr) return;
            const int id = sw_->source->getSelectedId();
            if (id < 1) return;
            const auto v = bank::SynthSource(id - 1);
            editState(String("shape ") + bank::synthSourceName(v), [v](bank::SynthState& s2) { s2.source = v; });
            rebuildSynthLater();
        };
        sw_->source = src.get();
        shape->add("Shape", std::move(src), 148, 24, String("What this end starts as, before the chain. ") + bank::synthSourceHelp(st.source));
    }
    if (bank::synthSourceHasWidth(st.source)) {
        auto width = std::make_unique<Stepper>();
        width->setRange(1, 31, 16);
        width->setTooltip("The square's pulse width: how many of the 32 samples are high.");
        width->onChange = [this](int v) { editState("width " + String(v), [v](bank::SynthState& s2) { s2.width = uint8_t(std::clamp(v, 1, 31)); }); };
        sw_->width = width.get();
        shape->add("Width", std::move(width), 90, Stepper::kHeight, "The square's pulse width: how many of the 32 samples are high.");
    }
    if (bank::synthSourceHasPartials(st.source)) {
        auto bar = std::make_unique<PartialsBar>();
        bar->setTooltip("Eight harmonics, 0-15 each, drawn with the mouse. The sum is scaled to the rails.");
        bar->onChange = [this](int k, int v) {
            editState("partial " + String(k + 1) + " " + String(v), [k, v](bank::SynthState& s2) { s2.partials[size_t(k)] = uint8_t(std::clamp(v, 0, 15)); });
        };
        sw_->partials = bar.get();
        shape->add("Partials", std::move(bar), 190, kPartialsHeight, "Eight harmonics, 0-15 each, drawn with the mouse.");
    }
    if (st.source == bank::SynthSource::Noise) {
        auto seed = std::make_unique<Stepper>();
        seed->setRange(0, 255, 1);
        seed->setTooltip("The noise's seed, so a run is repeatable. One seed for both ends.");
        seed->onChange = [this](int v) { editSynth("seed " + String(v), [v](bank::Synth& sy2) { sy2.seed = uint8_t(std::clamp(v, 0, 255)); }); };
        sw_->seed = seed.get();
        shape->add("Seed", std::move(seed), 90, Stepper::kHeight, "The noise's seed, so a run is repeatable.");
    }
    stack->add(std::move(shape));

    // --- CHAIN: four shapers in order, each with its amount and, on the
    // filters, its resonance; a line under each says what its amount does.
    auto chain = std::make_unique<FormGroup>("Chain", kSynthLabel);
    for (int k = 0; k < bank::kSynthStages; ++k) {
        auto row = std::make_unique<ShaperRow>("Shaper " + String(k + 1));
        row->setTooltip("Shaper " + String(k + 1) + " of the chain, applied in order. Its amount is the edited end's; the shaper itself is shared.");
        row->kind.setSelectedId(int(sy.chain[size_t(k)]) + 1, dontSendNotification);
        row->setShaper(sy.chain[size_t(k)]);
        row->kind.onChange = [this, k] {
            if (sw_ == nullptr || sw_->stage[k] == nullptr) return;
            const int id = sw_->stage[k]->kind.getSelectedId();
            if (id < 1) return;
            const auto v = bank::SynthShaper(id - 1);
            editSynth("shaper " + String(k + 1) + " " + bank::synthShaperName(v), [k, v](bank::Synth& sy2) { sy2.chain[size_t(k)] = v; });
            syncSynth();
        };
        row->amount.onChange = [this, k](int v) {
            editState("shaper " + String(k + 1) + " amount " + String(v), [k, v](bank::SynthState& s2) { s2.amount[size_t(k)] = int8_t(std::clamp(v, -15, 15)); });
        };
        row->resonance.onChange = [this, k](int v) {
            editState("shaper " + String(k + 1) + " resonance " + String(v), [k, v](bank::SynthState& s2) { s2.resonance[size_t(k)] = uint8_t(std::clamp(v, 0, 15)); });
        };
        sw_->stage[k] = row.get();
        chain->addWide(std::move(row));
    }
    stack->add(std::move(chain));

    // --- RUN: where in the slot the frames go, and the button.
    auto run = std::make_unique<FormGroup>("Run", kSynthLabel);
    {
        auto from = std::make_unique<Stepper>();
        from->setRange(1, bank::kMaxFrames, 1);
        from->setTooltip("The slot frame the run starts at. The frames before it stay as they are.");
        from->onChange = [this](int v) {
            editSynth("from frame " + String(v), [v](bank::Synth& sy2) {
                const int to = bank::synthFirstFrame(sy2) + bank::synthFrameCount(sy2);   // 1-based: the last frame of the run
                sy2.first = uint8_t(std::clamp(v - 1, 0, bank::kMaxFrames - 1));
                sy2.frames = uint8_t(std::clamp(to - int(sy2.first), 1, bank::kMaxFrames - int(sy2.first)));
            });
        };
        sw_->from = from.get();
        run->add("From frame", std::move(from), 90, Stepper::kHeight, "The slot frame the run starts at; the frames before it stay as they are.");
        auto to = std::make_unique<Stepper>();
        to->setRange(1, bank::kMaxFrames, 1);
        to->setTooltip("The slot frame the run ends at. The wave grows to reach it; the frames after it stay.");
        to->onChange = [this](int v) {
            editSynth("to frame " + String(v), [v](bank::Synth& sy2) {
                const int first = bank::synthFirstFrame(sy2);
                sy2.frames = uint8_t(std::clamp(v - first, 1, bank::kMaxFrames - first));
            });
        };
        sw_->to = to.get();
        run->add("To frame", std::move(to), 90, Stepper::kHeight, "The slot frame the run ends at; the wave grows to reach it and the frames after it stay.");
        auto go = std::make_unique<juce::TextButton>("Generate");
        go->setTooltip("Write the run into the slot's frames From..To. One undo; the parameters stay, so the run can be made again.");
        go->onClick = [this] { runSynth(); };
        sw_->generate = go.get();
        run->add("", std::move(go), 110, 24, "Write the run into the slot's frames.");
    }
    stack->add(std::move(run));

    // --- PREVIEW: the two ends and the run they morph through.
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
        auto strip = std::make_unique<RunStrip>();
        strip->setTooltip("The frames Generate would write, From to To.");
        sw_->run = strip.get();
        previews->add("Run", std::move(strip), 0, kPreviewHeight, "The frames Generate would write.");
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
    if (sw_->source) sw_->source->setSelectedId(int(st.source) + 1, dontSendNotification);
    if (sw_->width) sw_->width->setValue(st.width, dontSendNotification);
    if (sw_->partials) sw_->partials->set(st.partials);
    if (sw_->seed) sw_->seed->setValue(sy.seed, dontSendNotification);
    bool relayout = false;
    for (int k = 0; k < bank::kSynthStages; ++k) {
        auto* row = sw_->stage[k];
        if (row == nullptr) continue;
        row->kind.setSelectedId(int(sy.chain[size_t(k)]) + 1, dontSendNotification);
        row->amount.setValue(st.amount[size_t(k)], dontSendNotification);
        row->resonance.setValue(st.resonance[size_t(k)], dontSendNotification);
        if (row->setShaper(sy.chain[size_t(k)])) relayout = true;
        row->resized();
    }
    if (relayout) synthScroll_.relayout();
    const int first = bank::synthFirstFrame(sy), count = bank::synthFrameCount(sy);
    if (sw_->from) sw_->from->setValue(first + 1, dontSendNotification);
    if (sw_->to) sw_->to->setValue(first + count, dontSendNotification);
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
    const int first = bank::synthFirstFrame(sy), last = first + int(run.size());
    const String where = run.size() == 1 ? "frame " + String(first + 1) : "frames " + String(first + 1) + "-" + String(last);
    editWave("generated " + where, [&run, &sy](bank::Wave& w) { bank::synthWriteRun(sy, run, w); w.synth.used = true; }, true);
    frame_ = first;
    syncFromBank(true);
    message("Generated " + where + " of wave " + ValueFormat::slot(slot_));
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
    // The synth takes the right column, the drawing grid, its tools and its
    // frames the rest (docs/COMMANDS_AND_TEMPO.md sections 33 and 36).
    auto synth = area.removeFromRight(std::min(kSynthWidth, std::max(0, area.getWidth() / 2)));
    area.removeFromRight(kSynthGap);
    auto top = area.removeFromTop(kTopRow);
    name_.setBounds(top.removeFromLeft(200).withHeight(NameField::kHeight));
    top.removeFromLeft(12);
    frameLabel_.setBounds(top.removeFromLeft(44));
    frameText_.setBounds(top.removeFromLeft(64));
    area.removeFromTop(6);
    scroll_.setBounds(area);
    synthScroll_.setBounds(synth);
}

} // namespace chipboy::plugin
