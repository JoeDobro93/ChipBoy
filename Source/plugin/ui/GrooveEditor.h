// ChipBoy -- the groove editor (docs/COMMANDS_AND_TEMPO.md sections 9.2 and
// 10; UI_DESIGN section 7).
//
// A groove is sixteen tick counts, so the editor is sixteen cells in a
// column beside the phrase grid, row for row with its steps: the value a
// step lasts, a bar drawn to scale so the swing is visible, and the tick
// that step starts on. The head carries the slot the editor is browsing,
// the total against the bar's ticks and the swing the first pair makes,
// with a nudge that trades one tick between the entries of every pair.
//
// The song is the only copy: the editor reads the groove through the song
// it was given and reports an edited one, exactly as the phrase grid
// reports a cell.
#pragma once

#include "core/Tracker/Song.h"
#include "plugin/ui/Widgets.h"

#include <functional>
#include <memory>

namespace chipboy::ui {

class GrooveEditor : public juce::Component, public juce::TooltipClient {
public:
    GrooveEditor();
    ~GrooveEditor() override;

    void setSong(std::shared_ptr<const tracker::Song> song);
    /// 0 is straight and not editable (section 9.2); 1-16 are the song's.
    void setSlot(int slot);
    int slot() const;
    /// What the total is measured against: the bar's ticks, from the processor.
    void setBarTicks(int ticks);
    void setPlayingStep(int step);   ///< -1 none

    /// An edited groove, for the panel to write into the song.
    std::function<void(int slot, const tracker::Groove&)> onChange;

    juce::String getTooltip() override;
    static constexpr int kRowHeight = 22, kHeaderHeight = 48, kWidth = 164;
    static constexpr int preferredHeight() { return kHeaderHeight + tracker::kSteps * kRowHeight; }

    void resized() override; void paint(juce::Graphics&) override;
    void mouseMove(const juce::MouseEvent&) override; void mouseExit(const juce::MouseEvent&) override; void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override; void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed(const juce::KeyPress&) override; void focusGained(FocusChangeType) override; void focusLost(FocusChangeType) override;

private:
    struct Impl; std::unique_ptr<Impl> impl_;
};

} // namespace chipboy::ui
