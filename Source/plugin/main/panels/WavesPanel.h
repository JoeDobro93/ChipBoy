// ChipBoy -- the Waves tab (spec 9.7): 64 slots, up to 16 frames of 32
// samples by 16 levels, drawn with the mouse.
//
// A wave's run can also be generated (docs/COMMANDS_AND_TEMPO.md section 33):
// the bank keeps a bank::Synth per slot -- a source, a chain of shapers, a
// start and an end state and the frames to morph between them -- and the
// panel edits it beside previews of the two ends and of the run. Generate
// writes the frames into the slot, undoably; the exporter still only ships
// frames.
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
    void bankChanged() override;
    void hexChanged() override;
    void resized() override;

private:
    class FrameStrip;
    class MiniWave;
    class RunStrip;
    class PartialsBar;
    struct SynthWidgets;

    void rebuildList();
    void showSlot(int slot);
    void syncFromBank(bool pushToGrid);
    /// One edit of the shown wave; `what` names it on the undo history.
    void editWave(const juce::String& what, const std::function<void(bank::Wave&)>& fn, bool pushToGrid);
    void generate(int shape);
    void interpolate();
    /// The synth section (section 33): built when the source changes, read
    /// back whenever the bank does.
    void buildSynth();
    void syncSynth();
    /// One edit of the shown wave's synth; `editEnd_` says which of the two
    /// states a value belongs to.
    void editSynth(const juce::String& what, const std::function<void(bank::Synth&)>& fn);
    void editState(const juce::String& what, const std::function<void(bank::SynthState&)>& fn);
    bank::Synth currentSynth() const;
    /// The frame the Drawn source reads: the one on show.
    bank::Frame drawnFrame() const;
    /// Generate: the run written into the slot, as one undo.
    void runSynth();

    ui::SlotList list_;
    TextLine listTitle_;
    juce::TextButton newBtn_;
    ui::NameField name_;
    TextLine frameLabel_, frameText_, shapeLabel_;
    ui::Segmented shape_;
    juce::TextButton interp_;
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
