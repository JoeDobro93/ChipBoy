#include "plugin/main/panels/WavesPanel.h"

namespace chipboy::plugin {

using namespace juce;
using namespace chipboy::ui;

namespace {
constexpr int kListWidth = 220, kGap = 14, kListHeader = 28, kTopRow = 30, kGridHeight = 200;
constexpr int kThumbW = 40, kThumbH = 30, kThumbGap = 4;
String middot() { return String(CharPointer_UTF8(" \xc2\xb7 ")); }
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
    for (auto* c : std::initializer_list<Component*>{ &list_, &listTitle_, &newBtn_, &name_, &frameLabel_, &frameText_, &shapeLabel_, &shape_, &interp_, &scroll_ }) addAndMakeVisible(c);
    list_.onSelect = [this](int slot) { showSlot(slot); };
    list_.onRename = [this](int slot, const String& n) {
        const int s = std::clamp(slot, 1, bank::kWaveSlots);
        processor.mutateBank([s, n](bank::Bank& b) { auto& w = b.waves[size_t(s - 1)]; w.used = true; if (w.frames.empty()) w.frames.push_back(bank::Frame{}); w.name = n.toStdString(); });
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
        processor.mutateBank([slot](bank::Bank& bk) { auto& w = bk.waves[size_t(slot - 1)]; w.used = true; w.name = ("Wave " + String(slot)).toStdString(); w.frames = { bank::frameTriangle() }; });
        selfBank_ = processor.bank().get();
        rebuildList();
        showSlot(slot);
    };
    name_.onChange = [this](const String& n) { editWave([n](bank::Wave& w) { w.name = n.toStdString(); }, false); };
    shape_.setMini(true);
    shape_.setTooltip("Generate a shape into the current frame, quantised to 4 bits");
    shape_.onChange = [this](int i) { generate(i); };
    interp_.setTooltip("Interpolate: every frame between the first and the last becomes a linear blend of those two, so the wave morphs evenly from frame 1 to the last frame");
    interp_.onClick = [this] { interpolate(); };

    auto stack = std::make_unique<Stack>(8);
    stack->add(std::make_unique<Hold>(grid_, kGridHeight));
    auto frames = std::make_unique<FrameStrip>();
    frames_ = frames.get();
    frames->onSelect = [this](int k) { frame_ = k; syncFromBank(true); contextChanged(); };
    frames->onExtendTo = [this](int k) {
        editWave([k](bank::Wave& w) { const bank::Frame last = w.frames.empty() ? bank::Frame{} : w.frames.back(); while (int(w.frames.size()) <= k && int(w.frames.size()) < bank::kMaxFrames) w.frames.push_back(last); }, false);
        frame_ = k;
        syncFromBank(true);
    };
    frames->onAdd = [this] {
        const int cur = frame_;
        editWave([cur](bank::Wave& w) { if (int(w.frames.size()) >= bank::kMaxFrames) return; const bank::Frame f = w.frames.empty() ? bank::Frame{} : w.frames[size_t(std::clamp(cur, 0, int(w.frames.size()) - 1))]; w.frames.insert(w.frames.begin() + std::min<long>(long(cur) + 1, long(w.frames.size())), f); }, false);
        const auto b = processor.bank();
        if (b) frame_ = std::min(frame_ + 1, int(b->waves[size_t(slot_ - 1)].frames.size()) - 1);
        syncFromBank(true);
    };
    frames->onRemove = [this] {
        const int cur = frame_;
        editWave([cur](bank::Wave& w) { if (w.frames.size() > 1 && cur >= 0 && cur < int(w.frames.size())) w.frames.erase(w.frames.begin() + cur); }, false);
        const auto b = processor.bank();
        if (b) frame_ = std::clamp(frame_, 0, std::max(0, int(b->waves[size_t(slot_ - 1)].frames.size()) - 1));
        syncFromBank(true);
    };
    stack->add(std::move(frames));
    RichText help;
    help.plain("Draw with the mouse. Each frame is one load of wave RAM " + String(CharPointer_UTF8("\xe2\x80\x94")) + " 32 samples, 16 levels, quantised on creation so what you see is what plays. ")
        .bold("On a DMG a frame change costs a click").plain(" (DAC off, write, re-trigger); on a CGB it does not.");
    stack->add(std::make_unique<HelpText>(help));
    scroll_.setContent(std::move(stack));
    grid_.onChange = [this](const bank::Frame& f) {
        const int k = frame_;
        editWave([f, k](bank::Wave& w) { if (w.frames.empty()) w.frames.push_back(bank::Frame{}); w.frames[size_t(std::clamp(k, 0, int(w.frames.size()) - 1))] = f; }, false);
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
}

void WavesPanel::editWave(const std::function<void(bank::Wave&)>& fn, bool pushToGrid)
{
    const int slot = slot_;
    processor.mutateBank([&fn, slot](bank::Bank& b) {
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
    editWave([f, k](bank::Wave& w) { w.frames[size_t(std::clamp(k, 0, int(w.frames.size()) - 1))] = f; }, true);
}

void WavesPanel::interpolate()
{
    editWave([](bank::Wave& w) {
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
    if (b && b.get() != selfBank_) syncFromBank(true);
    contextChanged();
}

void WavesPanel::hexChanged()
{
    lastRows_.clear();
    rebuildList();
    repaint();
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
    auto top = area.removeFromTop(kTopRow);
    name_.setBounds(top.removeFromLeft(220).withHeight(NameField::kHeight));
    top.removeFromLeft(12);
    frameLabel_.setBounds(top.removeFromLeft(48));
    frameText_.setBounds(top.removeFromLeft(70));
    interp_.setBounds(top.removeFromRight(92).reduced(0, 3));
    top.removeFromRight(8);
    shape_.setBounds(top.removeFromRight(shape_.preferredWidth()).withSizeKeepingCentre(shape_.preferredWidth(), shape_.preferredHeight()));
    top.removeFromRight(8);
    shapeLabel_.setBounds(top.removeFromRight(48));
    area.removeFromTop(6);
    scroll_.setBounds(area);
}

} // namespace chipboy::plugin
