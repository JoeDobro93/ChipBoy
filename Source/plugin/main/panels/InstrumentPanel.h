// ChipBoy -- the Instrument tab (UI_DESIGN section 6,
// docs/COMMANDS_AND_TEMPO.md section 29): the 128 slots on the left and a
// compact form on the right -- labels down one column, controls down the
// other, a thin caption over each group and no card chrome. The envelope is
// a small graph over its fields, the chip's ramp or the shaped ADSR with its
// curves; what a field means is its tooltip, so the panel itself is labels
// and values.
#pragma once

#include "plugin/main/panels/PanelCommon.h"

#include <array>
#include <memory>

namespace chipboy::plugin {

class InstrumentPanel : public EditorPanel {
public:
    explicit InstrumentPanel(ChipBoyProcessor& p);
    ~InstrumentPanel() override;

    void setChannel(int ch) override;
    void selectSlot(int slot) override { showSlot(slot); }
    void saveView(juce::ValueTree& v) const override { v.setProperty("slot", slot_, nullptr); }
    void restoreView(const juce::ValueTree& v) override { if (v.hasProperty("slot")) showSlot(int(v["slot"])); }
    RichText contextLine() const override;
    void bankChanged() override;
    void songChanged() override;
    void hexChanged() override;
    void tick() override;
    void resized() override;

private:
    struct Widgets;
    class EnvPreview;
    class HeadRow;

    void showSlot(int slot);                    ///< show the editor for a slot (a single click)
    void assignSlot(int slot);                  ///< show it and give it to the channel when it fits (double click, Assign)
    void refreshAssignButton();
    void rebuildList();
    void rebuildEditor();
    void syncValues();
    void updateUsedOn();
    void refreshDerived();               ///< the envelope preview and every hint that shows a value
    /// One edit of the shown instrument. `what` names it on the undo
    /// history ("Instrument 7 . Duty 25"); a run of edits under the same
    /// name is one undo (UI_DESIGN section 2.1).
    void edit(const juce::String& what, const std::function<void(bank::Instrument&)>& fn);
    void newInstrument();
    void duplicate();
    /// The bank's slots by name, for the right click every slot field takes
    /// (UI_DESIGN section 2.1).
    void showTableMenu();
    void showWaveMenu();
    void showKitMenu();
    /// The instrument with every table, wave and kit it uses, as a .cbi file
    /// (docs/COMMANDS_AND_TEMPO.md section 15).
    void savePreset();
    void loadPreset();
    static int firstEmptySlot(const bank::Bank& b);
    std::vector<int> computeUses(const bank::Bank& b, const tracker::Song* song) const;

    ui::SlotList list_;
    TextLine listTitle_;
    juce::TextButton newBtn_, dupBtn_, assignBtn_, savePresetBtn_, loadPresetBtn_;
    std::unique_ptr<juce::FileChooser> chooser_;
    ScrollBlock scroll_;
    std::unique_ptr<Widgets> w_;
    int slot_ = 1;
    int builtSlot_ = -1, builtType_ = -1, builtChannel_ = -1, builtEnvMode_ = -1;
    bool builtUsed_ = false;
    const bank::Bank* selfBank_ = nullptr;
    std::array<int, 4> lastChannelInst_{ { -1, -1, -1, -1 } };
    std::vector<ui::SlotRow> lastRows_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InstrumentPanel)
};

} // namespace chipboy::plugin
