// ChipBoy -- the widgets (UI_DESIGN sections 2-3, 6-8; the mockup is the
// reference: docs/mockups/chipboy_mockup.html).
//
// Every control is discrete except the Fader (the output trim, C3). Widgets
// either attach to a host parameter (juce::ParameterAttachment does the
// host round-trip) or stand alone with an onChange callback; never both.
// They read ValueFormat (decimal / hex) when they paint.
#pragma once

#include "core/Bank/Bank.h"
#include "core/Link/LinkLayout.h"
#include "core/Tracker/Song.h"
#include "plugin/shared/ScopeBuffers.h"
#include "plugin/ui/EditHistory.h"
#include "plugin/ui/Theme.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <functional>
#include <memory>
#include <vector>

namespace chipboy::ui {

/// A discrete rotary control with its label underneath and the value text
/// on the dial (the mockup's .knob: 50 px dial). Dragging steps by whole
/// values; a double click opens the value for typing and Alt with it puts
/// the default back. The wheel never edits (UI_DESIGN section 2.1).
class Knob : public juce::Component, public juce::SettableTooltipClient {
public:
    explicit Knob(const juce::String& label = {});
    ~Knob() override;
    void attach(juce::RangedAudioParameter& p);   ///< value text comes from the parameter
    void setRange(int lo, int hi, int defaultValue = 0);
    void setValue(int v, juce::NotificationType = juce::sendNotification);
    int value() const;
    void setTextFunction(std::function<juce::String(int)> fn);
    void setLabel(const juce::String& text);
    void setAccent(juce::Colour c);
    std::function<void(int)> onChange;
    static constexpr int kDial = 50, kHeight = 70, kWidth = 64;
    void resized() override; void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override; void mouseDrag(const juce::MouseEvent&) override; void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;
private:
    struct Impl; std::unique_ptr<Impl> impl_;
};

/// A row of exclusive buttons (the mockup's .seg; setMini for .seg.mini).
class Segmented : public juce::Component, public juce::SettableTooltipClient {
public:
    explicit Segmented(const juce::StringArray& options = {});
    ~Segmented() override;
    void setOptions(const juce::StringArray& options);
    void attach(juce::RangedAudioParameter& p);   ///< option i <-> value lo + i
    void setSelected(int index, juce::NotificationType = juce::sendNotification);
    int selected() const;
    void setMini(bool mini);
    void setOptionEnabled(int index, bool enabled);
    void setOptionTooltip(int index, const juce::String& tip);
    void setOptionColour(int index, juce::Colour c);   ///< selected colour per option (e.g. the model switch)
    std::function<void(int)> onChange;
    int preferredWidth() const;
    int preferredHeight() const;          ///< 26, or 22 when mini
    juce::String getTooltip() override;   ///< the hovered option's tip, else the widget's
    void resized() override; void paint(juce::Graphics&) override;
    void mouseMove(const juce::MouseEvent&) override; void mouseExit(const juce::MouseEvent&) override; void mouseDown(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;
private:
    struct Impl; std::unique_ptr<Impl> impl_;
};

/// [-] value [+] (the mockup's .stepper). Whole values, hardware ranges.
/// The readout is a typed field with the grids' grammar (UI_DESIGN section
/// 2.1, docs/COMMANDS_AND_TEMPO.md section 35): a click selects it, digits
/// typed at it build a value and Backspace takes them back, a double click
/// or Enter opens the inline box (a slot stepper's double click opens the
/// item instead), Shift with the arrows moves it by one and by sixteen, and
/// the wheel never edits.
class Stepper : public juce::Component, public juce::SettableTooltipClient {
public:
    Stepper();
    ~Stepper() override;
    void setRange(int lo, int hi, int defaultValue = 0);
    void attach(juce::RangedAudioParameter& p);
    void setValue(int v, juce::NotificationType = juce::sendNotification);
    int value() const;
    void setTextFunction(std::function<juce::String(int)> fn);
    void setWraps(bool wraps);
    /// The readout counts like LSDj in Hex (docs/COMMANDS_AND_TEMPO.md
    /// section 52): a slot field shows slot 1 as 00 and reads a typed 00 as
    /// slot 1; a transpose field shows and reads the two's-complement byte.
    void setSlotNumbering(bool on);
    void setTransposeNumbering(bool on);
    /// Digits typed straight at the readout, the grids' convention: they
    /// build a value and Backspace takes the last one back. On everywhere;
    /// turning it off leaves a stepper that only steps and only takes the
    /// inline box.
    void setTyped(bool typed);
    /// How the inline box reads and parses when the readout is not a plain
    /// number in the field's own units (the song start, held in tenths of a
    /// second). Both or neither.
    void setEntryFormat(std::function<juce::String(int)> toText, std::function<bool(const juce::String&, int&)> fromText);
    /// Open the inline box on the readout, as a double click on it does.
    void beginTypedEntry();
    std::function<void(int)> onChange;
    /// A slot stepper follows the one selector convention (UI_DESIGN 2.1,
    /// section 35): a right click lists the slots by name, with the entry
    /// that opens the item's own tab at the top, and a double click opens
    /// that tab too -- where onOpen is set, the double click is the item's
    /// rather than the box's; Enter still opens the box.
    std::function<void()> onList, onOpen;
    static constexpr int kHeight = 24;
    int preferredWidth() const;           ///< 80: two 22 px buttons and a 34 px readout
    void resized() override; void paint(juce::Graphics&) override;
    void mouseMove(const juce::MouseEvent&) override; void mouseExit(const juce::MouseEvent&) override; void mouseDown(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override; void mouseDoubleClick(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;
private:
    struct Impl; std::unique_ptr<Impl> impl_;
};

/// A switch with text (the mockup's .switch / .toggle-row).
class Toggle : public juce::Component, public juce::SettableTooltipClient {
public:
    explicit Toggle(const juce::String& text = {});
    ~Toggle() override;
    void attach(juce::RangedAudioParameter& p);
    void setToggled(bool on, juce::NotificationType = juce::sendNotification);
    bool toggled() const;
    void setText(const juce::String& text);
    void setDescription(const juce::String& text);   ///< second, muted line (the Hardware tab's facts)
    std::function<void(bool)> onChange;
    static constexpr int kHeight = 24;
    int preferredHeight(int width) const; ///< kHeight, or the .toggle-row height when a description is set
    void resized() override; void paint(juce::Graphics&) override;
    void mouseEnter(const juce::MouseEvent&) override; void mouseExit(const juce::MouseEvent&) override; void mouseUp(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;
private:
    struct Impl; std::unique_ptr<Impl> impl_;
};

/// The one continuous control: a vertical fader with a dB readout (the
/// output trim, C3). Attach to the "trim" parameter. A double click on the
/// readout types a value; the wheel never edits.
class Fader : public juce::Component, public juce::SettableTooltipClient {
public:
    Fader();
    ~Fader() override;
    void attach(juce::RangedAudioParameter& p);
    void resized() override; void paint(juce::Graphics&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
private:
    struct Impl; std::unique_ptr<Impl> impl_;
};

/// A small status capsule (the mockup's .pill).
class Pill : public juce::Component {
public:
    enum class Tone { Neutral, Ok, Warn, Bad, Accent };
    void set(const juce::String& text, Tone tone);
    juce::String text() const { return text_; }
    int preferredWidth() const;
    void paint(juce::Graphics&) override;
private:
    juce::String text_; Tone tone_ = Tone::Neutral;
};

/// The activity LED on a channel strip.
class Led : public juce::Component {
public:
    void setOn(bool on);
    void setColour(juce::Colour c);
    void paint(juce::Graphics&) override;
private:
    bool on_ = false; juce::Colour colour_ = colours::ok;
};

/// A button that carries a glyph rather than a word (UI_DESIGN D-UI-36):
/// the transport's play / pause, stop, loop and follow over the chain. On
/// (its toggle state) it is filled like a TextButton that is on; the word
/// is the tooltip's.
class IconButton : public juce::Button {
public:
    enum class Icon { Play, Pause, Stop, Loop, Follow };
    explicit IconButton(Icon i);
    void setIcon(Icon i);
    Icon icon() const { return icon_; }
    static constexpr int kWidth = 30, kHeight = 24;
    void paintButton(juce::Graphics&, bool over, bool down) override;
private:
    Icon icon_;
};

/// Which of the bank's lists a slot field names. Every selector in the
/// window obeys one convention (UI_DESIGN section 2.1, COMMANDS_AND_TEMPO
/// section 35): a click selects the field and types into it, a double click
/// opens its box, and a right click lists the slots by name with the entry
/// that opens that item's own tab at the top.
enum class SlotKind { Instrument, Table, Wave, Kit, Groove };

/// One row of a bank list: "01 . Square lead".
struct SlotRow {
    int slot = 0;                 ///< 1-based
    juce::String name;
    bool used = false;
    int kind = -1;                ///< bank::InstrumentType for instruments, else -1
    juce::String note;            ///< right-aligned muted text (e.g. "x3" uses)
};

/// A scrolling list of bank slots (the mockup's .list). Click selects,
/// double-click renames in place, Up/Down move, Enter renames.
class SlotList : public juce::Component {
public:
    SlotList();
    ~SlotList() override;
    void setRows(const std::vector<SlotRow>& rows);
    void setSelected(int slot, juce::NotificationType = juce::sendNotification);
    int selected() const;
    void setKindColours(std::function<juce::Colour(int kind)> fn);
    /// Rows that have no name to edit (the grooves) turn renaming off.
    void setRenameable(bool on);
    std::function<void(int slot)> onSelect;
    std::function<void(int slot)> onDoubleClick;   ///< a double click on a row (the panel decides what it means)
    std::function<void(int slot, const juce::String& name)> onRename;
    void beginRename();                   ///< open the in-place editor on the selected row (Enter or F2)
    static constexpr int kRowHeight = 26;
    void resized() override; void paint(juce::Graphics&) override;
    bool keyPressed(const juce::KeyPress&) override;
private:
    struct Impl; std::unique_ptr<Impl> impl_;
};

/// A rename-able title (the mockup's .name-input): click to edit.
class NameField : public juce::Component {
public:
    NameField();
    ~NameField() override;
    void setText(const juce::String& text, juce::NotificationType = juce::dontSendNotification);
    juce::String text() const;
    std::function<void(const juce::String&)> onChange;
    static constexpr int kHeight = 26;
    void resized() override; void paint(juce::Graphics&) override;
    void focusOfChildComponentChanged(FocusChangeType) override;
private:
    struct Impl; std::unique_ptr<Impl> impl_;
};

/// The table editor: 16 steps of volume, transpose and two commands
/// (spec 9.5). Keyboard: type digits to set, Backspace blanks, letters
/// pick a command in the command columns, Tab/arrows move; Enter or a
/// double click on a blank cell fills it with the column's most recent
/// value, and a vertical drag moves a value (section 38).
class TableGrid : public juce::Component, public juce::TooltipClient {
public:
    TableGrid();
    ~TableGrid() override;
    void setTable(const bank::Table& t);
    const bank::Table& table() const;
    /// The three lanes' running rows (section 64): the volume column, the
    /// transpose-and-first-command column and the second command column each
    /// run on their own pointer, so each lights its own row. -1 for a lane
    /// that is not running.
    void setPlayingSteps(int volLane, int cmdLane, int cmd2Lane);
    void setPlayingStep(int step) { setPlayingSteps(step, step, step); }   ///< -1 none
    std::function<void(const bank::Table&)> onChange;
    juce::String getTooltip() override;   ///< the hovered cell: what the column is, and what the command says
    static constexpr int kRowHeight = 22, kHeaderHeight = 22;
    static constexpr int preferredHeight() { return kHeaderHeight + bank::kTableSteps * kRowHeight; }
    void resized() override; void paint(juce::Graphics&) override;
    void mouseMove(const juce::MouseEvent&) override; void mouseExit(const juce::MouseEvent&) override; void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override; void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override; void focusGained(FocusChangeType) override; void focusLost(FocusChangeType) override;
private:
    struct Impl; std::unique_ptr<Impl> impl_;
};

/// One song open in the window: what its tab shows
/// (docs/COMMANDS_AND_TEMPO.md section 18).
struct SongTabInfo {
    juce::String name;
    bool dirty = false;
    juce::String tip;            ///< the file behind it, or where it came from
    bool operator==(const SongTabInfo& o) const { return name == o.name && dirty == o.dirty && tip == o.tip; }
};

/// The song tabs, where the Tracker tab's summary line was (section 18): one
/// tab per loaded song with a dot while it has unsaved work and an x to drop
/// it, the active one lit, and a + that starts an empty song. Only the
/// active tab is live -- it is what plays and what every other tab edits.
class SongTabStrip : public juce::Component, public juce::TooltipClient {
public:
    SongTabStrip();
    ~SongTabStrip() override;
    /// Nothing happens when the list and the active index are unchanged, so
    /// the panel can hand it the processor's tabs every frame.
    void setTabs(const std::vector<SongTabInfo>& tabs, int active);
    std::function<void(int)> onActivate;
    std::function<void(int)> onClose;      ///< the x; the panel asks before dropping unsaved work
    std::function<void()> onNew;           ///< the + tab
    juce::String getTooltip() override;
    static constexpr int kHeight = 26;
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseMove(const juce::MouseEvent&) override; void mouseExit(const juce::MouseEvent&) override; void mouseDown(const juce::MouseEvent&) override;
private:
    struct Impl; std::unique_ptr<Impl> impl_;
};

/// The tracker lane: four channels side by side, the row's steps of note,
/// velocity, instrument, table and two commands (UI_DESIGN section 7). Each
/// channel's head carries its record arm, the playback switch (MIDI / Trkr /
/// Hyb, docs/COMMANDS_AND_TEMPO.md section 20), its phrase's LEN and its
/// groove chip; the groove itself is edited in the Grooves tab.
///
/// The rows are the phrase's steps, one to sixty-four
/// (docs/COMMANDS_AND_TEMPO.md section 25): the grid is as tall as the
/// longest phrase in the row asks and scrolls inside its pane past sixteen.
class PhraseGrid : public juce::Component, public juce::TooltipClient {
public:
    PhraseGrid();
    ~PhraseGrid() override;
    /// The song and each channel's own row -- the one it is in at the play
    /// head (D-UI-35, docs/COMMANDS_AND_TEMPO.md section 25).
    void setSong(std::shared_ptr<const tracker::Song> song, const int rows[4]);
    /// The bank the right-click lists read: the instruments and tables a
    /// cell can name, by slot and name (UI_DESIGN section 2.1).
    void setBank(std::shared_ptr<const bank::Bank> bank);
    int steps() const;                                         ///< rows on show, 1-64
    void setPlayingStep(int ch, int step);                     ///< -1 none
    void setRollNote(int ch, int midiNote);                    ///< the piano roll's current note, greyed; -1 none
    std::function<void(int ch, int step, const tracker::Cell&)> onCellChange;
    /// The cursor left the cell it was typing into: what follows is a new
    /// undo, not more of the same one.
    std::function<void()> onEntryEnd;
    std::function<void(int ch, tracker::NoteSource)> onSourceChange;
    std::function<void(int ch, int groove)> onGrooveChange;    ///< per-phrase groove slot, 0 straight
    /// STEPS in this channel's head: the length of the phrase it plays in
    /// its row, 1-64 (docs/COMMANDS_AND_TEMPO.md section 25, D-UI-37).
    std::function<void(int ch, int steps)> onLengthChange;
    /// TSP in this channel's head: its row's transpose (section 48, D-UI-37).
    std::function<void(int ch, int semis)> onTransposeChange;
    std::function<void(int ch, bool armed)> onArmChange;       ///< the channel's record arm (section 14)
    std::function<void(int row)> onCursorRow;                  ///< the cursor moved: keep this row in view
    /// The right-click list's first entry on a slot field: open that item's
    /// own tab with it selected (section 35).
    std::function<void(SlotKind, int slot)> onOpenSlot;
    juce::String getTooltip() override;   ///< the hovered cell: what the column is, and what the command says
    /// The head is the name row, the chip row (PHRASE, TSP, STEPS, TICKS --
    /// D-UI-37) and the column captions: 26 + 20 + 22.
    static constexpr int kRowHeight = 22, kHeaderHeight = 68, kVisibleSteps = 16;
    /// How tall the grid is for a bar of `steps` steps; sixteen is what the
    /// pane holds without scrolling.
    static constexpr int heightForSteps(int steps)
    {
        return kHeaderHeight + (steps < 1 ? 1 : steps > tracker::kMaxSteps ? tracker::kMaxSteps : steps) * kRowHeight;
    }
    static constexpr int preferredHeight() { return heightForSteps(kVisibleSteps); }
    void resized() override; void paint(juce::Graphics&) override;
    void mouseMove(const juce::MouseEvent&) override; void mouseExit(const juce::MouseEvent&) override; void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override; void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override; void focusGained(FocusChangeType) override; void focusLost(FocusChangeType) override;
private:
    struct Impl; std::unique_ptr<Impl> impl_;
};

/// The chain, drawn in time (UI_DESIGN D-UI-35, docs/plan-chain-timeline.md):
/// channels across, ticks down, a block per row of each channel as tall as
/// the row lasts under its groove and its H hops, the phrase and its
/// transpose in the block. The play head is one line across the four
/// columns (docs/COMMANDS_AND_TEMPO.md section 223); the gutter counts the
/// song's time signatures (section 222) and the zoom's grid is what the
/// play head snaps to. It stands in the column right of the lane.
class ChainColumn : public juce::Component, public juce::TooltipClient {
public:
    ChainColumn();
    ~ChainColumn() override;
    /// The song and the play head, a tick.
    void setSong(std::shared_ptr<const tracker::Song> song, int64_t cursorTick);
    /// The transport's own position: the moving line while it plays, kept
    /// in view when the panel follows it (D-UI-16).
    void setTransport(bool playing, int64_t tick, bool keepInView);
    /// The zoom slider's value, 0..1; pixels per tick are derived from it
    /// and the first time signature.
    void setZoom(double t);
    double zoom() const;
    int64_t cursorTick() const;
    /// The loop region to draw (section 223); `to` < 0 is none.
    void setLoopRegion(bool on, int64_t from, int64_t to);
    /// What the grid is at this zoom, for the readout beside the slider.
    juce::String gridText() const;
    /// The play head moved by a click, a drag or a key.
    std::function<void(int64_t tick)> onSelectTick;
    /// Shift-drag on the gutter set a loop region; `to` < 0 clears it.
    std::function<void(int64_t from, int64_t to)> onLoopRegion;
    /// The signature editor changed the list; the panel normalises it.
    std::function<void(std::vector<tracker::TimeSignature>)> onSignaturesChange;
    std::function<void()> onPlayPause;                                    ///< Space
    /// A run of edits the cursor keeps inside is one undo, so the panel is
    /// told when a typed value is finished with (a click, a cursor move).
    std::function<void()> onEntryEnd;
    std::function<void(int ch, int row, int phraseSlot)> onChainChange;   ///< 0 clears; a row past the chain's end grows it
    std::function<void(int ch, int row, int semis)> onChainTransposeChange;   ///< the row's transpose on that channel (section 48)
    std::function<void(int ch, bool loop)> onChainEndChange;              ///< the channel plays its chain round again, or stops at its end (section 212)
    juce::String getTooltip() override;
    /// 264 wide, a 48 px head of its own.
    static constexpr int kHeaderHeight = 48, kWidth = 264;
    void resized() override; void paint(juce::Graphics&) override;
    void mouseMove(const juce::MouseEvent&) override; void mouseExit(const juce::MouseEvent&) override; void mouseDown(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override; void mouseUp(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed(const juce::KeyPress&) override; void focusGained(FocusChangeType) override; void focusLost(FocusChangeType) override;
private:
    struct Impl; std::unique_ptr<Impl> impl_;
};

/// 32 samples by 16 levels, drawn with the mouse (spec 9.7). Two views
/// (docs/COMMANDS_AND_TEMPO.md section 36): bars, and points on a grid. In
/// both the pointer's column and row are lit and a caption in the corner
/// reads the sample number and its level.
class WaveGrid : public juce::Component {
public:
    enum class View { Bars, Points };
    WaveGrid();
    ~WaveGrid() override;
    void setFrame(const bank::Frame& f);
    const bank::Frame& frame() const;
    void setView(View v);
    View view() const;
    std::function<void(const bank::Frame&)> onChange;
    void paint(juce::Graphics&) override;
    void mouseMove(const juce::MouseEvent&) override; void mouseExit(const juce::MouseEvent&) override;
    void mouseDown(const juce::MouseEvent&) override; void mouseDrag(const juce::MouseEvent&) override; void mouseUp(const juce::MouseEvent&) override;
    /// D-UI-24: the arrows move the sample last clicked -- left and right between
    /// samples, up and down on its level. Everything else passes through.
    bool keyPressed(const juce::KeyPress&) override;
    void focusGained(FocusChangeType) override; void focusLost(FocusChangeType) override;
private:
    struct Impl; std::unique_ptr<Impl> impl_;
};

/// A period-locked oscilloscope of one channel's digital trace, optionally
/// with an approximation of the analog trace after the coupling capacitor
/// (UI_DESIGN section 3). Redraws on its own timer.
class ScopeView : public juce::Component {
public:
    enum class Trace { Digital, Analog, Both };
    struct Source {
        const link::ScopeRing* ring = nullptr;
        const std::atomic<uint64_t>* state = nullptr;        ///< link::packState, for the period
        const std::atomic<uint64_t>* latestCycle = nullptr;  ///< optional; else the newest sample's cycle
        const std::atomic<uint32_t>* mix = nullptr;          ///< optional: NR50 | NR51 << 8 | powered << 16, so a channel the mix silences draws as off (section 53)
    };
    ScopeView();
    ~ScopeView() override;
    void setSource(Source s);
    void setChannel(int ch);            ///< 0-3: colour, and whether it is pulse / wave / noise
    void setTrace(Trace t);
    void setPeriods(int periods);       ///< 1, 2, 4 or 8; noise uses a fixed time window
    /// A kit plays a sample rather than a repeating wave, so there is no
    /// period to lock to: the scope keeps noise's fixed time window while
    /// one is playing (docs/COMMANDS_AND_TEMPO.md section 22).
    void setFixedWindow(bool on);
    /// A left and a right monitor side by side (UI_DESIGN section 3): each
    /// half draws the channel's trace over `periods` periods, and a half whose
    /// NR51 gate is off draws the off baseline instead, so an `O` reads at a
    /// glance. Off, one picture across the width.
    void setStereo(bool on);
    void setAnalogCornerHz(double hz);  ///< the coupling corner of the current model; <= 0 = RAW (no analog trace)
    void setLcdGround(bool lcd);        ///< LCD green on near-black, or the panel's dark ground
    void setLineWidth(float px);
    void setChrome(bool on);            ///< the trace / zoom controls along the bottom edge
    void setFrozen(bool frozen);        ///< stop redrawing (hidden tab)
    /// Tools that snapshot editors without a window: keep the timers running.
    static void setOffscreenRefresh(bool on);
    static bool offscreenRefresh();
    void setGround(juce::Colour ground, juce::Colour grid, juce::Colour border);   ///< any ground (the visualizer's black); setLcdGround picks the two presets
    void setIdleDim(bool on);           ///< dim the picture while the channel is silent (default on; the visualizer turns it off)
    Trace trace() const; int periods() const;
    void resized() override; void paint(juce::Graphics&) override;
    void mouseEnter(const juce::MouseEvent&) override; void mouseExit(const juce::MouseEvent&) override;
private:
    struct Impl; std::unique_ptr<Impl> impl_;
};

/// The stereo output over the last few milliseconds, LCD green.
class MasterScope : public juce::Component {
public:
    MasterScope();
    ~MasterScope() override;
    void setSource(const plugin::AudioRing* ring);
    void setWindowMs(double ms);
    void setLcdGround(bool lcd);
    void setLineWidth(float px);
    void setFrozen(bool frozen);
    void setGround(juce::Colour ground, juce::Colour grid, juce::Colour border);
    void resized() override; void paint(juce::Graphics&) override;
private:
    struct Impl; std::unique_ptr<Impl> impl_;
};

/// "NR11 80 · NR12 A3 · NR13 C1 · NR14 C7" from a packed state (spec 13.1).
class RegisterLine : public juce::Component {
public:
    void setChannel(int ch);
    void setState(uint64_t packed);
    void paint(juce::Graphics&) override;
private:
    int ch_ = 0; uint64_t state_ = 0;
};

/// The visualizer: a separate, resizable, clean window with the four
/// channel scopes and the master (UI_DESIGN section 3).
class VisualizerWindow : public juce::DocumentWindow {
public:
    VisualizerWindow(plugin::ScopeBuffers& buffers, std::function<double()> analogCornerHz);
    ~VisualizerWindow() override;
    void closeButtonPressed() override;
    std::function<void()> onClose;
private:
    struct Impl; std::unique_ptr<Impl> impl_;
};

} // namespace chipboy::ui
