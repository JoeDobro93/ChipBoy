// ChipBoy -- the Instrument tab (UI_DESIGN section 6, the mockup's
// instEditor): the 128 slots on the left, the field groups of the selected
// instrument's type on the right, every field naming the register it lands in.
#pragma once

#include "plugin/main/panels/PanelCommon.h"

#include <array>

namespace chipboy::plugin {

class InstrumentPanel : public EditorPanel {
public:
    explicit InstrumentPanel(ChipBoyProcessor& p);
    ~InstrumentPanel() override;

    void setChannel(int ch) override;
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
    void edit(const std::function<void(bank::Instrument&)>& fn);
    void newInstrument();
    void duplicate();
    static int firstEmptySlot(const bank::Bank& b);
    std::vector<int> computeUses(const bank::Bank& b, const tracker::Song* song) const;

    ui::SlotList list_;
    TextLine listTitle_;
    juce::TextButton newBtn_, dupBtn_, assignBtn_;
    ScrollBlock scroll_;
    std::unique_ptr<Widgets> w_;
    int slot_ = 1;
    int builtSlot_ = -1, builtType_ = -1, builtChannel_ = -1;
    bool builtUsed_ = false;
    const bank::Bank* selfBank_ = nullptr;
    std::array<int, 4> lastChannelInst_{ { -1, -1, -1, -1 } };
    std::vector<ui::SlotRow> lastRows_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(InstrumentPanel)
};

} // namespace chipboy::plugin
