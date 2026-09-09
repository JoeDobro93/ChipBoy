// ChipBoy -- the controls: knob, segmented row, stepper, switch, fader,
// pill, LED, the slot list and the name field (UI_DESIGN section 2; the
// mockup's .knob, .seg, .stepper, .switch, .fader, .pill, .led, .list and
// .name-input).
#include "plugin/ui/Widgets.h"

#include "plugin/ui/InlineEntry.h"

#include <cmath>

namespace chipboy::ui {

namespace {

/// The parameter round-trip shared by the discrete controls. A widget is
/// either attached (the host is the source of truth, edits are complete
/// gestures) or free-standing (onChange).
struct Binding {
    juce::RangedAudioParameter* param = nullptr;
    std::unique_ptr<juce::ParameterAttachment> attachment;
    int lo = 0, hi = 15, def = 0;

    void attach(juce::RangedAudioParameter& p, std::function<void(float)> onParam)
    {
        param = &p;
        const auto& r = p.getNormalisableRange();
        lo = juce::roundToInt(r.start);
        hi = juce::jmax(lo, juce::roundToInt(r.end));
        def = juce::jlimit(lo, hi, juce::roundToInt(p.convertFrom0to1(p.getDefaultValue())));
        attachment = std::make_unique<juce::ParameterAttachment>(p, std::move(onParam));
        attachment->sendInitialUpdate();
    }
    bool attached() const { return attachment != nullptr; }
    juce::String text(int v) const { return param->getText(param->convertTo0to1(float(v)), 32); }
    /// Every hand-made move goes through the window's history, so the host
    /// sees the change and Ctrl+Z can take it back (UI_DESIGN section 2.1).
    void push(const juce::Component& owner, int v)
    {
        if (auto* history = historyFor(owner)) history->setParameter(*param, float(v));
        else attachment->setValueAsCompleteGesture(float(v));
    }
    int span() const { return juce::jmax(1, hi - lo); }
};

using detail::parseTypedInt;
using detail::parseTypedFloat;
using detail::TypedEntry;

juce::Colour contrastText(juce::Colour fill)
{
    return fill.getPerceivedBrightness() > 0.65f ? colours::bg : juce::Colours::white;
}

void focusRing(juce::Graphics& g, juce::Rectangle<float> r, float radius)
{
    g.setColour(colours::accentHi.withAlpha(0.8f));
    g.drawRoundedRectangle(r.reduced(0.5f), radius, 1.5f);
}

} // namespace

// ===========================================================================
// Knob
// ===========================================================================
struct Knob::Impl {
    juce::String label;
    int value = 0;
    Binding bind;
    std::function<juce::String(int)> textFn;
    juce::Colour accent = colours::accent;
    bool hasAccent = false;
    int dragStart = 0;
    bool dragging = false;
    TypedEntry entry;

    juce::String text() const
    {
        if (textFn) return textFn(value);
        if (bind.attached()) return bind.text(value);
        return ValueFormat::number(value);
    }
};

Knob::Knob(const juce::String& label) : impl_(std::make_unique<Impl>())
{
    impl_->label = label;
    setWantsKeyboardFocus(true);
    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    setRepaintsOnMouseActivity(true);
    setSize(kWidth, kHeight);
}
Knob::~Knob() = default;

void Knob::attach(juce::RangedAudioParameter& p)
{
    impl_->bind.attach(p, [this](float v) { impl_->value = juce::roundToInt(v); repaint(); });
    if (getTooltip().isEmpty()) setTooltip(p.getName(64));
}
void Knob::setRange(int lo, int hi, int defaultValue)
{
    auto& b = impl_->bind;
    b.lo = lo; b.hi = juce::jmax(lo, hi); b.def = juce::jlimit(b.lo, b.hi, defaultValue);
    impl_->value = juce::jlimit(b.lo, b.hi, impl_->value);
    repaint();
}
void Knob::setValue(int v, juce::NotificationType n)
{
    v = juce::jlimit(impl_->bind.lo, impl_->bind.hi, v);
    if (v == impl_->value) return;
    impl_->value = v;
    repaint();
    if (n == juce::dontSendNotification) return;
    if (impl_->bind.attached()) impl_->bind.push(*this, v);
    else if (onChange) onChange(v);
}
int Knob::value() const { return impl_->value; }
void Knob::setTextFunction(std::function<juce::String(int)> fn) { impl_->textFn = std::move(fn); repaint(); }
void Knob::setLabel(const juce::String& text) { impl_->label = text; repaint(); }
void Knob::setAccent(juce::Colour c) { impl_->accent = c; impl_->hasAccent = true; repaint(); }
void Knob::resized() {}

void Knob::paint(juce::Graphics& g)
{
    auto& im = *impl_;
    const auto b = getLocalBounds();
    const auto dialArea = juce::Rectangle<int>(b.getCentreX() - kDial / 2, 0, kDial, kDial).toFloat();
    const float pct = float(im.value - im.bind.lo) / float(im.bind.span());
    const float alpha = isEnabled() ? 1.0f : 0.45f;
    draw::dial(g, dialArea.reduced(1.0f), pct, im.hasAccent ? im.accent : colours::accent, im.hasAccent ? im.accent : colours::text, isEnabled());
    g.setColour(colours::text.withMultipliedAlpha(alpha));
    g.setFont(Fonts::mono(11.0f));
    g.drawFittedText(im.text(), dialArea.toNearestInt().reduced(9, 0), juce::Justification::centred, 1, 0.7f);
    draw::caption(g, im.label, juce::Rectangle<int>(0, kDial + 3, b.getWidth(), 14), juce::Justification::centred,
                  colours::textDim.withMultipliedAlpha(alpha), 9.0f);
    if (hasKeyboardFocus(false)) focusRing(g, b.toFloat(), 6.0f);
}

void Knob::mouseDown(const juce::MouseEvent&)
{
    impl_->dragStart = impl_->value;
    grabKeyboardFocus();
}
void Knob::mouseDrag(const juce::MouseEvent& e)
{
    if (!isEnabled()) return;
    // One drag is one undo: the transaction opens on the first movement and
    // closes when the button comes up (UI_DESIGN section 2.1).
    if (!impl_->dragging) {
        impl_->dragging = true;
        if (auto* history = historyFor(*this))
            history->beginGesture(impl_->bind.attached() ? impl_->bind.param->getName(64) : impl_->label);
    }
    const float pixelsForRange = e.mods.isShiftDown() ? 480.0f : 120.0f;   // the mockup: 120 px of travel for the whole range
    setValue(impl_->dragStart + juce::roundToInt(-float(e.getDistanceFromDragStartY()) / pixelsForRange * float(impl_->bind.span())));
}
void Knob::mouseUp(const juce::MouseEvent&)
{
    if (!impl_->dragging) return;
    impl_->dragging = false;
    if (auto* history = historyFor(*this)) history->endGesture();
}
/// A double click types the value; with Alt it puts the default back.
void Knob::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (!isEnabled()) return;
    if (e.mods.isAltDown()) { setValue(impl_->bind.def); return; }
    auto& im = *impl_;
    const auto b = getLocalBounds();
    const auto area = juce::Rectangle<int>(b.getCentreX() - kDial / 2 + 6, (kDial - 18) / 2, kDial - 12, 18);
    im.entry.begin(*this, area, ValueFormat::number(im.value), juce::Justification::centred,
                   [this](const juce::String& text) {
                       int v = 0;
                       if (parseTypedInt(text, impl_->bind.lo, impl_->bind.hi, v)) setValue(v);
                   });
}
bool Knob::keyPressed(const juce::KeyPress& k)
{
    const int code = k.getKeyCode();
    if (code == juce::KeyPress::upKey || code == juce::KeyPress::rightKey) { setValue(impl_->value + 1); return true; }
    if (code == juce::KeyPress::downKey || code == juce::KeyPress::leftKey) { setValue(impl_->value - 1); return true; }
    if (code == juce::KeyPress::homeKey) { setValue(impl_->bind.lo); return true; }
    if (code == juce::KeyPress::endKey) { setValue(impl_->bind.hi); return true; }
    return false;
}

// ===========================================================================
// Segmented
// ===========================================================================
struct Segmented::Impl {
    juce::StringArray options;
    std::vector<bool> enabled;
    std::vector<juce::String> tips;
    std::vector<juce::Colour> optionColours;
    std::vector<bool> hasColour;
    std::vector<juce::Rectangle<float>> buttons;
    int selected = 0;
    bool mini = false;
    int hover = -1;
    Binding bind;

    juce::Font font() const { return Fonts::sans(mini ? 11.0f : 12.0f); }
    int padX() const { return mini ? 5 : 9; }
    int naturalWidth(int i) const { return juce::roundToInt(draw::textWidth(font(), options[i])) + 2 * padX(); }
    void resizeVectors()
    {
        const auto n = size_t(options.size());
        enabled.resize(n, true); tips.resize(n); optionColours.resize(n, colours::raisedHi); hasColour.resize(n, false);
        buttons.resize(n);
    }
    int indexAt(juce::Point<float> p) const
    {
        for (int i = 0; i < int(buttons.size()); ++i) if (buttons[size_t(i)].contains(p)) return i;
        return -1;
    }
};

Segmented::Segmented(const juce::StringArray& options) : impl_(std::make_unique<Impl>())
{
    setWantsKeyboardFocus(true);
    setRepaintsOnMouseActivity(true);
    setOptions(options);
}
Segmented::~Segmented() = default;

void Segmented::setOptions(const juce::StringArray& options)
{
    impl_->options = options;
    impl_->resizeVectors();
    impl_->selected = juce::jlimit(0, juce::jmax(0, options.size() - 1), impl_->selected);
    setSize(preferredWidth(), preferredHeight());
    resized();
    repaint();
}

void Segmented::attach(juce::RangedAudioParameter& p)
{
    if (impl_->options.isEmpty()) {
        juce::StringArray opts;
        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(&p)) opts = choice->choices;
        else {
            const auto& r = p.getNormalisableRange();
            for (int v = juce::roundToInt(r.start); v <= juce::roundToInt(r.end); ++v) opts.add(p.getText(p.convertTo0to1(float(v)), 16));
        }
        setOptions(opts);
    }
    impl_->bind.attach(p, [this](float v) {
        impl_->selected = juce::jlimit(0, juce::jmax(0, impl_->options.size() - 1), juce::roundToInt(v) - impl_->bind.lo);
        repaint();
    });
    if (getTooltip().isEmpty()) setTooltip(p.getName(64));
}

void Segmented::setSelected(int index, juce::NotificationType n)
{
    index = juce::jlimit(0, juce::jmax(0, impl_->options.size() - 1), index);
    if (index == impl_->selected) return;
    impl_->selected = index;
    repaint();
    if (n == juce::dontSendNotification) return;
    if (impl_->bind.attached()) impl_->bind.push(*this, impl_->bind.lo + index);
    else if (onChange) onChange(index);
}
int Segmented::selected() const { return impl_->selected; }
void Segmented::setMini(bool mini) { impl_->mini = mini; setSize(preferredWidth(), preferredHeight()); resized(); repaint(); }
void Segmented::setOptionEnabled(int i, bool on) { if (i >= 0 && i < int(impl_->enabled.size())) { impl_->enabled[size_t(i)] = on; repaint(); } }
void Segmented::setOptionTooltip(int i, const juce::String& tip) { if (i >= 0 && i < int(impl_->tips.size())) impl_->tips[size_t(i)] = tip; }
void Segmented::setOptionColour(int i, juce::Colour c)
{
    if (i >= 0 && i < int(impl_->optionColours.size())) { impl_->optionColours[size_t(i)] = c; impl_->hasColour[size_t(i)] = true; repaint(); }
}

int Segmented::preferredWidth() const
{
    int w = 6;   // border + padding
    for (int i = 0; i < impl_->options.size(); ++i) w += impl_->naturalWidth(i) + (i > 0 ? 2 : 0);
    return w;
}
int Segmented::preferredHeight() const { return impl_->mini ? 22 : 26; }

juce::String Segmented::getTooltip()
{
    const int h = impl_->hover;
    if (h >= 0 && h < int(impl_->tips.size()) && impl_->tips[size_t(h)].isNotEmpty()) return impl_->tips[size_t(h)];
    return juce::SettableTooltipClient::getTooltip();
}

void Segmented::resized()
{
    auto& im = *impl_;
    const int n = im.options.size();
    if (n == 0) return;
    const float inner = float(getWidth()) - 6.0f;
    float natural = 0.0f;
    for (int i = 0; i < n; ++i) natural += float(im.naturalWidth(i));
    natural += 2.0f * float(n - 1);
    const float extra = juce::jmax(0.0f, (inner - natural) / float(n));
    float x = 3.0f;
    const float h = juce::jmax(1.0f, float(getHeight()) - 6.0f);
    for (int i = 0; i < n; ++i) {
        const float w = float(im.naturalWidth(i)) + extra;
        im.buttons[size_t(i)] = { x, 3.0f, w, h };
        x += w + 2.0f;
    }
}

void Segmented::paint(juce::Graphics& g)
{
    using namespace colours;
    auto& im = *impl_;
    const float alpha = isEnabled() ? 1.0f : 0.45f;
    draw::panel(g, getLocalBounds(), well.withMultipliedAlpha(alpha), line.withMultipliedAlpha(alpha), 4.0f);
    g.setFont(im.font());
    for (int i = 0; i < im.options.size(); ++i) {
        const auto r = im.buttons[size_t(i)];
        const bool on = i == im.selected, hov = i == im.hover, en = im.enabled[size_t(i)] && isEnabled();
        juce::Colour fg = textMute;
        if (on) {
            const juce::Colour fill = im.hasColour[size_t(i)] ? im.optionColours[size_t(i)] : raisedHi;
            g.setColour(fill.withMultipliedAlpha(alpha));
            g.fillRoundedRectangle(r, 3.0f);
            g.setColour(juce::Colours::white.withAlpha(0.06f * alpha));
            g.fillRect(r.getX() + 2.0f, r.getY(), r.getWidth() - 4.0f, 1.0f);
            fg = im.hasColour[size_t(i)] ? contrastText(fill) : text;
        }
        else if (hov && en) {
            g.setColour(raised.withMultipliedAlpha(alpha));
            g.fillRoundedRectangle(r, 3.0f);
            fg = text;
        }
        if (!en) fg = textDim.withAlpha(0.6f);
        g.setColour(fg.withMultipliedAlpha(alpha));
        g.drawText(im.options[i], r.toNearestInt(), juce::Justification::centred, false);
    }
    if (hasKeyboardFocus(false)) focusRing(g, getLocalBounds().toFloat(), 4.0f);
}

void Segmented::mouseMove(const juce::MouseEvent& e)
{
    const int i = impl_->indexAt(e.position);
    if (i != impl_->hover) { impl_->hover = i; repaint(); }
}
void Segmented::mouseExit(const juce::MouseEvent&) { impl_->hover = -1; repaint(); }
void Segmented::mouseDown(const juce::MouseEvent& e)
{
    if (!isEnabled()) return;
    const int i = impl_->indexAt(e.position);
    if (i >= 0 && impl_->enabled[size_t(i)]) setSelected(i);
}
bool Segmented::keyPressed(const juce::KeyPress& k)
{
    const int code = k.getKeyCode();
    const int dir = code == juce::KeyPress::rightKey || code == juce::KeyPress::downKey ? 1 : code == juce::KeyPress::leftKey || code == juce::KeyPress::upKey ? -1 : 0;
    if (dir == 0) return false;
    for (int i = impl_->selected + dir; i >= 0 && i < impl_->options.size(); i += dir)
        if (impl_->enabled[size_t(i)]) { setSelected(i); break; }
    return true;
}

// ===========================================================================
// Stepper
// ===========================================================================
struct Stepper::Impl {
    int value = 0;
    Binding bind;
    std::function<juce::String(int)> textFn;
    std::function<juce::String(int)> entryToText;
    std::function<bool(const juce::String&, int&)> entryFromText;
    bool wraps = false;
    bool typed = true;                ///< every number is typeable (UI_DESIGN 2.1)
    int hover = -1;   ///< 0 minus, 1 plus
    bool stepping = false;
    TypedEntry entry;
    /// Typed entry, as the grids read digits (docs/COMMANDS_AND_TEMPO.md
    /// section 35): the first digit replaces the value and the rest append,
    /// a digit that would pass the top is refused, and Backspace takes the
    /// last one back. The base is the one the inline box would parse.
    int acc = 0, count = 0;
    void resetEntry() { acc = 0; count = 0; }
    int base() const { return entryFromText ? 10 : (ValueFormat::hex() ? 16 : 10); }
    /// The value after typing `ch` into the entry; -1 when `ch` is not a
    /// digit in the base, -2 when it is one but would push the value past
    /// the range and is refused.
    int typeDigit(juce::juce_wchar ch)
    {
        int d = -1;
        if (ch >= '0' && ch <= '9') d = int(ch - '0');
        else if (base() == 16 && ch >= 'a' && ch <= 'f') d = int(ch - 'a') + 10;
        else if (base() == 16 && ch >= 'A' && ch <= 'F') d = int(ch - 'A') + 10;
        if (d < 0) return -1;
        const int v = acc * base() + d;
        if (v > bind.hi) return -2;
        acc = v;
        ++count;
        return juce::jmax(bind.lo, v);
    }
    /// The value after Backspace: the last digit gone, or, with none left to
    /// take, zero (the range's low end above it) -- a stepper has no blank.
    int popDigit()
    {
        if (count > 0) { acc /= base(); --count; }
        return count > 0 ? juce::jmax(bind.lo, acc) : juce::jlimit(bind.lo, bind.hi, 0);
    }

    juce::String text() const
    {
        if (textFn) return textFn(value);
        if (bind.attached()) return bind.text(value);
        return ValueFormat::number(value);
    }
    /// What the inline box starts with: the field's own units, not the
    /// decorated readout ("120", not "120 BPM").
    juce::String entryText() const { return entryToText ? entryToText(value) : ValueFormat::number(value); }
    bool parseEntry(const juce::String& text, int& out) const
    {
        if (entryFromText) return entryFromText(text, out);
        return parseTypedInt(text, bind.lo, bind.hi, out);
    }
    int stepped(int delta) const
    {
        const int span = bind.hi - bind.lo + 1;
        int v = value + delta;
        if (wraps && span > 0) { v = (v - bind.lo) % span; if (v < 0) v += span; return bind.lo + v; }
        return juce::jlimit(bind.lo, bind.hi, v);
    }
};

Stepper::Stepper() : impl_(std::make_unique<Impl>())
{
    setWantsKeyboardFocus(true);
    setRepaintsOnMouseActivity(true);
    setSize(preferredWidth(), kHeight);
}
Stepper::~Stepper() = default;

void Stepper::setRange(int lo, int hi, int defaultValue)
{
    auto& b = impl_->bind;
    b.lo = lo; b.hi = juce::jmax(lo, hi); b.def = juce::jlimit(b.lo, b.hi, defaultValue);
    impl_->value = juce::jlimit(b.lo, b.hi, impl_->value);
    repaint();
}
void Stepper::attach(juce::RangedAudioParameter& p)
{
    impl_->bind.attach(p, [this](float v) { impl_->value = juce::roundToInt(v); repaint(); });
    if (getTooltip().isEmpty()) setTooltip(p.getName(64));
}
void Stepper::setValue(int v, juce::NotificationType n)
{
    v = juce::jlimit(impl_->bind.lo, impl_->bind.hi, v);
    if (v == impl_->value) return;
    impl_->value = v;
    repaint();
    if (n == juce::dontSendNotification) return;
    if (impl_->bind.attached()) impl_->bind.push(*this, v);
    else if (onChange) onChange(v);
}
int Stepper::value() const { return impl_->value; }
void Stepper::setTextFunction(std::function<juce::String(int)> fn) { impl_->textFn = std::move(fn); repaint(); }
void Stepper::setWraps(bool wraps) { impl_->wraps = wraps; }
void Stepper::setTyped(bool typed) { impl_->typed = typed; }
void Stepper::setEntryFormat(std::function<juce::String(int)> toText, std::function<bool(const juce::String&, int&)> fromText)
{
    impl_->entryToText = std::move(toText);
    impl_->entryFromText = std::move(fromText);
}
int Stepper::preferredWidth() const { return 22 + 34 + 22 + 2; }
void Stepper::resized() {}

/// The readout between the two buttons: what a click opens for typing.
void Stepper::beginTypedEntry()
{
    if (!isEnabled()) return;
    auto& im = *impl_;
    im.resetEntry();
    const auto area = juce::Rectangle<int>(24, 2, juce::jmax(8, getWidth() - 48), getHeight() - 4);
    im.entry.begin(*this, area, im.entryText(), juce::Justification::centred, [this](const juce::String& text) {
        int v = 0;
        if (impl_->parseEntry(text, v)) setValue(juce::jlimit(impl_->bind.lo, impl_->bind.hi, v));
    });
}

void Stepper::paint(juce::Graphics& g)
{
    using namespace colours;
    auto& im = *impl_;
    const float alpha = isEnabled() ? 1.0f : 0.45f;
    const auto b = getLocalBounds();
    draw::panel(g, b, well.withMultipliedAlpha(alpha), line.withMultipliedAlpha(alpha), 4.0f);
    const auto minus = juce::Rectangle<int>(1, 1, 22, b.getHeight() - 2), plus = juce::Rectangle<int>(b.getWidth() - 23, 1, 22, b.getHeight() - 2);
    g.setFont(Fonts::mono(13.0f));
    for (int i = 0; i < 2; ++i) {
        const auto r = i == 0 ? minus : plus;
        const bool hov = im.hover == i && isEnabled();
        if (hov) { g.setColour(raised); g.fillRoundedRectangle(r.toFloat(), 3.0f); }
        g.setColour((hov ? text : textMute).withMultipliedAlpha(alpha));
        g.drawText(i == 0 ? juce::String::charToString(0x2212) : "+", r, juce::Justification::centred, false);
    }
    g.setColour(text.withMultipliedAlpha(alpha));
    g.setFont(Fonts::mono(12.0f));
    g.drawFittedText(im.text(), juce::Rectangle<int>(minus.getRight(), 0, plus.getX() - minus.getRight(), b.getHeight()), juce::Justification::centred, 1, 0.7f);
    if (hasKeyboardFocus(false)) focusRing(g, b.toFloat(), 4.0f);
}

void Stepper::mouseMove(const juce::MouseEvent& e)
{
    const int h = e.x < 23 ? 0 : e.x >= getWidth() - 23 ? 1 : -1;
    if (h != impl_->hover) { impl_->hover = h; repaint(); }
}
void Stepper::mouseExit(const juce::MouseEvent&) { impl_->hover = -1; repaint(); }
void Stepper::mouseDown(const juce::MouseEvent& e)
{
    if (!isEnabled()) return;
    grabKeyboardFocus();
    impl_->resetEntry();
    if (e.mods.isPopupMenu() && onList) { onList(); return; }
    const bool onButton = e.x < 23 || e.x >= getWidth() - 23;
    // A click on the readout selects it and nothing else: digits type
    // straight at it, and the box is the double click's (section 35).
    if (!onButton) return;
    // Press and hold is one undo, however many steps it makes.
    impl_->stepping = true;
    if (auto* history = historyFor(*this))
        history->beginGesture(impl_->bind.attached() ? impl_->bind.param->getName(64) : getTooltip());
    setValue(impl_->stepped(e.x < 23 ? -1 : 1));
}
void Stepper::mouseUp(const juce::MouseEvent&)
{
    if (!impl_->stepping) return;
    impl_->stepping = false;
    if (auto* history = historyFor(*this)) history->endGesture();
}
void Stepper::mouseDoubleClick(const juce::MouseEvent& e)
{
    if (e.x < 23 || e.x >= getWidth() - 23) return;
    beginTypedEntry();
}
bool Stepper::keyPressed(const juce::KeyPress& k)
{
    const int code = k.getKeyCode();
    auto& im = *impl_;
    // Shift with the arrows: left and right by one, up and down by sixteen
    // (section 35). Without it the arrows step by one, as the buttons do.
    const bool shift = k.getModifiers().isShiftDown();
    if (code == juce::KeyPress::upKey || code == juce::KeyPress::rightKey || k.getTextCharacter() == '+') { im.resetEntry(); setValue(im.stepped(shift && code == juce::KeyPress::upKey ? 16 : 1)); return true; }
    if (code == juce::KeyPress::downKey || code == juce::KeyPress::leftKey || k.getTextCharacter() == '-') { im.resetEntry(); setValue(im.stepped(shift && code == juce::KeyPress::downKey ? -16 : -1)); return true; }
    if (code == juce::KeyPress::returnKey || code == juce::KeyPress::F2Key) { beginTypedEntry(); return true; }
    if (!im.typed) return false;
    if (code == juce::KeyPress::escapeKey) { im.resetEntry(); return true; }
    if (code == juce::KeyPress::backspaceKey) { setValue(im.popDigit()); return true; }
    if (code == juce::KeyPress::deleteKey) { im.resetEntry(); setValue(juce::jlimit(im.bind.lo, im.bind.hi, 0)); return true; }
    const int v = im.typeDigit(k.getTextCharacter());
    if (v == -1) return false;
    if (v >= 0) setValue(v);      // -2: refused, and the value stays
    return true;
}

// ===========================================================================
// Toggle
// ===========================================================================
struct Toggle::Impl {
    juce::String text, description;
    bool on = false, hover = false;
    Binding bind;

    bool rowStyle() const { return description.isNotEmpty(); }
    juce::TextLayout descriptionLayout(float width) const
    {
        juce::AttributedString s;
        s.setJustification(juce::Justification::topLeft);
        s.append(description, Fonts::sans(12.0f), colours::textMute);
        juce::TextLayout tl;
        tl.createLayout(s, juce::jmax(20.0f, width));
        return tl;
    }
};

Toggle::Toggle(const juce::String& text) : impl_(std::make_unique<Impl>())
{
    impl_->text = text;
    setWantsKeyboardFocus(true);
    setRepaintsOnMouseActivity(true);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
    setSize(38 + juce::roundToInt(draw::textWidth(Fonts::sans(12.5f), text)) + 4, kHeight);
}
Toggle::~Toggle() = default;

void Toggle::attach(juce::RangedAudioParameter& p)
{
    impl_->bind.attach(p, [this](float v) { impl_->on = v >= 0.5f; repaint(); });
    if (getTooltip().isEmpty()) setTooltip(p.getName(64));
}
void Toggle::setToggled(bool on, juce::NotificationType n)
{
    if (on == impl_->on) return;
    impl_->on = on;
    repaint();
    if (n == juce::dontSendNotification) return;
    if (impl_->bind.attached()) impl_->bind.push(*this, on ? 1 : 0);
    else if (onChange) onChange(on);
}
bool Toggle::toggled() const { return impl_->on; }
void Toggle::setText(const juce::String& text) { impl_->text = text; repaint(); }
void Toggle::setDescription(const juce::String& text) { impl_->description = text; repaint(); }

int Toggle::preferredHeight(int width) const
{
    if (!impl_->rowStyle()) return kHeight;
    const auto tl = impl_->descriptionLayout(float(width - 20 - 30 - 12));
    return 8 + 18 + 2 + int(std::ceil(tl.getHeight())) + 8;
}
void Toggle::resized() {}

void Toggle::paint(juce::Graphics& g)
{
    using namespace colours;
    auto& im = *impl_;
    const float alpha = isEnabled() ? 1.0f : 0.45f;
    const auto b = getLocalBounds();
    if (!im.rowStyle()) {
        draw::switchTrack(g, b.toFloat().removeFromLeft(30.0f), im.on, im.hover, isEnabled());
        g.setColour(text.withMultipliedAlpha(alpha));
        g.setFont(Fonts::sans(12.5f));
        g.drawText(im.text, b.withTrimmedLeft(38), juce::Justification::centredLeft, true);
    }
    else {
        draw::panel(g, b, well.withMultipliedAlpha(alpha), lineSoft.withMultipliedAlpha(alpha), 4.0f);
        auto inner = b.reduced(10, 8);
        const auto sw = inner.removeFromRight(30).toFloat();
        inner.removeFromRight(12);
        draw::switchTrack(g, sw, im.on, im.hover, isEnabled());
        g.setColour(text.withMultipliedAlpha(alpha));
        g.setFont(Fonts::sans(13.0f));
        g.drawText(im.text, inner.removeFromTop(18), juce::Justification::centredLeft, true);
        inner.removeFromTop(2);
        g.setOpacity(alpha);
        im.descriptionLayout(float(inner.getWidth())).draw(g, inner.toFloat());
        g.setOpacity(1.0f);
    }
    if (hasKeyboardFocus(false)) focusRing(g, b.toFloat(), 4.0f);
}

void Toggle::mouseEnter(const juce::MouseEvent&) { impl_->hover = true; repaint(); }
void Toggle::mouseExit(const juce::MouseEvent&) { impl_->hover = false; repaint(); }
void Toggle::mouseUp(const juce::MouseEvent& e)
{
    if (!isEnabled() || !getLocalBounds().contains(e.getPosition())) return;
    grabKeyboardFocus();
    setToggled(!impl_->on);
}
bool Toggle::keyPressed(const juce::KeyPress& k)
{
    if (k.getKeyCode() == juce::KeyPress::spaceKey || k.getKeyCode() == juce::KeyPress::returnKey) { setToggled(!impl_->on); return true; }
    return false;
}

// ===========================================================================
// Fader
// ===========================================================================
struct Fader::Impl {
    juce::Slider slider { juce::Slider::LinearVertical, juce::Slider::NoTextBox };
    std::unique_ptr<juce::ParameterAttachment> attachment;
    juce::RangedAudioParameter* param = nullptr;
    TypedEntry entry;
    bool pushing = false;      ///< inside the attachment's own callback

    juce::String readout() const
    {
        if (param == nullptr) return juce::String::charToString(0x2014);
        auto s = param->getText(param->convertTo0to1(float(slider.getValue())), 32).trim();
        if (s.startsWithChar('-')) s = juce::String::charToString(0x2212) + s.substring(1);
        else if (slider.getValue() > 0.0 && !s.startsWithChar('+')) s = "+" + s;
        return s;
    }
};

Fader::Fader() : impl_(std::make_unique<Impl>())
{
    auto& s = impl_->slider;
    s.setSliderSnapsToMousePosition(false);
    s.setEnabled(false);
    s.setScrollWheelEnabled(false);              // the wheel never edits (UI_DESIGN 2.1)
    s.onValueChange = [this] {
        auto& im = *impl_;
        repaint();
        if (im.param == nullptr || im.pushing) return;
        if (auto* history = historyFor(*this)) history->setParameter(*im.param, float(im.slider.getValue()));
        else im.attachment->setValueAsCompleteGesture(float(im.slider.getValue()));
    };
    s.onDragStart = [this] { if (auto* history = historyFor(*this)) history->beginGesture(impl_->param != nullptr ? impl_->param->getName(64) : juce::String()); };
    s.onDragEnd = [this] { if (auto* history = historyFor(*this)) history->endGesture(); };
    addAndMakeVisible(s);
    setSize(56, 120);
}
Fader::~Fader() = default;

void Fader::attach(juce::RangedAudioParameter& p)
{
    auto& im = *impl_;
    im.param = &p;
    im.slider.setEnabled(true);
    const auto& range = p.getNormalisableRange();
    im.slider.setRange(double(range.start), double(range.end), double(range.interval));
    im.attachment = std::make_unique<juce::ParameterAttachment>(p, [this](float v) {
        const juce::ScopedValueSetter<bool> guard(impl_->pushing, true);
        impl_->slider.setValue(double(v), juce::dontSendNotification);
        repaint();
    });
    im.attachment->sendInitialUpdate();
    if (getTooltip().isEmpty()) setTooltip(p.getName(64));
    repaint();
}
void Fader::resized()
{
    impl_->slider.setBounds(getLocalBounds().withTrimmedBottom(18).reduced(2, 4));
}
void Fader::paint(juce::Graphics& g)
{
    g.setColour(colours::text.withMultipliedAlpha(impl_->param != nullptr ? 1.0f : 0.45f));
    g.setFont(Fonts::mono(11.0f));
    g.drawText(impl_->readout(), getLocalBounds().removeFromBottom(18), juce::Justification::centred, false);
}
/// The dB readout under the fader is a typed field like every other number.
void Fader::mouseDoubleClick(const juce::MouseEvent&)
{
    auto& im = *impl_;
    if (im.param == nullptr) return;
    im.entry.begin(*this, getLocalBounds().removeFromBottom(18), juce::String(im.slider.getValue(), 1), juce::Justification::centred,
                   [this](const juce::String& text) {
                       auto& in = *impl_;
                       const auto& r = in.param->getNormalisableRange();
                       float v = 0.0f;
                       if (parseTypedFloat(text, r.start, r.end, v)) in.slider.setValue(double(v), juce::sendNotificationSync);
                   });
}

// ===========================================================================
// Pill, Led
// ===========================================================================
void Pill::set(const juce::String& text, Tone tone) { text_ = text; tone_ = tone; repaint(); }
int Pill::preferredWidth() const { return juce::roundToInt(draw::textWidth(Fonts::sans(11.0f).withExtraKerningFactor(0.04f), text_)) + 18; }

void Pill::paint(juce::Graphics& g)
{
    using namespace colours;
    juce::Colour fill = well, border = line, fg = textMute;
    switch (tone_) {
        case Tone::Ok:     border = ok.withAlpha(0.5f);   fg = ok;   break;
        case Tone::Warn:   border = warn.withAlpha(0.5f); fg = warn; break;
        case Tone::Bad:    border = bad.withAlpha(0.5f);  fg = bad;  break;
        case Tone::Accent: fill = accentSoft; border = accent; fg = accentHi; break;
        case Tone::Neutral: break;
    }
    const auto r = getLocalBounds().toFloat().reduced(0.5f);
    const float radius = r.getHeight() * 0.5f;
    g.setColour(fill); g.fillRoundedRectangle(r, radius);
    g.setColour(border); g.drawRoundedRectangle(r, radius, 1.0f);
    g.setColour(fg);
    g.setFont(Fonts::sans(11.0f).withExtraKerningFactor(0.04f));
    g.drawText(text_, getLocalBounds().reduced(6, 0), juce::Justification::centred, false);
}

void Led::setOn(bool on) { if (on != on_) { on_ = on; repaint(); } }
void Led::setColour(juce::Colour c) { colour_ = c; repaint(); }
void Led::paint(juce::Graphics& g)
{
    const auto c = getLocalBounds().toFloat().getCentre();
    const float r = 3.5f;
    if (on_) {
        g.setColour(colour_.withAlpha(0.16f)); g.fillEllipse(c.x - r - 4.0f, c.y - r - 4.0f, 2.0f * (r + 4.0f), 2.0f * (r + 4.0f));
        g.setColour(colour_.withAlpha(0.35f)); g.fillEllipse(c.x - r - 2.0f, c.y - r - 2.0f, 2.0f * (r + 2.0f), 2.0f * (r + 2.0f));
        g.setColour(colour_);                  g.fillEllipse(c.x - r, c.y - r, 2.0f * r, 2.0f * r);
    }
    else {
        g.setColour(colours::ledOff); g.fillEllipse(c.x - r, c.y - r, 2.0f * r, 2.0f * r);
        g.setColour(juce::Colours::black.withAlpha(0.4f)); g.drawEllipse(c.x - r + 0.5f, c.y - r + 0.5f, 2.0f * r - 1.0f, 2.0f * r - 1.0f, 1.0f);
    }
}

// ===========================================================================
// SlotList
// ===========================================================================
struct SlotList::Impl {
    struct Content : juce::Component {
        Impl& im;
        explicit Content(Impl& i) : im(i) { setRepaintsOnMouseActivity(true); }
        int rowAt(int y) const { const int r = (y - 4) / kRowHeight; return y >= 4 && r >= 0 && r < int(im.rows.size()) ? r : -1; }
        void paint(juce::Graphics& g) override { im.paintRows(g); }
        void mouseMove(const juce::MouseEvent& e) override { const int r = rowAt(e.y); if (r != im.hover) { im.hover = r; repaint(); } }
        void mouseExit(const juce::MouseEvent&) override { im.hover = -1; repaint(); }
        void mouseDown(const juce::MouseEvent& e) override
        {
            im.owner.grabKeyboardFocus();
            const int r = rowAt(e.y);
            if (r >= 0) im.selectRow(r, juce::sendNotification);
        }
        void mouseDoubleClick(const juce::MouseEvent& e) override
        {
            const int r = rowAt(e.y);
            if (r < 0) return;
            im.selectRow(r, juce::sendNotification);
            if (im.owner.onDoubleClick) im.owner.onDoubleClick(im.rows[size_t(r)].slot);
        }
    };

    SlotList& owner;
    std::vector<SlotRow> rows;
    int selectedSlot = 0;
    std::function<juce::Colour(int)> kindColour;
    int hover = -1;
    juce::Viewport viewport;
    Content content;
    std::unique_ptr<juce::TextEditor> editor;
    int editingRow = -1;
    bool renameable = true;

    explicit Impl(SlotList& o) : owner(o), content(*this)
    {
        viewport.setViewedComponent(&content, false);
        viewport.setScrollBarsShown(true, false);
        viewport.setScrollBarThickness(8);
    }
    ~Impl() { viewport.setViewedComponent(nullptr, false); }

    int rowOfSlot(int slot) const
    {
        for (size_t i = 0; i < rows.size(); ++i) if (rows[i].slot == slot) return int(i);
        return -1;
    }
    void layout()
    {
        content.setSize(juce::jmax(10, viewport.getMaximumVisibleWidth()), int(rows.size()) * kRowHeight + 8);
        if (editor != nullptr && editor->isVisible() && editingRow >= 0) editor->setBounds(editorBounds(editingRow));
    }
    juce::Rectangle<int> rowBounds(int r) const { return { 0, 4 + r * kRowHeight, content.getWidth(), kRowHeight }; }
    juce::Rectangle<int> editorBounds(int r) const { return rowBounds(r).withTrimmedLeft(30).withTrimmedRight(6).reduced(0, 2); }
    void scrollTo(int r)
    {
        if (r < 0) return;
        const auto b = rowBounds(r);
        const auto view = viewport.getViewArea();
        if (b.getY() < view.getY()) viewport.setViewPosition(0, b.getY() - 4);
        else if (b.getBottom() > view.getBottom()) viewport.setViewPosition(0, b.getBottom() + 4 - view.getHeight());
    }
    void selectRow(int r, juce::NotificationType n)
    {
        if (r < 0 || r >= int(rows.size())) return;
        const int slot = rows[size_t(r)].slot;
        const bool changed = slot != selectedSlot;
        selectedSlot = slot;
        scrollTo(r);
        content.repaint();
        if (changed && n == juce::sendNotification && owner.onSelect) owner.onSelect(slot);
    }
    void beginRename(int r)
    {
        if (!renameable || r < 0 || r >= int(rows.size())) return;
        selectRow(r, juce::sendNotification);
        if (editor == nullptr) {
            editor = std::make_unique<juce::TextEditor>();
            editor->setFont(Fonts::sans(12.5f));
            editor->setSelectAllWhenFocused(true);
            editor->setIndents(6, 2);
            editor->setBorder(juce::BorderSize<int>(0));
            editor->onReturnKey = [this] { endRename(true); };
            editor->onEscapeKey = [this] { endRename(false); };
            editor->onFocusLost = [this] { endRename(true); };
            content.addChildComponent(*editor);
        }
        editingRow = r;
        editor->setText(rows[size_t(r)].name, false);
        editor->setBounds(editorBounds(r));
        editor->setVisible(true);
        editor->grabKeyboardFocus();
    }
    void endRename(bool commit)
    {
        if (editor == nullptr || !editor->isVisible() || editingRow < 0) return;
        const int r = editingRow;
        editingRow = -1;
        const auto name = editor->getText().trim();
        editor->setVisible(false);
        if (commit && r < int(rows.size()) && name != rows[size_t(r)].name) {
            rows[size_t(r)].name = name;
            rows[size_t(r)].used = rows[size_t(r)].used || name.isNotEmpty();
            if (owner.onRename) owner.onRename(rows[size_t(r)].slot, name);
        }
        content.repaint();
        owner.grabKeyboardFocus();
    }
    void paintRows(juce::Graphics& g)
    {
        using namespace colours;
        const auto clip = g.getClipBounds();
        const int first = juce::jmax(0, (clip.getY() - 4) / kRowHeight), last = juce::jmin(int(rows.size()) - 1, (clip.getBottom() - 4) / kRowHeight);
        const juce::Font nameFont = Fonts::sans(12.5f), slotFont = Fonts::mono(11.0f), chipFont = Fonts::sans(9.5f).withExtraKerningFactor(0.08f), noteFont = Fonts::mono(10.5f);
        for (int r = first; r <= last; ++r) {
            const auto& row = rows[size_t(r)];
            const auto b = rowBounds(r);
            const bool sel = row.slot == selectedSlot, hov = r == hover;
            if (sel) { g.setColour(accentSoft); g.fillRect(b); }
            else if (hov) { g.setColour(raised); g.fillRect(b); }
            auto inner = b.reduced(8, 0);
            g.setFont(slotFont); g.setColour(textDim);
            g.drawText(ValueFormat::number(row.slot), inner.removeFromLeft(26), juce::Justification::centredLeft, false);
            if (row.kind >= 0) {
                static const char* kinds[] = { "pulse", "wave", "kit", "noise" };
                const juce::String chip = row.kind < 4 ? kinds[row.kind] : "?";
                const juce::Colour kc = kindColour ? kindColour(row.kind) : textDim;
                const int w = juce::roundToInt(draw::textWidth(chipFont, chip.toUpperCase())) + 10;
                const auto cr = inner.removeFromRight(w).withSizeKeepingCentre(w, 14);
                g.setColour(kc == textDim ? lineSoft : kc.withAlpha(0.45f)); g.drawRoundedRectangle(cr.toFloat().reduced(0.5f), 3.0f, 1.0f);
                g.setColour(kc); g.setFont(chipFont);
                g.drawText(chip.toUpperCase(), cr, juce::Justification::centred, false);
                inner.removeFromRight(6);
            }
            if (row.note.isNotEmpty()) {
                g.setFont(noteFont); g.setColour(textDim);
                const int w = juce::roundToInt(draw::textWidth(noteFont, row.note)) + 2;
                g.drawText(row.note, inner.removeFromRight(w), juce::Justification::centredRight, false);
                inner.removeFromRight(6);
            }
            if (r == editingRow) continue;
            g.setFont(nameFont);
            const bool empty = !row.used && row.name.isEmpty();
            g.setColour(empty ? textDim : (sel || hov) ? text : row.used ? textMute : textDim);
            g.drawText(empty ? juce::String::charToString(0x2014) + " empty " + juce::String::charToString(0x2014) : row.name, inner, juce::Justification::centredLeft, true);
        }
    }
};

SlotList::SlotList() : impl_(std::make_unique<Impl>(*this))
{
    setWantsKeyboardFocus(true);
    addAndMakeVisible(impl_->viewport);
}
SlotList::~SlotList() = default;

void SlotList::setRows(const std::vector<SlotRow>& rows)
{
    impl_->endRename(false);
    impl_->rows = rows;
    impl_->layout();
    impl_->content.repaint();
}
void SlotList::setSelected(int slot, juce::NotificationType n)
{
    const int r = impl_->rowOfSlot(slot);
    if (r >= 0) impl_->selectRow(r, n);
    else { impl_->selectedSlot = slot; impl_->content.repaint(); }
}
int SlotList::selected() const { return impl_->selectedSlot; }
void SlotList::setRenameable(bool on) { impl_->renameable = on; }
void SlotList::setKindColours(std::function<juce::Colour(int)> fn) { impl_->kindColour = std::move(fn); impl_->content.repaint(); }
void SlotList::beginRename() { impl_->beginRename(impl_->rowOfSlot(impl_->selectedSlot)); }

void SlotList::resized()
{
    impl_->viewport.setBounds(getLocalBounds().reduced(1));
    impl_->layout();
}
void SlotList::paint(juce::Graphics& g)
{
    draw::panel(g, getLocalBounds(), colours::panel2, colours::lineSoft, 5.0f);
    if (hasKeyboardFocus(false)) focusRing(g, getLocalBounds().toFloat(), 5.0f);
}
bool SlotList::keyPressed(const juce::KeyPress& k)
{
    auto& im = *impl_;
    const int code = k.getKeyCode();
    const int cur = im.rowOfSlot(im.selectedSlot);
    const int n = int(im.rows.size());
    if (n == 0) return false;
    if (code == juce::KeyPress::upKey)   { im.selectRow(juce::jmax(0, cur - 1), juce::sendNotification); return true; }
    if (code == juce::KeyPress::downKey) { im.selectRow(juce::jmin(n - 1, cur + 1), juce::sendNotification); return true; }
    if (code == juce::KeyPress::pageUpKey)   { im.selectRow(juce::jmax(0, cur - 8), juce::sendNotification); return true; }
    if (code == juce::KeyPress::pageDownKey) { im.selectRow(juce::jmin(n - 1, cur + 8), juce::sendNotification); return true; }
    if (code == juce::KeyPress::homeKey) { im.selectRow(0, juce::sendNotification); return true; }
    if (code == juce::KeyPress::endKey)  { im.selectRow(n - 1, juce::sendNotification); return true; }
    if (code == juce::KeyPress::returnKey || code == juce::KeyPress::F2Key) { im.beginRename(cur); return true; }
    return false;
}

// ===========================================================================
// NameField
// ===========================================================================
struct NameField::Impl {
    juce::TextEditor editor;
    juce::String committed;
};

NameField::NameField() : impl_(std::make_unique<Impl>())
{
    auto& e = impl_->editor;
    e.setFont(Fonts::sans(13.0f));
    e.setColour(juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
    e.setColour(juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    e.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
    e.setBorder(juce::BorderSize<int>(0));
    e.setIndents(8, 0);
    e.setSelectAllWhenFocused(true);
    e.onReturnKey = [this] {
        const auto t = impl_->editor.getText().trim();
        if (t != impl_->committed) { impl_->committed = t; if (onChange) onChange(t); }
        impl_->editor.moveKeyboardFocusToSibling(true);
    };
    e.onEscapeKey = [this] { impl_->editor.setText(impl_->committed, false); impl_->editor.moveKeyboardFocusToSibling(true); };
    e.onFocusLost = [this] {
        const auto t = impl_->editor.getText().trim();
        if (t != impl_->committed) { impl_->committed = t; if (onChange) onChange(t); }
    };
    addAndMakeVisible(e);
    setSize(220, kHeight);
}
NameField::~NameField() = default;

void NameField::setText(const juce::String& text, juce::NotificationType n)
{
    const bool changed = text != impl_->committed;
    impl_->committed = text;
    impl_->editor.setText(text, false);
    if (changed && n == juce::sendNotification && onChange) onChange(text);
}
juce::String NameField::text() const { return impl_->editor.getText(); }

void NameField::resized()
{
    auto& e = impl_->editor;
    const int fontH = juce::roundToInt(e.getFont().getHeight());
    e.setIndents(8, juce::jmax(0, (getHeight() - 2 - fontH) / 2));
    e.setBounds(getLocalBounds().reduced(1));
}
void NameField::paint(juce::Graphics& g)
{
    const bool focused = impl_->editor.hasKeyboardFocus(true);
    draw::panel(g, getLocalBounds(), colours::well, focused ? colours::accentHi : colours::line, 4.0f);
}
void NameField::focusOfChildComponentChanged(FocusChangeType) { repaint(); }

} // namespace chipboy::ui
