// ChipBoy -- the Grooves tab (UI_DESIGN section 7): the song's sixteen
// grooves, listed on the left with their tick counts, and the editor on the
// right with the total, the swing and the nudge.
//
// A groove says how many ticks each step of a phrase lasts
// (docs/COMMANDS_AND_TEMPO.md section 9.2). It serves tables as well as
// phrases, so it has a tab of its own rather than a corner of the lane; the
// lane keeps the chip that says which groove a phrase runs on.
#pragma once

#include "plugin/main/panels/PanelCommon.h"
#include "plugin/ui/GrooveEditor.h"

namespace chipboy::plugin {

class GroovesPanel : public EditorPanel {
public:
    explicit GroovesPanel(ChipBoyProcessor& p);
    ~GroovesPanel() override;

    RichText contextLine() const override;
    void setChannel(int ch) override;
    void songChanged() override;
    void hexChanged() override;
    void tick() override;
    void resized() override;

private:
    void rebuildList();
    void showSlot(int slot);
    /// The groove slot in force on a channel: a G in a slot or a cell, else
    /// the phrase's own (docs/COMMANDS_AND_TEMPO.md section 9.2).
    int grooveInForce(int ch) const;
    /// Which slot the tab opens on: the lowest that is not straight, since a
    /// fresh song's slot 1 is 6 6 and shows nothing.
    int firstSwung() const;
    /// The groove a slot holds, summarised: "7 5", "4 4 4", "straight".
    juce::String ticksText(int slot) const;
    juce::String swingText(int slot) const;

    ui::SlotList list_;
    TextLine listTitle_;
    ui::GrooveEditor editor_;
    HelpText help_;
    int slot_ = 1;
    int inForce_ = -1;               ///< the last groove the selected channel had in force
    int lastStep_ = -2;
    std::vector<ui::SlotRow> lastRows_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GroovesPanel)
};

} // namespace chipboy::plugin
