#include "plugin/main/panels/KitsPanel.h"

#include "plugin/shared/KitImport.h"

#include <cmath>

namespace chipboy::plugin {

using namespace juce;
using namespace chipboy::ui;

namespace {
constexpr int kListWidth = 220, kGap = 14, kListHeader = 28, kRow = 24;
constexpr int kMinPeriod = 1500, kMaxPeriod = 1990;   // 3.8 kHz .. 36 kHz
String middot() { return String(CharPointer_UTF8(" \xc2\xb7 ")); }
String seconds(size_t frames, double rate) { return String(rate > 0.0 ? double(frames) / rate : 0.0, 2) + " s"; }
}

/// The samples with their notes (the mockup's .list.samples).
class KitsPanel::SampleList : public Block {
public:
    struct Row { String note, name, length; };
    void set(std::vector<Row> rows, int selected) { rows_ = std::move(rows); selected_ = selected; repaint(); }
    std::function<void(int)> onSelect;
    int preferredHeight(int) override { return std::max(1, int(rows_.size())) * kRow + 8; }
    void paint(Graphics& g) override
    {
        draw::panel(g, getLocalBounds(), colours::well, colours::lineSoft, 4.0f);
        if (rows_.empty()) {
            g.setColour(colours::textDim);
            g.setFont(Fonts::sans(12.0f));
            g.drawText("No samples yet " + String(CharPointer_UTF8("\xe2\x80\x94")) + " Import" + String(CharPointer_UTF8("\xe2\x80\xa6")) + " adds audio files, quantized to 4 bits", getLocalBounds().reduced(8, 0), Justification::centredLeft, true);
            return;
        }
        for (int i = 0; i < int(rows_.size()); ++i) {
            auto r = Rectangle<int>(4, 4 + i * kRow, getWidth() - 8, kRow);
            if (i == selected_) { g.setColour(colours::accentSoft); g.fillRoundedRectangle(r.toFloat(), 3.0f); }
            const auto& row = rows_[size_t(i)];
            g.setFont(Fonts::mono(11.0f));
            g.setColour(colours::textDim);
            g.drawText(row.note, r.withTrimmedLeft(8).withWidth(44), Justification::centredLeft, false);
            g.setFont(Fonts::mono(12.0f));
            g.setColour(i == selected_ ? colours::text : colours::textMute);
            g.drawText(row.name, r.withTrimmedLeft(56).withTrimmedRight(70), Justification::centredLeft, true);
            const auto chip = r.removeFromRight(62).reduced(4, 5);
            g.setColour(colours::lineSoft);
            g.drawRoundedRectangle(chip.toFloat(), 3.0f, 1.0f);
            g.setFont(Fonts::mono(9.5f));
            g.setColour(colours::textDim);
            g.drawText(row.length, chip, Justification::centred, false);
        }
    }
    void mouseDown(const MouseEvent& e) override
    {
        const int i = (e.y - 4) / kRow;
        if (i >= 0 && i < int(rows_.size()) && onSelect) onSelect(i);
    }
private:
    std::vector<Row> rows_;
    int selected_ = -1;
};

/// The 4-bit result, nibble per byte, on the LCD ground.
class KitsPanel::Preview : public Block {
public:
    void set(std::shared_ptr<const bank::Bank> b, int slot, int sample)
    {
        bank_ = std::move(b); slot_ = slot; sample_ = sample;
        repaint();
    }
    int preferredHeight(int) override { return 70; }
    void paint(Graphics& g) override
    {
        const float W = float(getWidth()), H = float(getHeight());
        g.fillAll(colours::lcd);
        g.setColour(colours::lcdGrid);
        for (int i = 0; i <= 4; ++i) g.drawHorizontalLine(int(std::round(H * float(i) / 4.0f)), 0.0f, W);
        const bank::Kit* k = bank_ ? bank_->kit(slot_) : nullptr;
        const bank::KitSample* s = k && sample_ >= 0 && sample_ < int(k->samples.size()) ? &k->samples[size_t(sample_)] : nullptr;
        if (!s || s->data.empty()) {
            g.setColour(colours::textDim);
            g.setFont(Fonts::mono(11.0f));
            g.drawText("no sample", getLocalBounds(), Justification::centred, false);
        } else {
            const size_t n = s->data.size();
            g.setColour(colours::wav);
            const int cols = std::max(1, getWidth());
            float lastY = -1.0f;
            for (int x = 0; x < cols; ++x) {
                const size_t a = n * size_t(x) / size_t(cols), bEnd = std::max(a + 1, n * size_t(x + 1) / size_t(cols));
                int lo = 15, hi = 0;
                for (size_t i = a; i < bEnd && i < n; ++i) { lo = std::min<int>(lo, s->data[i]); hi = std::max<int>(hi, s->data[i]); }
                const float y0 = H * (1.0f - float(hi) / 15.0f), y1 = H * (1.0f - float(lo) / 15.0f);
                const float top = lastY >= 0.0f ? std::min(y0, lastY) : y0, bottom = lastY >= 0.0f ? std::max(y1, lastY) : y1;
                g.fillRect(float(x), top, 1.0f, std::max(1.0f, bottom - top));
                lastY = y1;
            }
            const uint32_t lp = s->loopPoint;
            if (lp > 0 && lp < n) { g.setColour(colours::accentHi); const float x = W * float(lp) / float(n); g.fillRect(x, 0.0f, 1.0f, H); }
        }
        g.setColour(colours::scopeBorder);
        g.drawRect(getLocalBounds());
    }
private:
    std::shared_ptr<const bank::Bank> bank_;
    int slot_ = 1, sample_ = -1;
};

KitsPanel::KitsPanel(ChipBoyProcessor& p)
    : EditorPanel(p),
      listTitle_("Kits" + middot() + "32 slots", Fonts::sans(11.0f), colours::textMute),
      importBtn_("Import" + String(CharPointer_UTF8("\xe2\x80\xa6")))
{
    for (auto* c : std::initializer_list<Component*>{ &list_, &listTitle_, &importBtn_, &scroll_ }) addAndMakeVisible(c);
    list_.onSelect = [this](int slot) { showSlot(slot); };
    list_.onRename = [this](int slot, const String& n) {
        const int s = std::clamp(slot, 1, bank::kKitSlots);
        processor.editBank("Kit " + ValueFormat::number(s) + " named " + n, [s, n](bank::Bank& b) { auto& k = b.kits[size_t(s - 1)]; k.used = true; k.name = n.toStdString(); });
        selfBank_ = processor.bank().get();
        rebuildList();
        contextChanged();
    };
    importBtn_.setTooltip("Add audio files to the selected kit: mono, resampled to the kit's rate, 4 bits, up to 4 s each, notes assigned upward");
    importBtn_.onClick = [this] { importSamples(); };
    rebuildList();
    rebuildContent();
}

KitsPanel::~KitsPanel() = default;

RichText KitsPanel::contextLine() const
{
    RichText r;
    const auto b = processor.bank();
    const bank::Kit* k = b ? b->kit(slot_) : nullptr;
    r.plain("Kit ").bold(k ? slotAndName(slot_, k->name) : slotAndName(slot_, "empty"));
    if (k) r.plain(middot() + String(int(k->samples.size())) + (k->samples.size() == 1 ? " sample" : " samples") + middot() + withThousands(int(std::lround(bank::sampleRateForPeriod(k->period)))) + " Hz");
    return r;
}

void KitsPanel::rebuildList()
{
    const auto b = processor.bank();
    if (!b) return;
    std::vector<SlotRow> rows;
    rows.resize(size_t(bank::kKitSlots));
    for (int k = 0; k < bank::kKitSlots; ++k) {
        const auto& kit = b->kits[size_t(k)];
        auto& r = rows[size_t(k)];
        r.slot = k + 1;
        r.used = kit.used;
        r.name = kit.used ? String(kit.name) : String();
        r.note = kit.used ? String(int(kit.samples.size())) + " smp" : String();
    }
    bool same = rows.size() == lastRows_.size();
    for (size_t k = 0; same && k < rows.size(); ++k)
        same = rows[k].used == lastRows_[k].used && rows[k].name == lastRows_[k].name && rows[k].note == lastRows_[k].note;
    if (!same) { lastRows_ = rows; list_.setRows(rows); }
    if (list_.selected() != slot_) list_.setSelected(slot_, dontSendNotification);
}

void KitsPanel::showSlot(int slot)
{
    slot_ = std::clamp(slot, 1, bank::kKitSlots);
    sample_ = 0;
    if (list_.selected() != slot_) list_.setSelected(slot_, dontSendNotification);
    rebuildContent();
    contextChanged();
}

void KitsPanel::rebuildContent()
{
    const auto b = processor.bank();
    const bank::Kit* kit = b ? b->kit(slot_) : nullptr;
    const int count = kit ? int(kit->samples.size()) : 0;
    sample_ = count > 0 ? std::clamp(sample_, 0, count - 1) : -1;
    builtSlot_ = slot_; builtCount_ = count; builtSample_ = sample_;

    auto cols = std::make_unique<Columns>(12);
    auto samples = std::make_unique<SampleList>();
    samples_ = samples.get();
    samples->onSelect = [this](int i) {
        sample_ = i;
        Component::SafePointer<KitsPanel> safe(this);
        MessageManager::callAsync([safe] { if (safe != nullptr) safe->rebuildContent(); });
    };
    cols->add(std::make_unique<Card>("Samples" + middot() + "note map", std::move(samples)));

    auto stack = std::make_unique<Stack>(12);
    auto grid = std::make_unique<FlowGrid>();
    const bool have = sample_ >= 0;
    const int cur = sample_;
    {
        auto f = std::make_unique<NameField>();
        f->onChange = [this, cur](const String& n) { editKit("sample " + String(cur + 1) + " named " + n, [cur, n](bank::Kit& k) { if (cur >= 0 && cur < int(k.samples.size())) k.samples[size_t(cur)].name = n.toStdString(); }); };
        f->setEnabled(have);
        name_ = grid->addField("Name", have ? String() : "select a sample", std::move(f), NameField::kHeight, 0, 2);
    }
    {
        auto s = std::make_unique<Stepper>();
        s->setRange(0, 127, 60);
        s->setTextFunction([](int v) { return ValueFormat::noteName(v); });
        s->setTooltip("The MIDI note that plays this sample");
        s->onChange = [this, cur](int v) { editKit("sample " + String(cur + 1) + " note", [cur, v](bank::Kit& k) { if (cur >= 0 && cur < int(k.samples.size())) k.samples[size_t(cur)].note = uint8_t(v); }); };
        s->setEnabled(have);
        note_ = grid->addField("Note", "note map", std::move(s), Stepper::kHeight, 100);
    }
    {
        auto s = std::make_unique<Stepper>();
        s->setRange(0, 0, 0);
        s->setTextFunction([](int v) { return v == 0 ? String("start") : String(v) + " smp"; });
        s->setTooltip("Where a looped sample continues from (Loop from point)");
        s->onChange = [this, cur](int v) { editKit("sample " + String(cur + 1) + " loop point", [cur, v](bank::Kit& k) { if (cur >= 0 && cur < int(k.samples.size())) k.samples[size_t(cur)].loopPoint = uint32_t(std::max(0, v)); }); };
        s->setEnabled(have);
        loopPoint_ = grid->addField("Loop point", "in 4-bit samples", std::move(s), Stepper::kHeight, 0);
    }
    {
        auto remove = std::make_unique<TextButton>("Remove");
        remove->setTooltip("Remove this sample from the kit");
        remove->setEnabled(have);
        remove->onClick = [this, cur] {
            editKit("sample " + String(cur + 1) + " removed", [cur](bank::Kit& k) { if (cur >= 0 && cur < int(k.samples.size())) k.samples.erase(k.samples.begin() + cur); });
            Component::SafePointer<KitsPanel> safe(this);
            MessageManager::callAsync([safe] { if (safe != nullptr) safe->rebuildContent(); });   // the button lives in the content being rebuilt
        };
        grid->addField("Sample", String(), std::move(remove), Stepper::kHeight, 90);
    }
    {
        auto s = std::make_unique<Stepper>();
        s->setRange(kMinPeriod, kMaxPeriod, 1865);
        s->setTextFunction([](int v) { return withThousands(int(std::lround(bank::sampleRateForPeriod(uint16_t(v))))) + " Hz"; });
        s->setTooltip("The kit's playback rate, quantized to what the period register allows (NR33/34 = " + String(kit ? int(kit->period) : 1865) + ")");
        s->onChange = [this](int v) { editKit("period " + String(v), [v](bank::Kit& k) { k.period = uint16_t(std::clamp(v, 0, 2047)); }); };
        rate_ = grid->addField("Rate", "NR33/34" + middot() + "whole kit", std::move(s), Stepper::kHeight, 0);
    }
    {
        auto s = std::make_unique<Segmented>(StringArray{ "One-shot", "Loop", "From point" });
        s->setMini(true);
        s->setTooltip("How every sample of this kit plays; the instrument's own loop setting overrides per note");
        s->onChange = [this](int v) { editKit("loop", [v](bank::Kit& k) { k.loop = bank::KitLoop(std::clamp(v, 0, 2)); }); };
        const int h = s->preferredHeight(), w = s->preferredWidth();
        loop_ = grid->addField("Loop", "whole kit", std::move(s), h, w);
    }
    {
        auto t = std::make_unique<TextLine>(String(), Fonts::mono(12.0f), colours::text);
        info_ = grid->addField("Length", "the 4-bit result", std::move(t), Stepper::kHeight, 0);
    }
    stack->add(std::move(grid));
    auto preview = std::make_unique<Preview>();
    preview_ = preview.get();
    stack->add(std::move(preview));
    stack->add(std::make_unique<HelpText>(RichText("The preview is the 4-bit result, not the source file. Playing a sample an octave up plays it twice as fast " + String(CharPointer_UTF8("\xe2\x80\x94")) + " one register does both.")));
    cols->add(std::make_unique<Card>("Selected sample", std::move(stack)));
    scroll_.setContent(std::move(cols));
    syncValues();
}

void KitsPanel::syncValues()
{
    const auto b = processor.bank();
    if (!b) return;
    const bank::Kit& kit = b->kits[size_t(slot_ - 1)];
    const double rate = bank::sampleRateForPeriod(kit.period);
    if (samples_) {
        std::vector<SampleList::Row> rows;
        for (const auto& s : kit.samples) rows.push_back({ ValueFormat::noteName(s.note), String(s.name), seconds(s.data.size(), rate) });
        samples_->set(std::move(rows), sample_);
    }
    const bank::KitSample* s = sample_ >= 0 && sample_ < int(kit.samples.size()) ? &kit.samples[size_t(sample_)] : nullptr;
    if (name_ && s && name_->text() != String(s->name)) name_->setText(String(s->name));
    if (note_) note_->setValue(s ? s->note : 60, dontSendNotification);
    if (loopPoint_) {
        loopPoint_->setRange(0, s ? int(std::min<size_t>(s->data.size(), 1u << 20)) : 0, 0);
        loopPoint_->setValue(s ? int(std::min<uint32_t>(s->loopPoint, 1u << 20)) : 0, dontSendNotification);
    }
    if (rate_) rate_->setValue(std::clamp(int(kit.used ? kit.period : uint16_t(1865)), kMinPeriod, kMaxPeriod), dontSendNotification);
    if (loop_) loop_->setSelected(int(kit.loop), dontSendNotification);
    if (info_) info_->setText(s ? withThousands(int(s->data.size())) + " smp" + middot() + seconds(s->data.size(), rate) : String(CharPointer_UTF8("\xe2\x80\x94")));
    if (preview_) preview_->set(b, slot_, sample_);
    scroll_.relayout();
}

void KitsPanel::editKit(const String& what, const std::function<void(bank::Kit&)>& fn)
{
    const int slot = slot_;
    processor.editBank("Kit " + ValueFormat::number(slot) + " " + what, [&fn, slot](bank::Bank& b) {
        auto& k = b.kits[size_t(slot - 1)];
        if (!k.used) { k.used = true; if (k.name.empty()) k.name = ("Kit " + String(slot)).toStdString(); }
        fn(k);
    });
    selfBank_ = processor.bank().get();
    syncValues();
    rebuildList();
    contextChanged();
}

void KitsPanel::importSamples()
{
    const auto b = processor.bank();
    const bank::Kit* kit = b ? b->kit(slot_) : nullptr;
    const uint16_t period = kit ? kit->period : uint16_t(1865);
    const int slot = slot_;
    Component::SafePointer<KitsPanel> safe(this);
    chooseAndImportKitSamples(this, period, [safe, slot](KitImportResult r) {
        if (safe == nullptr) return;
        if (!r.ok) { AlertWindow::showMessageBoxAsync(MessageBoxIconType::WarningIcon, "ChipBoy", r.error.isEmpty() ? String("The file could not be imported.") : r.error); return; }
        safe->appendSample(slot, r.sample);
    });
}

void KitsPanel::appendSample(int slot, const bank::KitSample& sample)
{
    const auto b = processor.bank();
    if (!b) return;
    if (b->kits[size_t(slot - 1)].samples.size() >= size_t(bank::kMaxKitSamples)) {
        AlertWindow::showMessageBoxAsync(MessageBoxIconType::InfoIcon, "ChipBoy", "A kit holds 32 samples; this one is full.");
        return;
    }
    processor.editBank("Kit " + ValueFormat::number(slot) + " sample " + String(sample.name), [slot, sample](bank::Bank& bk) {
        auto& k = bk.kits[size_t(slot - 1)];
        if (!k.used) { k.used = true; if (k.name.empty()) k.name = ("Kit " + String(slot)).toStdString(); }
        if (k.samples.size() >= size_t(bank::kMaxKitSamples)) return;
        int note = 35;
        for (const auto& s : k.samples) note = std::max(note, int(s.note));
        bank::KitSample s = sample;
        s.note = uint8_t(std::min(127, note + 1));
        k.samples.push_back(std::move(s));
    });
    selfBank_ = processor.bank().get();
    if (slot == slot_) { sample_ = int(processor.bank()->kits[size_t(slot - 1)].samples.size()) - 1; rebuildContent(); }
    rebuildList();
    contextChanged();
}

void KitsPanel::bankChanged()
{
    const auto b = processor.bank();
    rebuildList();
    if (!b) return;
    const bank::Kit* kit = b->kit(slot_);
    const int count = kit ? int(kit->samples.size()) : 0;
    if (count != builtCount_ || slot_ != builtSlot_) rebuildContent();
    else if (b.get() != selfBank_) syncValues();
    contextChanged();
}

void KitsPanel::hexChanged()
{
    lastRows_.clear();
    rebuildList();
    syncValues();
    repaint();
}

void KitsPanel::resized()
{
    auto area = getLocalBounds();
    auto left = area.removeFromLeft(kListWidth);
    auto head = left.removeFromTop(kListHeader);
    importBtn_.setBounds(head.removeFromRight(70).reduced(2, 3));
    listTitle_.setBounds(head.withTrimmedLeft(8));
    list_.setBounds(left);
    area.removeFromLeft(kGap);
    scroll_.setBounds(area);
}

} // namespace chipboy::plugin
