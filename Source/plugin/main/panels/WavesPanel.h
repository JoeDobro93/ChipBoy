// ChipBoy -- the Waves tab (spec 9.7): 64 slots, up to 16 frames of 32
// samples by 16 levels, drawn with the mouse as bars or as points on a grid
// (docs/COMMANDS_AND_TEMPO.md section 36).
//
// A wave's run can also be generated (section 33, amended in 36): the bank
// keeps a bank::Synth per slot -- a chain of shapers, a start and an end
// state each with its own shape, and the frames of the slot the run goes
// into -- and the panel edits it beside previews of the two ends and of the
// run. Generate writes the run into the slot's frames, undoably; the
// exporter still only ships frames.
#pragma once

#include "core/Bank/WaveSynth.h"
#include "plugin/main/panels/PanelCommon.h"

namespace chipboy::plugin {

class WavesPanel : public EditorPanel {
public:
    explicit WavesPanel(ChipBoyProcessor& p);
    ~WavesPanel() override;

    RichText contextLine() const override;
    void selectSlot(int slot) override { showSlot(slot); }
    void saveView(juce::ValueTree& v) const override;
    void restoreView(const juce::ValueTree& v) override;
    void bankChanged() override;
    void hexChanged() override;
    void resized() override;

private:
    class FrameStrip;
    class ToolsRow;
    class MiniWave;
    class RunStrip;
    class PartialsBar;
    class ShaperRow;
    struct SynthWidgets;

    void rebuildList();
    void showSlot(int slot);
    void syncFromBank(bool pushToGrid);
    /// One edit of the shown wave; `what` names it on the undo history.
    void editWave(const juce::String& what, const std::function<void(bank::Wave&)>& fn, bool pushToGrid);
    void generate(int shape);
    void interpolate();
    /// Import...: an audio file read as one cycle into the frame on show (section 40).
    void importWave();
    /// The synth section (sections 33 and 36): built when the edited end's
    /// shape changes, read back whenever the bank does.
    void buildSynth();
    void rebuildSynthLater();
    void syncSynth();
    /// One edit of the shown wave's synth; `editEnd_` says which of the two
    /// states a value belongs to.
    void editSynth(const juce::String& what, const std::function<void(bank::Synth&)>& fn);
    void editState(const juce::String& what, const std::function<void(bank::SynthState&)>& fn);
    bank::Synth currentSynth() const;
    /// The frame the Drawn shape reads: the one on show.
    bank::Frame drawnFrame() const;
    /// Generate: the run written into the slot's frames From..To, as one undo.
    void runSynth();

    ui::SlotList list_;
    TextLine listTitle_;
    juce::TextButton newBtn_;
    ui::NameField name_;
    TextLine frameLabel_, frameText_;
    ui::Segmented shape_, view_;
    juce::TextButton interp_, import_;
    ScrollBlock scroll_, synthScroll_;
    ui::WaveGrid grid_;
    FrameStrip* frames_ = nullptr;
    std::unique_ptr<SynthWidgets> sw_;
    int slot_ = 1, frame_ = 0;
    int editEnd_ = 0;                    ///< 0 = the start state, 1 = the end
    int builtSource_ = -1;
    const bank::Bank* selfBank_ = nullptr;
    std::vector<ui::SlotRow> lastRows_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WavesPanel)
};

} // namespace chipboy::plugin
