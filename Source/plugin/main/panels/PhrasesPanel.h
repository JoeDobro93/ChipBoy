// ChipBoy -- the Phrases tab (UI_DESIGN section 7): a tracker that follows
// the host transport. Transport line, record arm, steps per bar, groove, the
// tempo group (docs/COMMANDS_AND_TEMPO.md section 4), the bar chain and the
// four-channel lane.
#pragma once

#include "plugin/main/panels/PanelCommon.h"

#include <array>

namespace chipboy::plugin {

class PhrasesPanel : public EditorPanel {
public:
    explicit PhrasesPanel(ChipBoyProcessor& p);
    ~PhrasesPanel() override;

    void setChannel(int ch) override;
    RichText contextLine() const override;
    void songChanged() override;
    void hexChanged() override;
    void tick() override;
    void resized() override;

private:
    void refreshViews();
    void syncGroove();
    void syncTempo();
    void applyGroove(int id);
    void editSong(const std::function<void(tracker::Song&)>& fn);
    static uint8_t ensurePhrase(tracker::Song& s, int ch, int bar);

    ui::Led playLed_;
    TextLine playText_, pos_, stepsLabel_, grooveLabel_;
    juce::TextButton rec_, export_;
    ui::Segmented steps_;
    juce::ComboBox groove_;
    // tempo: the source, the song's own tempo, where its tick 0 sits, how
    // long its bar is, and whether MIDI notes wait for a tick
    TextLine tempoLabel_, songTempoLabel_, startLabel_, beatsLabel_;
    ui::Segmented tempoSource_;
    ui::Stepper songTempo_, songStart_, beats_;
    ui::Toggle quantise_;
    std::unique_ptr<ParamWatch> tempoWatch_;
    HelpText help_;
    ScrollBlock scroll_;
    ui::ChainStrip chain_;
    ui::PhraseGrid grid_;
    int bar_ = 0, playingBar_ = -1;
    std::array<int, 4> lastRoll_{ { -2, -2, -2, -2 } };
    std::array<int, 4> lastStep_{ { -2, -2, -2, -2 } };
    bool grooveSyncing_ = false, songMode_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PhrasesPanel)
};

} // namespace chipboy::plugin
