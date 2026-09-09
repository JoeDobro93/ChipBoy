#include "plugin/main/panels/TablesPanel.h"

namespace chipboy::plugin {

using namespace juce;
using namespace chipboy::ui;

namespace {
constexpr int kListWidth = 220, kGap = 14, kListHeader = 28, kTopRow = 30;
String middot() { return String(CharPointer_UTF8(" \xc2\xb7 ")); }
}

/// "At the end" behaviour, the hop step, and the help text (the mockup's .end-row).
class TablesPanel::EndRow : public Block {
public:
    EndRow()
        : label("At the end", Fonts::caption(10.0f), colours::textDim),
          end({ "Loop", "Hop to step", "Stop and hold" }),
          help(RichText("Type a value; Backspace blanks it."))
    {
        label.setUpperCase(true);
        end.setMini(true);
        end.setTooltip("Loop runs the 16 steps again; Hop jumps to a step; Stop holds the last values");
        hop.setRange(1, bank::kTableSteps, 1);
        hop.setTooltip("The step the table hops to");
        addAndMakeVisible(label);
        addAndMakeVisible(end);
        addAndMakeVisible(hop);
        addAndMakeVisible(help);
    }
    int preferredHeight(int width) override
    {
        const int helpW = std::min(300, std::max(120, width - 380));
        return std::max(26, help.preferredHeight(helpW));
    }
    void resized() override
    {
        const int h = getHeight();
        label.setBounds(0, 0, 84, 26);
        end.setBounds(90, (26 - end.preferredHeight()) / 2, end.preferredWidth(), end.preferredHeight());
        const int hx = 90 + end.preferredWidth() + 10;
        hop.setBounds(hx, 1, hop.preferredWidth(), Stepper::kHeight);
        const int helpW = std::min(300, std::max(120, getWidth() - 380));
        help.setBounds(getWidth() - helpW, 0, helpW, h);
    }
    TextLine label;
    Segmented end;
    Stepper hop;
    HelpText help;
};

TablesPanel::TablesPanel(ChipBoyProcessor& p)
    : EditorPanel(p),
      listTitle_("Tables" + middot() + "64 slots", Fonts::sans(11.0f), colours::textMute),
      newBtn_("New"),
      used_({}, Fonts::sans(12.0f), colours::textMute),
      stepRateLabel_("Step rate", Fonts::caption(10.0f), colours::textDim),
      stepRate_({}, Fonts::mono(12.0f), colours::text)
{
    stepRateLabel_.setUpperCase(true);
    for (auto* c : std::initializer_list<Component*>{ &list_, &listTitle_, &newBtn_, &name_, &used_, &stepRateLabel_, &stepRate_, &scroll_ }) addAndMakeVisible(c);
    list_.onSelect = [this](int slot) { showSlot(slot); };
    list_.onRename = [this](int slot, const String& n) {
        const int s = std::clamp(slot, 1, bank::kTableSlots);
        processor.editBank("Table " + ValueFormat::slot(s) + " named " + n, [s, n](bank::Bank& b) { auto& t = b.tables[size_t(s - 1)]; t.used = true; t.name = n.toStdString(); });
        selfBank_ = processor.bank().get();
        if (s == slot_) name_.setText(n);
        rebuildList();
        contextChanged();
    };
    newBtn_.setTooltip("New table in the first empty slot");
    newBtn_.onClick = [this] {
        const auto b = processor.bank();
        if (!b) return;
        int slot = 0;
        for (int k = 0; k < bank::kTableSlots; ++k) if (!b->tables[size_t(k)].used) { slot = k + 1; break; }
        if (slot == 0) return;
        processor.editBank("New table " + ValueFormat::slot(slot), [slot](bank::Bank& bk) { auto& t = bk.tables[size_t(slot - 1)]; t = bank::Table{}; t.used = true; t.name = ("Table " + String(slot)).toStdString(); });
        selfBank_ = processor.bank().get();
        rebuildList();
        showSlot(slot);
    };
    name_.onChange = [this](const String& n) { editTable("named " + n, [n](bank::Table& t) { t.name = n.toStdString(); }, false); };

    auto stack = std::make_unique<Stack>(10);
    stack->add(std::make_unique<Hold>(grid_, TableGrid::preferredHeight()));
    auto end = std::make_unique<EndRow>();
    endRow_ = end.get();
    end->end.onChange = [this](int i) {
        editTable("end", [i](bank::Table& t) { t.end = bank::TableEnd(std::clamp(i, 0, 2)); }, true);
        endRow_->hop.setVisible(i == 1);
    };
    end->hop.onChange = [this](int v) { editTable("hop step " + ValueFormat::index(v - 1), [v](bank::Table& t) { t.hopStep = uint8_t(std::clamp(v, 1, 16)); }, true); };
    stack->add(std::move(end));
    scroll_.setContent(std::move(stack));
    grid_.onChange = [this](const bank::Table& t) { const auto steps = t.steps; editTable("steps", [steps](bank::Table& tb) { tb.steps = steps; }, false); };

    rebuildList();
    syncFromBank(true);
}

TablesPanel::~TablesPanel() = default;

RichText TablesPanel::contextLine() const
{
    RichText r;
    const auto b = processor.bank();
    const bank::Table* t = b ? b->table(slot_) : nullptr;
    r.plain("Table ").bold(t ? slotAndName(slot_, t->name) : slotAndName(slot_, "empty"));
    if (b) {
        const int n = usedBy(*b, slot_);
        r.plain(middot() + (n == 0 ? String("not used yet") : "used by " + String(n) + (n == 1 ? " instrument" : " instruments")));
    }
    return r;
}

int TablesPanel::usedBy(const bank::Bank& b, int slot)
{
    int n = 0;
    for (const auto& i : b.instruments) if (i.used && int(i.table) == slot) ++n;
    return n;
}

String TablesPanel::stepRateText() const
{
    // Ticks are always 24 per beat (docs/COMMANDS_AND_TEMPO.md section 4).
    return "1 per tick (24 per beat, " + String(int(std::lround(processor.effectiveTempo()))) + " BPM)";
}

void TablesPanel::rebuildList()
{
    const auto b = processor.bank();
    if (!b) return;
    std::vector<SlotRow> rows;
    rows.resize(size_t(bank::kTableSlots));
    for (int k = 0; k < bank::kTableSlots; ++k) {
        const auto& t = b->tables[size_t(k)];
        auto& r = rows[size_t(k)];
        r.slot = k + 1;
        r.used = t.used;
        r.name = t.used ? String(t.name) : String();
        const int n = t.used ? usedBy(*b, k + 1) : 0;
        r.note = n > 0 ? String(n) + (n == 1 ? " use" : " uses") : String();
    }
    bool same = rows.size() == lastRows_.size();
    for (size_t k = 0; same && k < rows.size(); ++k)
        same = rows[k].used == lastRows_[k].used && rows[k].name == lastRows_[k].name && rows[k].note == lastRows_[k].note;
    if (!same) { lastRows_ = rows; list_.setRows(rows); }
    if (list_.selected() != slot_) list_.setSelected(slot_, dontSendNotification);
}

void TablesPanel::showSlot(int slot)
{
    slot_ = std::clamp(slot, 1, bank::kTableSlots);
    if (list_.selected() != slot_) list_.setSelected(slot_, dontSendNotification);
    syncFromBank(true);
    contextChanged();
}

void TablesPanel::syncFromBank(bool pushToGrid)
{
    const auto b = processor.bank();
    if (!b) return;
    const bank::Table& t = b->tables[size_t(slot_ - 1)];
    const String n = t.used ? String(t.name) : String();
    if (name_.text() != n) name_.setText(n);
    const int uses = usedBy(*b, slot_);
    String line = !t.used ? String("Empty slot. Editing it creates the table.")
                          : uses == 0 ? String("Not used yet.")
                                      : "Used by " + String(uses) + (uses == 1 ? " instrument" : " instruments");
    // Which channel's run the lit row belongs to (section 32).
    if (playingChannel_ >= 0) line += middot() + String(colours::channelName(playingChannel_)) + " is running it";
    used_.setText(line);
    if (pushToGrid) grid_.setTable(t);
    if (endRow_) {
        endRow_->end.setSelected(int(t.end), dontSendNotification);
        endRow_->hop.setValue(t.hopStep, dontSendNotification);
        endRow_->hop.setVisible(t.end == bank::TableEnd::Hop);
    }
    stepRate_.setText(stepRateText());
}

void TablesPanel::editTable(const String& what, const std::function<void(bank::Table&)>& fn, bool pushToGrid)
{
    const int slot = slot_;
    processor.editBank("Table " + ValueFormat::slot(slot) + " " + what, [&fn, slot](bank::Bank& b) {
        auto& t = b.tables[size_t(slot - 1)];
        if (!t.used) { t.used = true; if (t.name.empty()) t.name = ("Table " + String(slot)).toStdString(); }
        fn(t);
    });
    selfBank_ = processor.bank().get();
    syncFromBank(pushToGrid);
    rebuildList();
    contextChanged();
}

void TablesPanel::bankChanged()
{
    const auto b = processor.bank();
    rebuildList();
    if (b && b.get() != selfBank_) syncFromBank(true);
    contextChanged();
}

void TablesPanel::hexChanged()
{
    lastRows_.clear();
    rebuildList();
    grid_.repaint();
    repaint();
}

/// The running row of the table on view (docs/COMMANDS_AND_TEMPO.md 32).
/// Two channels can run the same table; the newer run wins, and the serial
/// wraps, so "newer" is a signed difference rather than a comparison.
void TablesPanel::tick()
{
    stepRate_.setText(stepRateText());
    int row = -1, from = -1;
    uint32_t newest = 0;
    for (int ch = 0; ch < 4; ++ch) {
        int slot = 0, r = -1;
        uint32_t run = 0;
        unpackTableRun(processor.scopes().tableRun[size_t(ch)].load(std::memory_order_relaxed), slot, r, run);
        if (r < 0 || slot != slot_) continue;
        if (from < 0 || int16_t(uint16_t(run) - uint16_t(newest)) > 0) { newest = run; row = r; from = ch; }
    }
    if (row == playingRow_ && from == playingChannel_) return;
    playingRow_ = row;
    playingChannel_ = from;
    grid_.setPlayingStep(row);
    syncFromBank(false);
}

void TablesPanel::resized()
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
    top.removeFromLeft(10);
    stepRate_.setBounds(top.removeFromRight(250));
    stepRateLabel_.setBounds(top.removeFromRight(70));
    used_.setBounds(top);
    area.removeFromTop(6);
    scroll_.setBounds(area);
}

} // namespace chipboy::plugin
