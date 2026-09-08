// ChipBoy -- the main window (UI_DESIGN section 2): header, mixer row,
// tabs with their context line, the eight editor panels and the status
// line. 1180 wide at 100 %, scaled as a whole at 125 / 150 %; the height
// stretches from kMainHeight up, and everything the extra height buys goes
// to the editor pane.
//
// One 30 Hz timer drives everything live: LEDs, badges, registers, the
// context line, the transport, focus requests from Voice plugins, and the
// bank / song pointers, which are compared so a Voice push or a recording
// shows up in the lists without copying anything on the timer.
#pragma once

#include "plugin/main/ChipBoyProcessor.h"
#include "plugin/main/panels/HeaderBar.h"
#include "plugin/main/panels/MixerRow.h"
#include "plugin/main/panels/PanelCommon.h"
#include "plugin/main/panels/StatusBar.h"
#include "plugin/ui/Theme.h"
#include "plugin/ui/Widgets.h"

#include <array>
#include <memory>

namespace chipboy::plugin {

class ChipBoyEditor : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit ChipBoyEditor(ChipBoyProcessor& p);
    ~ChipBoyEditor() override;

    enum Tab { Instrument = 0, Tables, Grooves, Waves, Kits, Tracker, Link, Hardware, kTabs };

    void selectChannel(int ch);      ///< the editing context: strip highlight, panels, context line
    void showTab(int tab);
    void setScale(float factor);     ///< 1.0, 1.25 or 1.5, remembered as apvts.state "ui_scale"

    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;

private:
    class TabBar;
    void timerCallback() override;
    void refreshContext();
    void toggleVisualizer();
    void layoutContent();

    ChipBoyProcessor& processor_;
    ui::ChipBoyLookAndFeel lookAndFeel_;
    juce::Component content_;
    HeaderBar header_;
    MixerRow mixer_;
    std::unique_ptr<TabBar> tabs_;
    std::array<std::unique_ptr<EditorPanel>, kTabs> panels_;
    StatusBar status_;
    juce::TooltipWindow tooltips_;
    std::unique_ptr<ui::VisualizerWindow> visualizer_;
    std::unique_ptr<ParamWatch> hexWatch_;
    int selected_ = 0, tab_ = 0;
    float scale_ = 1.0f;
    int height_ = ui::kMainHeight;   ///< the window height at 100 %, what the panels are laid out in
    bool rescaling_ = false;         ///< inside setScale: a resize then is the scale's, not the user's
    const bank::Bank* lastBank_ = nullptr;
    const tracker::Song* lastSong_ = nullptr;
    RichText lastContext_;
    double lastCorner_ = -1.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChipBoyEditor)
};

} // namespace chipboy::plugin
