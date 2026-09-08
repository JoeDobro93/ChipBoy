// ChipBoy -- the mixer row (UI_DESIGN section 2): four channel strips and
// the master strip, scopes on top, mixer-bridge style.
#pragma once

#include "plugin/main/panels/PanelCommon.h"
#include "plugin/ui/CommandSlot.h"

#include <array>

namespace chipboy::plugin {

/// One hardware channel, drawn as the tracker row it is
/// (docs/COMMANDS_AND_TEMPO.md section 3): name, LED, source badge, scope,
/// register line, instrument, level, table, transpose, pan, the two command
/// slots, the running state the driver publishes, and M / S / KS.
class ChannelStrip : public juce::Component {
public:
    ChannelStrip(ChipBoyProcessor& p, int ch);
    ~ChannelStrip() override;

    void setSelected(bool on);
    bool selected() const { return selected_; }
    void tick();                                   ///< 30 Hz: LED, badge, registers, running state, names
    void bankChanged();
    void hexChanged();
    void setScopeSettings(ui::ScopeView::Trace trace, int periods);
    void setAnalogCornerHz(double hz);
    std::function<void(int)> onSelect;

    /// 8 + 20 head + 60 scope + 14 registers + 24 instrument + 70 level
    /// + 22 pan + two 34 command slots + 14 running state, with the gaps
    /// (6 down to the pan row, then 4 through the block at the bottom).
    static constexpr int kHeight = 350;
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    void showSourceMenu();
    void refreshInstrumentName();
    void refreshState();

    ChipBoyProcessor& processor_;
    const int ch_;
    bool selected_ = false;

    ui::Led led_;
    TextLine name_;
    ui::Pill source_;
    TipBox sourceBox_;
    ui::ScopeView scope_;
    ui::RegisterLine regs_;
    ui::Stepper instrument_;
    TextLine instrumentName_;
    std::unique_ptr<ui::Knob> level_;
    std::unique_ptr<TextLine> waveLevelLabel_;
    std::unique_ptr<ui::Segmented> waveLevel_;
    TextLine tableLabel_, transposeLabel_;
    ui::Stepper table_, transpose_;
    ui::Segmented pan_;
    juce::TextButton mute_, solo_, keyswitch_;
    ui::CommandSlot cmd1_, cmd2_;
    TextLine state_;
    TipBox stateBox_;
    std::unique_ptr<SegmentedParam> panParam_;
    std::unique_ptr<juce::ButtonParameterAttachment> keyswitchAtt_;
    std::unique_ptr<juce::ParameterAttachment> sourceAtt_;
    int sourceValue_ = 0;
    const bank::Bank* namesFor_ = nullptr;
    int instrumentShown_ = -1;
    uint64_t stateShown_ = ~uint64_t(0);
    int stateBar_ = -1, stateWidth_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChannelStrip)
};

/// The stereo mix scope, master volume L / R, Headphone Noise, De-click
/// and the output trim.
class MasterStrip : public juce::Component {
public:
    explicit MasterStrip(ChipBoyProcessor& p);
    ~MasterStrip() override;
    void tick();
    static constexpr int kWidth = 224;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    ChipBoyProcessor& processor_;
    TextLine name_;
    ui::Pill model_;
    TipBox modelBox_;
    ui::MasterScope scope_;
    TextLine volLLabel_, volRLabel_, trimLabel_;
    TextLine mix_;
    TipBox mixBox_;
    ui::Stepper volL_, volR_;
    ui::Toggle noise_, declick_;
    ui::Fader trim_;
    int lastModel_ = -1;
    uint32_t mixShown_ = ~uint32_t(0);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MasterStrip)
};

class MixerRow : public juce::Component {
public:
    explicit MixerRow(ChipBoyProcessor& p);
    ~MixerRow() override;

    void setSelected(int ch);
    int selected() const { return selected_; }
    void tick();
    void bankChanged();
    void hexChanged();
    void setScopeSettings(ui::ScopeView::Trace trace, int periods);
    void setAnalogCornerHz(double hz);
    std::function<void(int)> onSelect;

    static constexpr int kHeight = 10 + ChannelStrip::kHeight + 10;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    std::array<std::unique_ptr<ChannelStrip>, 4> strips_;
    MasterStrip master_;
    int selected_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerRow)
};

} // namespace chipboy::plugin
