// ChipBoy -- the MIDI tab (UI_DESIGN D-UI-43, docs/COMMANDS_AND_TEMPO.md
// section 225): the port's sixteen MIDI channels, each assigned to one of the
// four, and within each the velocity regions -- tracker rows without the
// note -- that say what a note at that velocity loads and fires.
//
// The map is the song's (`Song::midiMap`): saved with it, undone with it.
// With the map off the strips' Source menus route as they always did.
#pragma once

#include "plugin/main/panels/MainCommon.h"

#include <memory>
#include <vector>

namespace chipboy::plugin {

class MidiMapPanel : public MainPanel {
public:
    explicit MidiMapPanel(ChipBoyProcessor& p);
    ~MidiMapPanel() override;

    RichText contextLine() const override;
    void bankChanged() override;
    void songChanged() override;
    void hexChanged() override;
    void saveView(juce::ValueTree& v) const override { v.setProperty("midiChannel", midiCh_, nullptr); }
    void restoreView(const juce::ValueTree& v) override { if (v.hasProperty("midiChannel")) select(int(v["midiChannel"])); }
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    /// The velocity bar over the grid: the regions across 1-127, a
    /// boundary dragged to move a region's `from` (D-UI-43).
    class VelocityBar;
    void rebuildList();
    void select(int midiCh);          ///< 1-16
    void refresh();
    /// One edit of the map, through the song's undo history.
    void editMap(const juce::String& what, const std::function<void(tracker::MidiMap&)>& fn, bool routingChanged = false);
    tracker::MidiChannelMap current() const;

    ui::SlotList list_;
    TextLine listTitle_;
    ui::Toggle on_;
    TextLine head_, targetLabel_;
    ui::Segmented target_;
    juce::TextButton addRegion_, removeRegion_;
    std::unique_ptr<VelocityBar> bar_;
    juce::Viewport gridView_;         ///< the grid scrolls: a region a velocity at most (section 225)
    ui::RegionGrid grid_;
    int midiCh_ = 1;
    std::vector<ui::SlotRow> lastRows_;
    juce::Rectangle<int> pane_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiMapPanel)
};

} // namespace chipboy::plugin
