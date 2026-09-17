// ChipBoy -- the Tracker tab (UI_DESIGN section 7): a tracker that follows
// the host, or runs the song itself when no host offers a transport
// (docs/COMMANDS_AND_TEMPO.md sections 16 and 223).
//
// The head is two rows of grouped tools under their captions -- RECORD and
// SONG, then FILE, on the left; TRANSPORT over the chain's own column
// (D-UI-36) -- over the song tab strip (section 18). The lane below shows,
// per channel, the phrase that channel is in at the play head, with the
// chain drawn in time beside it (D-UI-35): a block per row, as tall as the
// row lasts, and the play head one line across the four channels.
//
// A tab is a song and the bank it plays through, and only the active one is
// live. Which tempo is in force, and whether notes wait for a tick, is the
// header bar's group; the song's own master tempo is typed here, beside its
// start. Bars have left the model (docs/COMMANDS_AND_TEMPO.md section 25),
// so a phrase's STEPS is typed in the lane's head (D-UI-37), and its groove
// is chosen in the lane's chip and edited in the Grooves tab.
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
    /// The play head, whether the view follows the transport and the zoom
    /// survive a close and a reopen (section 35, UI_DESIGN D-UI-16, D-UI-35).
    void saveView(juce::ValueTree& v) const override
    {
        v.setProperty("tick", juce::String(cursor_), nullptr);
        v.setProperty("follow", followOn_, nullptr);
        v.setProperty("zoom", chain_.zoom(), nullptr);
    }
    void restoreView(const juce::ValueTree& v) override;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void refreshViews();
    void syncSongTime();
    void syncTransport(bool force);
    void syncGridHeight();
    void syncTabs();
    /// Each channel's own row at the play head (section 25, D-UI-35).
    int rowOf(int ch) const;
    /// Play from the play head, or pause where it stands (section 223).
    void playPause();
    /// What a file did, for the status bar: the head has no line of its own
    /// any more -- the tab strip stands where it did (section 18).
    void report(const RichText& text);
    void saveSong();
    void loadSong();
    /// Import .sav... (docs/plan-lsdj-import.md): a chooser, then the dialog
    /// listing the save's songs; each chosen one opens in a tab of its own.
    void importSav();
    void closeTab(int index);
    /// One edit of the song; `what` names it on the undo history, and a run
    /// of edits under the same name -- the digits of one typed value -- is
    /// one undo (UI_DESIGN section 2.1).
    void editSong(const juce::String& what, const std::function<void(tracker::Song&)>& fn);
    static uint8_t ensurePhrase(tracker::Song& s, int ch, int bar);

    ui::Led playLed_;
    TextLine pos_, startLabel_, tempoLabel_, transposeLabel_, zoomLabel_, gridText_;
    /// The transport over the chain (D-UI-36): play reads pause while it
    /// plays, stop returns to the start, loop and follow are toggles.
    ui::IconButton play_, stop_, loop_, follow_;
    juce::TextButton rec_, saveSong_, loadSong_, importSav_, export_;
    juce::Slider zoom_;
    // the song's own timeline: its master tempo (section 19) and where its
    // tick 0 sits on the host's. Beats and Steps / bar went with the bars.
    ui::Stepper tempo_, songStart_, transpose_;
    std::unique_ptr<ParamWatch> tempoWatch_;
    /// The songs open in this window (section 18); the active one is live.
    ui::SongTabStrip tabs_;
    ScrollBlock scroll_;
    Hold* laneHold_ = nullptr;
    ui::PhraseGrid grid_;
    ui::ChainColumn chain_;
    std::unique_ptr<juce::FileChooser> chooser_;
    /// The four head groups as resized() laid them out: their captions go
    /// over them and a hairline stands before the second of a row and
    /// before the transport's column (section 23).
    std::array<juce::Rectangle<int>, 4> groups_{};
    /// The play head, a tick (section 223): where the lanes look and where
    /// Play starts. Follow moves it with the transport (D-UI-16).
    int64_t cursor_ = 0;
    /// The loop region Shift-dragged on the gutter; `loopTo_` < 0 is the
    /// whole song (section 223).
    int64_t loopFrom_ = 0, loopTo_ = -1;
    int gridSteps_ = 0;
    std::array<int, 4> lastRoll_{ { -2, -2, -2, -2 } };
    std::array<int, 4> lastStep_{ { -2, -2, -2, -2 } };
    std::array<int, 4> shownRow_{ { -1, -1, -1, -1 } };
    bool songMode_ = false, owns_ = false, wasPlaying_ = false, loopOn_ = false;
    /// The view follows the transport (UI_DESIGN D-UI-16); off, the play
    /// head and the lanes are the user's while the song plays.
    bool followOn_ = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TrackerPanel)
};

} // namespace chipboy::plugin
