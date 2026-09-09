// ChipBoy -- what the main window's panels share: a small block layout
// (cards, field grids, stacks, columns, scrolling), text helpers, and the
// glue between the widgets and the host parameters where the widget's own
// attach() cannot express the mapping (display order, float steps).
//
// Everything here runs on the message thread and reads the processor only
// through atomics, shared_ptr snapshots or the packed scope state.
#pragma once

#include "plugin/main/ChipBoyProcessor.h"
#include "plugin/ui/Theme.h"
#include "plugin/ui/Widgets.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <functional>
#include <memory>
#include <vector>

namespace chipboy::plugin {

/* ------------------------------------------------------------- text */

/// A line of text with bold runs: the tab bar's context line, help texts.
struct RichText {
    struct Run { juce::String text; bool bold = false; };
    std::vector<Run> runs;

    RichText() = default;
    explicit RichText(const juce::String& plainText) { plain(plainText); }
    RichText& plain(const juce::String& t) { runs.push_back({ t, false }); return *this; }
    RichText& bold(const juce::String& t) { runs.push_back({ t, true }); return *this; }
    juce::String toString() const;
    juce::AttributedString attributed(float px, juce::Colour normal, juce::Colour strong, juce::Justification j = juce::Justification::centredLeft) const;
    bool operator==(const RichText& o) const;
    bool operator!=(const RichText& o) const { return !(*this == o); }
};

/// One line of styled text (labels, values, headings).
class TextLine : public juce::Component {
public:
    explicit TextLine(const juce::String& text = {}, juce::Font font = ui::Fonts::sans(12.0f), juce::Colour colour = ui::colours::text,
                      juce::Justification j = juce::Justification::centredLeft);
    /// The mockup's .label: 10 px, upper-case, tracked, dim.
    static TextLine* label(const juce::String& text);
    void setText(const juce::String& text);
    juce::String text() const { return text_; }
    void setFont(juce::Font f) { font_ = f; repaint(); }
    void setColour(juce::Colour c) { colour_ = c; repaint(); }
    void setJustification(juce::Justification j) { just_ = j; repaint(); }
    void setUpperCase(bool on) { upper_ = on; repaint(); }
    int preferredWidth() const;
    void paint(juce::Graphics&) override;
private:
    juce::String text_; juce::Font font_; juce::Colour colour_; juce::Justification just_; bool upper_ = false;
};

/* ----------------------------------------------------------- blocks */

/// A component that knows how tall it wants to be for a given width.
class Block : public juce::Component {
public:
    virtual int preferredHeight(int width) = 0;
};

/// A wrapped paragraph (the mockup's .help): muted 12 px, bold runs allowed.
class HelpText : public Block {
public:
    explicit HelpText(const RichText& text = {}, float px = 12.0f, juce::Colour colour = ui::colours::textMute);
    void setText(const RichText& text);
    void setMaxWidth(int w) { maxWidth_ = w; }
    int preferredHeight(int width) override;
    void paint(juce::Graphics&) override;
private:
    RichText text_; float px_; juce::Colour colour_; int maxWidth_ = 0;
};

/// A component held at a fixed height inside a block layout. Not owned.
class Hold : public Block {
public:
    Hold(juce::Component& c, int height, int maxWidth = 0);
    int preferredHeight(int) override { return height_; }
    void setHeight(int h) { height_ = h; }
    void resized() override;
private:
    juce::Component& c_; int height_; int maxWidth_;
};

/// Blank vertical space.
class Space : public Block {
public:
    explicit Space(int h) : h_(h) {}
    int preferredHeight(int) override { return h_; }
private:
    int h_;
};

/// A label line and a hint ("→ NR12 bits 7–4") above one control (the
/// mockup's .field). Owns the control.
class Field : public Block {
public:
    Field(const juce::String& label, const juce::String& hint, std::unique_ptr<juce::Component> control, int controlHeight, int controlWidth = 0, int columns = 1);
    ~Field() override;
    juce::Component& control() { return *control_; }
    int columns() const { return columns_; }
    void setHint(const juce::String& h);
    int preferredHeight(int) override;
    void resized() override;
    void paint(juce::Graphics&) override;
    static constexpr int kCaption = 16;
private:
    juce::String label_, hint_; std::unique_ptr<juce::Component> control_; int controlHeight_, controlWidth_, columns_;
};

/// Fields flowing into columns of at least 150 px (the mockup's .grid-fields).
class FlowGrid : public Block {
public:
    FlowGrid() = default;
    ~FlowGrid() override;
    Field* add(std::unique_ptr<Field> f);
    template <typename W>
    W* addField(const juce::String& label, const juce::String& hint, std::unique_ptr<W> w, int height, int width = 0, int columns = 1)
    {
        W* raw = w.get();
        add(std::make_unique<Field>(label, hint, std::move(w), height, width, columns));
        return raw;
    }
    void clear();
    int preferredHeight(int width) override;
    void resized() override;
private:
    int layout(int width, bool apply);
    std::vector<std::unique_ptr<Field>> fields_;
};

/// One line of a form: the label on the left, the control on the right, and
/// the row's tooltip is what the field means -- so the panel itself stays
/// labels and values (docs/COMMANDS_AND_TEMPO.md section 29). Owns the
/// control.
class FormRow : public Block, public juce::SettableTooltipClient {
public:
    FormRow(const juce::String& label, std::unique_ptr<juce::Component> control, int controlWidth, int controlHeight, int labelWidth);
    ~FormRow() override;
    juce::Component& control() { return *control_; }
    int preferredHeight(int) override;
    void resized() override;
    void paint(juce::Graphics&) override;
private:
    juce::String label_;
    std::unique_ptr<juce::Component> control_;
    int controlW_, controlH_, labelW_;
};

/// A thin caption over a run of form rows: a group with no box around it.
class FormGroup : public Block {
public:
    static constexpr int kCaption = 18, kRow = 26, kLabel = 100;
    explicit FormGroup(const juce::String& caption, int labelWidth = kLabel);
    ~FormGroup() override;
    template <typename W>
    W* add(const juce::String& label, std::unique_ptr<W> control, int width, int height, const juce::String& tip = {})
    {
        W* raw = control.get();
        addRow(std::make_unique<FormRow>(label, std::move(control), width, height, labelWidth_), tip);
        return raw;
    }
    /// A block that takes the whole width of the group: a picture.
    void addWide(std::unique_ptr<Block> b);
    int preferredHeight(int width) override;
    void resized() override;
    void paint(juce::Graphics&) override;
private:
    void addRow(std::unique_ptr<FormRow> row, const juce::String& tip);
    juce::String caption_;
    int labelWidth_;
    std::vector<std::unique_ptr<Block>> rows_;
};

/// A titled box (the mockup's .card) around one block.
class Card : public Block {
public:
    Card(const juce::String& heading, std::unique_ptr<Block> content, const juce::String& note = {});
    ~Card() override;
    Block& content() { return *content_; }
    void setHeading(const juce::String& h);
    int preferredHeight(int width) override;
    void resized() override;
    void paint(juce::Graphics&) override;
    static constexpr int kPad = 12, kHeading = 22;
private:
    juce::String heading_, note_; std::unique_ptr<Block> content_;
};

/// Blocks one under the other.
class Stack : public Block {
public:
    explicit Stack(int gap = 12) : gap_(gap) {}
    ~Stack() override;
    Block* add(std::unique_ptr<Block> b);
    void clear();
    int preferredHeight(int width) override;
    void resized() override;
private:
    int gap_; std::vector<std::unique_ptr<Block>> items_;
};

/// Blocks side by side in equal columns (the mockup's .groups / .link-cols).
class Columns : public Block {
public:
    explicit Columns(int gap = 12) : gap_(gap) {}
    ~Columns() override;
    Block* add(std::unique_ptr<Block> b);
    void setWidths(std::vector<int> fixedWidths);   ///< 0 = share the rest
    int preferredHeight(int width) override;
    void resized() override;
private:
    int columnWidth(int index, int total) const;
    int gap_; std::vector<std::unique_ptr<Block>> items_; std::vector<int> widths_;
};

/// A block inside a vertical-scrolling viewport.
class ScrollBlock : public juce::Component {
public:
    ScrollBlock();
    ~ScrollBlock() override;
    void setContent(std::unique_ptr<Block> b);
    Block* content() { return content_.get(); }
    void relayout();
    /// Scroll the least that brings [y, y + height) of the content into view.
    void scrollToKeepVisible(int y, int height);
    void resized() override;
private:
    juce::Viewport viewport_; std::unique_ptr<Block> content_;
};

/// A container giving a tooltip (and a click) to a child that has neither.
class TipBox : public juce::Component, public juce::SettableTooltipClient {
public:
    explicit TipBox(juce::Component& child);
    std::function<void()> onClick;
    void resized() override;
    void mouseUp(const juce::MouseEvent& e) override;
};

/* ------------------------------------------------------- parameters */

juce::RangedAudioParameter& param(ChipBoyProcessor& p, const juce::String& id);
int paramValue(const ChipBoyProcessor& p, const juce::String& id);          ///< denormalised, rounded
float paramFloat(const ChipBoyProcessor& p, const juce::String& id);
juce::String paramText(ChipBoyProcessor& p, const juce::String& id);
/// Set by hand from `owner`'s window: one gesture, on that window's undo
/// history (UI_DESIGN section 2.1).
void setParam(const juce::Component& owner, juce::RangedAudioParameter& p, float denormalised);

/// A Segmented whose option order differs from the parameter's value order.
class SegmentedParam {
public:
    SegmentedParam(ui::Segmented& seg, juce::RangedAudioParameter& p, std::vector<int> valueForOption);
    ~SegmentedParam();
private:
    ui::Segmented& seg_; juce::RangedAudioParameter& p_; std::vector<int> map_; std::unique_ptr<juce::ParameterAttachment> att_;
};

/// A Stepper over integer steps of a float parameter (value = step * scale).
class StepperParam {
public:
    StepperParam(ui::Stepper& stepper, juce::RangedAudioParameter& p, float scale, int lo, int hi);
    ~StepperParam();
private:
    ui::Stepper& stepper_; juce::RangedAudioParameter& p_; float scale_; std::unique_ptr<juce::ParameterAttachment> att_;
};

/// A juce::Button bound to a bool parameter both ways, with the click going
/// on the window's undo history -- juce::ButtonParameterAttachment writes
/// straight to the parameter and would not be undoable.
class ToggleParam {
public:
    ToggleParam(juce::Button& b, juce::RangedAudioParameter& p);
    ~ToggleParam();
private:
    juce::Button& button_;
    juce::RangedAudioParameter& param_;
    std::unique_ptr<juce::ParameterAttachment> att_;
};

/// A callback when a parameter changes (message thread), with the value.
class ParamWatch {
public:
    ParamWatch(juce::RangedAudioParameter& p, std::function<void(float)> fn);
private:
    std::unique_ptr<juce::ParameterAttachment> att_;
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
juce::String modelName(int index);
juce::Colour modelColour(int index);
/// "Omni", "MIDI 2", "Off", or "Voice: <name>" when a Voice owns the channel.
juce::String channelSourceText(ChipBoyProcessor& p, int ch, bool* voiceOwned = nullptr);
juce::Colour instrumentKindColour(int kind);
juce::String instrumentTypeName(bank::InstrumentType t);
bank::InstrumentType channelInstrumentType(int ch);
bool instrumentFitsChannel(bank::InstrumentType t, int ch);
juce::String shortUuid(const juce::String& uuid);
juce::String withThousands(int v);   ///< "11 468"
juce::String slotAndName(int slot, const std::string& name);   ///< "05 Bass 07"

/* ------------------------------------------------------------ panels */

/// One of the seven editor tabs.
class EditorPanel : public juce::Component {
public:
    explicit EditorPanel(ChipBoyProcessor& p) : processor(p) {}
    virtual void setChannel(int ch) { channel = ch; }
    virtual RichText contextLine() const = 0;
    virtual void bankChanged() {}          ///< processor.bank() is a new object
    virtual void songChanged() {}
    virtual void tick() {}                 ///< 30 Hz while the tab shows
    virtual void hexChanged() { repaint(); }
    virtual void shown(bool) {}
    std::function<void()> onContextChanged;
    std::function<void(int)> onSelectChannel;
    /// The right-click list's first entry on a slot field anywhere: open
    /// that item's own tab with it selected (UI_DESIGN section 2.1, the one
    /// selector convention, docs/COMMANDS_AND_TEMPO.md section 35).
    std::function<void(ui::SlotKind, int slot)> onOpenSlot;
    /// The window is opening this panel on one of its slots.
    virtual void selectSlot(int) {}
    /// What the panel was showing, kept across a close and a reopen of the
    /// window (section 35): the slot, the frame, the row. The tree is the
    /// panel's own; the window stores it beside its scale.
    virtual void saveView(juce::ValueTree&) const {}
    virtual void restoreView(const juce::ValueTree&) {}
    /// A line for the status bar: what a file did, what a preset went where.
    std::function<void(const juce::String&)> onMessage;
protected:
    void contextChanged() { if (onContextChanged) onContextChanged(); }
    void openSlot(ui::SlotKind kind, int slot) { if (onOpenSlot && slot > 0) onOpenSlot(kind, slot); }
    void message(const juce::String& text) { if (onMessage) onMessage(text); }
    ChipBoyProcessor& processor;
    int channel = 0;
};

} // namespace chipboy::plugin
