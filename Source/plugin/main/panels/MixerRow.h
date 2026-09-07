// ChipBoy -- the mixer row (UI_DESIGN section 2): four channel strips and
// the master strip, scopes on top, mixer-bridge style.
#pragma once

#include "plugin/main/panels/PanelCommon.h"

#include <array>

namespace chipboy::plugin {

/// One hardware channel: name, LED, source badge, scope, register line,
/// instrument, quick controls, mute / solo, table and pan.
class ChannelStrip : public juce::Component {
public:
    ChannelStrip(ChipBoyProcessor& p, int ch);
    ~ChannelStrip() override;

    void setSelected(bool on);
    bool selected() const { return selected_; }
    void tick();                                   ///< 30 Hz: LED, badge, registers, names
    void bankChanged();
    void hexChanged();
    void setScopeSettings(ui::ScopeView::Trace trace, int periods);
    void setAnalogCornerHz(double hz);
    std::function<void(int)> onSelect;

    static constexpr int kHeight = 276;
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    void showSourceMenu();
    void refreshInstrumentName();

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
    juce::TextButton mute_, solo_, keyswitch_;
    ui::Stepper table_;
    ui::Segmented pan_;
    std::unique_ptr<SegmentedParam> panParam_;
    std::unique_ptr<juce::ButtonParameterAttachment> keyswitchAtt_;
    std::unique_ptr<juce::ParameterAttachment> sourceAtt_;
    int sourceValue_ = 0;
    const bank::Bank* namesFor_ = nullptr;
    int instrumentShown_ = -1;

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
    ui::Stepper volL_, volR_;
    ui::Toggle noise_, declick_;
    ui::Fader trim_;
    int lastModel_ = -1;

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

    static constexpr int kHeight = 10 + ChannelStrip::kHeight + 12;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    std::array<std::unique_ptr<ChannelStrip>, 4> strips_;
    MasterStrip master_;
    int selected_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MixerRow)
};

} // namespace chipboy::plugin
