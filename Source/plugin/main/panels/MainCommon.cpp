#include "plugin/main/panels/MainCommon.h"

#include <algorithm>
#include <cmath>

namespace chipboy::plugin {

using namespace juce;
using namespace chipboy::ui;

/* --------------------------------------------------------- the tracker */

TrackerPosition trackerPosition(const ChipBoyProcessor& p)
{
    TrackerPosition t;
    t.playing = p.transportPlaying();
    t.tick = std::max<int64_t>(0, p.trackerTick());
    const auto s = p.song();
    // Each channel's rows lie end to end on its own prefix table, so two
    // channels are in different rows at the same tick (section 25).
    for (int ch = 0; ch < 4; ++ch) {
        if (s) tracker::rowAtTick(*s, ch, t.tick, t.row[ch], t.inRow[ch]);
        else { t.row[ch] = int(t.tick / tracker::kEmptyRowTicks); t.inRow[ch] = int(t.tick % tracker::kEmptyRowTicks); }
    }
    return t;
}

int playingStepOf(const ChipBoyProcessor& p, const tracker::Song& s, int ch, int row, int inRow)
{
    const tracker::Phrase* phrase = s.phrase(s.phraseAt(ch, row));
    const int steps = s.stepsOfRow(ch, row);
    // Positions, not steps: an `H` can put more of them in a row than the
    // phrase has cells (section 102), so the array is the play order's.
    std::vector<int> start(size_t(tracker::kMaxPlaySteps) + 1, 0);
    std::vector<uint8_t> stepOf(size_t(tracker::kMaxPlaySteps) + 1, 0);
    tracker::GrooveWalk w = p.player().walkFor(ch, row);
    const int n = tracker::stepStartTicks(s, phrase, w, start.data(), stepOf.data());
    const int length = int(tracker::rowStartTick(s, ch, row + 1) - tracker::rowStartTick(s, ch, row));
    // The latest position at or before the tick, and the step it plays: an
    // H loop comes back up the phrase, and the lit step comes back with it.
    int step = -1;
    for (int i = 0; i < n; ++i) {
        if (start[size_t(i)] >= length || start[size_t(i)] > inRow) break;   // that step never fires, or has not come yet
        step = phrase != nullptr ? int(stepOf[size_t(i)]) : i;
    }
    return step < steps ? step : -1;
}

int modelIndex(const ChipBoyProcessor& p) { return std::clamp(paramValue(p, ids::model), 0, 2); }

double analogCornerHz(const ChipBoyProcessor& p)
{
    const int m = modelIndex(p);
    if (m == 2) return 0.0;
    if (m == 0) return 25.0;
    const int mod = std::clamp(paramValue(p, ids::bassMod), 0, 2);
    return 338.0 / (mod == 0 ? 1.0 : mod == 1 ? 10.0 : 47.0);
}

String channelSourceText(ChipBoyProcessor& p, int ch, bool* voiceOwned)
{
    const bool owned = (p.voiceOwnedMask() & (1u << ch)) != 0;
    if (voiceOwned) *voiceOwned = owned;
    if (owned) {
        String n = p.voiceName(ch);
        if (n.isEmpty()) n = "Voice";
        return "Voice: " + n;
    }
    // Section 225: with the map on, the MIDI channels that reach this one.
    if (const auto s = p.song(); s && s->midiMap.on) {
        String t;
        for (int m = 0; m < tracker::kMidiChannels; ++m)
            if (s->midiMap.channels[size_t(m)].target == ch) t += (t.isEmpty() ? "" : String(CharPointer_UTF8("\xc2\xb7"))) + String(m + 1);
        return "MAP " + (t.isEmpty() ? String(CharPointer_UTF8("\xe2\x80\x94")) : t);
    }
    return paramText(p, channelParamId(ch, ids::source));
}

} // namespace chipboy::plugin
