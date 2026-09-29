// ChipBoy -- the main window's own helpers: what a panel of the machine
// needs of ChipBoyProcessor beyond the EditorHost the shared panels use
// (docs/plan-solo.md section 7).
#pragma once

#include "plugin/main/ChipBoyProcessor.h"
#include "plugin/main/panels/PanelCommon.h"

namespace chipboy::plugin {

/// An editor tab of the main window: an EditorPanel that also keeps the
/// machine, for the transport, the link, the song tabs and the rest.
class MainPanel : public EditorPanel {
public:
    explicit MainPanel(ChipBoyProcessor& p) : EditorPanel(p), processor(p) {}
protected:
    ChipBoyProcessor& processor;
};

/* --------------------------------------------------------- the tracker */

/// Where the transport stands, as the tracker counts it: the song's tick, and
/// per channel the row of its own chain it is in and how far into that row --
/// the channels drift apart by design (docs/COMMANDS_AND_TEMPO.md 25).
struct TrackerPosition {
    int64_t tick = 0;
    int row[4] = { 0, 0, 0, 0 };
    int inRow[4] = { 0, 0, 0, 0 };
    bool playing = false;
};
TrackerPosition trackerPosition(const ChipBoyProcessor& p);
/// The step a channel is really playing: its groove says how long each step
/// lasts, so a swung phrase marks the row that is sounding. -1 for none.
int playingStepOf(const ChipBoyProcessor& p, const tracker::Song& s, int ch, int row, int inRow);

/* ----------------------------------------------------------- lookups */

/// The coupling corner of the current model: DMG 25 Hz, CGB 338 Hz over the
/// bass-mod factor, RAW 0.
double analogCornerHz(const ChipBoyProcessor& p);
int modelIndex(const ChipBoyProcessor& p);
/// "Omni", "MIDI 2", "Off", or "Voice: <name>" when a Voice owns the channel.
juce::String channelSourceText(ChipBoyProcessor& p, int ch, bool* voiceOwned = nullptr);

} // namespace chipboy::plugin
