// ChipBoy -- the inline box every typed number opens, and the parsing behind
// it (UI_DESIGN section 2.1: every number is typeable, Enter commits, Escape
// cancels, and anything that is not a number is refused). Shared by the
// widgets and the grids, which are separate translation units.
#pragma once

#include "plugin/ui/Theme.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <functional>
#include <memory>

namespace chipboy::ui {
namespace detail {

/// A typed whole number, in the display's base. A leading minus is only a
/// number where the range goes below zero; anything else is refused and the
/// field keeps what it had (UI_DESIGN section 2.1).
inline bool parseTypedInt(const juce::String& text, int lo, int hi, int& out)
{
    const juce::String t = text.trim().removeCharacters(" ");
    if (t.isEmpty()) return false;
    const bool hex = ValueFormat::hex();
    int i = 0;
    bool negative = false;
    const auto first = t[0];
    if (first == '-' || first == juce::juce_wchar(0x2212)) { if (lo >= 0) return false; negative = true; i = 1; }
    else if (first == '+') i = 1;
    if (i >= t.length()) return false;
    int64_t v = 0;
    for (; i < t.length(); ++i) {
        const auto c = t[i];
        int d = -1;
        if (c >= '0' && c <= '9') d = int(c - '0');
        else if (hex && c >= 'a' && c <= 'f') d = int(c - 'a') + 10;
        else if (hex && c >= 'A' && c <= 'F') d = int(c - 'A') + 10;
        if (d < 0) return false;
        v = v * (hex ? 16 : 10) + int64_t(d);
        if (v > 1000000) v = 1000000;              // a long paste cannot overflow
    }
    out = juce::jlimit(lo, hi, int(negative ? -v : v));
    return true;
}

/// A typed slot number: in Hex the display counts from 00, so what is typed
/// is one less than the slot (section 52); Decimal types the slot itself.
inline bool parseSlotTyped(const juce::String& text, int hiSlot, int& out)
{
    if (!ValueFormat::hex()) return parseTypedInt(text, 0, hiSlot, out);
    int shown = 0;
    if (!parseTypedInt(text, 0, hiSlot - 1, shown)) return false;
    out = shown + 1;
    return true;
}
/// A typed transpose: in Hex an unsigned byte read two's complement (E0 is
/// -32), a signed number otherwise; a sign in Hex still reads as a sign.
inline bool parseTransposeTyped(const juce::String& text, int lo, int hi, int& out)
{
    const juce::String t = text.trim();
    if (ValueFormat::hex() && t.isNotEmpty() && t[0] != '-' && t[0] != '+' && t[0] != juce::juce_wchar(0x2212)) {
        int b = 0;
        if (!parseTypedInt(t, 0, 255, b)) return false;
        out = juce::jlimit(lo, hi, int(int8_t(uint8_t(b))));
        return true;
    }
    return parseTypedInt(t, lo, hi, out);
}

/// The same for the one continuous control: a decimal number, always base
/// ten -- decibels are not a register.
inline bool parseTypedFloat(const juce::String& text, float lo, float hi, float& out)
{
    juce::String t = text.trim().removeCharacters(" ").replaceCharacter(juce::juce_wchar(0x2212), '-').replaceCharacter(',', '.');
    t = t.upToFirstOccurrenceOf("dB", false, true).trim();
    if (t.isEmpty()) return false;
    int i = (t[0] == '-' || t[0] == '+') ? 1 : 0;
    if (i >= t.length()) return false;
    int dots = 0;
    for (int k = i; k < t.length(); ++k) {
        const auto c = t[k];
        if (c == '.') { if (++dots > 1) return false; continue; }
        if (c < '0' || c > '9') return false;
    }
    out = juce::jlimit(lo, hi, t.getFloatValue());
    return true;
}

/// The inline box a number opens: click or double-click into the value,
/// Enter commits, Escape cancels, focus loss commits. One per control, kept
/// alive and hidden, so committing from inside its own focus callback is
/// safe (UI_DESIGN section 2.1).
struct TypedEntry {
    std::unique_ptr<juce::TextEditor> editor;
    std::function<void(const juce::String&)> commit;
    /// Tab commits and moves on -- the next argument of a command, the next
    /// field of a row (docs/COMMANDS_AND_TEMPO.md section 34). Unset, Tab
    /// does what it always did and moves the focus.
    std::function<void()> onTab;
    bool open = false;

    /// Tab reaches a TextEditor as a focus change, so it is caught here
    /// instead.
    struct TabKey : juce::KeyListener {
        TypedEntry* self = nullptr;
        bool keyPressed(const juce::KeyPress& k, juce::Component*) override
        {
            if (self == nullptr || k.getKeyCode() != juce::KeyPress::tabKey || !self->onTab) return false;
            auto next = self->onTab;
            self->finish(true, false);
            next();
            return true;
        }
    } tabKey;

    void begin(juce::Component& owner, juce::Rectangle<int> area, const juce::String& text,
               juce::Justification j, std::function<void(const juce::String&)> onCommit)
    {
        if (area.getWidth() < 8 || area.getHeight() < 8) return;
        onTab = nullptr;                       // the caller sets it again if it wants one
        if (editor == nullptr) {
            editor = std::make_unique<juce::TextEditor>();
            editor->setFont(Fonts::mono(12.0f));
            editor->setBorder(juce::BorderSize<int>(0));
            editor->setIndents(3, 1);
            editor->setSelectAllWhenFocused(true);
            editor->setPopupMenuEnabled(false);
            editor->onReturnKey = [this] { finish(true, true); };
            editor->onEscapeKey = [this] { finish(false, true); };
            editor->onFocusLost = [this] { finish(true, false); };
            tabKey.self = this;
            editor->addKeyListener(&tabKey);
            owner.addChildComponent(*editor);
        }
        owner_ = &owner;
        editor->setJustification(j);
        commit = std::move(onCommit);
        editor->setText(text, false);
        editor->setBounds(area);
        editor->setVisible(true);
        open = true;
        editor->grabKeyboardFocus();
        editor->selectAll();
    }

    void finish(bool doCommit, bool returnFocus)
    {
        if (!open || editor == nullptr) return;
        open = false;
        onTab = nullptr;
        const auto text = editor->getText().trim();
        editor->setVisible(false);
        if (returnFocus && owner_ != nullptr) owner_->grabKeyboardFocus();
        if (doCommit && commit) commit(text);
    }

private:
    juce::Component* owner_ = nullptr;
};

} // namespace detail
} // namespace chipboy::ui
