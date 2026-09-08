#include "plugin/main/panels/PhrasesPanel.h"

#include <cmath>

namespace chipboy::plugin {

using namespace juce;
using namespace chipboy::ui;

namespace {
/// The head of the tab: two rows of tools with the help under them on the
/// left, the bar chain beside them on the right, so the lane below keeps as
/// much of a fixed window as it can get.
constexpr int kToolRow = 26, kToolGap = 6, kChainWidth = 340;
constexpr int kHeadHeight = ui::ChainStrip::preferredHeight();
/// Song start is held in tenths of a second so a stepper can reach it.
constexpr int kStartSteps = 10, kStartMax = 600 * kStartSteps;
struct GroovePreset { int id; uint8_t a, b; const char* text; };
const GroovePreset kPresets[] = {
    { 1, 6, 6, "6 / 6 \xe2\x80\x94 straight" },
    { 2, 7, 5, "7 / 5 \xe2\x80\x94 light swing" },
    { 3, 8, 4, "8 / 4 \xe2\x80\x94 heavy swing" },
    { 4, 5, 7, "5 / 7 \xe2\x80\x94 reverse" },
};
constexpr int kCustomBase = 10;
String middot() { return String(CharPointer_UTF8(" \xc2\xb7 ")); }
}

PhrasesPanel::PhrasesPanel(ChipBoyProcessor& p)
    : EditorPanel(p),
      playText_("stopped", Fonts::sans(12.0f), colours::textMute),
      pos_("1.1.1", Fonts::mono(12.0f), colours::text),
      stepsLabel_("Steps / bar", Fonts::caption(10.0f), colours::textDim),
      grooveLabel_("Groove", Fonts::caption(10.0f), colours::textDim),
      rec_(String(CharPointer_UTF8("\xe2\x97\x8f Rec"))), export_("Export .gb" + String(CharPointer_UTF8("\xe2\x80\xa6"))),
      steps_({ "8", "16", "32" }),
      startLabel_("Start", Fonts::caption(10.0f), colours::textDim),
      beatsLabel_("Beats", Fonts::caption(10.0f), colours::textDim)
{
    for (auto* l : { &stepsLabel_, &grooveLabel_, &startLabel_, &beatsLabel_ }) l->setUpperCase(true);
    playLed_.setColour(colours::ok);
    playLed_.setInterceptsMouseClicks(false, false);
    for (auto* c : std::initializer_list<Component*>{ &playLed_, &playText_, &pos_, &rec_, &stepsLabel_, &steps_, &grooveLabel_, &groove_, &export_,
                                                     &startLabel_, &songStart_, &beatsLabel_, &beats_, &help_, &scroll_ }) addAndMakeVisible(c);

    rec_.setTooltip("Record arm: while the transport runs, incoming MIDI notes and the parameter values in force are written into the cells of channels set to Trk.");
    rec_.setClickingTogglesState(true);
    rec_.setColour(TextButton::textColourOnId, colours::accentHi);
    rec_.setColour(TextButton::buttonOnColourId, colours::accentSoft);
    rec_.onClick = [this] { processor.setRecordArm(rec_.getToggleState()); };

    steps_.setMini(true);
    steps_.setTooltip("Steps per bar: a phrase's 16 steps are sixteenths at 16");
    steps_.onChange = [this](int i) { const uint8_t v = i == 0 ? 8 : i == 2 ? 32 : 16; editSong([v](tracker::Song& s) { s.stepsPerBar = v; }); };

    groove_.setTooltip("Groove, ticks per step, alternating: swing for the selected channel's phrase in this bar");
    for (const auto& g : kPresets) groove_.addItem(String(CharPointer_UTF8(g.text)), g.id);
    groove_.onChange = [this] { if (!grooveSyncing_) applyGroove(groove_.getSelectedId()); };

    export_.setEnabled(false);
    export_.setTooltip("Later: compile this song " + String(CharPointer_UTF8("\xe2\x80\x94")) + " tracker, bank, waves, kits " + String(CharPointer_UTF8("\xe2\x80\x94")) + " into a playback ROM for real hardware. The tracker is kept self-contained for it.");

    // The song's own timeline (docs/COMMANDS_AND_TEMPO.md section 4). Whose
    // beat the ticks follow is the header's Tempo group; these two are song
    // data, so they live with the song and grey out in Host mode.
    songStart_.setRange(0, kStartMax, 0);
    songStart_.setTooltip("Where tick 0 of the song sits on the host's timeline, in seconds");
    songStart_.setTextFunction([](int v) { return String(double(v) / kStartSteps, 1) + " s"; });
    songStart_.onChange = [this](int v) { editSong([v](tracker::Song& s) { s.songStartSeconds = double(v) / kStartSteps; }); };
    beats_.setRange(1, 16, 4);
    beats_.setTooltip("How many beats the song's own bar holds; in Host mode the host's time signature says instead");
    beats_.onChange = [this](int v) { editSong([v](tracker::Song& s) { s.beatsPerBar = double(v); }); };
    tempoWatch_ = std::make_unique<ParamWatch>(param(processor, ids::tempoSource), [this](float v) {
        songMode_ = v > 0.5f;
        songStart_.setEnabled(songMode_);
        beats_.setEnabled(songMode_);
        startLabel_.setColour(songMode_ ? colours::textDim : colours::lineSoft);
        beatsLabel_.setColour(songMode_ ? colours::textDim : colours::lineSoft);
    });

    RichText h;
    h.plain("Per channel, ").bold("Roll").plain(" shows the piano roll's notes, greyed; ").bold("Trk").plain(" plays the tracker's own notes and ignores incoming MIDI. Commands fire on their step and latch for the notes that follow.");
    help_.setText(h);

    addAndMakeVisible(chain_);
    auto stack = std::make_unique<Stack>(0);
    stack->add(std::make_unique<Hold>(grid_, PhraseGrid::preferredHeight()));
    scroll_.setContent(std::move(stack));

    chain_.onSelectBar = [this](int bar) { bar_ = std::max(0, bar); refreshViews(); contextChanged(); };
    chain_.onChainChange = [this](int ch, int bar, int slot) {
        editSong([ch, bar, slot](tracker::Song& s) {
            if (bar < 0 || bar > 4095) return;
            auto& chain = s.chain[size_t(ch & 3)];
            if (int(chain.size()) <= bar) chain.resize(size_t(bar) + 1, 0);
            chain[size_t(bar)] = uint8_t(std::clamp(slot, 0, tracker::kPhraseSlots));
            if (slot >= 1 && slot <= tracker::kPhraseSlots) s.phrases[size_t(slot - 1)].used = true;
        });
    };
    grid_.onCellChange = [this](int ch, int step, const tracker::Cell& cell) {
        const int bar = bar_;
        editSong([ch, step, bar, cell](tracker::Song& s) {
            const uint8_t slot = ensurePhrase(s, ch, bar);
            if (slot == 0 || step < 0 || step >= tracker::kSteps) return;
            s.phrases[size_t(slot - 1)].steps[size_t(step)] = cell;
        });
    };
    grid_.onSourceChange = [this](int ch, tracker::NoteSource src) { editSong([ch, src](tracker::Song& s) { s.noteSource[size_t(ch & 3)] = src; }); };
    grid_.onGrooveChange = [this](int ch, int groove) {
        const int bar = bar_;
        editSong([ch, bar, groove](tracker::Song& s) {
            const uint8_t slot = ensurePhrase(s, ch, bar);
            if (slot) s.phrases[size_t(slot - 1)].groove = uint8_t(std::clamp(groove, 0, 16));
        });
    };
    refreshViews();
}

PhrasesPanel::~PhrasesPanel() = default;

uint8_t PhrasesPanel::ensurePhrase(tracker::Song& s, int ch, int bar)
{
    if (bar < 0 || bar > 4095) return 0;
    auto& chain = s.chain[size_t(ch & 3)];
    if (int(chain.size()) <= bar) chain.resize(size_t(bar) + 1, 0);
    uint8_t slot = chain[size_t(bar)];
    if (slot == 0) {
        for (int i = 0; i < tracker::kPhraseSlots; ++i) if (!s.phrases[size_t(i)].used) { slot = uint8_t(i + 1); break; }
        if (slot == 0) return 0;
        s.phrases[size_t(slot - 1)].used = true;
        chain[size_t(bar)] = slot;
    }
    return slot;
}

void PhrasesPanel::setChannel(int ch)
{
    EditorPanel::setChannel(ch);
    syncGroove();
}

RichText PhrasesPanel::contextLine() const
{
    RichText r;
    r.plain("Bar ").bold(String(bar_ + 1)).plain(middot()).bold(colours::channelName(channel));
    const auto s = processor.song();
    const int slot = s ? s->phraseAt(channel, bar_) : 0;
    if (slot) r.plain(" plays phrase ").bold(ValueFormat::number(slot));
    else r.plain(" has no phrase this bar " + String(CharPointer_UTF8("\xe2\x80\x94")) + " it just plays its notes");
    return r;
}

void PhrasesPanel::refreshViews()
{
    const auto s = processor.song();
    chain_.setSong(s, bar_, playingBar_);
    grid_.setSong(s, bar_);
    const int spb = s ? int(s->stepsPerBar) : 16;
    steps_.setSelected(spb == 8 ? 0 : spb == 32 ? 2 : 1, dontSendNotification);
    syncGroove();
    syncSongTime();
}

/// The song's own timeline: the two fields that live in the song rather than
/// in a parameter, read back after every edit and after a recording.
void PhrasesPanel::syncSongTime()
{
    const auto s = processor.song();
    if (!s) return;
    songStart_.setValue(std::clamp(int(std::lround(s->songStartSeconds * kStartSteps)), 0, kStartMax), dontSendNotification);
    beats_.setValue(std::clamp(int(std::lround(s->beatsPerBar)), 1, 16), dontSendNotification);
}

void PhrasesPanel::syncGroove()
{
    const auto s = processor.song();
    int id = 1;
    String custom;
    if (s) {
        const int slot = s->phraseAt(channel, bar_);
        const tracker::Phrase* ph = s->phrase(slot);
        const int g = ph ? int(ph->groove) : 0;
        if (g >= 1 && g <= 16) {
            const auto gr = s->grooves[size_t(g - 1)];
            id = 0;
            for (const auto& pr : kPresets) if (pr.a == gr.a && pr.b == gr.b) { id = pr.id; break; }
            if (id == 0) { id = kCustomBase + g; custom = "slot " + String(g) + ": " + String(int(gr.a)) + " / " + String(int(gr.b)); }
        }
    }
    grooveSyncing_ = true;
    if (custom.isNotEmpty() && groove_.indexOfItemId(id) < 0) {
        groove_.clear(dontSendNotification);
        for (const auto& g : kPresets) groove_.addItem(String(CharPointer_UTF8(g.text)), g.id);
        groove_.addItem(custom, id);
    }
    if (groove_.getSelectedId() != id) groove_.setSelectedId(id, dontSendNotification);
    grooveSyncing_ = false;
}

void PhrasesPanel::applyGroove(int id)
{
    const int ch = channel, bar = bar_;
    editSong([ch, bar, id](tracker::Song& s) {
        const uint8_t slot = ensurePhrase(s, ch, bar);
        if (slot == 0) return;
        auto& ph = s.phrases[size_t(slot - 1)];
        if (id == 1) { ph.groove = 0; return; }
        for (const auto& pr : kPresets)
            if (pr.id == id) { const int g = pr.id - 1; s.grooves[size_t(g - 1)] = tracker::Groove{ pr.a, pr.b }; ph.groove = uint8_t(g); return; }
        if (id >= kCustomBase + 1 && id <= kCustomBase + 16) ph.groove = uint8_t(id - kCustomBase);
    });
}

void PhrasesPanel::editSong(const std::function<void(tracker::Song&)>& fn)
{
    processor.mutateSong(fn);
    refreshViews();
    contextChanged();
}

void PhrasesPanel::songChanged()
{
    refreshViews();
    contextChanged();
}

void PhrasesPanel::hexChanged()
{
    chain_.repaint();
    grid_.repaint();
    contextChanged();
}

void PhrasesPanel::tick()
{
    // The position is counted in ticks, so it reads the same whichever tempo
    // source is in force (docs/COMMANDS_AND_TEMPO.md section 4).
    const auto s = processor.song();
    const bool playing = processor.transportPlaying();
    const int64_t tick = std::max<int64_t>(0, processor.trackerTick());
    const int barTicks = std::max(1, processor.barTicks());
    const int spb = s ? std::max(1, int(s->stepsPerBar)) : 16;
    const int bar = int(tick / barTicks);
    const int inBar = int(tick % barTicks);
    const int step = inBar * spb / barTicks;
    const int beat = inBar / driver::kTicksPerBeat;
    const int stepInBeat = (inBar - beat * driver::kTicksPerBeat) * spb / barTicks;
    pos_.setText(String(bar + 1) + "." + String(beat + 1) + "." + String(stepInBeat + 1));
    playLed_.setOn(playing);
    playText_.setText(playing ? "playing" : "stopped");

    const int pb = playing ? bar : -1;
    bool views = false;
    if (pb != playingBar_) { playingBar_ = pb; views = true; }
    if (playing && bar != bar_) { bar_ = bar; views = true; contextChanged(); }   // the view follows the transport
    if (views) refreshViews();

    for (int ch = 0; ch < 4; ++ch) {
        const int st = playing && bar == bar_ && step >= 0 && step < tracker::kSteps ? step : -1;
        if (st != lastStep_[size_t(ch)]) { lastStep_[size_t(ch)] = st; grid_.setPlayingStep(ch, st); }
        const bool roll = s && s->noteSource[size_t(ch)] == tracker::NoteSource::PianoRoll;
        const int note = roll ? processor.lastNotes[size_t(ch)].load() : -1;
        if (note != lastRoll_[size_t(ch)]) { lastRoll_[size_t(ch)] = note; grid_.setRollNote(ch, note); }
    }
    const bool arm = processor.recordArm();
    if (arm != rec_.getToggleState()) rec_.setToggleState(arm, dontSendNotification);
}

void PhrasesPanel::resized()
{
    auto area = getLocalBounds();
    auto head = area.removeFromTop(kHeadHeight);
    chain_.setBounds(head.removeFromRight(std::min(kChainWidth, head.getWidth() / 2)));
    head.removeFromRight(16);
    auto top = head.removeFromTop(kToolRow);
    head.removeFromTop(kToolGap);
    auto bottom = head.removeFromTop(kToolRow);
    head.removeFromTop(kToolGap);
    auto place = [](Rectangle<int>& row, Component& c, int w, int ch) { c.setBounds(row.removeFromLeft(w).withSizeKeepingCentre(w, ch)); };
    auto label = [&place](Rectangle<int>& row, TextLine& t) { place(row, t, t.preferredWidth() + 6, kToolRow); };

    // the transport, the arm, and what a step is worth
    place(top, playLed_, 8, 8);
    top.removeFromLeft(6);
    place(top, playText_, 54, kToolRow);
    place(top, pos_, 60, kToolRow);
    top.removeFromLeft(6);
    place(top, rec_, 62, 24);
    top.removeFromLeft(14);
    label(top, stepsLabel_);
    place(top, steps_, steps_.preferredWidth(), steps_.preferredHeight());
    top.removeFromLeft(14);
    label(top, grooveLabel_);
    place(top, groove_, std::min(160, std::max(0, top.getWidth() - 104)), 24);
    top.removeFromLeft(12);
    place(top, export_, std::min(92, top.getWidth()), 24);

    // the song's own timeline (docs/COMMANDS_AND_TEMPO.md section 4)
    label(bottom, startLabel_);
    place(bottom, songStart_, 84, Stepper::kHeight);
    bottom.removeFromLeft(12);
    label(bottom, beatsLabel_);
    place(bottom, beats_, 62, Stepper::kHeight);

    help_.setBounds(head);
    scroll_.setBounds(area);
}

} // namespace chipboy::plugin
