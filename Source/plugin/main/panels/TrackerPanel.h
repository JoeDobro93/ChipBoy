// ChipBoy -- the Tracker tab (UI_DESIGN section 7): a tracker that follows
// the host, or runs the song itself when no host offers a transport
// (docs/COMMANDS_AND_TEMPO.md section 16). The head carries the transport,
// the record arm, the steps per bar, the song's own timeline and its file;
// the lane below shows the selected bar's steps, with the chain rotated
// into the column beside it -- one row per bar, the four channels and the
// bar's own step count across it.
//
// Which tempo is in force, and whether notes wait for a tick, is the header
// bar's group. A phrase's groove is chosen in the lane's chip and edited in
// the Grooves tab.
#pragma once

#include "plugin/main/panels/PanelCommon.h"

#include <array>
#include <memory>

namespace chipboy::plugin {

class TrackerPanel : public EditorPanel {
public:
    explicit TrackerPanel(ChipBoyProcessor& p);
    ~TrackerPanel() override;

    RichText contextLine() const override;
    void setChannel(int ch) override;
    void songChanged() override;
    void hexChanged() override;
    void tick() override;
    void resized() override;

private:
    void refreshViews();
    void syncSongTime();
    void syncTransport(bool force);
    void syncGridHeight();
    void showReport(const RichText& text);
    /// The default line: what the song is, and where its file came from.
    void showSongSummary();
    void saveSong();
    void loadSong();
    void editSong(const std::function<void(tracker::Song&)>& fn);
    static uint8_t ensurePhrase(tracker::Song& s, int ch, int bar);

    ui::Led playLed_;
    TextLine playText_, pos_, stepsLabel_, startLabel_, beatsLabel_;
    juce::TextButton play_, stop_, loop_, rec_, saveSong_, loadSong_, export_;
    ui::Stepper steps_;
    // the song's own timeline: where its tick 0 sits and how long its bar
    // is. Both are song data, and both only bite in Song mode.
    ui::Stepper songStart_, beats_;
    std::unique_ptr<ParamWatch> tempoWatch_;
    HelpText report_;
    ScrollBlock scroll_;
    Hold* laneHold_ = nullptr;
    ui::PhraseGrid grid_;
    ui::ChainColumn chain_;
    std::unique_ptr<juce::FileChooser> chooser_;
    juce::File songFile_;
    RichText reported_;              ///< the line the head shows, empty for the summary
    int bar_ = 0, playingBar_ = -1;
    int gridSteps_ = 0;
    std::array<int, 4> lastRoll_{ { -2, -2, -2, -2 } };
    std::array<int, 4> lastStep_{ { -2, -2, -2, -2 } };
    bool songMode_ = false, owns_ = false, wasPlaying_ = false, loopOn_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrackerPanel)
};

} // namespace chipboy::plugin
