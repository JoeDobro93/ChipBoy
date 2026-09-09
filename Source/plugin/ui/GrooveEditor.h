// ChipBoy -- the groove editor (docs/COMMANDS_AND_TEMPO.md sections 9.2 and
// 10; UI_DESIGN section 7).
//
// A groove is sixteen tick counts, so the editor is sixteen cells in a
// column: the value a step lasts, a bar drawn to scale so the swing is
// visible, and the tick that step starts on. The head carries the slot the
// editor is browsing, the total against the bar's ticks and the swing the
// first pair makes, with a nudge that trades one tick between the entries of
// every pair. It lives in the Grooves tab, beside the sixteen slots, where
// it takes whatever width and row height the tab gives it.
//
// The song is the only copy: the editor reads the groove through the song
// it was given and reports an edited one, exactly as the phrase grid
// reports a cell. The wheel scrolls the tab, never a value
// (UI_DESIGN section 2.1).
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
    /// What the groove's total is measured against: the ticks the phrase it
    /// is running on would take at the straight groove (section 25).
    void setRowTicks(int ticks);
    void setPlayingStep(int step);   ///< -1 none
    /// Taller rows where there is room for them (the Grooves tab).
    void setRowHeight(int px);

    /// An edited groove, for the panel to write into the song.
    std::function<void(int slot, const tracker::Groove&)> onChange;
    /// The head's stepper browsed to another slot.
    std::function<void(int slot)> onSlotChange;
    /// A drag or a typed value is one edit: this brackets it, so the panel
    /// can put the whole gesture on the undo history as one (UI_DESIGN 2.1).
    std::function<void(bool begin)> onGesture;

    juce::String getTooltip() override;
    /// kRowHeight is the smallest row (the lane's rhythm) and kWidth the
    /// width the editor starts at; the Grooves tab gives it more of both.
    static constexpr int kRowHeight = 22, kHeaderHeight = 48, kWidth = 164;
    static constexpr int heightForRows(int rowHeight) { return kHeaderHeight + tracker::kGrooveSteps * rowHeight; }
    int preferredHeight() const;

    void resized() override; void paint(juce::Graphics&) override;
    void mouseMove(const juce::MouseEvent&) override; void mouseExit(const juce::MouseEvent&) override; void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override; void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override; void focusGained(FocusChangeType) override; void focusLost(FocusChangeType) override;

private:
    struct Impl; std::unique_ptr<Impl> impl_;
};

} // namespace chipboy::ui
