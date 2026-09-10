#include "plugin/main/panels/TrackerPanel.h"

#include "plugin/main/panels/LsdjImportDialog.h"
#include "plugin/shared/LsdjImport.h"
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
      pos_("1" + String(CharPointer_UTF8("\xc2\xb7")) + "1   0.0 b", Fonts::mono(12.0f), colours::text),
      startLabel_("Start", Fonts::caption(10.0f), colours::textDim),
      tempoLabel_("Tempo", Fonts::caption(10.0f), colours::textDim),
      play_(String(CharPointer_UTF8("\xe2\x96\xb6 Play"))), stop_(String(CharPointer_UTF8("\xe2\x96\xa0 Stop"))), loop_("Loop"), follow_("Follow"),
      rec_(String(CharPointer_UTF8("\xe2\x97\x8f Rec"))),
      saveSong_("Save song" + ellipsis()), loadSong_("Load song" + ellipsis()), importSav_("Import .sav" + ellipsis()),
      export_("Export .gb" + ellipsis())
{
    for (auto* l : { &startLabel_, &tempoLabel_ }) l->setUpperCase(true);
    playLed_.setColour(colours::ok);
    playLed_.setInterceptsMouseClicks(false, false);
    for (auto* c : std::initializer_list<Component*>{ &play_, &stop_, &loop_, &follow_, &playLed_, &playText_, &pos_, &rec_,
                                                     &tempoLabel_, &tempo_, &startLabel_, &songStart_,
                                                     &saveSong_, &loadSong_, &importSav_, &export_, &tabs_, &scroll_, &chain_ }) addAndMakeVisible(c);

    // The transport (docs/COMMANDS_AND_TEMPO.md section 16). With no host
    // play head these run the song themselves; in a host they mirror it and
    // are disabled, so the buttons never disagree with what is playing.
    play_.onClick = [this] { processor.transportPlay(); };
    stop_.onClick = [this] { processor.transportStop(); };
    loop_.setClickingTogglesState(true);
    loop_.setColour(TextButton::textColourOnId, colours::text);
    loop_.setColour(TextButton::buttonOnColourId, colours::accentSoft);
    loop_.onClick = [this] {
        processor.setLoopRows(0, -1);              // the whole song, end to start
        processor.setLoop(loop_.getToggleState());
    };

    // Follow (UI_DESIGN D-UI-16): on, the row on show is the one playing; off,
    // the lane stays where it was put so another phrase can be edited while
    // this one is heard. The chain's per-channel marks light either way.
    follow_.setClickingTogglesState(true);
    follow_.setToggleState(true, dontSendNotification);
    follow_.setColour(TextButton::textColourOnId, colours::text);
    follow_.setColour(TextButton::buttonOnColourId, colours::accentSoft);
    follow_.setTooltip("Follow the transport: the row on show and the lane's scroll track what is playing. Off, they stay where you put them while the song plays.");
    follow_.onClick = [this] { followOn_ = follow_.getToggleState(); };

    rec_.setTooltip("Record: the MIDI arriving on an armed channel is written into its cells while the transport runs. The dot beside a channel's name is its arm.");
    rec_.setClickingTogglesState(true);
    rec_.setColour(TextButton::textColourOnId, colours::accentHi);
    rec_.setColour(TextButton::buttonOnColourId, colours::accentSoft);
    rec_.onClick = [this] { processor.setRecordArm(rec_.getToggleState()); };

    saveSong_.setTooltip("Write this song -- chains, phrases, grooves, arms, its tempo and the bank it plays through -- to a .cbsong file.");
    saveSong_.onClick = [this] { saveSong(); };
    loadSong_.setTooltip("Read a .cbsong file into a tab of its own, with the bank it was written with.");
    loadSong_.onClick = [this] { loadSong(); };

    importSav_.setTooltip("Read an LSDj .sav: pick the songs it holds -- the working song and every saved file -- and open each in a tab with its own bank, read by the rules of its LSDj version.");
    importSav_.onClick = [this] { importSav(); };

    export_.setEnabled(false);
    export_.setTooltip("Later: compile this song into a playback ROM for real hardware.");

    // The song's own tempo and timeline (sections 4 and 19). Whose beat the
    // ticks follow is the header's Tempo group, which only reads the tempo
    // out now; the master tempo is typed here, because it belongs to the
    // song and the window can hold several.
    tempo_.setRange(40, 255, 120);
    tempo_.setTyped(true);
    tempo_.setTooltip("This song's master tempo, 40-255 BPM: the base its T commands move from, and what the header reads in Song mode.");
    tempo_.onChange = [this](int v) { processor.setMasterTempo(double(v)); refreshViews(); contextChanged(); };
    songStart_.setRange(0, kStartMax, 0);
    songStart_.setTooltip("Where tick 0 of the song sits on the host's timeline, in seconds");
    songStart_.setTextFunction([](int v) { return String(double(v) / kStartSteps, 1) + " s"; });
    // The tempo is BPM, decimal in either display -- LSDj shows it so too;
    // only its T byte is hex (section 52).
    tempo_.setTextFunction([](int v) { return String(v); });
    tempo_.setEntryFormat([](int v) { return String(v); },
                          [](const String& t, int& out) { const String d = t.trim(); if (d.isEmpty() || !d.containsOnly("0123456789")) return false; out = d.getIntValue(); return true; });
    songStart_.onChange = [this](int v) { editSong("Song start", [v](tracker::Song& s) { s.songStartSeconds = double(v) / kStartSteps; }); };
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
        editSong("Chain: " + String(colours::channelName(ch)) + " row " + String(bar + 1), [ch, bar, slot](tracker::Song& s) {
            if (bar < 0 || bar > 4095) return;
            auto& chain = s.chain[size_t(ch & 3)];
            if (int(chain.size()) <= bar) chain.resize(size_t(bar) + 1, 0);
            chain[size_t(bar)] = uint8_t(std::clamp(slot, 0, tracker::kPhraseSlots));
            if (slot >= 1 && slot <= tracker::kPhraseSlots) s.phrases[size_t(slot - 1)].used = true;
        });
    };
    // The row's transpose on a channel (docs/COMMANDS_AND_TEMPO.md section 48).
    chain_.onChainTransposeChange = [this](int ch, int bar, int semis) {
        editSong("Chain: " + String(colours::channelName(ch)) + " row " + String(bar + 1) + " transpose", [ch, bar, semis](tracker::Song& s) {
            if (bar < 0 || bar > 4095) return;
            s.setTranspose(ch, bar, int8_t(std::clamp(semis, -128, 127)));
        });
    };
    // The chain's last column is the row's LEN: it sets the length of every
    // phrase the row holds (section 25). The lane's head does one channel.
    chain_.onRowLengthChange = [this](int row, int steps) {
        if (steps <= 0) return;
        editSong("Chain: row " + String(row + 1) + " LEN", [row, steps](tracker::Song& s) {
            if (row < 0 || row > 4095) return;
            for (int ch = 0; ch < 4; ++ch) {
                const int slot = s.phraseAt(ch, row);
                if (slot >= 1 && slot <= tracker::kPhraseSlots) s.phrases[size_t(slot - 1)].steps = uint8_t(std::clamp(steps, 1, tracker::kMaxSteps));
            }
        });
    };
    grid_.onCellChange = [this](int ch, int step, const tracker::Cell& cell) {
        const int bar = bar_;
        editSong("Tracker: " + String(colours::channelName(ch)) + " row " + String(bar + 1) + " step " + String(step + 1),
                 [ch, step, bar, cell](tracker::Song& s) {
            const uint8_t slot = ensurePhrase(s, ch, bar);
            if (slot == 0 || step < 0 || step >= tracker::kMaxSteps) return;
            s.phrases[size_t(slot - 1)].cells[size_t(step)] = cell;
        });
    };
    grid_.onSourceChange = [this](int ch, tracker::NoteSource src) {
        const char* what = src == tracker::NoteSource::Tracker ? "Trkr" : src == tracker::NoteSource::Hybrid ? "Hybrid" : "MIDI";
        editSong(String(colours::channelName(ch)) + " plays " + what,
                 [ch, src](tracker::Song& s) { s.noteSource[size_t(ch & 3)] = src; });
    };
    grid_.onArmChange = [this](int ch, bool on) { processor.setChannelArm(ch, on); refreshViews(); };
    // The head's LEN: this channel's own phrase, in the row on show.
    grid_.onLengthChange = [this](int ch, int steps) {
        const int row = bar_;
        editSong(String(colours::channelName(ch)) + " phrase length " + String(steps), [ch, row, steps](tracker::Song& s) {
            const int slot = s.phraseAt(ch, row);
            if (slot >= 1 && slot <= tracker::kPhraseSlots) s.phrases[size_t(slot - 1)].steps = uint8_t(std::clamp(steps, 1, tracker::kMaxSteps));
        });
    };
    grid_.onOpenSlot = [this](ui::SlotKind kind, int slot) { openSlot(kind, slot); };
    grid_.onGrooveChange = [this](int ch, int groove) {
        const int bar = bar_;
        editSong(String(colours::channelName(ch)) + " phrase groove " + ValueFormat::slot(groove), [ch, bar, groove](tracker::Song& s) {
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
    r.plain("Row ").bold(ValueFormat::index(bar_)).plain(middot()).bold(colours::channelName(channel));
    const auto s = processor.song();
    const int slot = s ? s->phraseAt(channel, bar_) : 0;
    if (slot) r.plain(" plays phrase ").bold(ValueFormat::slot(slot));
    else r.plain(" has no phrase here " + String(CharPointer_UTF8("\xe2\x80\x94")) + " it just plays its notes");
    if (s) r.plain(middot() + String(s->stepsOfRow(channel, bar_)) + " steps");
    return r;
}

void TrackerPanel::refreshViews()
{
    const auto s = processor.song();
    chain_.setSong(s, bar_, playingRow_.data());
    grid_.setBank(processor.bank());
    grid_.setSong(s, bar_);
    // The lane is as tall as the longest phrase in the row, so every cell it
    // holds can be reached; each head's LEN says how far its channel runs.
    int spb = 16;
    if (s) { spb = 1; for (int ch = 0; ch < 4; ++ch) spb = std::max(spb, s->stepsOfRow(ch, bar_)); }
    if (spb != gridSteps_) { gridSteps_ = spb; syncGridHeight(); }
    syncSongTime();
    syncTabs();
}

/// The lane is as tall as the longest phrase in the row asks; past sixteen
/// the tab's pane scrolls (docs/COMMANDS_AND_TEMPO.md section 25).
void TrackerPanel::syncGridHeight()
{
    if (laneHold_ == nullptr) return;
    laneHold_->setHeight(PhraseGrid::heightForSteps(gridSteps_));
    scroll_.relayout();
}

/// The two fields that live in the song rather than in a parameter: its
/// master tempo (section 19) and where it starts. Read back after every
/// edit, after a tab switch and after a recording.
void TrackerPanel::syncSongTime()
{
    const auto s = processor.song();
    if (!s) return;
    tempo_.setValue(std::clamp(int(std::lround(s->tempoBpm)), 40, 255), dontSendNotification);
    songStart_.setValue(std::clamp(int(std::lround(s->songStartSeconds * kStartSteps)), 0, kStartMax), dontSendNotification);

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
    tabs_.setTabs(list, processor.activeTab());
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

void TrackerPanel::restoreView(const juce::ValueTree& v)
{
    if (v.hasProperty("follow")) { followOn_ = bool(v["follow"]); follow_.setToggleState(followOn_, dontSendNotification); }
    if (!v.hasProperty("row")) return;
    const auto s = processor.song();
    const int rows = s ? s->rows() : 0;
    bar_ = std::clamp(int(v["row"]), 0, std::max(0, rows));
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

void TrackerPanel::importSav()
{
    chooser_ = std::make_unique<FileChooser>("Import from LSDj", songsFolder(), "*.sav", true, false, this);
    chooser_->launchAsync(kOpenFlags, [safe = Component::SafePointer<TrackerPanel>(this)](const FileChooser& fc) {
        if (safe == nullptr) return;
        const File file = fc.getResult();
        if (!file.existsAsFile()) return;
        SavePreview preview; String error;
        if (!readSave(file, preview, error)) { RichText r; r.plain("Not an LSDj save: ").bold(error); safe->report(r); return; }
        LsdjImportDialog::show(safe->processor, std::move(preview), safe.getComponent(), [safe, name = file.getFileName()](const LsdjImportDialog::Outcome& out) {
            if (safe == nullptr) return;
            safe->bar_ = 0;
            safe->refreshViews();
            safe->contextChanged();
            RichText r;
            r.plain("Imported ").bold(String(out.songs) + (out.songs == 1 ? " song" : " songs")).plain(" from ").bold(name);
            if (!out.notes.isEmpty()) r.plain(middot() + String(out.notes.size()) + (out.notes.size() == 1 ? " note" : " notes"));
            safe->report(r);
        });
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
    // The song's time, and the selected channel's own row and step (25).
    const int bar = at.row[channel & 3], inBar = at.inRow[channel & 3];
    const int step = s ? playingStepOf(processor, *s, channel & 3, bar, inBar) : -1;
    // The song's time, and the selected channel's own row and step (25).
    pos_.setText(String(bar + 1) + String(CharPointer_UTF8("\xc2\xb7")) + String(step + 1) + "   "
                 + String(double(at.tick) / double(driver::kTicksPerBeat), 1) + " b");
    playLed_.setOn(playing);
    playText_.setText(playing ? "playing" : "stopped");
    if (playing != wasPlaying_) { wasPlaying_ = playing; play_.setToggleState(playing, dontSendNotification); }
    syncTransport(false);
    syncTabs();

    // Every channel's own row, for the chain column's per-channel highlight.
    bool views = false;
    for (int ch = 0; ch < 4; ++ch) {
        const int row = playing ? at.row[size_t(ch)] : -1;
        if (row != playingRow_[size_t(ch)]) { playingRow_[size_t(ch)] = row; views = true; }
    }
    if (playing && followOn_ && bar != bar_) { bar_ = bar; views = true; contextChanged(); }   // the view follows the transport (D-UI-16)
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
    if (playing && followOn_ && gridSteps_ > PhraseGrid::kVisibleSteps) {
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
    row1.removeFromLeft(4);
    place(row1, follow_, 58, 24);
    row1.removeFromLeft(14);
    place(row1, playLed_, 8, 8);
    row1.removeFromLeft(6);
    place(row1, playText_, 54, kToolRow);
    place(row1, pos_, 110, kToolRow);
    close(0, row1, from);
    row1.removeFromLeft(kGroupGap);
    from = row1.getX();
    place(row1, rec_, 62, 24);
    close(1, row1, from);

    // row 2: SONG -- its tempo and where it starts, the two things that are
    // still the song's own (sections 19 and 25) -- then FILE
    from = row2.getX();
    label(row2, tempoLabel_);
    row2.removeFromLeft(kLabelGap);
    place(row2, tempo_, 84, Stepper::kHeight);
    row2.removeFromLeft(kFieldGap);
    label(row2, startLabel_);
    row2.removeFromLeft(kLabelGap);
    place(row2, songStart_, 84, Stepper::kHeight);
    close(2, row2, from);
    row2.removeFromLeft(kGroupGap);
    from = row2.getX();
    place(row2, saveSong_, 104, 24);
    row2.removeFromLeft(6);
    place(row2, loadSong_, 104, 24);
    row2.removeFromLeft(6);
    place(row2, importSav_, 108, 24);
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
