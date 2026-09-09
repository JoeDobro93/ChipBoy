#include "plugin/main/panels/GroovesPanel.h"

namespace chipboy::plugin {

using namespace juce;
using namespace chipboy::ui;

namespace {
/// The list is the instrument tab's width, the editor takes a fixed column
/// beside it -- wide enough for the bars to show the swing -- and what is
/// left explains what a groove is (UI_DESIGN section 7).
constexpr int kListWidth = 220, kGap = 14, kListHeader = 28, kEditorWidth = 520, kHelpGap = 20;
/// The rows grow with the pane: sixteen of them and the 48 px head fill the
/// 512 the editor pane has at the window's minimum height.
constexpr int kMinRow = ui::GrooveEditor::kRowHeight, kMaxRow = 34;
String middot() { return String(CharPointer_UTF8(" \xc2\xb7 ")); }
/// Slot 0 is straight and read-only; the song's own are 1-16.
constexpr int kSlots = 16;
}

GroovesPanel::GroovesPanel(ChipBoyProcessor& p)
    : EditorPanel(p),
      listTitle_("Grooves" + middot() + "16 slots", Fonts::sans(11.0f), colours::textMute)
{
    addAndMakeVisible(list_);
    addAndMakeVisible(listTitle_);
    addAndMakeVisible(editor_);
    addAndMakeVisible(help_);
    list_.setRenameable(false);                 // a groove is its ticks; there is no name to type
    list_.onSelect = [this](int slot) { showSlot(slot); };
    editor_.onChange = [this](int slot, const tracker::Groove& g) {
        processor.editSong("Groove " + ValueFormat::number(slot), [slot, g](tracker::Song& s) { if (slot >= 1 && slot <= int(s.grooves.size())) s.grooves[size_t(slot - 1)] = g; });
        rebuildList();
        contextChanged();
    };
    // A drag down one cell, or a value typed into it, is one undo.
    editor_.onGesture = [this](bool begin) {
        if (begin) processor.history().beginGesture("Groove " + ValueFormat::number(slot_));
        else processor.history().endGesture();
    };
    // The stepper in the editor's head and the list are one selection.
    editor_.onSlotChange = [this](int slot) { slot_ = slot; list_.setSelected(slot, dontSendNotification); contextChanged(); };

    const String em = String(CharPointer_UTF8("\xe2\x80\x94"));
    RichText h;
    h.bold("A groove").plain(" is sixteen tick counts. Step ").bold("i").plain(" of a phrase lasts ").bold("ticks[i mod length]")
     .plain(" ticks, so two entries swing, three make triplets, and one holds every step the same. Straight is six ticks a step, and a 4/4 bar of sixteen steps is 96.")
     .plain("\n\nEach row's bar is drawn against the longest entry, so the swing shows without arithmetic; the number on the right is the tick that step starts on, in the warn colour when it falls at or past the end of the bar and the step never fires.")
     .plain("\n\nThe head has the slot on show " + em + " 0 is straight and cannot be edited " + em + " the total against the bar's ticks, the swing the first pair makes, and a ")
     .bold("nudge").plain(" that moves one tick between the entries of every pair: 6 6, 7 5, 8 4.")
     .plain("\n\nGrooves serve tables as well as phrases. Which one a phrase runs on is the chip in the lane's head, over in the Tracker tab; a ")
     .bold("G").plain(" in a cell or a command slot overrides it.");
    help_.setText(h);

    editor_.setSong(processor.song());
    editor_.setRowTicks(rowTicks());
    rebuildList();
    showSlot(firstSwung());
    list_.setSelected(slot_, dontSendNotification);
}

GroovesPanel::~GroovesPanel() = default;

String GroovesPanel::ticksText(int slot) const
{
    const auto s = processor.song();
    if (slot <= 0 || !s || slot > int(s->grooves.size())) return "straight";
    const auto& g = s->grooves[size_t(slot - 1)];
    const int n = g.length();
    String t;
    for (int i = 0; i < std::min<int>(n, 8); ++i) t += (i ? " " : "") + String(int(g.ticks[size_t(i)]));
    if (n > 8) t += String(CharPointer_UTF8(" \xe2\x80\xa6"));
    return t;
}

int GroovesPanel::firstSwung() const
{
    const auto s = processor.song();
    if (!s) return 1;
    for (int slot = 1; slot <= kSlots; ++slot) {
        const auto& g = s->grooves[size_t(slot - 1)];
        for (int i = 0; i < g.length(); ++i) if (g.ticks[size_t(i)] != 6) return slot;
    }
    return 1;
}

/// What the first pair makes of the two, as the editor's head shows it.
String GroovesPanel::swingText(int slot) const
{
    const auto s = processor.song();
    if (slot <= 0 || !s || slot > int(s->grooves.size())) return {};
    const auto& g = s->grooves[size_t(slot - 1)];
    const int n = g.length();
    bool straight = true;
    for (int i = 0; i < n; ++i) if (g.ticks[size_t(i)] != 6) straight = false;
    if (straight) return "straight";
    if (n < 2) return {};
    const int a = int(g.ticks[0]), b = int(g.ticks[1]);
    return a + b > 0 ? String(100 * a / (a + b)) + " %" : String();
}

void GroovesPanel::rebuildList()
{
    std::vector<SlotRow> rows;
    rows.reserve(size_t(kSlots) + 1);
    rows.push_back({ 0, "straight", true, -1, "6 6, fixed" });
    for (int slot = 1; slot <= kSlots; ++slot) rows.push_back({ slot, ticksText(slot), true, -1, swingText(slot) });
    bool same = rows.size() == lastRows_.size();
    for (size_t k = 0; same && k < rows.size(); ++k)
        same = rows[k].slot == lastRows_[k].slot && rows[k].name == lastRows_[k].name && rows[k].note == lastRows_[k].note;
    if (same) return;
    lastRows_ = rows;
    list_.setRows(rows);
    list_.setSelected(slot_, dontSendNotification);
}

int GroovesPanel::grooveInForce(int ch) const
{
    const auto s = processor.song();
    if (!s) return 0;
    const uint8_t slot = processor.player().groove(ch);
    if (slot != tracker::kGrooveNone) return int(slot);
    const auto* p = s->phrase(s->phraseAt(ch, trackerPosition(processor).row[ch & 3]));
    return p != nullptr ? int(p->groove) : 0;
}

void GroovesPanel::setChannel(int ch)
{
    EditorPanel::setChannel(ch);
    inForce_ = -1;                   // picking a channel shows its groove again
}

void GroovesPanel::showSlot(int slot)
{
    slot_ = std::clamp(slot, 0, kSlots);
    editor_.setSlot(slot_);
    if (list_.selected() != slot_) list_.setSelected(slot_, juce::dontSendNotification);
    contextChanged();
}

RichText GroovesPanel::contextLine() const
{
    RichText r;
    r.plain("Editing groove ").bold(slot_ == 0 ? String("0 straight") : ValueFormat::number(slot_));
    if (slot_ > 0) r.plain(middot()).bold(ticksText(slot_)).plain(" ticks a step");
    else r.plain(" " + String(CharPointer_UTF8("\xe2\x80\x94")) + " six ticks a step, and not editable");
    return r;
}

void GroovesPanel::songChanged()
{
    editor_.setSong(processor.song());
    rebuildList();
    contextChanged();
}

void GroovesPanel::hexChanged()
{
    lastRows_.clear();
    rebuildList();
    repaint();
}

/// The ticks the selected channel's phrase would take at the straight groove:
/// what the editor measures a groove's total against (section 25).
int GroovesPanel::rowTicks() const
{
    const auto s = processor.song();
    if (!s) return tracker::kEmptyRowTicks;
    const int ch = channel & 3;
    return tracker::kTicksPerStep * s->stepsOfRow(ch, trackerPosition(processor).row[size_t(ch)]);
}

void GroovesPanel::tick()
{
    editor_.setRowTicks(rowTicks());
    // The row the selected channel is really playing, when it is playing the
    // groove on show; the editor's rows are a phrase's steps.
    // The editor follows the groove in force for the selected channel until
    // the list or the stepper browses elsewhere, as it did beside the lane;
    // groove 0 cannot be edited, so it never lands there by itself.
    const int inForce = grooveInForce(channel);
    if (inForce != inForce_) {
        inForce_ = inForce;
        if (inForce >= 1 && inForce != slot_) { showSlot(inForce); list_.setSelected(inForce, dontSendNotification); }
    }
    const auto s = processor.song();
    const auto at = trackerPosition(processor);
    int step = -1;
    if (s && at.playing && inForce == slot_) step = playingStepOf(processor, *s, channel, at.row[channel & 3], at.inRow[channel & 3]);
    if (step != lastStep_) { lastStep_ = step; editor_.setPlayingStep(step); }
}

void GroovesPanel::resized()
{
    auto area = getLocalBounds();
    auto left = area.removeFromLeft(kListWidth);
    listTitle_.setBounds(left.removeFromTop(kListHeader).withTrimmedLeft(8));
    list_.setBounds(left.withTrimmedTop(4));
    area.removeFromLeft(kGap);

    const int rowH = std::clamp((area.getHeight() - GrooveEditor::kHeaderHeight) / tracker::kGrooveSteps, kMinRow, kMaxRow);
    editor_.setRowHeight(rowH);
    editor_.setBounds(area.removeFromLeft(std::min<int>(kEditorWidth, area.getWidth())).withHeight(std::min<int>(area.getHeight(), GrooveEditor::heightForRows(rowH))));
    area.removeFromLeft(kHelpGap);
    help_.setBounds(area.withHeight(std::min<int>(area.getHeight(), help_.preferredHeight(area.getWidth()))));
}

} // namespace chipboy::plugin
