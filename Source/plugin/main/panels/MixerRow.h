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
/// slots and M / S / KS. The instrument name is the one the driver last
/// loaded, so it follows the tracker (section 30).
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
    /// A double click on the instrument or table stepper opens that item's
    /// own tab (UI_DESIGN section 2.1).
    std::function<void(ui::SlotKind, int slot)> onOpenSlot;

    /// 8 + 20 head + 60 scope + 14 registers + 24 instrument + 70 level
    /// + 22 pan + two 34 command slots, with the gaps (6 down to the pan
    /// row, then 4 through the block at the bottom).
    static constexpr int kHeight = 332;
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent&) override;

private:
    void showSourceMenu();
    void refreshInstrumentName();
    /// The bank's slots by name, for the right click every slot field takes.
    void showInstrumentMenu();
    void showTableMenu();

    ChipBoyProcessor& processor_;
    const int ch_;
    bool selected_ = false;

    /// Whether the song has this channel on Hybrid (section 20): the strip's
    /// Instrument, Table and both command slots are inert there, so they grey
    /// out and the head says why.
    void refreshHybrid();

    ui::Led led_;
    TextLine name_;
    ui::Pill hybrid_;
    TipBox hybridBox_;
    int hybridShown_ = -1;
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
    std::unique_ptr<SegmentedParam> panParam_;
    std::unique_ptr<ToggleParam> keyswitchAtt_;
    std::unique_ptr<juce::ParameterAttachment> sourceAtt_;
    int sourceValue_ = 0;
    const bank::Bank* namesFor_ = nullptr;
    int instrumentShown_ = -1;
    /// The instrument the driver last loaded on this channel, which is what
    /// the name shows so it follows the tracker (section 30).
    int loadedInstrument_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChannelStrip)
};

/// The stereo mix scope, the NR50 / NR51 readout, one VOL that sets both
/// NR50 sides, the three switches -- Headphone Noise, LCD Whine, De-click --
/// and the output trim (docs/COMMANDS_AND_TEMPO.md section 21).
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
    TextLine volLabel_, trimLabel_;
    TextLine mix_;
    TipBox mixBox_;
    /// One control for both NR50 sides (section 21). The two parameters
    /// stay -- the M command and existing automation address left and right
    /// -- so the readout shows the left value, and both when they differ.
    ui::Stepper vol_;
    ui::Toggle noise_, lcd_, declick_;
    ui::Fader trim_;
    std::unique_ptr<juce::ParameterAttachment> volLAtt_, volRAtt_;
    int volL_ = 7, volR_ = 7;
    int lastModel_ = -1;
    uint32_t mixShown_ = ~uint32_t(0);
    void refreshVol();

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
    std::function<void(ui::SlotKind, int slot)> onOpenSlot;

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
