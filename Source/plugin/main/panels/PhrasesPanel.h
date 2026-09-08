// ChipBoy -- the Phrases tab (UI_DESIGN section 7): a tracker that follows
// the host transport. Transport line, record arm, steps per bar, the song's
// own timeline (start and beats per bar, song data, live only in Song mode),
// the bar chain and the four-channel lane. Which tempo is in force, and
// whether notes wait for a tick, is the header bar's group. A phrase's groove
// is chosen in the lane's chip; the groove editor comes with section 10.
#pragma once

#include "plugin/main/panels/PanelCommon.h"

#include <array>

namespace chipboy::plugin {

class PhrasesPanel : public EditorPanel {
public:
    explicit PhrasesPanel(ChipBoyProcessor& p);
    ~PhrasesPanel() override;

    RichText contextLine() const override;
    void songChanged() override;
    void hexChanged() override;
    void tick() override;
    void resized() override;

private:
    void refreshViews();
    void syncSongTime();
    void editSong(const std::function<void(tracker::Song&)>& fn);
    static uint8_t ensurePhrase(tracker::Song& s, int ch, int bar);

    ui::Led playLed_;
    TextLine playText_, pos_, stepsLabel_;
    juce::TextButton rec_, export_;
    ui::Segmented steps_;
    // the song's own timeline: where its tick 0 sits and how long its bar
    // is. Both are song data, and both only bite in Song mode.
    TextLine startLabel_, beatsLabel_;
    ui::Stepper songStart_, beats_;
    std::unique_ptr<ParamWatch> tempoWatch_;
    HelpText help_;
    ScrollBlock scroll_;
    ui::ChainStrip chain_;
    ui::PhraseGrid grid_;
    int bar_ = 0, playingBar_ = -1;
    std::array<int, 4> lastRoll_{ { -2, -2, -2, -2 } };
    std::array<int, 4> lastStep_{ { -2, -2, -2, -2 } };
    bool songMode_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PhrasesPanel)
};

} // namespace chipboy::plugin
