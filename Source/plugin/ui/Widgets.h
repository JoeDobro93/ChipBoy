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
#include "plugin/ui/Theme.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <functional>
#include <memory>
#include <vector>

namespace chipboy::ui {

/// A discrete rotary control with its label underneath and the value text
/// on the dial (the mockup's .knob: 50 px dial). Drag or mouse-wheel steps
/// by whole values; double-click resets to the default.
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
    void mouseDoubleClick(const juce::MouseEvent&) override; void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
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
    std::function<void(int)> onChange;
    static constexpr int kHeight = 24;
    int preferredWidth() const;           ///< 80: two 22 px buttons and a 34 px readout
    void resized() override; void paint(juce::Graphics&) override;
    void mouseMove(const juce::MouseEvent&) override; void mouseExit(const juce::MouseEvent&) override; void mouseDown(const juce::MouseEvent&) override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
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
/// output trim, C3). Attach to the "trim" parameter.
class Fader : public juce::Component, public juce::SettableTooltipClient {
public:
    Fader();
    ~Fader() override;
    void attach(juce::RangedAudioParameter& p);
    void resized() override; void paint(juce::Graphics&) override;
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

/// One row of a bank list: "01 · Square lead".
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
/// pick a command in the command columns, Tab/arrows move.
class TableGrid : public juce::Component, public juce::TooltipClient {
public:
    TableGrid();
    ~TableGrid() override;
    void setTable(const bank::Table& t);
    const bank::Table& table() const;
    void setPlayingStep(int step);     ///< -1 none
    std::function<void(const bank::Table&)> onChange;
    juce::String getTooltip() override;   ///< the hovered cell: what the column is, and what the command says
    static constexpr int kRowHeight = 22, kHeaderHeight = 22;
    static constexpr int preferredHeight() { return kHeaderHeight + bank::kTableSteps * kRowHeight; }
    void resized() override; void paint(juce::Graphics&) override;
    void mouseMove(const juce::MouseEvent&) override; void mouseExit(const juce::MouseEvent&) override; void mouseDown(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override; void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed(const juce::KeyPress&) override; void focusGained(FocusChangeType) override; void focusLost(FocusChangeType) override;
private:
    struct Impl; std::unique_ptr<Impl> impl_;
};

/// The Phrases lane: four channels side by side, 16 steps of note,
/// instrument, table and two commands for one bar (UI_DESIGN section 7).
/// Column headers carry the Roll / Trk source switch.
class PhraseGrid : public juce::Component, public juce::TooltipClient {
public:
    PhraseGrid();
    ~PhraseGrid() override;
    void setSong(std::shared_ptr<const tracker::Song> song, int bar);
    int bar() const;
    void setPlayingStep(int ch, int step);                     ///< -1 none
    void setRollNote(int ch, int midiNote);                    ///< the piano roll's current note, greyed; -1 none
    std::function<void(int ch, int step, const tracker::Cell&)> onCellChange;
    std::function<void(int ch, tracker::NoteSource)> onSourceChange;
    std::function<void(int ch, int groove)> onGrooveChange;    ///< per-phrase groove slot, 0 straight
    juce::String getTooltip() override;   ///< the hovered cell: what the column is, and what the command says
    static constexpr int kRowHeight = 22, kHeaderHeight = 48;
    static constexpr int preferredHeight() { return kHeaderHeight + tracker::kSteps * kRowHeight; }
    void resized() override; void paint(juce::Graphics&) override;
    void mouseMove(const juce::MouseEvent&) override; void mouseExit(const juce::MouseEvent&) override; void mouseDown(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override; void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed(const juce::KeyPress&) override; void focusGained(FocusChangeType) override; void focusLost(FocusChangeType) override;
private:
    struct Impl; std::unique_ptr<Impl> impl_;
};

/// The bar chain above the lane: one cell per bar per channel with the
/// phrase slot; click selects the bar, drag moves through the song.
class ChainStrip : public juce::Component {
public:
    ChainStrip();
    ~ChainStrip() override;
    void setSong(std::shared_ptr<const tracker::Song> song, int selectedBar, int playingBar);
    std::function<void(int bar)> onSelectBar;
    std::function<void(int ch, int bar, int phraseSlot)> onChainChange;   ///< 0 clears
    static constexpr int kRowHeight = 22, kHeaderHeight = 18;
    static constexpr int preferredHeight() { return kHeaderHeight + 4 * kRowHeight + 3 * 2; }
    void resized() override; void paint(juce::Graphics&) override;
    void mouseMove(const juce::MouseEvent&) override; void mouseExit(const juce::MouseEvent&) override; void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override; void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    bool keyPressed(const juce::KeyPress&) override; void focusGained(FocusChangeType) override; void focusLost(FocusChangeType) override;
private:
    struct Impl; std::unique_ptr<Impl> impl_;
};

/// 32 samples by 16 levels, drawn with the mouse (spec 9.7).
class WaveGrid : public juce::Component {
public:
    WaveGrid();
    ~WaveGrid() override;
    void setFrame(const bank::Frame& f);
    const bank::Frame& frame() const;
    std::function<void(const bank::Frame&)> onChange;
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override; void mouseDrag(const juce::MouseEvent&) override; void mouseUp(const juce::MouseEvent&) override;
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
    };
    ScopeView();
    ~ScopeView() override;
    void setSource(Source s);
    void setChannel(int ch);            ///< 0-3: colour, and whether it is pulse / wave / noise
    void setTrace(Trace t);
    void setPeriods(int periods);       ///< 1, 2, 4 or 8; noise uses a fixed time window
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
