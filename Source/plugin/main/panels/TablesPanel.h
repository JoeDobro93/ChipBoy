// ChipBoy -- the Tables tab (spec 9.5): 64 slots, 16 steps of volume,
// transpose and two commands, the end behaviour, shared by every
// instrument that references the table.
//
// While a table runs, the row it is on is lit
// (docs/COMMANDS_AND_TEMPO.md section 32): the driver publishes the slot,
// the row and a run serial per channel, and the panel follows, for the table
// on view, the channel whose run started last. Nothing is lit when no
// channel is running it.
#pragma once

#include "plugin/main/panels/PanelCommon.h"

namespace chipboy::plugin {

class TablesPanel : public EditorPanel {
public:
    explicit TablesPanel(ChipBoyProcessor& p);
    ~TablesPanel() override;

    RichText contextLine() const override;
    void selectSlot(int slot) override { showSlot(slot); }
    void saveView(juce::ValueTree& v) const override { v.setProperty("slot", slot_, nullptr); }
    void restoreView(const juce::ValueTree& v) override { if (v.hasProperty("slot")) showSlot(int(v["slot"])); }
    void bankChanged() override;
    void hexChanged() override;
    void tick() override;
    void resized() override;

private:
    class EndRow;
    void rebuildList();
    void showSlot(int slot);
    void syncFromBank(bool pushToGrid);
    /// One edit of the shown table; `what` names it on the undo history.
    void editTable(const juce::String& what, const std::function<void(bank::Table&)>& fn, bool pushToGrid);
    static int usedBy(const bank::Bank& b, int slot);
    juce::String stepRateText() const;

    ui::SlotList list_;
    TextLine listTitle_;
    juce::TextButton newBtn_;
    ui::NameField name_;
    TextLine used_, stepRateLabel_, stepRate_;
    ScrollBlock scroll_;
    ui::TableGrid grid_;
    EndRow* endRow_ = nullptr;
    int slot_ = 1;
    /// The run the panel is following: the row, and the channel it belongs
    /// to (-1 for none).
    int playingRow_ = -1, playingChannel_ = -1;
    /// The volume lane's row and the second command lane's: they run on their
    /// own pointers (docs/COMMANDS_AND_TEMPO.md section 64).
    int playingRowE_ = -1, playingRow2_ = -1;
    const bank::Bank* selfBank_ = nullptr;
    std::vector<ui::SlotRow> lastRows_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TablesPanel)
};

} // namespace chipboy::plugin
