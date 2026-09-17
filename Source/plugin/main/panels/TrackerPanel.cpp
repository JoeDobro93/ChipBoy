#include "core/Driver/Clock.h"
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
/// tools, each under its caption, over the song tab strip -- 12 caption +
/// 26 controls, 2, 12 + 26, 4, and the 26 px strip: 108 px. The four px the
/// gaps gave up went to the lane's head, which grew a chip row (D-UI-37),
/// so the lane keeps its sixteen 22 px rows and the tab asks for no
/// scrolling at the window's minimum height (UI_DESIGN sections 2 and 7).
constexpr int kCaption = 12, kToolRow = 26, kRowGap = 2, kStripGap = 4;
constexpr int kHeadHeight = 2 * (kCaption + kToolRow) + kRowGap + kStripGap + SongTabStrip::kHeight;
static_assert(kHeadHeight == 108, "the head keeps its budget");
constexpr int kChainGap = 12;
/// Between two groups of one row, with the hairline in the middle of it;
/// then between two fields of a group, and between a caption and its field.
constexpr int kGroupGap = 16, kFieldGap = 14, kLabelGap = 2;
/// Song start is held in tenths of a second so a stepper can reach it.
constexpr int kStartSteps = 10, kStartMax = 600 * kStartSteps;
constexpr int kOpenFlags = FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles;
constexpr int kSaveFlags = FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::warnAboutOverwriting;
const char* kGroupNames[4] = { "Record", "Song", "File", "Transport" };
String middot() { return String(CharPointer_UTF8(" \xc2\xb7 ")); }
String dash() { return String(CharPointer_UTF8(" \xe2\x80\x94 ")); }
String ellipsis() { return String(CharPointer_UTF8("\xe2\x80\xa6")); }
}

TrackerPanel::TrackerPanel(ChipBoyProcessor& p)
    : EditorPanel(p),
      pos_("1" + String(CharPointer_UTF8("\xc2\xb7")) + "1" + String(CharPointer_UTF8("\xc2\xb7")) + "0", Fonts::mono(12.0f), colours::text),
      startLabel_("Start", Fonts::caption(10.0f), colours::textDim),
      tempoLabel_("Tempo", Fonts::caption(10.0f), colours::textDim),
      transposeLabel_("Transpose", Fonts::caption(10.0f), colours::textDim),
      zoomLabel_("Zoom", Fonts::caption(10.0f), colours::textDim),
      gridText_("", Fonts::mono(9.5f), colours::textDim),
      play_(IconButton::Icon::Play), stop_(IconButton::Icon::Stop), loop_(IconButton::Icon::Loop), follow_(IconButton::Icon::Follow),
      rec_(String(CharPointer_UTF8("\xe2\x97\x8f Rec"))),
      saveSong_("Save song" + ellipsis()), loadSong_("Load song" + ellipsis()), importSav_("Import .sav" + ellipsis()),
      export_("Export .gb" + ellipsis())
{
    for (auto* l : { &startLabel_, &tempoLabel_, &transposeLabel_, &zoomLabel_ }) l->setUpperCase(true);
    playLed_.setColour(colours::ok);
    playLed_.setInterceptsMouseClicks(false, false);
    for (auto* c : std::initializer_list<Component*>{ &play_, &stop_, &loop_, &follow_, &playLed_, &pos_, &rec_, &zoomLabel_, &zoom_, &gridText_,
                                                     &tempoLabel_, &tempo_, &transposeLabel_, &transpose_, &startLabel_, &songStart_,
                                                     &saveSong_, &loadSong_, &importSav_, &export_, &tabs_, &scroll_, &chain_ }) addAndMakeVisible(c);

    // The transport (docs/COMMANDS_AND_TEMPO.md sections 16 and 223), over
    // the chain (D-UI-36). With no host play head these run the song
    // themselves -- Play from the play head, Pause where it stands, Stop
    // back to the start; in a host they mirror it and are disabled, so the
    // buttons never disagree with what is playing.
    play_.onClick = [this] { playPause(); };
    stop_.onClick = [this] { processor.transportStop(); cursor_ = 0; refreshViews(); contextChanged(); };
    loop_.setClickingTogglesState(true);
    loop_.onClick = [this] {
        processor.setLoopTicks(loopFrom_, loopTo_);
        processor.setLoop(loop_.getToggleState());
        refreshViews();
    };
    pos_.setTooltip("The play head in the song's own bars: bar, beat and the tick into the beat, by the time signatures the chain's gutter shows.");

    // Follow (UI_DESIGN D-UI-16): on, the play head is the transport's and
    // the lanes show what plays; off, it stays where it was put so another
    // phrase can be edited while this one is heard.
    follow_.setClickingTogglesState(true);
    follow_.setToggleState(true, dontSendNotification);
    follow_.setTooltip("Follow the transport: the play head, the lanes and the chain's scroll track what is playing. Off, they stay where you put them while the song plays.");
    follow_.onClick = [this] { followOn_ = follow_.getToggleState(); };

    // The zoom (D-UI-35): pixels per tick on the chain, the grid derived
    // from the first time signature; the readout beside it says the grid.
    zoom_.setSliderStyle(Slider::LinearHorizontal);
    zoom_.setTextBoxStyle(Slider::NoTextBox, false, 0, 0);
    zoom_.setRange(0.0, 1.0, 0.0);
    zoom_.setValue(chain_.zoom(), dontSendNotification);
    zoom_.setSliderSnapsToMousePosition(true);
    zoom_.setTooltip("Zoom the chain: from four bars a row to one tick a row. The grid the play head snaps to is the finest division of the first time signature that leaves a row's height between lines.");
    zoom_.onValueChange = [this] { chain_.setZoom(zoom_.getValue()); gridText_.setText(chain_.gridText()); };
    gridText_.setTooltip("What one grid line is at this zoom: the play head snaps to it on a click or a drag in the gutter, PgUp and PgDn move by it.");

    rec_.setTooltip("Record: the MIDI arriving on an armed channel is written into its cells while the transport runs. The dot beside a channel's name is its arm.");
    rec_.setClickingTogglesState(true);
    rec_.setColour(TextButton::textColourOnId, colours::accentHi);
    rec_.setColour(TextButton::buttonOnColourId, colours::accentSoft);
    rec_.onClick = [this] { processor.setRecordArm(rec_.getToggleState()); };

    saveSong_.setTooltip("Write this song -- chains, phrases, grooves, arms, its tempo and the bank it plays through -- to a .cbsong file.");
    saveSong_.onClick = [this] { saveSong(); };
    loadSong_.setTooltip("Read a .cbsong file into a tab of its own, with the bank it was written with.");
    loadSong_.onClick = [this] { loadSong(); };

    importSav_.setTooltip("Read an LSDj .sav, or .lsdprj / .lsdsng project files: pick the songs -- the working song, every saved file, each project -- and open each in a tab with its own bank, read by the rules of its LSDj version.");
    importSav_.onClick = [this] { importSav(); };

    export_.setEnabled(false);
    export_.setTooltip("Later: compile this song into a playback ROM for real hardware.");

    // The song's own tempo and timeline (sections 4 and 19). Whose beat the
    // ticks follow is the header's Tempo group, which only reads the tempo
    // out now; the master tempo is typed here, because it belongs to the
    // song and the window can hold several.
    tempo_.setRange(int(driver::kMinSongBpm), int(driver::kMaxSongBpm), 120);   // section 219: 299, 448, 896 are LSDj's 2x, 3x, 6x
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
    // The whole song's transpose (section 61): LSDj's PROJECT TRANSPOSE, and
    // the same two's-complement byte in Hex that the chain's column shows.
    transpose_.setRange(-128, 127, 0);
    transpose_.setTyped(true);
    transpose_.setTransposeNumbering(true);
    transpose_.setTooltip("Semitones added to every note of this song whose instrument admits a transpose, on top of the chain's own column. LSDj's PROJECT TRANSPOSE.");
    transpose_.onChange = [this](int v) { editSong("Song transpose", [v](tracker::Song& s) { s.transpose = int8_t(v); }); };
    tempoWatch_ = std::make_unique<ParamWatch>(param(processor, ids::tempoSource), [this](float v) {
        songMode_ = v > 0.5f || processor.ownsTransport();
        songStart_.setEnabled(songMode_);
        startLabel_.setColour(songMode_ ? colours::textDim : colours::lineSoft);
    });

    // The song tabs (section 18): one per loaded song, the active one live.
    tabs_.onActivate = [this](int i) {
        processor.setActiveTab(i);
        cursor_ = 0;
        refreshViews();
        contextChanged();
    };
    tabs_.onClose = [this](int i) { closeTab(i); };
    tabs_.onNew = [this] {
        processor.newTab();
        cursor_ = 0;
        refreshViews();
        contextChanged();
        message("New song " + String(CharPointer_UTF8("\xe2\x80\x94")) + " sixteen empty bars on the factory bank");
    };

    auto lane = std::make_unique<Hold>(grid_, PhraseGrid::preferredHeight());
    laneHold_ = lane.get();
    scroll_.setContent(std::move(lane));

    // The play head moved by hand (D-UI-35): the lanes look there, and a
    // running own transport jumps there too (section 223).
    chain_.onSelectTick = [this](int64_t t) {
        cursor_ = std::max<int64_t>(0, t);
        if (processor.ownsTransport() && processor.transportPlaying()) processor.transportLocate(cursor_);
        refreshViews();
        contextChanged();
    };
    chain_.onPlayPause = [this] { playPause(); };
    // The loop region (section 223): Shift-dragged on the gutter, and on
    // the moment it is; cleared, the whole song loops as before.
    chain_.onLoopRegion = [this](int64_t from, int64_t to) {
        loopFrom_ = from; loopTo_ = to;
        processor.setLoopTicks(from, to);
        if (to >= 0) processor.setLoop(true);
        syncTransport(false);
        refreshViews();
    };
    // The time signatures (section 222): the editor hands the whole list
    // back and the song keeps it sorted, one per tick, tick 0 present.
    chain_.onSignaturesChange = [this](std::vector<tracker::TimeSignature> list) {
        editSong("Time signatures", [list](tracker::Song& s) { s.signatures = list; tracker::normalizeSignatures(s); });
    };
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
    // Section 212: the channel plays its chain round again, or stops at its end.
    chain_.onChainEndChange = [this](int ch, bool loop) {
        editSong("Chain: " + String(colours::channelName(ch)) + (loop ? " loops" : " stops"), [ch, loop](tracker::Song& s) {
            s.chainEnd[size_t(ch & 3)] = loop ? tracker::ChainEnd::Loop : tracker::ChainEnd::Stop;
        });
    };
    grid_.onCellChange = [this](int ch, int step, const tracker::Cell& cell) {
        const int bar = rowOf(ch);
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
    // The head's STEPS: this channel's own phrase, in its row at the play head.
    grid_.onLengthChange = [this](int ch, int steps) {
        const int row = rowOf(ch);
        editSong(String(colours::channelName(ch)) + " phrase length " + String(steps), [ch, row, steps](tracker::Song& s) {
            const int slot = s.phraseAt(ch, row);
            if (slot >= 1 && slot <= tracker::kPhraseSlots) s.phrases[size_t(slot - 1)].steps = uint8_t(std::clamp(steps, 1, tracker::kMaxSteps));
        });
    };
    // The head's TSP: the row's transpose on this channel (section 48).
    grid_.onTransposeChange = [this](int ch, int semis) {
        const int row = rowOf(ch);
        editSong("Chain: " + String(colours::channelName(ch)) + " row " + String(row + 1) + " transpose", [ch, row, semis](tracker::Song& s) {
            if (row < 0 || row > 4095) return;
            s.setTranspose(ch, row, int8_t(std::clamp(semis, -128, 127)));
        });
    };
    grid_.onOpenSlot = [this](ui::SlotKind kind, int slot) { openSlot(kind, slot); };
    grid_.onGrooveChange = [this](int ch, int groove) {
        const int bar = rowOf(ch);
        editSong(String(colours::channelName(ch)) + " phrase groove " + ValueFormat::slot(groove), [ch, bar, groove](tracker::Song& s) {
            const uint8_t slot = ensurePhrase(s, ch, bar);
            if (slot) s.phrases[size_t(slot - 1)].groove = uint8_t(std::clamp(groove, 0, tracker::kGrooveSlots));
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

int TrackerPanel::rowOf(int ch) const
{
    const auto s = processor.song();
    if (!s) return 0;
    int row = 0, inRow = 0;
    tracker::rowAtTick(*s, ch, cursor_, row, inRow);
    return row;
}

void TrackerPanel::playPause()
{
    if (!processor.ownsTransport()) return;
    if (processor.transportPlaying()) { processor.transportPause(); return; }
    processor.transportLocate(cursor_);
    processor.transportPlay();
}

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
    const auto s = processor.song();
    const int row = rowOf(channel);
    r.bold(colours::channelName(channel)).plain(" row ").bold(ValueFormat::index(row));
    const int slot = s ? s->phraseAt(channel, row) : 0;
    if (slot) r.plain(" plays phrase ").bold(ValueFormat::slot(slot));
    else r.plain(" has no phrase here " + String(CharPointer_UTF8("\xe2\x80\x94")) + " it just plays its notes");
    if (s) r.plain(middot() + String(s->stepsOfRow(channel, row)) + " steps, " + String(tracker::rowTicks(*s, channel, row)) + " ticks");
    return r;
}

void TrackerPanel::refreshViews()
{
    const auto s = processor.song();
    if (s) cursor_ = std::clamp<int64_t>(cursor_, 0, std::max<int64_t>(tracker::kEmptyRowTicks, tracker::songTicks(*s)));
    chain_.setSong(s, cursor_);
    chain_.setLoopRegion(processor.loopEnabled() && loopTo_ >= 0, loopFrom_, loopTo_);
    gridText_.setText(chain_.gridText());
    grid_.setBank(processor.bank());
    // Each channel's own row at the play head (D-UI-35): the lane is as tall
    // as the longest of the four phrases, so every cell it holds can be
    // reached, and each head's STEPS says how far its channel runs.
    int rows[4] = { 0, 0, 0, 0 };
    int spb = 16;
    if (s) { spb = 1; for (int ch = 0; ch < 4; ++ch) { rows[ch] = rowOf(ch); spb = std::max(spb, s->stepsOfRow(ch, rows[ch])); } }
    grid_.setSong(s, rows);
    for (int ch = 0; ch < 4; ++ch) shownRow_[size_t(ch)] = rows[ch];
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
    tempo_.setValue(std::clamp(int(std::lround(s->tempoBpm)), int(driver::kMinSongBpm), int(driver::kMaxSongBpm)), dontSendNotification);
    songStart_.setValue(std::clamp(int(std::lround(s->songStartSeconds * kStartSteps)), 0, kStartMax), dontSendNotification);
    transpose_.setValue(int(s->transpose), dontSendNotification);

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
    if (v.hasProperty("zoom")) { chain_.setZoom(double(v["zoom"])); zoom_.setValue(chain_.zoom(), dontSendNotification); }
    const auto s = processor.song();
    if (v.hasProperty("tick")) cursor_ = std::max<int64_t>(0, v["tick"].toString().getLargeIntValue());
    else if (v.hasProperty("row") && s) cursor_ = tracker::rowStartTick(*s, 0, std::clamp(int(v["row"]), 0, std::max(0, s->rows())));   // a state written before the play head was a tick
    else return;
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
        safe->cursor_ = 0;
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
    chooser_ = std::make_unique<FileChooser>("Import from LSDj", songsFolder(), "*.sav;*.lsdprj;*.lsdsng", true, false, this);
    chooser_->launchAsync(kOpenFlags | FileBrowserComponent::canSelectMultipleItems, [safe = Component::SafePointer<TrackerPanel>(this)](const FileChooser& fc) {
        if (safe == nullptr) return;
        const Array<File> files = fc.getResults();
        if (files.isEmpty()) return;
        SavePreview preview; String error;
        if (!readImportFiles(files, preview, error)) { RichText r; r.plain("Not an LSDj save or project: ").bold(error); safe->report(r); return; }
        if (error.isNotEmpty()) { RichText r; r.plain("Skipped: ").bold(error); safe->report(r); }
        const String name = files.size() == 1 ? files[0].getFileName() : String(files.size()) + " files";
        LsdjImportDialog::show(safe->processor, std::move(preview), safe.getComponent(), [safe, name](const LsdjImportDialog::Outcome& out) {
            if (safe == nullptr) return;
            safe->cursor_ = 0;
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
        safe->cursor_ = 0;
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
        for (auto* b : std::initializer_list<Component*>{ &play_, &stop_, &loop_ }) b->setEnabled(owns);
        play_.setTooltip(owns ? "Play from the play head on the plugin's own clock at the Song tempo (the header's Tempo source is fixed there while it does); Pause while it plays, and it stays where it stands. Space does the same with the chain focused."
                              : "The host owns the transport: this only shows whether it is running. Play from the host.");
        stop_.setTooltip(owns ? "Stop, and return to the song's start. Every channel is silenced."
                              : "The host owns the transport: stop it there.");
        loop_.setTooltip(owns ? "Loop: the region Shift-dragged on the chain's gutter, or the whole song, over and over."
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
    if (on != loopOn_) { loopOn_ = on; loop_.setToggleState(on, dontSendNotification); chain_.setLoopRegion(on && loopTo_ >= 0, loopFrom_, loopTo_); }
}

void TrackerPanel::tick()
{
    // The position is counted in ticks, so it reads the same whichever tempo
    // source is in force (docs/COMMANDS_AND_TEMPO.md section 4).
    const auto s = processor.song();
    const auto at = trackerPosition(processor);
    const bool playing = at.playing;
    bool views = false;
    // Follow (D-UI-16): the play head is the transport's while it plays, and
    // a pause leaves it where it stopped. The chain redraws on every move;
    // the lanes only when a channel's row changed.
    if ((playing || wasPlaying_) && followOn_ && at.tick != cursor_ && processor.ownsTransport()) { cursor_ = at.tick; chain_.setSong(s, cursor_); }
    else if (playing && followOn_ && at.tick != cursor_) { cursor_ = at.tick; chain_.setSong(s, cursor_); }
    chain_.setTransport(playing, at.tick, followOn_);
    // The readout: the play head in the song's own bars (section 222).
    if (s) {
        const auto q = tracker::barPositionAt(*s, cursor_);
        pos_.setText(String(q.bar) + String(CharPointer_UTF8("\xc2\xb7")) + String(q.beat) + String(CharPointer_UTF8("\xc2\xb7")) + String(q.tick));
    }
    playLed_.setOn(playing);
    if (playing != wasPlaying_) {
        wasPlaying_ = playing;
        play_.setToggleState(playing, dontSendNotification);
        play_.setIcon(playing ? IconButton::Icon::Pause : IconButton::Icon::Play);
    }
    syncTransport(false);
    syncTabs();
    // A channel that moved to another row at the play head redraws its lane.
    if (!views && s) for (int ch = 0; ch < 4; ++ch) if (rowOf(ch) != shownRow_[size_t(ch)]) views = true;
    if (views) { refreshViews(); contextChanged(); }

    for (int ch = 0; ch < 4; ++ch) {
        // The step lit in a lane is the one its own channel is playing, in
        // the row the lane shows (section 25); with Follow off and the
        // transport elsewhere, nothing is lit.
        const int st = playing && s && at.row[size_t(ch)] == shownRow_[size_t(ch)] ? playingStepOf(processor, *s, ch, at.row[size_t(ch)], at.inRow[size_t(ch)]) : -1;
        if (st != lastStep_[size_t(ch)]) { lastStep_[size_t(ch)] = st; grid_.setPlayingStep(ch, st); }
        // MIDI and Hybrid both take their notes from the host, so both show
        // the roll's note greyed in the note column (section 20).
        const bool roll = s && s->noteSource[size_t(ch)] != tracker::NoteSource::Tracker;
        const int note = roll ? processor.lastNotes[size_t(ch)].load() : -1;
        if (note != lastRoll_[size_t(ch)]) { lastRoll_[size_t(ch)] = note; grid_.setRollNote(ch, note); }
    }
    // Past sixteen steps the lane is taller than its pane, so it follows the
    // step the selected channel is playing.
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
        if (i == 0 || i == 2) continue;                     // the hairline stands before the second group of a row, and before the transport's column
        g.setColour(colours::lineSoft);
        g.fillRect(r.getX() - kGroupGap / 2, r.getY() + 2, 1, r.getHeight() - 4);
    }
}

void TrackerPanel::resized()
{
    auto area = getLocalBounds();
    auto head = area.removeFromTop(kHeadHeight);
    // the tab strip closes the head across the whole width; over it, the
    // two tool rows on the left and the transport's column over the chain
    tabs_.setBounds(head.removeFromBottom(SongTabStrip::kHeight));
    head.removeFromBottom(kStripGap);
    auto right = head.removeFromRight(ChainColumn::kWidth);
    head.removeFromRight(kChainGap);
    auto row1 = head.removeFromTop(kCaption + kToolRow);
    head.removeFromTop(kRowGap);
    auto row2 = head.removeFromTop(kCaption + kToolRow);

    // A group is laid out left to right inside its row; `place` centres each
    // control on the control line under the caption.
    auto place = [](Rectangle<int>& row, Component& c, int w, int ch) {
        c.setBounds(row.removeFromLeft(w).withTrimmedTop(kCaption).withSizeKeepingCentre(w, ch));
    };
    auto label = [&place](Rectangle<int>& row, TextLine& t) { place(row, t, t.preferredWidth() + 4, kToolRow); };
    auto close = [this](int i, const Rectangle<int>& row, int from) {
        groups_[size_t(i)] = { from, row.getY(), row.getX() - from, kCaption + kToolRow };
    };

    // row 1: RECORD, then SONG -- its tempo, transpose and where it starts,
    // the things that are still the song's own (sections 19 and 25)
    int from = row1.getX();
    place(row1, rec_, 62, 24);
    close(0, row1, from);
    row1.removeFromLeft(kGroupGap);
    from = row1.getX();
    label(row1, tempoLabel_);
    row1.removeFromLeft(kLabelGap);
    place(row1, tempo_, 84, Stepper::kHeight);
    row1.removeFromLeft(kFieldGap);
    label(row1, transposeLabel_);
    row1.removeFromLeft(kLabelGap);
    place(row1, transpose_, 70, Stepper::kHeight);
    row1.removeFromLeft(kFieldGap);
    label(row1, startLabel_);
    row1.removeFromLeft(kLabelGap);
    place(row1, songStart_, 84, Stepper::kHeight);
    close(1, row1, from);

    // row 2: FILE
    from = row2.getX();
    place(row2, saveSong_, 104, 24);
    row2.removeFromLeft(6);
    place(row2, loadSong_, 104, 24);
    row2.removeFromLeft(6);
    place(row2, importSav_, 108, 24);
    row2.removeFromLeft(12);
    place(row2, export_, 96, 24);
    close(2, row2, from);

    // TRANSPORT, over the chain (D-UI-36): the four icons, the LED and the
    // readout on its first row, the zoom on its second.
    groups_[3] = right;
    auto t1 = right.removeFromTop(kCaption + kToolRow);
    right.removeFromTop(kRowGap);
    auto t2 = right.removeFromTop(kCaption + kToolRow);
    from = t1.getX();
    for (auto* b : { &play_, &stop_, &loop_, &follow_ }) { place(t1, *b, IconButton::kWidth, IconButton::kHeight); t1.removeFromLeft(4); }
    t1.removeFromLeft(4);
    place(t1, playLed_, 8, 8);
    t1.removeFromLeft(4);
    place(t1, pos_, t1.getWidth(), kToolRow);
    t2 = t2.withTrimmedTop(kCaption);
    zoomLabel_.setBounds(t2.removeFromLeft(zoomLabel_.preferredWidth() + 4));
    t2.removeFromLeft(2);
    gridText_.setBounds(t2.removeFromRight(112));
    t2.removeFromRight(4);
    zoom_.setBounds(t2.withSizeKeepingCentre(t2.getWidth(), 20));

    // the chain takes the column to the right of the lane, the transport's
    // own column of the head over it (UI_DESIGN section 7)
    chain_.setBounds(area.removeFromRight(ChainColumn::kWidth));
    area.removeFromRight(kChainGap);
    scroll_.setBounds(area);
}

} // namespace chipboy::plugin
