#include "plugin/main/panels/TrackerPanel.h"

#include "plugin/shared/SongFiles.h"

#include <cmath>

namespace chipboy::plugin {

using namespace juce;
using namespace chipboy::ui;

namespace {
/// The head (docs/COMMANDS_AND_TEMPO.md section 23): two rows of grouped
/// tools, each under its caption, over the song tab strip. It is the same
/// 112 px it was -- 12 caption + 26 controls, 4, 12 + 26, 6, and the 26 px
/// strip where the summary line stood -- so the lane keeps its 400 px, a
/// 48 px grid head and sixteen 22 px rows, and the tab asks for no
/// scrolling at the window's minimum height (UI_DESIGN sections 2 and 7).
constexpr int kCaption = 12, kToolRow = 26, kRowGap = 4, kStripGap = 6;
constexpr int kHeadHeight = 2 * (kCaption + kToolRow) + kRowGap + kStripGap + SongTabStrip::kHeight;
static_assert(kHeadHeight == 112, "the head keeps its budget");
constexpr int kChainGap = 12;
/// Between two groups of one row, with the hairline in the middle of it;
/// then between two fields of a group, and between a caption and its field.
constexpr int kGroupGap = 16, kFieldGap = 14, kLabelGap = 2;
/// Song start is held in tenths of a second so a stepper can reach it.
constexpr int kStartSteps = 10, kStartMax = 600 * kStartSteps;
constexpr int kOpenFlags = FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles;
constexpr int kSaveFlags = FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::warnAboutOverwriting;
const char* kGroupNames[4] = { "Transport", "Record", "Song", "File" };
String middot() { return String(CharPointer_UTF8(" \xc2\xb7 ")); }
String dash() { return String(CharPointer_UTF8(" \xe2\x80\x94 ")); }
String ellipsis() { return String(CharPointer_UTF8("\xe2\x80\xa6")); }
}

TrackerPanel::TrackerPanel(ChipBoyProcessor& p)
    : EditorPanel(p),
      playText_("stopped", Fonts::sans(12.0f), colours::textMute),
      pos_("1.1.1", Fonts::mono(12.0f), colours::text),
      stepsLabel_("Steps / bar", Fonts::caption(10.0f), colours::textDim),
      startLabel_("Start", Fonts::caption(10.0f), colours::textDim),
      beatsLabel_("Beats", Fonts::caption(10.0f), colours::textDim),
      tempoLabel_("Tempo", Fonts::caption(10.0f), colours::textDim),
      play_(String(CharPointer_UTF8("\xe2\x96\xb6 Play"))), stop_(String(CharPointer_UTF8("\xe2\x96\xa0 Stop"))), loop_("Loop"),
      rec_(String(CharPointer_UTF8("\xe2\x97\x8f Rec"))),
      saveSong_("Save song" + ellipsis()), loadSong_("Load song" + ellipsis()),
      export_("Export .gb" + ellipsis())
{
    for (auto* l : { &stepsLabel_, &startLabel_, &beatsLabel_, &tempoLabel_ }) l->setUpperCase(true);
    playLed_.setColour(colours::ok);
    playLed_.setInterceptsMouseClicks(false, false);
    for (auto* c : std::initializer_list<Component*>{ &play_, &stop_, &loop_, &playLed_, &playText_, &pos_, &rec_, &stepsLabel_, &steps_,
                                                     &tempoLabel_, &tempo_, &startLabel_, &songStart_, &beatsLabel_, &beats_,
                                                     &saveSong_, &loadSong_, &export_, &tabs_, &scroll_, &chain_ }) addAndMakeVisible(c);

    // The transport (docs/COMMANDS_AND_TEMPO.md section 16). With no host
    // play head these run the song themselves; in a host they mirror it and
    // are disabled, so the buttons never disagree with what is playing.
    play_.onClick = [this] { processor.transportPlay(); };
    stop_.onClick = [this] { processor.transportStop(); };
    loop_.setClickingTogglesState(true);
    loop_.setColour(TextButton::textColourOnId, colours::text);
    loop_.setColour(TextButton::buttonOnColourId, colours::accentSoft);
    loop_.onClick = [this] {
        processor.setLoopBars(0, -1);              // the whole song, end to start
        processor.setLoop(loop_.getToggleState());
    };

    rec_.setTooltip("Record: while the transport runs, the MIDI arriving on an armed channel is written into its cells, whatever that channel plays. "
                    "The dot beside a channel's name in the lane is its arm.");
    rec_.setClickingTogglesState(true);
    rec_.setColour(TextButton::textColourOnId, colours::accentHi);
    rec_.setColour(TextButton::buttonOnColourId, colours::accentSoft);
    rec_.onClick = [this] { processor.setRecordArm(rec_.getToggleState()); };

    // Steps per bar is a number now, 1-64 (section 11); a bar may still take
    // one of its own in the chain's STP column.
    steps_.setRange(1, tracker::kMaxSteps, 16);
    steps_.setTyped(true);
    steps_.setTooltip("How many steps a bar holds, 1-64: at 16 they are sixteenths of a 4/4 bar, at 8 eighths. Type a number, or step it. "
                      "A single bar can take a count of its own in the chain's STP column.");
    steps_.onChange = [this](int v) {
        const auto n = uint8_t(std::clamp(v, 1, tracker::kMaxSteps));
        editSong("Steps per bar " + String(int(n)), [n](tracker::Song& s) { s.stepsPerBar = n; });
    };

    saveSong_.setTooltip("Write the active tab's song -- chains, phrases, grooves, steps, arms, its tempo and the bank it plays through -- to a .cbsong file. "
                         "A song file is complete, so loading one opens a tab with its own sounds.");
    saveSong_.onClick = [this] { saveSong(); };
    loadSong_.setTooltip("Read a .cbsong file into a tab of its own, with the bank it was written with. A file written before the bank travelled with the song "
                         "takes a copy of this tab's bank, and the status line says where that bank differs.");
    loadSong_.onClick = [this] { loadSong(); };

    export_.setEnabled(false);
    export_.setTooltip("Later: compile this song " + String(CharPointer_UTF8("\xe2\x80\x94")) + " tracker, bank, waves, kits " + String(CharPointer_UTF8("\xe2\x80\x94")) + " into a playback ROM for real hardware. The tracker is kept self-contained for it.");

    // The song's own tempo and timeline (sections 4 and 19). Whose beat the
    // ticks follow is the header's Tempo group, which only reads the tempo
    // out now; the master tempo is typed here, because it belongs to the
    // song and the window can hold several.
    tempo_.setRange(40, 255, 120);
    tempo_.setTyped(true);
    tempo_.setTooltip("This song's master tempo, 40-255 BPM: the base its T commands move from, and what the header reads while the tempo source is Song. "
                      "Every song carries its own; the active one is mirrored into the automatable Song tempo parameter.");
    tempo_.onChange = [this](int v) { processor.setMasterTempo(double(v)); refreshViews(); contextChanged(); };
    songStart_.setRange(0, kStartMax, 0);
    songStart_.setTooltip("Where tick 0 of the song sits on the host's timeline, in seconds");
    songStart_.setTextFunction([](int v) { return String(double(v) / kStartSteps, 1) + " s"; });
    songStart_.onChange = [this](int v) { editSong("Song start", [v](tracker::Song& s) { s.songStartSeconds = double(v) / kStartSteps; }); };
    beats_.setRange(1, 16, 4);
    // The host contributes the tempo and never its signature (section 11 as
    // amended, section 19), so this is the bar's length in both modes.
    beats_.setTooltip("How many beats this song's bar holds. The host's time signature never reaches the tracker, so this says how long a bar is in both tempo "
                      "modes: a song in 3/4 says 3 here.");
    beats_.onChange = [this](int v) { editSong("Beats per bar " + String(v), [v](tracker::Song& s) { s.beatsPerBar = double(v); }); };
    tempoWatch_ = std::make_unique<ParamWatch>(param(processor, ids::tempoSource), [this](float v) {
        songMode_ = v > 0.5f || processor.ownsTransport();
        songStart_.setEnabled(songMode_);
        startLabel_.setColour(songMode_ ? colours::textDim : colours::lineSoft);
    });

    // The song tabs (section 18): one per loaded song, the active one live.
    tabs_.onActivate = [this](int i) {
        processor.setActiveTab(i);
        bar_ = 0;
        refreshViews();
        contextChanged();
    };
    tabs_.onClose = [this](int i) { closeTab(i); };
    tabs_.onNew = [this] {
        processor.newTab();
        bar_ = 0;
        refreshViews();
        contextChanged();
        message("New song " + String(CharPointer_UTF8("\xe2\x80\x94")) + " sixteen empty bars on the factory bank");
    };

    auto lane = std::make_unique<Hold>(grid_, PhraseGrid::preferredHeight());
    laneHold_ = lane.get();
    scroll_.setContent(std::move(lane));

    chain_.onSelectBar = [this](int bar) { bar_ = std::max(0, bar); refreshViews(); contextChanged(); };
    chain_.onChainChange = [this](int ch, int bar, int slot) {
        editSong("Chain: " + String(colours::channelName(ch)) + " bar " + String(bar + 1), [ch, bar, slot](tracker::Song& s) {
            if (bar < 0 || bar > 4095) return;
            auto& chain = s.chain[size_t(ch & 3)];
            if (int(chain.size()) <= bar) chain.resize(size_t(bar) + 1, 0);
            chain[size_t(bar)] = uint8_t(std::clamp(slot, 0, tracker::kPhraseSlots));
            if (slot >= 1 && slot <= tracker::kPhraseSlots) s.phrases[size_t(slot - 1)].used = true;
        });
    };
    // The bar's own step count (section 11): blank is the song's, and a
    // cleared last entry goes away so the song does not keep the bar.
    chain_.onBarStepsChange = [this](int bar, int steps) {
        editSong("Chain: bar " + String(bar + 1) + " steps", [bar, steps](tracker::Song& s) {
            if (bar < 0 || bar > 4095) return;
            if (int(s.barSteps.size()) <= bar) {
                if (steps == 0) return;
                s.barSteps.resize(size_t(bar) + 1, 0);
            }
            s.barSteps[size_t(bar)] = uint8_t(std::clamp(steps, 0, tracker::kMaxSteps));
            while (!s.barSteps.empty() && s.barSteps.back() == 0) s.barSteps.pop_back();
        });
    };
    grid_.onCellChange = [this](int ch, int step, const tracker::Cell& cell) {
        const int bar = bar_;
        editSong("Tracker: " + String(colours::channelName(ch)) + " bar " + String(bar + 1) + " step " + String(step + 1),
                 [ch, step, bar, cell](tracker::Song& s) {
            const uint8_t slot = ensurePhrase(s, ch, bar);
            if (slot == 0 || step < 0 || step >= tracker::kMaxSteps) return;
            s.phrases[size_t(slot - 1)].steps[size_t(step)] = cell;
        });
    };
    grid_.onSourceChange = [this](int ch, tracker::NoteSource src) {
        const char* what = src == tracker::NoteSource::Tracker ? "Trkr" : src == tracker::NoteSource::Hybrid ? "Hybrid" : "MIDI";
        editSong(String(colours::channelName(ch)) + " plays " + what,
                 [ch, src](tracker::Song& s) { s.noteSource[size_t(ch & 3)] = src; });
    };
    grid_.onArmChange = [this](int ch, bool on) { processor.setChannelArm(ch, on); refreshViews(); };
    grid_.onGrooveChange = [this](int ch, int groove) {
        const int bar = bar_;
        editSong(String(colours::channelName(ch)) + " phrase groove " + ValueFormat::number(groove), [ch, bar, groove](tracker::Song& s) {
            const uint8_t slot = ensurePhrase(s, ch, bar);
            if (slot) s.phrases[size_t(slot - 1)].groove = uint8_t(std::clamp(groove, 0, 16));
        });
    };
    // A cursor move or a click closes the run of digits being typed, so what
    // follows is a new undo (UI_DESIGN section 2.1).
    grid_.onEntryEnd = [this] { processor.history().endGesture(); };
    chain_.onEntryEnd = [this] { processor.history().endGesture(); };
    grid_.onCursorRow = [this](int row) {
        scroll_.scrollToKeepVisible(PhraseGrid::kHeaderHeight + row * PhraseGrid::kRowHeight, PhraseGrid::kRowHeight);
    };
    refreshViews();
    syncTransport(true);
}

TrackerPanel::~TrackerPanel() = default;

uint8_t TrackerPanel::ensurePhrase(tracker::Song& s, int ch, int bar)
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

void TrackerPanel::setChannel(int ch)
{
    EditorPanel::setChannel(ch);
    refreshViews();
}

RichText TrackerPanel::contextLine() const
{
    RichText r;
    r.bold(processor.tabName(processor.activeTab())).plain(middot());
    r.plain("Bar ").bold(String(bar_ + 1)).plain(middot()).bold(colours::channelName(channel));
    const auto s = processor.song();
    const int slot = s ? s->phraseAt(channel, bar_) : 0;
    if (slot) r.plain(" plays phrase ").bold(ValueFormat::number(slot));
    else r.plain(" has no phrase this bar " + String(CharPointer_UTF8("\xe2\x80\x94")) + " it just plays its notes");
    if (s) r.plain(middot() + String(s->stepsOfBar(bar_)) + " steps");
    return r;
}

void TrackerPanel::refreshViews()
{
    const auto s = processor.song();
    chain_.setSong(s, bar_, playingBar_);
    grid_.setBank(processor.bank());
    grid_.setSong(s, bar_);
    const int spb = s ? s->stepsOfBar(bar_) : 16;
    steps_.setValue(s ? s->steps() : 16, dontSendNotification);
    if (spb != gridSteps_) { gridSteps_ = spb; syncGridHeight(); }
    syncSongTime();
    syncTabs();
}

/// The lane is as tall as the bar's steps ask; past sixteen the tab's pane
/// scrolls (docs/COMMANDS_AND_TEMPO.md section 11).
void TrackerPanel::syncGridHeight()
{
    if (laneHold_ == nullptr) return;
    laneHold_->setHeight(PhraseGrid::heightForSteps(gridSteps_));
    scroll_.relayout();
}

/// The three fields that live in the song rather than in a parameter: its
/// master tempo (section 19), where it starts and how long its bar is. Read
/// back after every edit, after a tab switch and after a recording.
void TrackerPanel::syncSongTime()
{
    const auto s = processor.song();
    if (!s) return;
    tempo_.setValue(std::clamp(int(std::lround(s->tempoBpm)), 40, 255), dontSendNotification);
    songStart_.setValue(std::clamp(int(std::lround(s->songStartSeconds * kStartSteps)), 0, kStartMax), dontSendNotification);
    beats_.setValue(std::clamp(int(std::lround(s->beatsPerBar)), 1, 16), dontSendNotification);
}

/// The strip, from the processor's tabs (section 18). An unchanged list
/// costs a comparison, so this runs on the timer as well as after an edit.
void TrackerPanel::syncTabs()
{
    std::vector<SongTabInfo> list;
    const int n = processor.tabCount();
    list.reserve(size_t(n > 0 ? n : 0));
    for (int i = 0; i < n; ++i) {
        SongTabInfo t;
        t.name = processor.tabName(i);
        t.dirty = processor.tabDirty(i);
        const File f = processor.tabFile(i);
        t.tip = f != File() ? f.getFullPathName() : String("not saved to a file yet");
        list.push_back(std::move(t));
    }
    const int active = processor.activeTab();
    tabs_.setTabs(list, active);
    tabsShown_ = std::move(list);
    activeTabShown_ = active;
}

void TrackerPanel::editSong(const String& what, const std::function<void(tracker::Song&)>& fn)
{
    processor.editSong(what, fn);
    refreshViews();
    contextChanged();
}

void TrackerPanel::songChanged()
{
    refreshViews();
    contextChanged();
}

void TrackerPanel::hexChanged()
{
    chain_.repaint();
    grid_.repaint();
    contextChanged();
}

/* ------------------------------------------------------------- files */

void TrackerPanel::report(const RichText& text) { message(text.toString()); }

void TrackerPanel::closeTab(int index)
{
    if (index < 0 || index >= processor.tabCount()) return;
    if (processor.tabCount() <= 1) {
        message("The window keeps one song open. Start another with + first, or load one.");
        return;
    }
    const String name = processor.tabName(index);
    Component::SafePointer<TrackerPanel> safe(this);
    auto drop = [safe, index, name] {
        if (safe == nullptr || !safe->processor.closeTab(index)) return;
        safe->bar_ = 0;
        safe->refreshViews();
        safe->contextChanged();
        safe->message("Closed " + name);
    };
    if (!processor.tabDirty(index)) { drop(); return; }
    // Unsaved work is never dropped without asking (section 18).
    AlertWindow::showOkCancelBox(MessageBoxIconType::QuestionIcon, "ChipBoy",
                                 "\"" + name + "\" has been edited since it was saved. Close it and lose the changes?",
                                 "Close it", "Keep it", this,
                                 ModalCallbackFunction::create([drop](int r) { if (r == 1) drop(); }));
}

void TrackerPanel::saveSong()
{
    const int tab = processor.activeTab();
    const File current = processor.tabFile(tab);
    if (current != File()) {                     // a tab that came from a file writes back to it
        RichText r;
        if (processor.saveSongFile(current)) r.plain("Saved ").bold(current.getFileName()).plain(" to " + current.getParentDirectory().getFullPathName());
        else r.plain("Could not write ").bold(current.getFileName());
        report(r);
        refreshViews();
        contextChanged();
        return;
    }
    const String name = File::createLegalFileName(processor.tabName(tab));
    chooser_ = std::make_unique<FileChooser>("Save song", songsFolder().getChildFile((name.isEmpty() ? String("Song") : name) + kSongExtension),
                                             String("*") + kSongExtension, true, false, this);
    chooser_->launchAsync(kSaveFlags, [safe = Component::SafePointer<TrackerPanel>(this)](const FileChooser& fc) {
        if (safe == nullptr) return;
        File file = fc.getResult();
        if (file == File()) return;
        if (!file.hasFileExtension(kSongExtension)) file = file.withFileExtension(kSongExtension);
        RichText r;
        if (safe->processor.saveSongFile(file)) r.plain("Saved ").bold(file.getFileName()).plain(" to " + file.getParentDirectory().getFullPathName());
        else r.plain("Could not write ").bold(file.getFileName());
        safe->report(r);
        safe->refreshViews();
        safe->contextChanged();
    });
}

void TrackerPanel::loadSong()
{
    chooser_ = std::make_unique<FileChooser>("Open song", songsFolder(), String("*") + kSongExtension, true, false, this);
    chooser_->launchAsync(kOpenFlags, [safe = Component::SafePointer<TrackerPanel>(this)](const FileChooser& fc) {
        if (safe == nullptr) return;
        const File file = fc.getResult();
        if (!file.existsAsFile()) return;
        auto& proc = safe->processor;
        // A song opens in a tab of its own, unless the active tab is a fresh
        // untitled song with nothing in it to lose (section 18).
        const int tab = proc.activeTab();
        const bool intoThisTab = proc.tabFile(tab) == File() && !proc.tabDirty(tab);
        SongReport rep;
        RichText r;
        if (!(intoThisTab ? proc.loadSongFile(file, rep) : proc.openSongFileInTab(file, rep))) {
            r.plain("Could not read ").bold(file.getFileName());
            safe->report(r);
            return;
        }
        safe->bar_ = 0;
        safe->refreshViews();
        safe->contextChanged();
        r.plain(intoThisTab ? "Loaded " : "Opened ").bold(file.getFileNameWithoutExtension());
        r.plain(dash() + (rep.hasBank ? "with its own bank " : "written with bank ")).bold(rep.bankName.isEmpty() ? String("(unnamed)") : rep.bankName);
        if (rep.instrumentsUsed > 0) r.plain(", " + String(rep.instrumentsUsed) + (rep.instrumentsUsed == 1 ? " instrument" : " instruments") + " used");
        if (rep.hasBank) r.plain(". The song brought its own sounds.");
        else {
            StringArray lines = rep.differences;
            lines.addArray(rep.missing);
            if (lines.isEmpty()) r.plain(". This bank has every slot it names.");
            else r.plain(". ").bold(lines.joinIntoString(middot()));
        }
        safe->report(r);
    });
}

/* ------------------------------------------------------ the transport */

void TrackerPanel::syncTransport(bool force)
{
    const bool owns = processor.ownsTransport();
    if (force || owns != owns_) {
        owns_ = owns;
        for (auto* b : { &play_, &stop_, &loop_ }) b->setEnabled(owns);
        play_.setTooltip(owns ? "Play the song from where it stands, on the plugin's own clock at the Song tempo (the header's Tempo source is fixed there while it does)."
                              : "The host owns the transport: this only shows whether it is running. Play from the host.");
        stop_.setTooltip(owns ? "Stop, and leave the position where it stopped. Every channel is silenced."
                              : "The host owns the transport: stop it there.");
        loop_.setTooltip(owns ? "Loop the whole song: from bar 1 to its last bar, over and over."
                              : "The host owns the transport, so its own loop rules.");
        // Song is the only tempo source while the plugin owns the transport
        // (section 16), so the song's start is live either way. Beats is live
        // in both modes: the host never contributes a signature (section 19).
        songMode_ = owns || paramValue(processor, ids::tempoSource) != 0;
        songStart_.setEnabled(songMode_);
        startLabel_.setColour(songMode_ ? colours::textDim : colours::lineSoft);
    }
    // The plugin's own loop does nothing while a host runs the transport, so
    // the button does not claim to be on there.
    const bool on = owns && processor.loopEnabled();
    if (on != loopOn_) { loopOn_ = on; loop_.setToggleState(on, dontSendNotification); }
}

void TrackerPanel::tick()
{
    // The position is counted in ticks, so it reads the same whichever tempo
    // source is in force (docs/COMMANDS_AND_TEMPO.md section 4).
    const auto s = processor.song();
    const auto at = trackerPosition(processor);
    const bool playing = at.playing;
    const int bar = at.bar, inBar = at.inBar;
    const int spb = s ? s->stepsOfBar(bar) : 16;
    const int beat = inBar / driver::kTicksPerBeat;
    const int stepInBeat = (inBar - beat * driver::kTicksPerBeat) * spb / at.barTicks;
    pos_.setText(String(bar + 1) + "." + String(beat + 1) + "." + String(stepInBeat + 1));
    playLed_.setOn(playing);
    playText_.setText(playing ? "playing" : "stopped");
    if (playing != wasPlaying_) { wasPlaying_ = playing; play_.setToggleState(playing, dontSendNotification); }
    syncTransport(false);
    syncTabs();

    const int pb = playing ? bar : -1;
    bool views = false;
    if (pb != playingBar_) { playingBar_ = pb; views = true; }
    if (playing && bar != bar_) { bar_ = bar; views = true; contextChanged(); }   // the view follows the transport
    if (views) refreshViews();

    for (int ch = 0; ch < 4; ++ch) {
        const int st = playing && bar == bar_ && s ? playingStepOf(processor, *s, ch, bar, inBar) : -1;
        if (st != lastStep_[size_t(ch)]) { lastStep_[size_t(ch)] = st; grid_.setPlayingStep(ch, st); }
        // MIDI and Hybrid both take their notes from the host, so both show
        // the roll's note greyed in the note column (section 20).
        const bool roll = s && s->noteSource[size_t(ch)] != tracker::NoteSource::Tracker;
        const int note = roll ? processor.lastNotes[size_t(ch)].load() : -1;
        if (note != lastRoll_[size_t(ch)]) { lastRoll_[size_t(ch)] = note; grid_.setRollNote(ch, note); }
    }
    // Past sixteen steps the lane is taller than its pane, so it follows the
    // row the selected channel is playing.
    if (playing && gridSteps_ > PhraseGrid::kVisibleSteps) {
        const int st = lastStep_[size_t(channel)];
        if (st >= 0) scroll_.scrollToKeepVisible(PhraseGrid::kHeaderHeight + st * PhraseGrid::kRowHeight, PhraseGrid::kRowHeight);
    }
    const bool arm = processor.recordArm();
    if (arm != rec_.getToggleState()) rec_.setToggleState(arm, dontSendNotification);
}

/* ----------------------------------------------------------- layout */

/// The captions over the four groups and the hairline between them, so the
/// head reads as TRANSPORT | RECORD over SONG | FILE (section 23).
void TrackerPanel::paint(Graphics& g)
{
    for (int i = 0; i < 4; ++i) {
        const auto r = groups_[size_t(i)];
        if (r.isEmpty()) continue;
        draw::caption(g, kGroupNames[i], { r.getX(), r.getY(), r.getWidth(), kCaption }, Justification::centredLeft, colours::textDim, 9.0f);
        if (i == 0 || i == 2) continue;                     // the hairline stands before the second group of a row
        g.setColour(colours::lineSoft);
        g.fillRect(r.getX() - kGroupGap / 2, r.getY() + 2, 1, kCaption + kToolRow - 4);
    }
}

void TrackerPanel::resized()
{
    auto area = getLocalBounds();
    auto head = area.removeFromTop(kHeadHeight);
    // two rows, each a caption line over its controls, then the tab strip
    auto row1 = head.removeFromTop(kCaption + kToolRow);
    head.removeFromTop(kRowGap);
    auto row2 = head.removeFromTop(kCaption + kToolRow);
    head.removeFromTop(kStripGap);
    tabs_.setBounds(head.removeFromTop(SongTabStrip::kHeight));

    // A group is laid out left to right inside its row; `place` centres each
    // control on the control line under the caption.
    auto place = [](Rectangle<int>& row, Component& c, int w, int ch) {
        c.setBounds(row.removeFromLeft(w).withTrimmedTop(kCaption).withSizeKeepingCentre(w, ch));
    };
    auto label = [&place](Rectangle<int>& row, TextLine& t) { place(row, t, t.preferredWidth() + 4, kToolRow); };
    auto close = [this](int i, const Rectangle<int>& row, int from) {
        groups_[size_t(i)] = { from, row.getY(), row.getX() - from, kCaption + kToolRow };
    };

    // row 1: TRANSPORT -- what is playing and where it stands -- then RECORD
    int from = row1.getX();
    place(row1, play_, 62, 24);
    row1.removeFromLeft(4);
    place(row1, stop_, 62, 24);
    row1.removeFromLeft(4);
    place(row1, loop_, 52, 24);
    row1.removeFromLeft(14);
    place(row1, playLed_, 8, 8);
    row1.removeFromLeft(6);
    place(row1, playText_, 54, kToolRow);
    place(row1, pos_, 62, kToolRow);
    close(0, row1, from);
    row1.removeFromLeft(kGroupGap);
    from = row1.getX();
    place(row1, rec_, 62, 24);
    close(1, row1, from);

    // row 2: SONG -- its tempo and the shape of its bar (sections 11 and 19)
    // -- then FILE
    from = row2.getX();
    label(row2, tempoLabel_);
    row2.removeFromLeft(kLabelGap);
    place(row2, tempo_, 84, Stepper::kHeight);
    row2.removeFromLeft(kFieldGap);
    label(row2, startLabel_);
    row2.removeFromLeft(kLabelGap);
    place(row2, songStart_, 84, Stepper::kHeight);
    row2.removeFromLeft(kFieldGap);
    label(row2, beatsLabel_);
    row2.removeFromLeft(kLabelGap);
    place(row2, beats_, 62, Stepper::kHeight);
    row2.removeFromLeft(kFieldGap);
    label(row2, stepsLabel_);
    row2.removeFromLeft(kLabelGap);
    place(row2, steps_, 80, Stepper::kHeight);
    close(2, row2, from);
    row2.removeFromLeft(kGroupGap);
    from = row2.getX();
    place(row2, saveSong_, 104, 24);
    row2.removeFromLeft(6);
    place(row2, loadSong_, 104, 24);
    row2.removeFromLeft(12);
    place(row2, export_, 96, 24);
    close(3, row2, from);

    // the chain takes the column to the right of the lane, row for row with
    // the lane's steps (UI_DESIGN section 7)
    chain_.setBounds(area.removeFromRight(ChainColumn::kWidth));
    area.removeFromRight(kChainGap);
    scroll_.setBounds(area);
}

} // namespace chipboy::plugin
