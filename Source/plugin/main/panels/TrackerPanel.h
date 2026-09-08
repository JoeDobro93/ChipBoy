// ChipBoy -- the Tracker tab (UI_DESIGN section 7): a tracker that follows
// the host, or runs the song itself when no host offers a transport
// (docs/COMMANDS_AND_TEMPO.md section 16).
//
// The head is two rows of grouped tools under their captions -- TRANSPORT,
// RECORD, SONG, FILE (section 23) -- over the song tab strip (section 18);
// the lane below shows the selected bar's steps, with the chain rotated into
// the column beside it -- one row per bar, the four channels and the bar's
// own step count across it.
//
// A tab is a song and the bank it plays through, and only the active one is
// live. Which tempo is in force, and whether notes wait for a tick, is the
// header bar's group; the song's own master tempo is typed here, beside its
// start and its beats per bar. A phrase's groove is chosen in the lane's
// chip and edited in the Grooves tab.
#pragma once

#include "plugin/main/panels/PanelCommon.h"

#include <array>
#include <memory>
#include <vector>

namespace chipboy::plugin {

class TrackerPanel : public EditorPanel {
public:
    explicit TrackerPanel(ChipBoyProcessor& p);
    ~TrackerPanel() override;

    RichText contextLine() const override;
    void setChannel(int ch) override;
    void songChanged() override;
    /// The lane's right-click lists read the bank by name.
    void bankChanged() override { grid_.setBank(processor.bank()); }
    void hexChanged() override;
    void tick() override;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void refreshViews();
    void syncSongTime();
    void syncTransport(bool force);
    void syncGridHeight();
    void syncTabs();
    /// What a file did, for the status bar: the head has no line of its own
    /// any more -- the tab strip stands where it did (section 18).
    void report(const RichText& text);
    void saveSong();
    void loadSong();
    void closeTab(int index);
    /// One edit of the song; `what` names it on the undo history, and a run
    /// of edits under the same name -- the digits of one typed value -- is
    /// one undo (UI_DESIGN section 2.1).
    void editSong(const juce::String& what, const std::function<void(tracker::Song&)>& fn);
    static uint8_t ensurePhrase(tracker::Song& s, int ch, int bar);

    ui::Led playLed_;
    TextLine playText_, pos_, stepsLabel_, startLabel_, beatsLabel_, tempoLabel_;
    juce::TextButton play_, stop_, loop_, rec_, saveSong_, loadSong_, export_;
    ui::Stepper steps_;
    // the song's own timeline: its master tempo (section 19), where its tick
    // 0 sits on the host's, and how long its bar is. All three are song data.
    ui::Stepper tempo_, songStart_, beats_;
    std::unique_ptr<ParamWatch> tempoWatch_;
    /// The songs open in this window (section 18); the active one is live.
    ui::SongTabStrip tabs_;
    std::vector<ui::SongTabInfo> tabsShown_;
    ScrollBlock scroll_;
    Hold* laneHold_ = nullptr;
    ui::PhraseGrid grid_;
    ui::ChainColumn chain_;
    std::unique_ptr<juce::FileChooser> chooser_;
    /// The four head groups as resized() laid them out: their captions go
    /// over them and a hairline stands between them (section 23).
    std::array<juce::Rectangle<int>, 4> groups_{};
    int bar_ = 0, playingBar_ = -1;
    int gridSteps_ = 0;
    int activeTabShown_ = -1;
    std::array<int, 4> lastRoll_{ { -2, -2, -2, -2 } };
    std::array<int, 4> lastStep_{ { -2, -2, -2, -2 } };
    bool songMode_ = false, owns_ = false, wasPlaying_ = false, loopOn_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrackerPanel)
};

} // namespace chipboy::plugin
