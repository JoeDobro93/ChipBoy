// ChipBoy -- the header bar (UI_DESIGN section 2, item 1): wordmark, the
// model switch, the tempo group (source, the tempo readout, quantize), the
// bank with previous / next and its menu, undo and redo, the STOCK /
// MODIFIED badge, the visualizer and hex buttons and the settings menu.
//
// The tempo here is a readout, not a control (docs/COMMANDS_AND_TEMPO.md
// section 19): the host's BPM in Host mode, the active song's tempo in force
// in Song mode. A song's own tempo is typed in the Tracker tab, beside Start
// and Beats, because it belongs to the song and the window can hold several.
#pragma once

#include "plugin/main/panels/PanelCommon.h"

#include <memory>

namespace chipboy::plugin {

/// The tempo in force and where it comes from: "120.0" with a "host" or
/// "song" tag beside it, drawn as a readout so nothing invites a drag.
class TempoReadout;

class HeaderBar : public juce::Component {
public:
    explicit HeaderBar(ChipBoyProcessor& p);
    ~HeaderBar() override;

    std::function<void(float)> onScale;            ///< 1.0, 1.25, 1.5
    std::function<void()> onToggleVisualizer;
    std::function<void()> onBankLoaded;            ///< after a bank was replaced from here
    std::function<void()> onUndo, onRedo;          ///< the two arrows, and Ctrl+Z / Ctrl+Shift+Z
    void setScale(float f) { scale_ = f; }
    void setVisualizerOpen(bool open);
    void tick();                                   ///< badge, bank name

    static constexpr int kHeight = 54;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    juce::StringArray bankNames() const;           ///< "Factory" then the .chipboy files
    void cycleBank(int direction);
    void loadBankByName(const juce::String& name);
    void applyBank(const bank::Bank& b, const juce::String& name);
    void showBankMenu();
    void showSettingsMenu();

    ChipBoyProcessor& processor_;
    TextLine wordmark_, wordmarkSmall_, modelLabel_, tempoLabel_, bankLabel_;
    ui::Segmented model_;
    // tempo (docs/COMMANDS_AND_TEMPO.md section 4): whose beat the ticks
    // follow, the song's own tempo, and whether notes wait for a tick
    ui::Segmented tempoSource_;
    std::unique_ptr<TempoReadout> tempoRead_;
    juce::TextButton quantize_;
    juce::TextButton bankPrev_, bankNext_, bankMenu_;
    /// Undo and redo, right of the bank: what they would take back is in
    /// their tooltips, and they grey out when there is nothing (UI_DESIGN 2.1).
    juce::TextButton undo_, redo_;
    ui::NameField bankName_;
    ui::Pill stock_;
    TipBox stockBox_;
    juce::TextButton visualizer_, hex_, settings_;
    std::unique_ptr<ToggleParam> hexAtt_, quantizeAtt_;
    std::unique_ptr<ParamWatch> tempoWatch_;
    float scale_ = 1.0f;
    bool lastModified_ = false;
    int lastModel_ = -1;
    /// Whether the plugin was running its own transport last tick: while it
    /// does, the tempo source is fixed on Song (COMMANDS_AND_TEMPO 16).
    int lastOwns_ = -1;
    juce::String undoShown_, redoShown_;
    /// What the readout last showed, so the header only repaints when the
    /// tempo really moved.
    double tempoShown_ = -1.0;
    int tempoFromShown_ = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HeaderBar)
};

} // namespace chipboy::plugin
