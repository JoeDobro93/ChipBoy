// ChipBoy -- the Kits tab (spec 9.8): 32 slots of up to 32 one-shots with
// a note map, the playback rate quantised to the period register, the loop
// mode, and the 4-bit preview.
#pragma once

#include "plugin/main/panels/PanelCommon.h"

namespace chipboy::plugin {

class KitsPanel : public EditorPanel {
public:
    explicit KitsPanel(ChipBoyProcessor& p);
    ~KitsPanel() override;

    RichText contextLine() const override;
    void bankChanged() override;
    void hexChanged() override;
    void resized() override;

private:
    class SampleList;
    class Preview;
    void rebuildList();
    void showSlot(int slot);
    void rebuildContent();
    void syncValues();
    void editKit(const std::function<void(bank::Kit&)>& fn);
    void importSamples();
    void appendSample(int slot, const bank::KitSample& s);

    ui::SlotList list_;
    TextLine listTitle_;
    juce::TextButton importBtn_;
    ScrollBlock scroll_;
    SampleList* samples_ = nullptr;
    Preview* preview_ = nullptr;
    ui::NameField* name_ = nullptr;
    ui::Stepper* note_ = nullptr;
    ui::Stepper* loopPoint_ = nullptr;
    ui::Stepper* rate_ = nullptr;
    ui::Segmented* loop_ = nullptr;
    TextLine* info_ = nullptr;
    int slot_ = 1, sample_ = 0;
    int builtSlot_ = -1, builtCount_ = -1, builtSample_ = -1;
    const bank::Bank* selfBank_ = nullptr;
    std::vector<ui::SlotRow> lastRows_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KitsPanel)
};

} // namespace chipboy::plugin
