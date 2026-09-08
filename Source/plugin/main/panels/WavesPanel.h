// ChipBoy -- the Waves tab (spec 9.7): 64 slots, up to 16 frames of 32
// samples by 16 levels, shape generators, interpolation between frames.
#pragma once

#include "plugin/main/panels/PanelCommon.h"

namespace chipboy::plugin {

class WavesPanel : public EditorPanel {
public:
    explicit WavesPanel(ChipBoyProcessor& p);
    ~WavesPanel() override;

    RichText contextLine() const override;
    void bankChanged() override;
    void hexChanged() override;
    void resized() override;

private:
    class FrameStrip;
    void rebuildList();
    void showSlot(int slot);
    void syncFromBank(bool pushToGrid);
    /// One edit of the shown wave; `what` names it on the undo history.
    void editWave(const juce::String& what, const std::function<void(bank::Wave&)>& fn, bool pushToGrid);
    void generate(int shape);
    void interpolate();

    ui::SlotList list_;
    TextLine listTitle_;
    juce::TextButton newBtn_;
    ui::NameField name_;
    TextLine frameLabel_, frameText_, shapeLabel_;
    ui::Segmented shape_;
    juce::TextButton interp_;
    ScrollBlock scroll_;
    ui::WaveGrid grid_;
    FrameStrip* frames_ = nullptr;
    int slot_ = 1, frame_ = 0;
    const bank::Bank* selfBank_ = nullptr;
    std::vector<ui::SlotRow> lastRows_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WavesPanel)
};

} // namespace chipboy::plugin
