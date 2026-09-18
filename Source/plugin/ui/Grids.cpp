// ChipBoy -- the grids: the table editor, the tracker lane, the chain
// column and the wave editor (UI_DESIGN sections 6-7; the mockup's .tracker,
// .chain and .wavegrid, with the keyboard of a tracker).
//
// One grammar in every grid (docs/COMMANDS_AND_TEMPO.md section 35): a click
// selects a cell, digits typed at it build a value -- the first replaces,
// the rest append, one that would pass the limit is refused -- Backspace
// takes the last digit back and then blanks the cell, Delete blanks it, +
// and - step it, Shift with the arrows moves it by one (left, right) and by
// sixteen (up, down), a double click opens the inline box and a right click
// lists the choices the cell has. Arrows / Tab move the cursor. The wheel
// never edits: it scrolls the pane the grid stands in (UI_DESIGN 2.1).
//
// Command cells are two parts and read as two (docs/COMMANDS_AND_TEMPO.md
// section 34): the letter, chosen from the palette a right click (or a
// double click on the letter) opens or by typing the letter, and the values
// -- "4,6" in Decimal, the LSDj byte "46" in Hex. A double click on a value
// opens an inline box holding it; Tab moves to the next argument, Enter
// commits, Escape cancels, and anything outside the letter's range is
// refused. Digits typed straight at the cell still work, with a comma to
// move on. "=" makes the letter its revert form ("E =": put the envelope
// back where the instrument left it) and typing a value again clears that.
//
// The note column is a piano: z s x d c v g b h n j m are C..B, comma l
// period continue into the next octave, q 2 w 3 e r 5 t 6 y 7 u are the
// octave above and i 9 o 0 p the one above that; minus enters note off;
// Ctrl/Alt with + or - changes the octave. Shift with the arrows moves the
// note itself -- left and right a semitone, up and down an octave -- a
// vertical drag moves it a semitone every six pixels (Shift: octaves), and a
// double click types it with auto-correction: a1, A 1, a#1 and bb2 are A-1,
// A-1, A#1 and A#2, and "off" or "-" is a note off (sections 30 and 35).
//
// Every slot field obeys one convention (section 35): a click selects it and
// types, a double click opens that item's own tab, and a right click lists
// the bank's slots by name with the same entry at the top. The box is the
// double click's everywhere else -- a slot is typed at the selected cell.
#include "plugin/shared/Parameters.h"
#include "plugin/ui/InlineEntry.h"
#include "plugin/ui/Widgets.h"

#include <cmath>

namespace chipboy::ui {

namespace {

// ---------------------------------------------------------------------------
// commands: the letters, their argument ranges and the palette's defaults all
// come from one table (plugin::commandInfo), so a cell, a lane stepper and
// the palette agree (docs/COMMANDS_AND_TEMPO.md section 2).
// ---------------------------------------------------------------------------
using plugin::commandInfo;
using plugin::defaultCommand;
using detail::TypedEntry;

/// The revert form's marker, after the letter: "E =". One glyph wide, it
/// cannot be read as an argument, and it is the key that enters it
/// (UI_DESIGN section 7).
const char* kRevertMark = "=";

/// The letter alone: the cell's first part, the one the palette fills.
juce::String cmdLetterText(const bank::Command& c)
{
    const auto* info = commandInfo(c.cmd);
    return info == nullptr ? juce::String() : juce::String::charToString(juce::juce_wchar(info->letter));
}

/// "4,6" in Decimal, "46" -- the byte a playback ROM will carry -- in Hex
/// (docs/COMMANDS_AND_TEMPO.md section 34). One encoder, so a cell, a strip
/// slot and the Voice window all read the same command the same way.
juce::String cmdValueText(const bank::Command& c)
{
    if (commandInfo(c.cmd) == nullptr) return {};
    // The revert form has no arguments: it says "back to the instrument's".
    if (bank::isRevert(c)) return kRevertMark;
    return plugin::commandValueText(c, ValueFormat::hex());
}

/// Which letters a channel can carry, for the palette a cell opens.
plugin::ChannelKind kindOfChannel(int ch)
{
    return ch == 0   ? plugin::ChannelKind::Pulse1
           : ch == 1 ? plugin::ChannelKind::Pulse2
           : ch == 2 ? plugin::ChannelKind::Wave
                     : plugin::ChannelKind::Noise;
}

/// The letter changes and the values stay, clamped into what the new letter
/// takes. An empty slot takes the letter's own defaults instead, so one
/// keystroke still writes a command that does something
/// (UI_DESIGN section 2.1).
void setCmdLetter(bank::Command& c, bank::Cmd cmd)
{
    if (cmd == bank::Cmd::None) { c = {}; return; }
    if (c.cmd == bank::Cmd::None) { c = defaultCommand(cmd); return; }
    const bool revert = bank::isRevert(c) && bank::cmdPersists(cmd);
    const auto* info = commandInfo(cmd);
    c.cmd = cmd;
    c.c = revert ? bank::kRevert : int16_t(0);
    if (info == nullptr || revert) return;
    c.a = int16_t(juce::jlimit(info->lo[0], info->hi[0], int(c.a)));
    c.b = info->nargs > 1 ? int16_t(juce::jlimit(info->lo[1], info->hi[1], int(c.b))) : int16_t(0);
    if (info->nargs > 1 && c.b < info->lo[1]) c.b = int16_t(info->lo[1]);
}

/// What a cell says when the pointer rests on it: the letter, its name and
/// what its arguments mean right now.
juce::String cmdTooltip(const bank::Command& c)
{
    const auto* info = commandInfo(c.cmd);
    if (info == nullptr) return "A command, in two parts: click the letter (or type one) to choose it, then type its values.";
    juce::String s = juce::String::charToString(juce::juce_wchar(info->letter)) + "  " + info->name
                   + juce::String(juce::CharPointer_UTF8(" \xe2\x80\x94 ")) + info->args;
    const juce::String meaning = plugin::commandArgText(c);
    if (meaning.isNotEmpty()) s += juce::String(juce::CharPointer_UTF8("\n\xe2\x86\x92 ")) + meaning;
    if (bank::cmdPersists(c.cmd))
        s += juce::String("\n\"=\" puts the letter back where the instrument left it (") + juce::String::charToString(juce::juce_wchar(info->letter)) + " " + kRevertMark + "), as the slot going to none does";
    // Section 34: two views of one byte, and the view decides what typing means.
    s += ValueFormat::hex() ? juce::String("\nHex shows the byte a playback ROM carries; two digits set it.")
                            : juce::String("\nClick a value to type it, Tab for the next one.");
    s += "\nClick the letter for the palette.";
    return s;
}

// ---------------------------------------------------------------------------
// typed entry
// ---------------------------------------------------------------------------
struct Entry {
    int acc = 0, count = 0, arg = 0;
    bool negative = false;
    void reset() { acc = 0; count = 0; arg = 0; negative = false; }
    /// The digits end but the argument stays: + and -, a nudge, a comma.
    void restart() { acc = 0; count = 0; }
};

int digitValue(juce::juce_wchar ch, bool hex)
{
    if (ch >= '0' && ch <= '9') return int(ch - '0');
    if (hex) {
        if (ch >= 'a' && ch <= 'f') return int(ch - 'a') + 10;
        if (ch >= 'A' && ch <= 'F') return int(ch - 'A') + 10;
    }
    return -1;
}
/// Types one digit into a magnitude in [0, hi] (section 35): the first digit
/// of an entry replaces the value, every further one is appended, and a
/// digit that would push the value past `hi` is refused -- the value stays
/// where it was. False when `ch` is not a digit in the display's base;
/// otherwise `magnitude` is the new value, or -1 for a refused digit.
bool typeDigit(Entry& e, juce::juce_wchar ch, bool hex, int hi, int& magnitude)
{
    const int d = digitValue(ch, hex);
    if (d < 0) return false;
    const int v = e.acc * (hex ? 16 : 10) + d;
    if (v > hi) { magnitude = -1; return true; }
    e.acc = v;
    ++e.count;
    magnitude = v;
    return true;
}

/// Backspace inside an entry takes the digit typed last back (section 35):
/// "56" becomes "5". False when there was nothing typed to take back, or
/// when the last digit has just gone -- either way the cell is blanked, as
/// Backspace always did.
bool popDigit(Entry& e, bool hex, int& magnitude)
{
    if (e.count == 0) return false;
    e.acc /= hex ? 16 : 10;
    --e.count;
    magnitude = e.acc;
    return e.count > 0;
}

/// Shift with the arrows moves a value (section 35): left and right by one,
/// up and down by `vertical` -- sixteen, or an octave on a note. 0 when the
/// key is not one of the four.
int shiftDelta(const juce::KeyPress& k, int vertical = 16)
{
    if (!k.getModifiers().isShiftDown()) return 0;
    const int code = k.getKeyCode();
    return code == juce::KeyPress::rightKey ? 1 : code == juce::KeyPress::leftKey ? -1
         : code == juce::KeyPress::upKey ? vertical : code == juce::KeyPress::downKey ? -vertical : 0;
}

/// A nudged value -- Shift+arrows, +/-, a drag -- wraps at the range's ends
/// (UI_DESIGN D-UI-15): one down from 00 is the top, one up from the top is
/// the bottom. Typing never comes here; a typed value past the limit is refused.
int wrapRange(int v, int lo, int hi)
{
    const int span = hi - lo + 1;
    if (span <= 0) return lo;
    int r = (v - lo) % span;
    if (r < 0) r += span;
    return lo + r;
}

bool isBackspace(const juce::KeyPress& k) { return k.getKeyCode() == juce::KeyPress::backspaceKey; }
bool isDelete(const juce::KeyPress& k) { return k.getKeyCode() == juce::KeyPress::deleteKey; }
bool isBlankKey(const juce::KeyPress& k) { return isBackspace(k) || isDelete(k); }
bool isPlus(const juce::KeyPress& k) { return k.getTextCharacter() == '+' || k.getTextCharacter() == '='; }
bool isMinus(const juce::KeyPress& k) { return k.getTextCharacter() == '-'; }
bool hasModifier(const juce::KeyPress& k) { const auto m = k.getModifiers(); return m.isCtrlDown() || m.isAltDown() || m.isCommandDown(); }

// --- the piano ---------------------------------------------------------------
int pianoSemitone(juce::juce_wchar c)
{
    static const juce::String lower("zsxdcvgbhnjm,l.;/"), upper("q2w3er5t6y7ui9o0p");
    if (c >= 'A' && c <= 'Z') c = juce::juce_wchar(c - 'A' + 'a');
    if (const int i = lower.indexOfChar(c); i >= 0) return i;
    if (const int i = upper.indexOfChar(c); i >= 0) return 12 + i;
    return -1;
}

// --- per-kind editing; each returns true when the key was consumed ----------
bool editNote(uint8_t& note, const juce::KeyPress& k, int& octave)
{
    if (isBlankKey(k)) { note = 0; return true; }
    if (hasModifier(k)) {
        if (isPlus(k)) { octave = juce::jmin(8, octave + 1); return true; }
        if (isMinus(k)) { octave = juce::jmax(0, octave - 1); return true; }
        return false;
    }
    if (isMinus(k)) { note = tracker::kNoteOff; return true; }
    const int semi = pianoSemitone(k.getTextCharacter());
    if (semi < 0) return false;
    note = uint8_t(juce::jlimit(1, 127, 12 * (octave + 1) + semi));
    return true;
}
/// A table's LEN cell (section 64): blank, 1-15 ticks, or "H" and a row to hop
/// the volume lane there. Typing H arms the hop; the digit after it is the row.
bool editVolLen(uint8_t& ticks, int8_t& hop, const juce::KeyPress& k, Entry& e)
{
    const bool hex = ValueFormat::hex();
    const auto ch = juce::CharacterFunctions::toUpperCase(k.getTextCharacter());
    if (isBackspace(k) || isDelete(k)) { e.restart(); ticks = 0; hop = -1; return true; }
    if (ch == 'H') { e.restart(); hop = hop >= 0 ? hop : 0; ticks = 0; return true; }
    int mag = 0;
    if (!typeDigit(e, k.getTextCharacter(), hex, 15, mag)) return false;
    if (mag < 0) return true;
    if (hop >= 0) hop = int8_t(mag);
    else { ticks = uint8_t(mag); hop = -1; }
    return true;
}

bool editVol(int8_t& vol, const juce::KeyPress& k, Entry& e)
{
    const bool hex = ValueFormat::hex();
    if (isBackspace(k)) { int m = 0; vol = popDigit(e, hex, m) ? int8_t(m) : int8_t(-1); return true; }
    if (isDelete(k)) { e.restart(); vol = -1; return true; }
    if (isPlus(k) || isMinus(k)) { e.restart(); vol = int8_t(wrapRange((vol < 0 ? (isPlus(k) ? -1 : 16) : int(vol)) + (isPlus(k) ? 1 : -1), 0, 15)); return true; }
    int mag = 0;
    if (!typeDigit(e, k.getTextCharacter(), hex, 15, mag)) return false;
    if (mag >= 0) vol = int8_t(mag);
    return true;
}

/// A transpose types as LSDj shows it (section 52): in Hex the digits build
/// the two's-complement byte, E0 being -32; in Decimal they are a magnitude
/// and "-" with nothing typed flips the sign. `lo`..`hi` is the field's range.
bool editTranspose(bool& has, int8_t& t, const juce::KeyPress& k, Entry& e, int lo = -128, int hi = 127)
{
    const bool hex = ValueFormat::hex();
    auto fromByte = [lo, hi](int b) { return int8_t(juce::jlimit(lo, hi, int(int8_t(uint8_t(b))))); };
    if (isBackspace(k)) {
        int m = 0;
        if (popDigit(e, hex, m)) { has = true; t = hex ? fromByte(m) : int8_t(e.negative ? -m : m); }
        else { has = false; t = 0; }
        return true;
    }
    if (isDelete(k)) { e.restart(); has = false; t = 0; return true; }
    if (isMinus(k) && e.count == 0) { has = true; t = int8_t(juce::jlimit(lo, hi, -int(t))); e.negative = t < 0 || t == 0; return true; }
    if (isPlus(k) && e.count == 0) { has = true; t = int8_t(juce::jlimit(lo, hi, std::abs(int(t)))); e.negative = false; return true; }
    if (isMinus(k) || isPlus(k)) { e.restart(); has = true; t = int8_t(wrapRange(int(t) + (isPlus(k) ? 1 : -1), lo, hi)); return true; }
    if (hex) {
        int b = 0;
        if (!typeDigit(e, k.getTextCharacter(), true, 255, b)) return false;
        if (b < 0) return true;
        has = true; t = fromByte(b);
        return true;
    }
    if (e.count == 0) e.negative = t < 0 || (has && t == 0 && e.negative);
    int mag = 0;
    if (!typeDigit(e, k.getTextCharacter(), false, e.negative ? -lo : hi, mag)) return false;
    if (mag < 0) return true;
    has = true;
    t = int8_t(e.negative ? -mag : mag);
    return true;
}

/// A slot types as it shows: from 00 in Hex, so a typed 00 is slot 1
/// (section 52); from 1 in Decimal.
bool editSlot(uint8_t& v, int hi, const juce::KeyPress& k, Entry& e)
{
    const bool hex = ValueFormat::hex();
    auto fromShown = [hex](int m) { return uint8_t(hex ? m + 1 : m); };
    if (isBackspace(k)) { int m = 0; v = popDigit(e, hex, m) ? fromShown(m) : uint8_t(0); return true; }
    if (isDelete(k)) { e.restart(); v = 0; return true; }
    if (isPlus(k) || isMinus(k)) { e.restart(); v = uint8_t(wrapRange(int(v) + (isPlus(k) ? 1 : -1), 0, hi)); return true; }
    int mag = 0;
    if (!typeDigit(e, k.getTextCharacter(), hex, hex ? hi - 1 : hi, mag)) return false;
    if (mag >= 0) v = fromShown(mag);
    return true;
}

/// Shift with the arrows on a command cell (section 35): the argument the
/// cursor is on moves by `delta`, or the whole byte in Hex. False when the
/// slot holds no command.
bool nudgeCommand(bank::Command& c, int arg, int delta)
{
    const auto* info = commandInfo(c.cmd);
    if (info == nullptr) return false;
    if (ValueFormat::hex()) return plugin::setCommandByte(c, wrapRange(plugin::commandByte(c) + delta, 0, 255));
    const int a = juce::jlimit(0, info->nargs - 1, arg);
    int lo = 0, hi = 0;
    plugin::commandShownRange(c.cmd, a, lo, hi);
    return plugin::setCommandShownValue(c, a, wrapRange(plugin::commandShownValue(c, a) + delta, lo, hi));
}

bool editCmd(bank::Command& c, const juce::KeyPress& k, Entry& e)
{
    if (isDelete(k)) { c = {}; e.reset(); return true; }
    const auto ch = k.getTextCharacter();
    // "=" is the letter's revert form, for the letters that leave something
    // behind; a value typed afterwards clears it (docs/COMMANDS_AND_TEMPO.md
    // section 3). It costs the "=" alias of "+" in a command column.
    if (ch == '=' && bank::cmdPersists(c.cmd)) { c.a = 0; c.b = 0; c.c = bank::kRevert; e.reset(); return true; }
    const auto upper = juce::juce_wchar(ch >= 'a' && ch <= 'z' ? ch - 'a' + 'A' : ch);
    if (upper >= 'A' && upper <= 'Z') {
        const auto cmd = bank::cmdFromLetter(char(upper));
        if (cmd == bank::Cmd::None) return false;
        setCmdLetter(c, cmd);
        e.reset();
        return true;
    }
    const auto* info = commandInfo(c.cmd);
    if (info == nullptr) { if (isBackspace(k)) { c = {}; e.reset(); return true; } return false; }
    if (ch == ',' || ch == '.') { e.arg = (e.arg + 1) % info->nargs; e.restart(); return true; }
    const int a = juce::jlimit(0, info->nargs - 1, e.arg);
    // The range the window shows, which is the stored one everywhere but P:
    // it is a byte read as two's complement (section 34).
    int lo = 0, hi = 0;
    plugin::commandShownRange(c.cmd, a, lo, hi);
    const int cur = plugin::commandShownValue(c, a);
    auto put = [&c, a](int shown) { plugin::setCommandShownValue(c, a, shown); };
    // Backspace takes a typed digit back; with none left the slot empties.
    if (isBackspace(k)) {
        int m = 0;
        if (popDigit(e, false, m)) put(juce::jlimit(lo, hi, e.negative ? -m : m));
        else { c = {}; e.reset(); }
        return true;
    }
    if (isMinus(k) && lo < 0 && e.count == 0) { put(juce::jlimit(lo, hi, -cur)); e.negative = cur >= 0; return true; }
    if (isPlus(k) || isMinus(k)) { e.restart(); put(juce::jlimit(lo, hi, cur + (isPlus(k) ? 1 : -1))); return true; }
    if (e.count == 0) e.negative = lo < 0 && cur < 0;
    int mag = 0;
    if (!typeDigit(e, ch, false, juce::jmax(hi, -lo), mag)) return false;
    if (mag >= 0) put(juce::jlimit(lo, hi, e.negative ? -mag : mag));
    return true;
}

/* ------------------------------------------- typing a command's values */

/// What the inline box a click opens holds, and what it does with what is
/// typed into it (docs/COMMANDS_AND_TEMPO.md section 34). In Hex the whole
/// command is one two-digit byte; in Decimal each argument is typed on its
/// own, P signed, and anything outside the letter's range is refused -- the
/// cell keeps what it had.
juce::String cmdEntryText(const bank::Command& c, int arg)
{
    if (ValueFormat::hex()) return juce::String::toHexString(plugin::commandByte(c)).paddedLeft('0', 2).toUpperCase();
    return plugin::commandEntryText(c, arg);
}

bool cmdEntryCommit(bank::Command& c, int arg, const juce::String& text)
{
    const juce::String t = text.trim();
    if (t.isEmpty()) return false;
    if (ValueFormat::hex()) {
        if (t.length() > 2) return false;              // the byte is two digits
        for (int i = 0; i < t.length(); ++i) {
            const auto d = t[i];
            const bool digit = (d >= '0' && d <= '9') || (d >= 'a' && d <= 'f') || (d >= 'A' && d <= 'F');
            if (!digit) return false;
        }
        return plugin::setCommandByte(c, t.getHexValue32() & 255);
    }
    juce::String n = t.replaceCharacter(juce::juce_wchar(0x2212), '-');
    const bool negative = n.startsWithChar('-');
    if (negative || n.startsWithChar('+')) n = n.substring(1);
    if (n.isEmpty()) return false;
    for (int i = 0; i < n.length(); ++i) if (n[i] < '0' || n[i] > '9') return false;
    const int v = n.getIntValue();
    return plugin::setCommandShownValue(c, arg, negative ? -v : v);
}

/* --------------------------------------------------- typing a note */

/// A note typed with auto-correction (docs/COMMANDS_AND_TEMPO.md section 30):
/// "a1", "A 1", "a#1" and "bb2" are A-1, A-1, A#1 and A#2, "off" or "-" is a
/// note off, an empty box blanks the cell, and anything else is refused.
bool parseNoteText(const juce::String& text, uint8_t& out, bool numeric = false)
{
    juce::String t = text.trim().toLowerCase().removeCharacters(" -\xe2\x88\x92");
    if (text.trim().isEmpty()) { out = 0; return true; }
    if (t.isEmpty()) { out = tracker::kNoteOff; return true; }          // "-" and "---"
    if (t == "off" || t == "o") { out = tracker::kNoteOff; return true; }
    // Section 85: on the noise channel the column is a number, so a number is
    // what it takes -- in the grid's base, counted from zero as the column
    // prints it. A note name still works, because refusing one would only
    // puzzle a keyboard-minded edit.
    if (numeric) {
        const int v = ValueFormat::hex() ? t.getHexValue32() : t.getIntValue();
        bool digits = t.isNotEmpty();
        for (int k = 0; k < t.length() && digits; ++k)
            digits = juce::CharacterFunctions::isDigit(t[k]) || (ValueFormat::hex() && t[k] >= 'a' && t[k] <= 'f');
        if (digits && v >= 0 && v <= 126) { out = uint8_t(v + 1); return true; }
    }
    static const int base[7] = { 9, 11, 0, 2, 4, 5, 7 };                // a b c d e f g
    const auto letter = t[0];
    if (letter < 'a' || letter > 'g') return false;
    int semi = base[int(letter - 'a')];
    int i = 1;
    for (; i < t.length(); ++i) {
        const auto c = t[i];
        if (c == '#' || c == 's') { ++semi; continue; }
        if (c == 'b' || c == 'f') { --semi; continue; }
        break;
    }
    if (i >= t.length()) return false;                                  // a letter with no octave
    juce::String rest = t.substring(i);
    for (int k = 0; k < rest.length(); ++k) if (rest[k] < '0' || rest[k] > '9') return false;
    const int octave = rest.getIntValue();
    const int note = 12 * (octave + 1) + semi;
    if (note < 1 || note > 127) return false;
    out = uint8_t(note);
    return true;
}

/// A note moved by semitones, keeping OFF and a blank cell as they are.
void nudgeNote(uint8_t& note, int semitones)
{
    if (note == 0 || note == tracker::kNoteOff || semitones == 0) return;
    note = uint8_t(juce::jlimit(1, 127, int(note) + semitones));
}
bool sameCommand(const bank::Command& a, const bank::Command& b) { return a.cmd == b.cmd && a.a == b.a && a.b == b.b && a.c == b.c; }
bool sameCell(const tracker::Cell& a, const tracker::Cell& b)
{
    return a.note == b.note && a.vel == b.vel && a.inst == b.inst && a.table == b.table && sameCommand(a.cmd1, b.cmd1) && sameCommand(a.cmd2, b.cmd2);
}

constexpr int kPaletteNone = 100, kPaletteRevert = 101;

/// The palette a click on the letter opens: every letter this channel can
/// carry with its name and what its arguments mean, then "none", which
/// empties the slot, and the revert form for the letters that leave
/// something behind (UI_DESIGN section 2.1). `kind` is Any inside a table,
/// where every letter -- H included -- means something.
void showCommandPalette(juce::Component& target, juce::Rectangle<int> cellArea, const bank::Command& current,
                        plugin::ChannelKind kind, std::function<void(int id, bank::Cmd)> done)
{
    juce::PopupMenu m;
    m.addSectionHeader("Command");
    m.addItem(kPaletteNone, juce::String(juce::CharPointer_UTF8("\xe2\x80\x93   none")), true, current.cmd == bank::Cmd::None);
    for (int i = 0; i < bank::kCmdCount; ++i) {
        const auto cmd = bank::Cmd(i + 1);
        if (!plugin::commandAppliesTo(cmd, kind)) continue;
        const auto* info = commandInfo(cmd);
        juce::PopupMenu::Item item(juce::String::charToString(juce::juce_wchar(info->letter)) + "   " + info->name);
        item.itemID = i + 1;
        item.shortcutKeyDescription = info->args;
        item.isTicked = current.cmd == cmd;
        m.addItem(item);
    }
    if (bank::cmdPersists(current.cmd)) {
        m.addSeparator();
        m.addItem(kPaletteRevert, cmdLetterText(current) + " " + kRevertMark + "   back to the instrument's", true, bank::isRevert(current));
    }
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(target).withTargetScreenArea(target.localAreaToGlobal(cellArea)).withMinimumWidth(260),
                    [done, safe = juce::Component::SafePointer<juce::Component>(&target)](int id) {
                        if (safe == nullptr || id == 0) return;
                        done(id, id >= 1 && id <= bank::kCmdCount ? bank::Cmd(id) : bank::Cmd::None);
                    });
}

/// What the palette chose, applied to a slot.
void applyPalette(bank::Command& c, int id, bank::Cmd cmd)
{
    if (id == kPaletteNone) { c = bank::Command{}; return; }
    if (id == kPaletteRevert) { c.a = 0; c.b = 0; c.c = bank::kRevert; return; }
    setCmdLetter(c, cmd);
}

constexpr int kMenuOpen = 1000;

/// The bank's instruments, by slot and name: the list a right click on an
/// INS cell opens. Only slots in use; the ones this channel plays come
/// first, the rest are marked with their type (UI_DESIGN section 2.1). At
/// the top, where the cell names a slot and `open` is given, the entry that
/// opens that item's own tab (section 35).
void showSlotMenu(juce::Component& target, juce::Rectangle<int> cellArea, const juce::String& heading,
                  const std::vector<SlotRow>& rows, int current, std::function<void(int slot)> done,
                  std::function<void()> open = {})
{
    juce::PopupMenu m;
    m.addSectionHeader(heading);
    if (open && current > 0) {
        juce::String name;
        for (const auto& r : rows) if (r.slot == current) name = r.name;
        m.addItem(kMenuOpen, "Open " + heading.toLowerCase() + " " + ValueFormat::slot(current)
                                 + (name.isNotEmpty() ? juce::String(juce::CharPointer_UTF8(" \xc2\xb7 ")) + name : juce::String()) + " in its tab");
        m.addSeparator();
    }
    m.addItem(1, juce::String(juce::CharPointer_UTF8("\xe2\x80\x93   none")), true, current == 0);
    bool separated = false;
    for (const auto& r : rows) {
        if (!separated && r.note.isNotEmpty()) { m.addSeparator(); separated = true; }
        juce::PopupMenu::Item item(ValueFormat::slot(r.slot) + juce::String(juce::CharPointer_UTF8("  \xc2\xb7  ")) + (r.name.isEmpty() ? juce::String("(unnamed)") : r.name));
        item.itemID = r.slot + 1;
        item.shortcutKeyDescription = r.note;
        item.isTicked = r.slot == current;
        m.addItem(item);
    }
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(target).withTargetScreenArea(target.localAreaToGlobal(cellArea)).withMinimumWidth(240).withMaximumNumColumns(1),
                    [done, open, safe = juce::Component::SafePointer<juce::Component>(&target)](int id) {
                        if (safe == nullptr || id == 0) return;
                        if (id == kMenuOpen) { if (open) open(); return; }
                        done(id - 1);
                    });
}

const juce::String kBlank2 = "--", kBlank3 = "---";

// ---------------------------------------------------------------------------
// the grid core: columns, cursor, hover, navigation, painting
// ---------------------------------------------------------------------------
enum class Kind { Step, Vol, VolLen, Transpose, Cmd, Note, Vel, Inst, Table, Ghost, Info };

struct Column { Kind kind = Kind::Step; int ch = 0; int x = 0, w = 0; juce::String title; };

bool editableKind(Kind k)
{
    return k == Kind::Vol || k == Kind::VolLen || k == Kind::Transpose || k == Kind::Cmd || k == Kind::Note || k == Kind::Vel || k == Kind::Inst || k == Kind::Table;
}

struct GridCore {
    /// A command cell is two parts: the letter in this much of the cell's
    /// left, then the values (UI_DESIGN section 2.1).
    static constexpr int kLetterPad = 3, kLetterWidth = 15;

    std::vector<Column> cols;
    int rows = 16, rowH = 22, headerH = 22;
    int curRow = 0, curCol = 1, hoverRow = -1, hoverCol = -1;
    bool hoverLetter = false;      ///< the pointer is on the hovered command cell's letter
    Entry entry;

    int rowY(int row) const { return headerH + row * rowH; }
    juce::Rectangle<int> cellRect(int row, int col) const
    {
        const auto& c = cols[size_t(col)];
        return { c.x, rowY(row), c.w, rowH };
    }
    bool cellAt(juce::Point<int> p, int& row, int& col) const
    {
        if (p.y < headerH) return false;
        row = (p.y - headerH) / rowH;
        if (row < 0 || row >= rows) return false;
        for (size_t i = 0; i < cols.size(); ++i)
            if (p.x >= cols[i].x && p.x < cols[i].x + cols[i].w) { col = int(i); return true; }
        return false;
    }
    bool editable(int col) const { return col >= 0 && col < int(cols.size()) && editableKind(cols[size_t(col)].kind); }
    juce::Rectangle<int> letterRect(int row, int col) const
    {
        const auto r = cellRect(row, col);
        return { r.getX() + kLetterPad, r.getY(), kLetterWidth, r.getHeight() };
    }
    /// True where a click picks the letter rather than typing the values.
    bool onLetter(int col, int x) const
    {
        return col >= 0 && col < int(cols.size()) && cols[size_t(col)].kind == Kind::Cmd
               && x < cols[size_t(col)].x + kLetterPad + kLetterWidth;
    }
    /// Where the values are drawn, right of the letter and its hairline.
    static constexpr int kValueInset = kLetterPad + kLetterWidth + 4;
    juce::Rectangle<int> valueRect(int row, int col) const { return cellRect(row, col).withTrimmedLeft(kValueInset).withTrimmedRight(2); }
    /// Which argument a click landed on: the byte is one field in Hex, and
    /// in Decimal the comma is the boundary (section 34).
    int argAt(const bank::Command& c, int row, int col, int x) const
    {
        const auto* info = commandInfo(c.cmd);
        if (info == nullptr || info->nargs < 2 || ValueFormat::hex()) return 0;
        const auto r = valueRect(row, col);
        const float split = draw::textWidth(Fonts::mono(11.0f), juce::String(plugin::commandShownValue(c, 0)) + ",");
        return float(x - r.getX()) < split ? 0 : 1;
    }
    void setCursor(int r, int c)
    {
        if (r != curRow || c != curCol) entry.reset();
        curRow = r; curCol = c;
    }
    void ensureEditableCursor()
    {
        if (editable(curCol)) return;
        for (int c = 0; c < int(cols.size()); ++c) if (editable(c)) { setCursor(curRow, c); return; }
    }
    /// Arrows, Tab, Page Up/Down, Home/End. True when consumed.
    bool navigate(const juce::KeyPress& k)
    {
        const int code = k.getKeyCode();
        int dr = 0, dc = 0;
        if (code == juce::KeyPress::downKey) dr = 1;
        else if (code == juce::KeyPress::upKey) dr = -1;
        else if (code == juce::KeyPress::rightKey) dc = 1;
        else if (code == juce::KeyPress::leftKey) dc = -1;
        else if (code == juce::KeyPress::tabKey) dc = k.getModifiers().isShiftDown() ? -1 : 1;
        else if (code == juce::KeyPress::pageDownKey) dr = 4;
        else if (code == juce::KeyPress::pageUpKey) dr = -4;
        else if (code == juce::KeyPress::homeKey) dr = -rows;
        else if (code == juce::KeyPress::endKey) dr = rows;
        else return false;
        int r = juce::jlimit(0, rows - 1, curRow + dr), c = curCol;
        if (dc != 0) {
            int n = c + dc;
            while (n >= 0 && n < int(cols.size()) && !editable(n)) n += dc;
            if (n >= 0 && n < int(cols.size())) c = n;
        }
        setCursor(r, c);
        return true;
    }

    // --- painting ---------------------------------------------------------
    void paintHeader(juce::Graphics& g, int width, int y, int h) const
    {
        g.setColour(colours::lineSoft);
        g.fillRect(0, y + h - 1, width, 1);
        for (const auto& c : cols) {
            if (c.kind == Kind::Info) draw::label(g, c.title, juce::Rectangle<int>(c.x + 8, y, c.w - 8, h), juce::Justification::centredLeft, colours::textDim, 10.0f);
            else draw::caption(g, c.title, juce::Rectangle<int>(c.x + 6, y, c.w - 6, h), juce::Justification::centredLeft, colours::textDim, 10.0f);
        }
    }
    /// Columns a 2 px divider stands before: the lane's channel groups
    /// (docs/COMMANDS_AND_TEMPO.md section 41).
    std::vector<int> dividers;
    bool isDivider(int col) const { for (int d : dividers) if (d == col) return true; return false; }
    /// The rows' bands (section 41): every fourth row -- the beat at the
    /// straight groove -- tinted, and the rows between alternating a fainter
    /// tint, so a row and a beat are found without counting.
    void paintBands(juce::Graphics& g, int width) const
    {
        for (int r = 0; r < rows; ++r) {
            const float a = r % 4 == 0 ? 0.05f : r % 2 == 1 ? 0.02f : 0.0f;
            if (a <= 0.0f) continue;
            g.setColour(juce::Colours::white.withAlpha(a));
            g.fillRect(0, rowY(r), width, rowH);
        }
    }
    void paintRowLines(juce::Graphics& g, int width) const
    {
        g.setColour(colours::lineSoft);
        for (int r = 0; r < rows; ++r) g.fillRect(0, rowY(r) + rowH - 1, width, 1);
        for (size_t i = 1; i < cols.size(); ++i) {
            if (isDivider(int(i))) { g.setColour(colours::line); g.fillRect(cols[i].x - 1, headerH, 2, rows * rowH); g.setColour(colours::lineSoft); }
            else g.fillRect(cols[i].x, headerH, 1, rows * rowH);
        }
    }
    void paintStep(juce::Graphics& g, int row, bool playing) const
    {
        const auto r = cellRect(row, 0);
        g.setFont(Fonts::mono(12.0f));
        g.setColour(playing ? colours::accentHi : (row % 4 == 0 ? colours::textMute : colours::textDim));
        g.drawText(ValueFormat::index(row), r.withTrimmedLeft(8), juce::Justification::centredLeft, false);
    }
    void paintCell(juce::Graphics& g, int row, int col, const juce::String& text, bool blank, juce::Colour colour, bool focused) const
    {
        const auto r = cellRect(row, col);
        const bool cursor = row == curRow && col == curCol, hover = row == hoverRow && col == hoverCol;
        if (hover && editable(col)) { g.setColour(colours::raised); g.fillRect(r.withTrimmedLeft(1).withTrimmedBottom(1)); }
        g.setFont(Fonts::mono(12.0f));
        g.setColour(blank ? colours::textDim : colour);
        g.drawText(text, r.withTrimmedLeft(6).withTrimmedRight(2), juce::Justification::centredLeft, false);
        if (cursor && editable(col)) {
            g.setColour(colours::accentHi.withAlpha(focused ? 0.95f : 0.45f));
            g.drawRect(r.reduced(1), focused ? 2 : 1);
        }
    }
    /// A command cell, drawn as the two things it is: the letter, which a
    /// click opens the palette on, and the values, which are typed.
    void paintCmdCell(juce::Graphics& g, int row, int col, const bank::Command& c, bool focused) const
    {
        const auto r = cellRect(row, col);
        const auto lr = letterRect(row, col);
        const bool cursor = row == curRow && col == curCol, hover = row == hoverRow && col == hoverCol;
        const bool none = c.cmd == bank::Cmd::None;
        if (hover) { g.setColour(colours::raised); g.fillRect(r.withTrimmedLeft(1).withTrimmedBottom(1)); }
        if (hover && hoverLetter) { g.setColour(colours::raisedHi); g.fillRect(lr.reduced(0, 2)); }
        g.setFont(Fonts::mono(12.0f));
        g.setColour(none ? colours::textDim : colours::accentHi);
        g.drawText(none ? juce::String::charToString(0x2013) : cmdLetterText(c), lr, juce::Justification::centred, false);
        g.setColour(colours::lineSoft);
        g.fillRect(lr.getRight() + 1, r.getY() + 4, 1, r.getHeight() - 9);
        // The values sit a shade smaller: two of them and a comma have to
        // read in a cell the lane can afford.
        g.setFont(Fonts::mono(11.0f));
        g.setColour(none ? colours::textDim : colours::text);
        g.drawText(none ? kBlank2 : cmdValueText(c), r.withTrimmedLeft(lr.getWidth() + kLetterPad + 4).withTrimmedRight(2),
                   juce::Justification::centredLeft, false);
        if (cursor) {
            g.setColour(colours::accentHi.withAlpha(focused ? 0.95f : 0.45f));
            g.drawRect(r.reduced(1), focused ? 2 : 1);
        }
    }
};

} // namespace

// ===========================================================================
// TableGrid
// ===========================================================================
struct TableGrid::Impl {
    TableGrid& owner;
    bank::Table table;
    GridCore core;
    TypedEntry box;
    /// One running row per lane (section 64), in column order: the volume
    /// lane, the transpose-and-command lane, the second command lane.
    int playing = -1, playingE = -1, playing2 = -1;
    /// Which lane owns a column, so each lights only its own cells.
    static int laneOfColumn(Kind k)
    {
        if (k == Kind::Vol || k == Kind::VolLen) return 0;
        if (k == Kind::Transpose) return 1;
        return -1;              // Step and Info follow no lane; Cmd uses its own ch
    }
    int rowOfLane(int lane) const { return lane == 0 ? playingE : lane == 2 ? playing2 : playing; }
    /// The column's most recent values, what a blank cell fills with
    /// (section 38): refreshed from every step the grid writes.
    struct Recent { int vol = -1; int volTicks = 0; bool hasTranspose = false; int8_t transpose = 0; bank::Command cmd[2]; } recent;
    /// A vertical drag on a value cell: one unit every six pixels, sixteen
    /// with Shift, the whole drag one undo (section 38).
    static constexpr int kDragPixels = 6;
    int dragRow = -1, dragCol = -1, dragFrom = 0;
    bool dragging = false;

    explicit Impl(TableGrid& o) : owner(o)
    {
        core.rows = bank::kTableSteps; core.rowH = kRowHeight; core.headerH = kHeaderHeight;
        core.curCol = 1;
    }

    void buildColumns(int width)
    {
        auto& cols = core.cols;
        cols.clear();
        const int widths[6] = { 38, 52, 46, 72, 74, 74 };
        const Kind kinds[6] = { Kind::Step, Kind::Vol, Kind::VolLen, Kind::Transpose, Kind::Cmd, Kind::Cmd };
        const char* titles[6] = { "Step", "Vol", "Len", "Trans", "Cmd 1", "Cmd 2" };
        int x = 0;
        for (int i = 0; i < 6; ++i) { cols.push_back({ kinds[i], i == 5 ? 1 : 0, x, widths[i], titles[i] }); x += widths[i]; }
        if (width - x >= 70) cols.push_back({ Kind::Info, 0, x, width - x, juce::String::charToString(0x2192) + " written as" });
    }

    juce::String cellText(int row, int col, bool& blank) const
    {
        const auto& s = table.steps[size_t(row)];
        const auto k = core.cols[size_t(col)].kind;
        blank = false;
        if (k == Kind::Vol) { blank = s.vol < 0; return blank ? kBlank2 : ValueFormat::number(s.vol); }
        if (k == Kind::VolLen) {
            if (s.volHop >= 0) return "H" + juce::String::toHexString(int(s.volHop)).toUpperCase();
            blank = s.volTicks == 0;
            return blank ? kBlank2 : ValueFormat::number(s.volTicks);
        }
        if (k == Kind::Transpose) { blank = !s.hasTranspose; return blank ? kBlank2 : ValueFormat::transpose(s.transpose); }
        // A command cell is drawn in two parts by paintCmdCell, not here.
        if (k == Kind::Info) {
            juce::String t;
            if (s.vol >= 0) t << "NRx2 " << juce::String::charToString(0x2190) << " " << ValueFormat::byte(s.vol << 4);
            if (s.hasTranspose) t << (t.isEmpty() ? "" : "   ") << "period " << (s.transpose > 0 ? "+" : "") << juce::String(s.transpose) << " st";
            blank = true;
            return t;
        }
        return {};
    }

    void changed(int row)
    {
        table.used = true;
        remember(table.steps[size_t(row)]);
        owner.repaint(juce::Rectangle<int>(0, core.rowY(row), owner.getWidth(), core.rowH));
        if (owner.onChange) owner.onChange(table);
    }
    void remember(const bank::TableStep& s)
    {
        if (s.vol >= 0) recent.vol = s.vol;
        if (s.volTicks) recent.volTicks = s.volTicks;
        if (s.hasTranspose) { recent.hasTranspose = true; recent.transpose = s.transpose; }
        if (s.cmd1.cmd != bank::Cmd::None) recent.cmd[0] = s.cmd1;
        if (s.cmd2.cmd != bank::Cmd::None) recent.cmd[1] = s.cmd2;
    }

    /// Enter or a double click on a blank cell: the column's most recent
    /// value, else the nearest above, else a default -- volume 15, transpose
    /// 0; a command with nothing to copy stays blank (section 38). False when
    /// the cell was not blank.
    bool fillBlank(int row, int col)
    {
        if (row < 0 || row >= core.rows || !core.editable(col)) return false;
        const auto& c = core.cols[size_t(col)];
        auto& s = table.steps[size_t(row)];
        if (c.kind == Kind::Vol) {
            if (s.vol >= 0) return false;
            int v = recent.vol;
            for (int i = row - 1; v < 0 && i >= 0; --i) v = table.steps[size_t(i)].vol;
            s.vol = int8_t(v >= 0 ? v : 15);
        } else if (c.kind == Kind::VolLen) {
            if (s.volTicks || s.volHop >= 0) return false;
            int v = recent.volTicks;
            for (int i = row - 1; v <= 0 && i >= 0; --i) v = table.steps[size_t(i)].volTicks;
            s.volTicks = uint8_t(v > 0 ? v : 1);
        } else if (c.kind == Kind::Transpose) {
            if (s.hasTranspose) return false;
            int8_t t = 0; bool found = recent.hasTranspose;
            if (found) t = recent.transpose;
            for (int i = row - 1; !found && i >= 0; --i) if (table.steps[size_t(i)].hasTranspose) { found = true; t = table.steps[size_t(i)].transpose; }
            s.hasTranspose = true; s.transpose = t;
        } else if (c.kind == Kind::Cmd) {
            bank::Command& target = c.ch == 0 ? s.cmd1 : s.cmd2;
            if (target.cmd != bank::Cmd::None) return false;
            bank::Command src = recent.cmd[c.ch == 0 ? 0 : 1];
            for (int i = row - 1; src.cmd == bank::Cmd::None && i >= 0; --i) src = c.ch == 0 ? table.steps[size_t(i)].cmd1 : table.steps[size_t(i)].cmd2;
            if (src.cmd == bank::Cmd::None) return true;   // blank, and nothing to copy: still handled
            target = src;
        } else return false;
        core.entry.restart();
        changed(row);
        return true;
    }

    /// The value a drag starts from, and where it goes.
    int dragValue(int row, int col) const
    {
        const auto& c = core.cols[size_t(col)];
        const auto& s = table.steps[size_t(row)];
        if (c.kind == Kind::Vol) return juce::jmax(0, int(s.vol));
        if (c.kind == Kind::VolLen) return s.volHop >= 0 ? int(s.volHop) : int(s.volTicks);
        if (c.kind == Kind::Transpose) return s.hasTranspose ? int(s.transpose) : 0;
        if (c.kind == Kind::Cmd) {
            const auto& cmd = c.ch == 0 ? s.cmd1 : s.cmd2;
            if (cmd.cmd == bank::Cmd::None) return 0;
            return ValueFormat::hex() ? plugin::commandByte(cmd) : plugin::commandShownValue(cmd, juce::jlimit(0, commandInfo(cmd.cmd)->nargs - 1, core.entry.arg));
        }
        return 0;
    }
    void setDragValue(int row, int col, int want)
    {
        const auto& c = core.cols[size_t(col)];
        auto& s = table.steps[size_t(row)];
        if (c.kind == Kind::Vol) { const auto v = int8_t(wrapRange(want, 0, 15)); if (s.vol == v) return; s.vol = v; }
        else if (c.kind == Kind::VolLen) {
            if (s.volHop >= 0) { const auto v = int8_t(wrapRange(want, 0, 15)); if (s.volHop == v) return; s.volHop = v; }
            else { const auto v = uint8_t(wrapRange(want, 1, 15)); if (s.volTicks == v) return; s.volTicks = v; }
        }
        else if (c.kind == Kind::Transpose) { const auto v = int8_t(wrapRange(want, -128, 127)); if (s.hasTranspose && s.transpose == v) return; s.hasTranspose = true; s.transpose = v; }
        else if (c.kind == Kind::Cmd) {
            auto& cmd = c.ch == 0 ? s.cmd1 : s.cmd2;
            if (cmd.cmd == bank::Cmd::None) return;
            const int cur = dragValue(row, col);
            if (cur == want || !nudgeCommand(cmd, core.entry.arg, want - cur)) return;
        } else return;
        changed(row);
    }

    bool edit(const juce::KeyPress& k)
    {
        auto& s = table.steps[size_t(core.curRow)];
        const auto& col = core.cols[size_t(core.curCol)];
        bool done = false;
        if (col.kind == Kind::Vol) done = editVol(s.vol, k, core.entry);
        else if (col.kind == Kind::VolLen) done = editVolLen(s.volTicks, s.volHop, k, core.entry);
        else if (col.kind == Kind::Transpose) done = editTranspose(s.hasTranspose, s.transpose, k, core.entry);
        else if (col.kind == Kind::Cmd) done = editCmd(col.ch == 0 ? s.cmd1 : s.cmd2, k, core.entry);
        if (done) changed(core.curRow);
        return done;
    }

    bank::Command* commandAt(int row, int col)
    {
        if (row < 0 || row >= core.rows || col < 0 || col >= int(core.cols.size()) || core.cols[size_t(col)].kind != Kind::Cmd) return nullptr;
        auto& step = table.steps[size_t(row)];
        return core.cols[size_t(col)].ch == 0 ? &step.cmd1 : &step.cmd2;
    }

    /// A click on a value opens a box holding it (section 34): Enter
    /// commits, Tab moves to the next argument, Escape cancels, and what
    /// falls outside the letter's range is refused.
    bool openValueEntry(int row, int col, int arg)
    {
        bank::Command* c = commandAt(row, col);
        if (c == nullptr || c->cmd == bank::Cmd::None) return false;
        const auto* info = commandInfo(c->cmd);
        const int a = juce::jlimit(0, info->nargs - 1, arg);
        core.setCursor(row, col);
        core.entry.arg = a;
        box.begin(owner, core.cellRect(row, col).reduced(1), cmdEntryText(*c, a), juce::Justification::centredLeft,
                  [this, row, col, a](const juce::String& text) {
                      bank::Command* target = commandAt(row, col);
                      if (target != nullptr && cmdEntryCommit(*target, a, text)) changed(row);
                      else owner.repaint();
                  });
        if (!ValueFormat::hex() && info->nargs > 1)
            box.onTab = [this, row, col, a] { openValueEntry(row, col, (a + 1) % 2); };
        return true;
    }

    /// The double click on a volume or a transpose: the inline box holding
    /// it (section 35). An empty box blanks the cell; anything that is not a
    /// number in the display's base is refused and the cell keeps what it had.
    void openNumberEntry(int row, int col)
    {
        if (row < 0 || row >= core.rows || col < 0 || col >= int(core.cols.size())) return;
        const auto kind = core.cols[size_t(col)].kind;
        if (kind != Kind::Vol && kind != Kind::Transpose) return;
        const auto& s = table.steps[size_t(row)];
        juce::String now;
        if (kind == Kind::Vol && s.vol >= 0) now = ValueFormat::number(s.vol);
        if (kind == Kind::Transpose && s.hasTranspose) now = ValueFormat::transpose(s.transpose);
        core.setCursor(row, col);
        box.begin(owner, core.cellRect(row, col).reduced(1), now, juce::Justification::centredLeft,
                  [this, row, kind](const juce::String& text) {
                      auto& step = table.steps[size_t(row)];
                      if (text.trim().isEmpty()) {
                          if (kind == Kind::Vol) step.vol = -1; else { step.hasTranspose = false; step.transpose = 0; }
                          changed(row);
                          return;
                      }
                      int v = 0;
                      const bool ok = kind == Kind::Vol ? detail::parseTypedInt(text, 0, 15, v) : detail::parseTransposeTyped(text, -128, 127, v);
                      if (!ok) { owner.repaint(); return; }
                      if (kind == Kind::Vol) step.vol = int8_t(v); else { step.hasTranspose = true; step.transpose = int8_t(v); }
                      changed(row);
                  });
    }

    /// Shift with the arrows: the cell's value moves by `delta` (section 35).
    bool nudge(int row, int col, int delta)
    {
        if (delta == 0 || row < 0 || row >= core.rows || !core.editable(col)) return false;
        auto& s = table.steps[size_t(row)];
        const auto& c = core.cols[size_t(col)];
        core.entry.restart();
        if (c.kind == Kind::Vol) s.vol = int8_t(wrapRange((s.vol < 0 ? 0 : int(s.vol)) + delta, 0, 15));
        else if (c.kind == Kind::VolLen) {
            if (s.volHop >= 0) s.volHop = int8_t(wrapRange(int(s.volHop) + delta, 0, 15));
            else s.volTicks = uint8_t(wrapRange((s.volTicks == 0 ? (delta > 0 ? 0 : 16) : int(s.volTicks)) + delta, 1, 15));
        }
        else if (c.kind == Kind::Transpose) { s.hasTranspose = true; s.transpose = int8_t(wrapRange(int(s.transpose) + delta, -128, 127)); }
        else if (c.kind == Kind::Cmd) { if (!nudgeCommand(c.ch == 0 ? s.cmd1 : s.cmd2, core.entry.arg, delta)) return true; }
        else return false;
        changed(row);
        return true;
    }

    void openPalette(int row, int col)
    {
        if (col < 0 || col >= int(core.cols.size()) || core.cols[size_t(col)].kind != Kind::Cmd) return;
        const int which = core.cols[size_t(col)].ch;
        const auto& current = which == 0 ? table.steps[size_t(row)].cmd1 : table.steps[size_t(row)].cmd2;
        // A table takes every letter, H included: hopping is what a table
        // does with one.
        showCommandPalette(owner, core.cellRect(row, col), current, plugin::ChannelKind::Any,
                           [this, row, which](int id, bank::Cmd cmd) {
                               applyPalette(which == 0 ? table.steps[size_t(row)].cmd1 : table.steps[size_t(row)].cmd2, id, cmd);
                               core.entry.reset();
                               changed(row);
                           });
    }

    juce::String tooltip() const
    {
        const int row = core.hoverRow, col = core.hoverCol;
        if (row < 0 || row >= core.rows || col < 0 || col >= int(core.cols.size())) return {};
        const auto& c = core.cols[size_t(col)];
        const auto& s = table.steps[size_t(row)];
        if (c.kind == Kind::Vol) return "Volume at this step, 0-15; blank leaves it alone. Type it, double-click for a box, Shift+arrows move it.";
        if (c.kind == Kind::VolLen)
            return s.volHop >= 0
                 ? "The volume column hops to step " + ValueFormat::index(s.volHop) + " here and carries on from there; the other columns keep their own place (section 64). Backspace clears it."
                 : "How long this step's volume holds, in ticks; blank is as long as the table's row. The volume column keeps its own place, so it can run at its own rate. Type a number, or H and a step to hop it.";
        if (c.kind == Kind::Transpose) return "Semitones added to the note at this step; blank leaves it alone. Type it, double-click for a box, Shift+arrows move it.";
        if (c.kind == Kind::Cmd) return cmdTooltip(c.ch == 0 ? s.cmd1 : s.cmd2);
        return {};
    }
};

TableGrid::TableGrid() : impl_(std::make_unique<Impl>(*this))
{
    setWantsKeyboardFocus(true);
    setSize(420, preferredHeight());
}
TableGrid::~TableGrid() = default;

juce::String TableGrid::getTooltip() { return impl_->tooltip(); }

void TableGrid::setTable(const bank::Table& t) { impl_->table = t; repaint(); }
const bank::Table& TableGrid::table() const { return impl_->table; }
void TableGrid::setPlayingSteps(int volLane, int cmdLane, int cmd2Lane)
{
    auto& im = *impl_;
    if (volLane == im.playingE && cmdLane == im.playing && cmd2Lane == im.playing2) return;
    im.playingE = volLane; im.playing = cmdLane; im.playing2 = cmd2Lane;
    repaint();
}
void TableGrid::resized() { impl_->buildColumns(getWidth()); impl_->core.ensureEditableCursor(); }

void TableGrid::paint(juce::Graphics& g)
{
    auto& im = *impl_;
    auto& core = im.core;
    if (core.cols.empty()) im.buildColumns(getWidth());
    const bool focused = hasKeyboardFocus(false);
    core.paintHeader(g, getWidth(), 0, core.headerH);
    // Each lane lights the columns it drives, at its own row (section 64):
    // the three pointers drift apart, and a table whose columns loop at
    // different lengths is unreadable if they are drawn as one.
    for (int c = 1; c < int(core.cols.size()); ++c) {
        const auto& col = core.cols[size_t(c)];
        const int lane = col.kind == Kind::Cmd ? (col.ch == 0 ? 1 : 2) : Impl::laneOfColumn(col.kind);
        if (lane < 0) continue;
        const int row = im.rowOfLane(lane);
        if (row < 0 || row >= core.rows) continue;
        g.setColour(colours::playRow);
        g.fillRect(col.x, core.rowY(row), col.w, core.rowH);
    }
    core.paintRowLines(g, getWidth());
    for (int r = 0; r < core.rows; ++r) {
        // The step number lights for any lane on that row.
        core.paintStep(g, r, r == im.playing || r == im.playingE || r == im.playing2);
        for (int c = 1; c < int(core.cols.size()); ++c) {
            const auto kind = core.cols[size_t(c)].kind;
            if (kind == Kind::Cmd) {
                const auto& step = im.table.steps[size_t(r)];
                core.paintCmdCell(g, r, c, core.cols[size_t(c)].ch == 0 ? step.cmd1 : step.cmd2, focused);
                continue;
            }
            bool blank = false;
            const auto text = im.cellText(r, c, blank);
            if (kind == Kind::Info) {
                g.setFont(Fonts::mono(10.5f)); g.setColour(colours::textDim);
                g.drawText(text, core.cellRect(r, c).withTrimmedLeft(8), juce::Justification::centredLeft, true);
            }
            else core.paintCell(g, r, c, text, blank, colours::text, focused);
        }
    }
}

void TableGrid::mouseMove(const juce::MouseEvent& e)
{
    auto& core = impl_->core;
    int r = -1, c = -1;
    if (!core.cellAt(e.getPosition(), r, c)) { r = -1; c = -1; }
    const bool letter = c >= 0 && core.onLetter(c, e.x);
    if (r != core.hoverRow || c != core.hoverCol || letter != core.hoverLetter) {
        core.hoverRow = r; core.hoverCol = c; core.hoverLetter = letter;
        repaint();
    }
}
void TableGrid::mouseExit(const juce::MouseEvent&) { impl_->core.hoverRow = impl_->core.hoverCol = -1; impl_->core.hoverLetter = false; repaint(); }
void TableGrid::mouseDown(const juce::MouseEvent& e)
{
    auto& im = *impl_;
    auto& core = im.core;
    grabKeyboardFocus();
    im.dragging = false;
    im.dragRow = im.dragCol = -1;
    int r = 0, c = 0;
    if (!core.cellAt(e.getPosition(), r, c)) return;
    if (core.editable(c)) { core.setCursor(r, c); repaint(); }
    // A click selects; the palette is the right click's and the box the
    // double click's (section 35).
    if (e.mods.isPopupMenu()) { im.openPalette(r, c); return; }
    // A value takes a vertical drag (section 38), from the letter's right.
    if (core.editable(c) && !core.onLetter(c, e.x)) { im.dragRow = r; im.dragCol = c; im.dragFrom = im.dragValue(r, c); }
}
void TableGrid::mouseDrag(const juce::MouseEvent& e)
{
    auto& im = *impl_;
    if (im.dragRow < 0 || im.dragCol < 0) return;
    const int steps = -e.getDistanceFromDragStartY() / Impl::kDragPixels;
    if (steps == 0 && !im.dragging) return;
    im.dragging = true;
    im.setDragValue(im.dragRow, im.dragCol, im.dragFrom + steps * (e.mods.isShiftDown() ? 16 : 1));
}
void TableGrid::mouseUp(const juce::MouseEvent&)
{
    auto& im = *impl_;
    im.dragging = false;
    im.dragRow = im.dragCol = -1;
}
void TableGrid::mouseDoubleClick(const juce::MouseEvent& e)
{
    auto& im = *impl_;
    auto& core = im.core;
    int r = 0, c = 0;
    if (!core.cellAt(e.getPosition(), r, c) || !core.editable(c)) return;
    // A blank cell fills itself first (section 38).
    if (im.fillBlank(r, c)) return;
    if (core.cols[size_t(c)].kind == Kind::Cmd) {
        // The letter is its own part of the cell: a double click on it picks
        // one, and on the values opens the box that types them (section 34).
        const bank::Command* cmd = im.commandAt(r, c);
        if (core.onLetter(c, e.x) || cmd == nullptr || cmd->cmd == bank::Cmd::None) im.openPalette(r, c);
        else im.openValueEntry(r, c, core.argAt(*cmd, r, c, e.x));
        return;
    }
    im.openNumberEntry(r, c);
}
bool TableGrid::keyPressed(const juce::KeyPress& k)
{
    auto& core = impl_->core;
    if (k.getKeyCode() == juce::KeyPress::escapeKey) { core.entry.reset(); return true; }
    // Enter fills a blank cell (section 38), else opens the box: the values
    // of a command (Shift: its palette), or the number a volume or transpose
    // cell holds.
    if (k.getKeyCode() == juce::KeyPress::returnKey) {
        if (!k.getModifiers().isShiftDown() && impl_->fillBlank(core.curRow, core.curCol)) return true;
        if (core.editable(core.curCol) && core.cols[size_t(core.curCol)].kind != Kind::Cmd) impl_->openNumberEntry(core.curRow, core.curCol);
        else if (k.getModifiers().isShiftDown() || !impl_->openValueEntry(core.curRow, core.curCol, core.entry.arg))
            impl_->openPalette(core.curRow, core.curCol);
        return true;
    }
    if (const int d = shiftDelta(k); d != 0 && impl_->nudge(core.curRow, core.curCol, d)) return true;
    if (core.navigate(k)) { repaint(); return true; }
    return impl_->edit(k);
}
void TableGrid::focusGained(FocusChangeType) { repaint(); }
void TableGrid::focusLost(FocusChangeType) { impl_->core.entry.reset(); repaint(); }

// ===========================================================================
// PhraseGrid
// ===========================================================================
struct PhraseGrid::Impl {
    /// The columns a channel has: note, ins, tbl and the two commands; WAV a
    /// second note column for a kit's second sample (D-UI-34, section 221).
    static int channelCols(int ch) { return ch == 2 ? 6 : 5; }
    static int firstCol(int ch) { int c = 1; for (int k = 0; k < ch; ++k) c += channelCols(k); return c; }
    static constexpr int kStepWidth = 34;
    /// The head, per channel: the record arm, the name, the playback switch
    /// and the groove chip on the 26 px name row; then the 20 px chip row --
    /// PHRASE, TSP, STEPS, TICKS (D-UI-37) -- and the column captions. The
    /// "PLAYS" caption went with the rest of the window's spare words
    /// (docs/COMMANDS_AND_TEMPO.md section 30); the switch's tooltip says it.
    static constexpr int kArm = 14, kHead1 = 26, kHead2 = 20;

    PhraseGrid& owner;
    std::shared_ptr<const tracker::Song> song;
    std::shared_ptr<const bank::Bank> bank;
    /// Each channel's own row: the one it is in at the play head (D-UI-35).
    std::array<int, 4> chRow{};
    /// A phrase holds sixty-four cells; the grid shows a channel's own, then
    /// the rows that follow it as a preview down to the pane's foot (D-UI-40).
    static constexpr int kMax = PhraseGrid::kMaxRows;
    std::array<std::array<tracker::Cell, kMax>, 4> cells{};
    /// Per grid row and channel: the song row the cell belongs to, and
    /// whether it is that row's first (a new phrase begins there).
    std::array<std::array<int16_t, kMax>, 4> srcRow{};
    std::array<std::array<bool, kMax>, 4> boundary{};
    int visibleRows = PhraseGrid::kVisibleSteps;
    std::array<int, 4> playing { -1, -1, -1, -1 };
    std::array<int, 4> rollNote { -1, -1, -1, -1 };
    std::array<std::array<int, kMax>, 4> shadow{};   ///< the roll's notes as the bar played, greyed
    std::array<int, 4> groove{};
    std::array<bool, 4> trackerSource{};
    /// What each channel plays (section 20). Only Trkr types notes into the
    /// note column; MIDI and Hybrid both take their notes from the host, so
    /// both show the roll's note greyed there.
    std::array<tracker::NoteSource, 4> plays{ { tracker::NoteSource::PianoRoll, tracker::NoteSource::PianoRoll,
                                                tracker::NoteSource::PianoRoll, tracker::NoteSource::PianoRoll } };
    std::array<bool, 4> armed{ { true, true, true, true } };
    std::array<juce::Rectangle<int>, 4> grooveRects{}, armRects{}, phraseRects{}, tspRects{}, stepsRects{}, ticksRects{};
    std::array<int, 4> length{ { 16, 16, 16, 16 } };
    /// The chip row's other numbers (D-UI-37): the phrase slot, the row's
    /// transpose and what the row comes to in ticks under its groove and its
    /// H hops -- and which steps the play order reaches at all.
    std::array<int, 4> phraseSlot{}, tsp{}, tickCount{ { 96, 96, 96, 96 } };
    std::array<std::array<bool, kMax>, 4> reached{};
    Segmented source[4];
    GridCore core;
    TypedEntry box;
    int octave = 4;
    int hoverGroove = -1, hoverArm = -1, hoverSteps = -1, hoverTsp = -1, hoverPhrase = -1, hoverTicks = -1;
    /// The head chip the cursor is on, if any (section 35): a click on
    /// STEPS, TSP or the groove chip selects it, digits type into it, a
    /// double click opens its box, and the arrows hand the cursor back to
    /// the cells.
    int headCh = -1, headField = -1;   ///< headField: 0 STEPS, 1 groove, 2 TSP
    Entry headEntry;
    /// A vertical drag on a value: one unit every six pixels, sixteen with
    /// Shift -- a semitone and an octave on a note (sections 30 and 38).
    static constexpr int kDragPixels = 6;
    int dragRow = -1, dragCol = -1, dragFrom = 0;
    bool dragging = false;
    /// Each channel's most recent values, what a blank cell fills with and
    /// where a new note's instrument comes from (section 38): refreshed from
    /// every cell the grid writes.
    struct Recent { int note = 0, vel = 0, inst = 0, table = 0; bank::Command cmd[2]; };
    std::array<Recent, 4> recent{};

    explicit Impl(PhraseGrid& o) : owner(o)
    {
        core.rows = PhraseGrid::kVisibleSteps; core.rowH = kRowHeight; core.headerH = kHeaderHeight;
        for (int ch = 0; ch < 4; ++ch) {
            // The switch is the playback source (sections 14 and 20): the
            // channel plays the incoming MIDI, its own cells, or both -- MIDI
            // for the notes and the cells for everything else.
            source[ch].setOptions({ "MIDI", "Trkr", "Hyb" });
            source[ch].setMini(true);
            source[ch].setOptionTooltip(0, "MIDI: this channel plays the notes arriving from the host, and its cells are shown greyed beside them.");
            source[ch].setOptionTooltip(1, "Trkr: this channel plays its own cells. Incoming MIDI is ignored unless the channel is armed and recording.");
            source[ch].setOptionTooltip(2, "Hybrid: the notes come from MIDI and everything else from the cells at their steps -- the instrument and table they select, "
                                           "and their commands, fired once on whatever is sounding; the strip's Instrument, Table and both command slots are inert.");
            source[ch].onChange = [this, ch](int i) {
                plays[size_t(ch)] = i == 1 ? tracker::NoteSource::Tracker : i == 2 ? tracker::NoteSource::Hybrid : tracker::NoteSource::PianoRoll;
                trackerSource[size_t(ch)] = i == 1;
                buildColumns(owner.getWidth());
                owner.repaint();
                if (owner.onSourceChange)
                    owner.onSourceChange(ch, i == 1 ? tracker::NoteSource::Tracker : i == 2 ? tracker::NoteSource::Hybrid : tracker::NoteSource::PianoRoll);
            };
            owner.addAndMakeVisible(source[ch]);
        }
    }

    static const char* colTitle(int ch, int i)
    {
        static const char* wav[6] = { "note", "note", "ins", "tbl", "cmd", "cmd" };
        static const char* other[5] = { "note", "ins", "tbl", "cmd", "cmd" };
        return ch == 2 ? wav[i] : other[i];
    }

    int steps() const { return core.rows; }

    void buildColumns(int width)
    {
        auto& cols = core.cols;
        cols.clear();
        cols.push_back({ Kind::Step, 0, 0, kStepWidth, "Step" });
        // What each column has to show, in units of the widest: a note name
        // and a three-digit velocity, instrument and table slots, and a
        // command with two arguments. The chain takes the rest of the pane
        // (UI_DESIGN section 7).
        // D-UI-34: WAV's second note column takes a velocity's old share.
        const float wavWeights[6] = { 1.12f, 1.12f, 0.88f, 0.82f, 1.50f, 1.50f };   // the second note column as wide as the first: a three-character label
        const float otherWeights[5] = { 1.12f, 0.88f, 0.82f, 1.50f, 1.50f };
        float total = 0.0f;
        for (int ch = 0; ch < 4; ++ch) for (int i = 0; i < channelCols(ch); ++i) total += (ch == 2 ? wavWeights : otherWeights)[i];
        const float unit = juce::jmax(30.0f, float(width - kStepWidth) / total);
        float x = float(kStepWidth);
        core.dividers.clear();
        for (int ch = 0; ch < 4; ++ch) {
            const float groupX = x;
            if (ch > 0) core.dividers.push_back(int(cols.size()));
            const Kind noteKind = trackerSource[size_t(ch)] ? Kind::Note : Kind::Ghost;
            const Kind wavKinds[6] = { noteKind, Kind::Vel, Kind::Inst, Kind::Table, Kind::Cmd, Kind::Cmd };
            const Kind otherKinds[5] = { noteKind, Kind::Inst, Kind::Table, Kind::Cmd, Kind::Cmd };
            for (int i = 0; i < channelCols(ch); ++i) {
                const float w = unit * (ch == 2 ? wavWeights : otherWeights)[i];
                cols.push_back({ (ch == 2 ? wavKinds : otherKinds)[i], ch, juce::roundToInt(x), juce::roundToInt(x + w) - juce::roundToInt(x), colTitle(ch, i) });
                x += w;
            }
            layoutHeader(ch, juce::roundToInt(groupX), juce::roundToInt(x) - juce::roundToInt(groupX));
        }
        core.ensureEditableCursor();
    }
    /// The head, left to right: the arm dot, the channel's name, the
    /// playback switch and the groove chip, which takes what is left of the
    /// name row; under them the chip row, four chips of one width -- PHRASE,
    /// TSP, STEPS, TICKS -- so the four channels' chips line up (D-UI-37).
    void layoutHeader(int ch, int x, int w)
    {
        int cx = x + 3;
        armRects[size_t(ch)] = { cx, (kHead1 - kArm) / 2, kArm, kArm };
        cx += kArm + 3;
        const int nameW = juce::roundToInt(draw::textWidth(Fonts::pixel(10.0f), colours::channelName(ch))) + 3;
        cx += nameW + 4;
        auto& seg = source[ch];
        seg.setBounds(cx, 3, seg.preferredWidth(), 20);
        const int gx = seg.getRight() + 4;
        grooveRects[size_t(ch)] = { gx, 3, juce::jmin(84, juce::jmax(0, x + w - gx - 3)), 20 };
        // Four chips of unequal width -- TICKS holds four digits, PHR two --
        // laid out by the same weights on every channel, so they line up.
        const int gap = 3, room = juce::jmax(0, w - 6 - 3 * gap), y = kHead1 + 1, h = kHead2 - 2;
        const float weights[4] = { 0.84f, 0.90f, 1.10f, 1.16f };
        int cx2 = x + 3;
        juce::Rectangle<int>* rects[4] = { &phraseRects[size_t(ch)], &tspRects[size_t(ch)], &stepsRects[size_t(ch)], &ticksRects[size_t(ch)] };
        for (int i = 0; i < 4; ++i) {
            const int cw = i == 3 ? x + 3 + room + 3 * gap - cx2 : juce::roundToInt(float(room) * weights[i] / 4.0f);
            *rects[i] = { cx2, y, cw, h };
            cx2 += cw + gap;
        }
    }

    int cmdSlot(int col) const   // 0: cmd1, 1: cmd2
    {
        const auto& c = core.cols[size_t(col)];
        return (col >= 1 && c.kind == Kind::Cmd && core.cols[size_t(col - 1)].kind == Kind::Cmd) ? 1 : 0;
    }
    /// The groove's tick counts, as the chip and its menu print them.
    juce::String grooveTicks(int slot) const
    {
        if (slot <= 0 || song == nullptr || slot > int(song->grooves.size())) return "6/6";
        const auto& gr = song->grooves[size_t(slot - 1)];
        juce::String t;
        for (int i = 0; i < gr.length(); ++i) t += (i ? "/" : "") + juce::String(gr.at(i));
        return t;
    }
    /// The chip closes the head row and takes what the PLAYS switch leaves
    /// it, so it says as much as fits: the slot and its ticks where there is
    /// room, the ticks alone where there is not, and the slot when the row is
    /// down to a chip's width. The whole of it is in the tooltip and the menu.
    juce::String grooveText(int ch, int width) const
    {
        const int g = groove[size_t(ch)];
        const juce::String ticks = g <= 0 ? juce::String("6/6") : grooveTicks(g);
        if (g <= 0) return ticks;
        const juce::String full = ValueFormat::slot(g) + juce::String::charToString(0x00b7) + ticks;
        const auto fits = [width](const juce::String& s) { return draw::textWidth(Fonts::mono(10.5f), s) <= float(width - 8); };
        if (fits(full)) return full;
        if (fits(ticks)) return ticks;
        return ValueFormat::slot(g);
    }

    void refreshFromSong()
    {
        // Each channel's phrase has its own length now (section 25); the grid
        // shows as many steps as the longest of the four in this row, so every
        // cell it holds can be reached, and each head's LEN says how far its
        // own channel really runs.
        // As many rows as the longest of the four phrases, or as the pane
        // holds, whichever is more (D-UI-40).
        int longest = 1;
        if (song != nullptr) for (int ch = 0; ch < 4; ++ch) longest = juce::jmax(longest, song->stepsOfRow(ch, chRow[size_t(ch)]));
        else longest = PhraseGrid::kVisibleSteps;
        core.rows = juce::jlimit(1, kMax, juce::jmax(longest, visibleRows));
        for (int ch = 0; ch < 4; ++ch) {
            const int r = chRow[size_t(ch)];
            const auto* p = song != nullptr ? song->phrase(song->phraseAt(ch, r)) : nullptr;
            const int own = p != nullptr ? p->length() : (song != nullptr ? song->stepsOfRow(ch, r) : 16);
            for (int i = 0; i < kMax; ++i) { cells[size_t(ch)][size_t(i)] = p != nullptr && i < own ? p->cells[size_t(i)] : tracker::Cell{}; srcRow[size_t(ch)][size_t(i)] = int16_t(r); boundary[size_t(ch)][size_t(i)] = false; }
            // The preview: the rows that follow, as if the phrase ran on. A
            // looping chain comes round (section 212); one that stops shows
            // its empty rows.
            if (song != nullptr) {
                int at = r, gr = own;
                while (gr < core.rows) {
                    const int loopRows = song->loopRows(ch);
                    const bool loops = song->chainEnd[size_t(ch)] == tracker::ChainEnd::Loop && loopRows > 0;
                    at = loops && at + 1 >= loopRows ? 0 : at + 1;
                    const auto* np = song->phrase(song->phraseAt(ch, at));
                    const int len = np != nullptr ? np->length() : 16;
                    for (int i = 0; i < len && gr < core.rows; ++i, ++gr) {
                        cells[size_t(ch)][size_t(gr)] = np != nullptr ? np->cells[size_t(i)] : tracker::Cell{};
                        srcRow[size_t(ch)][size_t(gr)] = int16_t(juce::jmin(at, 32767));
                        boundary[size_t(ch)][size_t(gr)] = i == 0;
                    }
                }
            }
            groove[size_t(ch)] = p != nullptr ? p->groove : 0;
            length[size_t(ch)] = p != nullptr ? p->length() : (song != nullptr ? song->stepsOfRow(ch, r) : 16);
            // The chip row (D-UI-37): the slot, the row's transpose, and what
            // the row comes to under its groove and its H hops (section 102),
            // which is also which steps are reached at all.
            phraseSlot[size_t(ch)] = song != nullptr ? song->phraseAt(ch, r) : 0;
            tsp[size_t(ch)] = song != nullptr ? int(song->rowTranspose(ch, r)) : 0;
            tickCount[size_t(ch)] = song != nullptr ? tracker::rowTicks(*song, ch, r) : tracker::kEmptyRowTicks;
            auto& reach = reached[size_t(ch)];
            reach.fill(false);
            if (p != nullptr) {
                uint8_t order[tracker::kMaxPlaySteps];
                const int n = tracker::phrasePlayOrder(p, order, tracker::kMaxPlaySteps);
                for (int i = 0; i < n; ++i) if (order[i] < kMax) reach[order[i]] = true;
            } else for (int i = 0; i < length[size_t(ch)] && i < kMax; ++i) reach[size_t(i)] = true;
            plays[size_t(ch)] = song != nullptr ? song->noteSource[size_t(ch)] : tracker::NoteSource::PianoRoll;
            trackerSource[size_t(ch)] = plays[size_t(ch)] == tracker::NoteSource::Tracker;
            armed[size_t(ch)] = song == nullptr || song->recordArm[size_t(ch)];
            source[ch].setSelected(plays[size_t(ch)] == tracker::NoteSource::Tracker ? 1
                                   : plays[size_t(ch)] == tracker::NoteSource::Hybrid ? 2 : 0, juce::dontSendNotification);
        }
        // The cursor never rests on a preview row (D-UI-40).
        {
            const int ch = core.curCol >= 0 && core.curCol < int(core.cols.size()) ? core.cols[size_t(core.curCol)].ch : 0;
            core.curRow = juce::jlimit(0, juce::jmax(0, length[size_t(ch)] - 1), core.curRow);
        }
    }

    juce::String cellText(int row, int col, bool& blank, juce::Colour& colour) const
    {
        const auto& c = core.cols[size_t(col)];
        const auto& cell = cells[size_t(c.ch)][size_t(row)];
        blank = false; colour = colours::text;
        if (c.kind == Kind::Ghost) {
            // A MIDI or Hybrid channel shows its cells' notes dimmed and not
            // editable; where a cell is blank, the note the host played at
            // that step, fainter still (section 42).
            if (cell.note != 0) {
                blank = false;
                colour = cell.note == tracker::kNoteOff ? colours::textDim : colours::channel(c.ch).withAlpha(0.45f);
                return ValueFormat::noteValue(cell.note, c.ch == 3);   // section 85
            }
            const int n = shadow[size_t(c.ch)][size_t(row)];
            blank = true;
            return n > 0 ? ValueFormat::noteValue(n, c.ch == 3) : juce::String::charToString(0x00b7);
        }
        if (c.kind == Kind::Note) {
            blank = cell.note == 0;
            if (cell.note != 0 && cell.note != tracker::kNoteOff) {
                colour = colours::channel(c.ch);
                // D-UI-34: on a kit row the note names a sample -- the one the driver
                // picks, nearest by note -- so it reads by the sample's label.
                if (const bank::Kit* k = kitAt(c.ch, row)) { const int i = sampleOfNote(k, int(cell.note)); if (i >= 0) return sampleLabel(k, i + 1); }
            }
            return ValueFormat::noteValue(cell.note, c.ch == 3);       // section 85
        }
        if (c.kind == Kind::Vel) {
            // D-UI-34: a kit's second sample by its label; nothing at all on a
            // row whose instrument in force is not a kit.
            const bank::Kit* k = kitAt(c.ch, row);
            blank = k == nullptr || cell.vel == 0;
            if (k == nullptr) return {};
            if (!blank) colour = colours::channel(c.ch);                // a sample name reads like a note
            return blank ? kBlank2 : sampleLabel(k, int(cell.vel));
        }
        if (c.kind == Kind::Inst) { blank = cell.inst == 0; return blank ? kBlank2 : ValueFormat::slot(cell.inst); }
        if (c.kind == Kind::Table) { blank = cell.table == 0; return blank ? kBlank2 : ValueFormat::slot(cell.table); }
        // A command cell is drawn in two parts by paintCmdCell, not here.
        return {};
    }

    void changed(int ch, int row)
    {
        remember(ch, cells[size_t(ch)][size_t(row)]);
        owner.repaint(juce::Rectangle<int>(0, core.rowY(row), owner.getWidth(), core.rowH));
        if (owner.onCellChange) owner.onCellChange(ch, row, cells[size_t(ch)][size_t(row)]);
    }
    void remember(int ch, const tracker::Cell& c)
    {
        auto& r = recent[size_t(ch)];
        if (c.note != 0 && c.note != tracker::kNoteOff) r.note = c.note;
        if (c.vel) r.vel = c.vel;
        if (c.inst) r.inst = c.inst;
        if (c.table) r.table = c.table;
        if (c.cmd1.cmd != bank::Cmd::None) r.cmd[0] = c.cmd1;
        if (c.cmd2.cmd != bank::Cmd::None) r.cmd[1] = c.cmd2;
    }
    /// The instrument a new note brings (section 38): the channel's most
    /// recent, else the nearest above; 0 when there is none to bring.
    int instrumentFor(int ch, int row) const
    {
        if (recent[size_t(ch)].inst) return recent[size_t(ch)].inst;
        for (int i = row - 1; i >= 0; --i) if (cells[size_t(ch)][size_t(i)].inst) return cells[size_t(ch)][size_t(i)].inst;
        return 0;
    }
    /// A cell that went from blank to a note takes the instrument with it,
    /// once; a note off does not, and a note moved later never does.
    void noteEntered(int ch, int row, const tracker::Cell& before)
    {
        auto& cell = cells[size_t(ch)][size_t(row)];
        if (before.note != 0 || cell.note == 0 || cell.note == tracker::kNoteOff || cell.inst != 0) return;
        cell.inst = uint8_t(instrumentFor(ch, row));
    }

    /// Enter or a double click on a blank cell: the column's most recent
    /// value, else the nearest above, else a default -- the octave's C,
    /// velocity 100, instrument 1; a table or a command with nothing to copy
    /// stays blank (section 38). False when the cell was not blank.
    bool fillBlank(int row, int col)
    {
        if (row < 0 || row >= core.rows || !core.editable(col)) return false;
        const auto& c = core.cols[size_t(col)];
        const int ch = c.ch;
        auto& cell = cells[size_t(ch)][size_t(row)];
        const tracker::Cell before = cell;
        const auto& r = recent[size_t(ch)];
        auto nearest = [this, ch, row](auto get) -> int { for (int i = row - 1; i >= 0; --i) if (const int v = get(cells[size_t(ch)][size_t(i)])) return v; return 0; };
        if (c.kind == Kind::Note) {
            if (cell.note != 0) return false;
            int v = r.note ? r.note : nearest([](const tracker::Cell& x) { return x.note != tracker::kNoteOff ? int(x.note) : 0; });
            cell.note = uint8_t(v ? v : juce::jlimit(1, 127, 12 * (octave + 1)));
            noteEntered(ch, row, before);
        } else if (c.kind == Kind::Vel) {
            if (cell.vel != 0 || kitAt(c.ch, row) == nullptr) return false;   // D-UI-34: inert off a kit
            const int v = r.vel ? r.vel : nearest([](const tracker::Cell& x) { return int(x.vel); });
            cell.vel = uint8_t(juce::jlimit(1, velTop(c.ch, row), v ? v : 1));
        } else if (c.kind == Kind::Inst) {
            if (cell.inst != 0) return false;
            const int v = instrumentFor(ch, row);
            cell.inst = uint8_t(v ? v : 1);
        } else if (c.kind == Kind::Table) {
            if (cell.table != 0) return false;
            const int v = r.table ? r.table : nearest([](const tracker::Cell& x) { return int(x.table); });
            if (!v) return true;
            cell.table = uint8_t(v);
        } else if (c.kind == Kind::Cmd) {
            const int slot = cmdSlot(col);
            bank::Command& target = slot == 0 ? cell.cmd1 : cell.cmd2;
            if (target.cmd != bank::Cmd::None) return false;
            bank::Command src = r.cmd[slot];
            for (int i = row - 1; src.cmd == bank::Cmd::None && i >= 0; --i) src = slot == 0 ? cells[size_t(ch)][size_t(i)].cmd1 : cells[size_t(ch)][size_t(i)].cmd2;
            if (src.cmd == bank::Cmd::None) return true;
            target = src;
        } else return false;
        core.entry.restart();
        if (!sameCell(before, cell)) changed(ch, row);
        return true;
    }

    /// The value a drag starts from, and where it goes (section 38).
    int dragValue(int row, int col) const
    {
        const auto& c = core.cols[size_t(col)];
        const auto& cell = cells[size_t(c.ch)][size_t(row)];
        if (c.kind == Kind::Note) return int(cell.note);
        if (c.kind == Kind::Vel) return int(cell.vel);
        if (c.kind == Kind::Inst) return int(cell.inst);
        if (c.kind == Kind::Table) return int(cell.table);
        if (c.kind == Kind::Cmd) {
            const auto& cmd = cmdSlot(col) == 0 ? cell.cmd1 : cell.cmd2;
            if (cmd.cmd == bank::Cmd::None) return 0;
            return ValueFormat::hex() ? plugin::commandByte(cmd) : plugin::commandShownValue(cmd, juce::jlimit(0, commandInfo(cmd.cmd)->nargs - 1, core.entry.arg));
        }
        return 0;
    }
    void setDragValue(int row, int col, int want)
    {
        const auto& c = core.cols[size_t(col)];
        auto& cell = cells[size_t(c.ch)][size_t(row)];
        const tracker::Cell before = cell;
        if (c.kind == Kind::Note) { if (cell.note == 0 || cell.note == tracker::kNoteOff) return; cell.note = uint8_t(juce::jlimit(1, 127, want)); }
        else if (c.kind == Kind::Vel) { if (kitAt(c.ch, row) == nullptr) return; cell.vel = uint8_t(wrapRange(want, 0, velTop(c.ch, row))); }
        else if (c.kind == Kind::Inst) cell.inst = uint8_t(wrapRange(want, 0, bank::kInstrumentSlots));
        else if (c.kind == Kind::Table) cell.table = uint8_t(wrapRange(want, 0, bank::kTableSlots));
        else if (c.kind == Kind::Cmd) {
            auto& cmd = cmdSlot(col) == 0 ? cell.cmd1 : cell.cmd2;
            if (cmd.cmd == bank::Cmd::None) return;
            const int cur = dragValue(row, col);
            if (cur != want) nudgeCommand(cmd, core.entry.arg, want - cur);
        } else return;
        if (!sameCell(before, cell)) changed(c.ch, row);
    }

    bool edit(const juce::KeyPress& k)
    {
        const auto& col = core.cols[size_t(core.curCol)];
        auto& cell = cells[size_t(col.ch)][size_t(core.curRow)];
        const tracker::Cell before = cell;
        bool done = false;
        if (col.kind == Kind::Note) { done = editNote(cell.note, k, octave); if (done) noteEntered(col.ch, core.curRow, before); }
        else if (col.kind == Kind::Vel) done = kitAt(col.ch, core.curRow) != nullptr && editSlot(cell.vel, velTop(col.ch, core.curRow), k, core.entry);
        else if (col.kind == Kind::Inst) done = editSlot(cell.inst, bank::kInstrumentSlots, k, core.entry);
        else if (col.kind == Kind::Table) done = editSlot(cell.table, bank::kTableSlots, k, core.entry);
        else if (col.kind == Kind::Cmd) done = editCmd(cmdSlot(core.curCol) == 0 ? cell.cmd1 : cell.cmd2, k, core.entry);
        if (done && !sameCell(before, cell)) changed(col.ch, core.curRow);
        return done;
    }

    bank::Command* commandAt(int row, int col)
    {
        if (row < 0 || row >= core.rows || col < 0 || col >= int(core.cols.size())) return nullptr;
        if (core.cols[size_t(col)].kind != Kind::Cmd) return nullptr;
        auto& cell = cells[size_t(core.cols[size_t(col)].ch)][size_t(row)];
        return cmdSlot(col) == 0 ? &cell.cmd1 : &cell.cmd2;
    }

    /// A click on a value opens a box holding it (section 34).
    bool openValueEntry(int row, int col, int arg)
    {
        bank::Command* c = commandAt(row, col);
        if (c == nullptr || c->cmd == bank::Cmd::None) return false;
        const auto* info = commandInfo(c->cmd);
        const int a = juce::jlimit(0, info->nargs - 1, arg);
        const int ch = core.cols[size_t(col)].ch;
        core.setCursor(row, col);
        core.entry.arg = a;
        box.begin(owner, core.cellRect(row, col).reduced(1), cmdEntryText(*c, a), juce::Justification::centredLeft,
                  [this, row, col, a, ch](const juce::String& text) {
                      bank::Command* target = commandAt(row, col);
                      if (target != nullptr && cmdEntryCommit(*target, a, text)) changed(ch, row);
                      else owner.repaint();
                  });
        if (!ValueFormat::hex() && info->nargs > 1)
            box.onTab = [this, row, col, a] { openValueEntry(row, col, (a + 1) % 2); };
        return true;
    }

    /// The double click on a velocity, an instrument or a table: the inline
    /// box holding it (section 35). An empty box blanks the cell; anything
    /// that is not a number in the display's base is refused.
    void openNumberEntry(int row, int col)
    {
        if (row < 0 || row >= core.rows || col < 0 || col >= int(core.cols.size())) return;
        const auto& c = core.cols[size_t(col)];
        const auto kind = c.kind;
        if (kind != Kind::Vel && kind != Kind::Inst && kind != Kind::Table) return;
        const int ch = c.ch;
        if (kind == Kind::Vel && kitAt(ch, row) == nullptr) return;   // D-UI-34: inert off a kit
        const auto& cell = cells[size_t(ch)][size_t(row)];
        const int cur = kind == Kind::Vel ? int(cell.vel) : kind == Kind::Inst ? int(cell.inst) : int(cell.table);
        const int hi = kind == Kind::Vel ? velTop(ch, row) : kind == Kind::Inst ? bank::kInstrumentSlots : bank::kTableSlots;
        core.setCursor(row, col);
        const juce::String shown = cur == 0 ? juce::String() : kind == Kind::Vel ? sampleLabel(kitAt(ch, row), cur) : ValueFormat::slot(cur);
        box.begin(owner, core.cellRect(row, col).reduced(1), shown, juce::Justification::centredLeft,
                  [this, row, ch, kind, hi](const juce::String& text) {
                      int v = 0;
                      // A kit's second sample takes its label as well as its number.
                      const int byLabel = kind == Kind::Vel ? sampleByLabel(kitAt(ch, row), text) : 0;
                      if (byLabel > 0) v = byLabel;
                      else if (!text.trim().isEmpty() && !detail::parseSlotTyped(text, hi, v)) { owner.repaint(); return; }
                      auto& target = cells[size_t(ch)][size_t(row)];
                      uint8_t& field = kind == Kind::Vel ? target.vel : kind == Kind::Inst ? target.inst : target.table;
                      if (int(field) == v) { owner.repaint(); return; }
                      field = uint8_t(v);
                      changed(ch, row);
                  });
    }

    /// Shift with the arrows: the cell's value moves by `delta` -- a note by
    /// semitones, everything else by one or sixteen (section 35).
    bool nudge(int row, int col, int delta)
    {
        if (delta == 0 || row < 0 || row >= core.rows || !core.editable(col)) return false;
        const auto& c = core.cols[size_t(col)];
        if (c.kind == Kind::Note) return moveNote(row, col, delta);
        auto& cell = cells[size_t(c.ch)][size_t(row)];
        const tracker::Cell before = cell;
        core.entry.restart();
        if (c.kind == Kind::Vel) { if (kitAt(c.ch, row) == nullptr) return false; cell.vel = uint8_t(wrapRange(int(cell.vel) + delta, 0, velTop(c.ch, row))); }
        else if (c.kind == Kind::Inst) cell.inst = uint8_t(wrapRange(int(cell.inst) + delta, 0, bank::kInstrumentSlots));
        else if (c.kind == Kind::Table) cell.table = uint8_t(wrapRange(int(cell.table) + delta, 0, bank::kTableSlots));
        else if (c.kind == Kind::Cmd) nudgeCommand(cmdSlot(col) == 0 ? cell.cmd1 : cell.cmd2, core.entry.arg, delta);
        else return false;
        if (!sameCell(before, cell)) changed(c.ch, row);
        return true;
    }

    /// A double click on a note types it, with the auto-correction of
    /// section 30: "a1", "A 1", "a#1", "bb2", "off", "-".
    void openNoteEntry(int row, int col)
    {
        if (row < 0 || row >= core.rows || col < 0 || col >= int(core.cols.size())) return;
        const auto& c = core.cols[size_t(col)];
        if (c.kind != Kind::Note) return;
        const int ch = c.ch;
        const auto& cell = cells[size_t(ch)][size_t(row)];
        const juce::String now = cell.note == 0 ? juce::String() : ValueFormat::noteValue(cell.note, ch == 3);
        core.setCursor(row, col);
        box.begin(owner, core.cellRect(row, col).reduced(1), now, juce::Justification::centredLeft,
                  [this, row, ch](const juce::String& text) {
                      uint8_t note = 0;
                      if (!parseNoteText(text, note, ch == 3)) { owner.repaint(); return; }   // refused: the cell keeps what it had
                      auto& target = cells[size_t(ch)][size_t(row)];
                      if (target.note == note) { owner.repaint(); return; }
                      const tracker::Cell before = target;
                      target.note = note;
                      noteEntered(ch, row, before);
                      changed(ch, row);
                  });
    }

    /// Shift with the arrows (left and right a semitone, up and down an
    /// octave), and a vertical drag: the note moves in semitones (section 35).
    bool moveNote(int row, int col, int semitones)
    {
        if (semitones == 0 || col < 0 || col >= int(core.cols.size())) return false;
        const auto& c = core.cols[size_t(col)];
        if (c.kind != Kind::Note) return false;
        auto& cell = cells[size_t(c.ch)][size_t(row)];
        const uint8_t before = cell.note;
        nudgeNote(cell.note, semitones);
        if (cell.note == before) return true;      // OFF and blank cells stay as they are
        changed(c.ch, row);
        return true;
    }

    void openPalette(int row, int col)
    {
        if (col < 0 || col >= int(core.cols.size())) return;
        const auto& c = core.cols[size_t(col)];
        if (c.kind != Kind::Cmd) return;
        const int ch = c.ch, slot = cmdSlot(col);
        const auto& cell = cells[size_t(ch)][size_t(row)];
        showCommandPalette(owner, core.cellRect(row, col), slot == 0 ? cell.cmd1 : cell.cmd2, kindOfChannel(ch),
                           [this, row, ch, slot](int id, bank::Cmd cmd) {
                               auto& target = cells[size_t(ch)][size_t(row)];
                               applyPalette(slot == 0 ? target.cmd1 : target.cmd2, id, cmd);
                               core.entry.reset();
                               changed(ch, row);
                           });
    }

    /// The bank's used instruments: the ones this channel plays first, the
    /// rest below with their type. A right click on an INS cell picks one.
    std::vector<SlotRow> instrumentRows(int ch) const
    {
        std::vector<SlotRow> rows, others;
        if (bank == nullptr) return rows;
        static const char* kinds[] = { "pulse", "wave", "kit", "noise" };
        const auto want = ch == 2 ? bank::InstrumentType::Wave : ch == 3 ? bank::InstrumentType::Noise : bank::InstrumentType::Pulse;
        for (int slot = 1; slot <= bank::kInstrumentSlots; ++slot) {
            const auto* inst = bank->instrument(slot);
            if (inst == nullptr) continue;
            SlotRow r;
            r.slot = slot;
            r.name = juce::String(inst->name);
            r.used = true;
            r.kind = int(inst->type);
            // The wave channel plays kits as well as waves: both live in its RAM.
            const bool fits = inst->type == want || (ch == 2 && inst->type == bank::InstrumentType::Kit);
            if (fits) rows.push_back(r);
            else { r.note = kinds[juce::jlimit(0, 3, int(inst->type))]; others.push_back(r); }
        }
        rows.insert(rows.end(), others.begin(), others.end());
        return rows;
    }

    std::vector<SlotRow> tableRows() const
    {
        std::vector<SlotRow> rows;
        if (bank == nullptr) return rows;
        for (int slot = 1; slot <= bank::kTableSlots; ++slot)
            if (const auto* t = bank->table(slot)) rows.push_back({ slot, juce::String(t->name), true, -1, {} });
        return rows;
    }

    /// A right click on an INS or a TBL cell: the bank's slots by name.
    void openSlotMenu(int row, int col)
    {
        if (col < 0 || col >= int(core.cols.size())) return;
        const auto& c = core.cols[size_t(col)];
        if (c.kind == Kind::Vel) {
            // D-UI-34: the kit's samples, by label and name.
            const bank::Kit* k = kitAt(c.ch, row);
            if (k == nullptr) return;
            std::vector<SlotRow> rows;
            for (int i = 0; i < int(k->samples.size()); ++i) {
                SlotRow sr; sr.slot = i + 1; sr.used = true;
                sr.name = juce::String(bank::kitSampleLabel(k->samples[size_t(i)].name, i)) + juce::String(juce::CharPointer_UTF8("  \xc2\xb7  ")) + juce::String(k->samples[size_t(i)].name);
                rows.push_back(sr);
            }
            const int ch = c.ch, current = int(cells[size_t(ch)][size_t(row)].vel);
            showSlotMenu(owner, core.cellRect(row, col), "Second sample", rows, current,
                         [this, row, ch](int slot) { cells[size_t(ch)][size_t(row)].vel = uint8_t(juce::jmax(0, slot)); core.entry.reset(); changed(ch, row); });
            return;
        }
        if (c.kind != Kind::Inst && c.kind != Kind::Table) return;
        const bool instruments = c.kind == Kind::Inst;
        const auto rows = instruments ? instrumentRows(c.ch) : tableRows();
        if (rows.empty()) return;
        const auto& cell = cells[size_t(c.ch)][size_t(row)];
        const int ch = c.ch;
        const int current = instruments ? int(cell.inst) : int(cell.table);
        showSlotMenu(owner, core.cellRect(row, col), instruments ? "Instrument" : "Table", rows, current,
                     [this, row, ch, instruments](int slot) {
                         auto& target = cells[size_t(ch)][size_t(row)];
                         (instruments ? target.inst : target.table) = uint8_t(juce::jmax(0, slot));
                         core.entry.reset();
                         changed(ch, row);
                     },
                     [this, instruments, current] { openSlot(instruments ? SlotKind::Instrument : SlotKind::Table, current); });
    }

    /* ------------------------------------------------- the head's chips */

    /// A click on STEPS, TSP or the groove chip puts the cursor there (35).
    void selectHead(int ch, int field)
    {
        if (owner.onEntryEnd) owner.onEntryEnd();
        headCh = ch; headField = field;
        headEntry.reset();
        owner.repaint();
    }
    void leaveHead()
    {
        if (headField < 0) return;
        headField = -1; headCh = -1;
        headEntry.reset();
        owner.repaint();
    }
    void setHeadValue(int v)
    {
        const size_t ch = size_t(headCh);
        if (headField == 0) {
            v = juce::jlimit(1, tracker::kMaxSteps, v);
            if (v == length[ch]) return;
            length[ch] = v;
            owner.repaint();
            if (owner.onLengthChange) owner.onLengthChange(headCh, v);
        } else if (headField == 2) {
            v = juce::jlimit(-128, 127, v);
            if (v == tsp[ch]) return;
            tsp[ch] = v;
            owner.repaint();
            if (owner.onTransposeChange) owner.onTransposeChange(headCh, v);
        } else {
            v = juce::jlimit(0, 16, v);
            if (v == groove[ch]) return;
            groove[ch] = v;
            owner.repaint();
            if (owner.onGrooveChange) owner.onGrooveChange(headCh, v);
        }
    }
    /// The keys a selected chip takes: the grammar of section 35, with the
    /// range's low end where a cell would blank (a phrase is never shorter
    /// than one step; groove 0 is straight). False hands the key on -- the
    /// arrows and Tab go back to the cells.
    bool headKey(const juce::KeyPress& k)
    {
        if (headField < 0 || headCh < 0) return false;
        if (headField == 2) {
            // The transpose types like the chain's cell (sections 35 and 48):
            // digits a magnitude, "-" with nothing typed flips the sign, +/-
            // and Shift+arrows move it through zero.
            if (k.getKeyCode() == juce::KeyPress::returnKey) { openTspEntry(headCh); return true; }
            const int cur = tsp[size_t(headCh)];
            bool has = cur != 0; int8_t t = int8_t(cur);
            if (!editTranspose(has, t, k, headEntry, -128, 127)) return false;
            setHeadValue(has ? int(t) : 0);
            return true;
        }
        const bool len = headField == 0;
        const int cur = len ? length[size_t(headCh)] : groove[size_t(headCh)];
        const int lo = len ? 1 : 0, hi = len ? tracker::kMaxSteps : 16;
        const bool hex = !len && ValueFormat::hex();
        if (k.getKeyCode() == juce::KeyPress::returnKey) { if (len) openLengthEntry(headCh); else openGrooveEntry(headCh); return true; }
        if (const int d = shiftDelta(k); d != 0) { headEntry.restart(); setHeadValue(cur + d); return true; }
        if (isPlus(k) || isMinus(k)) { headEntry.restart(); setHeadValue(cur + (isPlus(k) ? 1 : -1)); return true; }
        if (isBackspace(k)) { int m = 0; setHeadValue(popDigit(headEntry, hex, m) ? m : lo); return true; }
        if (isDelete(k)) { headEntry.restart(); setHeadValue(lo); return true; }
        int mag = 0;
        if (typeDigit(headEntry, k.getTextCharacter(), hex, hi, mag)) { if (mag >= lo) setHeadValue(mag); return true; }
        return false;
    }

    /// The head's TSP: the row's transpose on this channel, a signed number
    /// in the display's base (sections 48 and 52).
    void openTspEntry(int ch)
    {
        const auto r = tspRects[size_t(ch)];
        if (r.getWidth() < 16) return;
        box.begin(owner, r, ValueFormat::transpose(tsp[size_t(ch)]), juce::Justification::centred,
                  [this, ch](const juce::String& text) {
                      int v = 0;
                      if (text.trim().isNotEmpty() && !detail::parseTransposeTyped(text, -128, 127, v)) { owner.repaint(); return; }
                      tsp[size_t(ch)] = v;
                      owner.repaint();
                      if (owner.onTransposeChange) owner.onTransposeChange(ch, v);
                  });
    }

    /// The head's STEPS: the length of the phrase this channel plays in its
    /// row, 1-64, typed (section 25).
    void openLengthEntry(int ch)
    {
        const auto r = stepsRects[size_t(ch)];
        if (r.getWidth() < 16) return;
        box.begin(owner, r, juce::String(length[size_t(ch)]), juce::Justification::centred,
                  [this, ch](const juce::String& text) {
                      const int v = text.trim().getIntValue();
                      if (text.trim().isEmpty() || v < 1 || v > tracker::kMaxSteps) { owner.repaint(); return; }
                      length[size_t(ch)] = v;
                      owner.repaint();
                      if (owner.onLengthChange) owner.onLengthChange(ch, v);
                  });
    }

    /// The groove chip types its slot; a right click lists them and a double
    /// click opens the Grooves tab on it (the one selector convention).
    void openGrooveEntry(int ch)
    {
        const auto r = grooveRects[size_t(ch)];
        if (r.getWidth() < 16) return;
        box.begin(owner, r, juce::String(groove[size_t(ch)]), juce::Justification::centred,
                  [this, ch](const juce::String& text) {
                      const int v = text.trim().getIntValue();
                      if (text.trim().isEmpty() || v < 0 || v > 16) { owner.repaint(); return; }
                      groove[size_t(ch)] = v;
                      owner.repaint();
                      if (owner.onGrooveChange) owner.onGrooveChange(ch, v);
                  });
    }

    void openSlot(SlotKind kind, int slot)
    {
        if (slot >= 0 && owner.onOpenSlot) owner.onOpenSlot(kind, slot);
    }

    void toggleArm(int ch)
    {
        armed[size_t(ch)] = !armed[size_t(ch)];
        owner.repaint();
        if (owner.onArmChange) owner.onArmChange(ch, armed[size_t(ch)]);
    }

    juce::String tooltip() const
    {
        if (hoverArm >= 0)
            return juce::String("Record arm for ") + colours::channelName(hoverArm) + ": its MIDI is written into its cells while Rec is on.";
        if (hoverSteps >= 0)
            return juce::String("STEPS: the steps this phrase holds, 1-64. Click and type it, or double-click for a box. TICKS beside it is what they come to.");
        if (hoverTsp >= 0) {
            const int t = tsp[size_t(hoverTsp)];
            return juce::String("TSP: ") + colours::channelName(hoverTsp) + (t == 0 ? " plays this row as written." : " plays this row " + ValueFormat::signedNumber(t) + " semitones, on instruments whose Transpose is on.")
                   + (ValueFormat::hex() ? " Click and type the byte, E0 for -32; Shift+arrows move it." : " Click and type a number, \"-\" flips its sign; Shift+arrows move it.");
        }
        if (hoverPhrase >= 0) {
            const int slot = phraseSlot[size_t(hoverPhrase)];
            return juce::String(colours::channelName(hoverPhrase)) + (slot ? " plays phrase " + ValueFormat::slot(slot) + " in row " + ValueFormat::index(chRow[size_t(hoverPhrase)]) : juce::String(" has no phrase in row ") + ValueFormat::index(chRow[size_t(hoverPhrase)]))
                   + ". The chain is where it is typed.";
        }
        if (hoverTicks >= 0)
            return "TICKS " + juce::String(tickCount[size_t(hoverTicks)]) + ": how long this row really lasts -- its steps under the groove in force, and the passes an H replays. The block's height in the chain.";
        if (hoverGroove >= 0) {
            const int g = groove[size_t(hoverGroove)];
            const bool named = g > 0 && song != nullptr && g <= int(song->grooves.size()) && song->grooves[size_t(g - 1)].named();
            return (named ? "Groove " + ValueFormat::slot(g) + " " + juce::String(juce::CharPointer_UTF8(song->grooves[size_t(g - 1)].nameOf())) + ": " : juce::String("Groove: "))
                   + "the ticks each step lasts. Click and type a slot, double-click to edit the groove in its tab, right-click to list them.";
        }
        const int row = core.hoverRow, col = core.hoverCol;
        if (row < 0 || row >= core.rows || col < 0 || col >= int(core.cols.size())) return {};
        const auto& c = core.cols[size_t(col)];
        if (c.kind != Kind::Step && row >= length[size_t(c.ch)]) {
            const int r = int(srcRow[size_t(c.ch)][size_t(row)]);
            const int slot = song != nullptr ? song->phraseAt(c.ch, r) : 0;
            return juce::String("A preview of ") + colours::channelName(c.ch) + "'s row " + ValueFormat::index(r) + (slot ? ", phrase " + ValueFormat::slot(slot) : juce::String(", no phrase"))
                   + ", as if this phrase ran on. Not editable: click to put the play head there.";
        }
        const auto& cell = cells[size_t(c.ch)][size_t(row)];
        if (c.kind == Kind::Ghost)
            return plays[size_t(c.ch)] == tracker::NoteSource::Hybrid
                       ? juce::String("The cell's note, dimmed: Hybrid takes its notes from MIDI, and the columns beside them still fire. A blank shows the note the host sent.")
                       : juce::String("The cell's note, dimmed: this channel plays MIDI. Set it to Trkr to play and type these notes. A blank shows the note the host sent.");
        if (c.kind == Kind::Note) return "The note this step plays. Shift+arrows move it (left/right a semitone, up/down an octave), a drag moves it, a double click types it; minus is a note off.";
        if (c.kind == Kind::Vel) {
            if (const bank::Kit* k = kitAt(c.ch, row)) {
                juce::String t = "Second sample: one of kit " + juce::String(k->name) + "'s " + juce::String(int(k->samples.size()))
                                 + " samples, by its three-character label, summed with this note through the kit's Dist. Blank plays one sample; a right click lists them.";
                const int v = int(cell.vel);
                if (v >= 1 && v <= int(k->samples.size())) t += " Now: " + juce::String(k->samples[size_t(v - 1)].name) + ".";
                return t;
            }
            return "A kit's second sample. Blank here: this row's instrument is not a kit.";
        }
        if (c.kind == Kind::Inst) return "Instrument at this step. Type it, double-click opens it in its tab, right-click lists the bank.";
        if (c.kind == Kind::Table) return "Table override at this step. Type it, double-click opens it in its tab, right-click lists the bank.";
        if (c.kind == Kind::Cmd) return cmdTooltip(cmdSlot(col) == 0 ? cell.cmd1 : cell.cmd2);
        return {};
    }

    /// The kit the instrument at this step plays, or nullptr: a kit's VEL
    /// column names the note's second sample (docs/plan-kit-pairs.md), so the
    /// column means something else there. The instrument is the nearest one at
    /// or above the row -- what the Player would have loaded inside this bar.
    const bank::Kit* kitAt(int ch, int row) const
    {
        if (!bank || ch < 0 || ch > 3) return nullptr;
        for (int r = juce::jlimit(0, kMax - 1, row); r >= 0; --r) {
            const int slot = int(cells[size_t(ch)][size_t(r)].inst);
            if (slot == 0) continue;
            const bank::Instrument* in = bank->instrument(slot);
            if (in == nullptr || in->type != bank::InstrumentType::Kit) return nullptr;
            return bank->kit(in->kit);
        }
        return nullptr;
    }
    /// How far the second-sample column counts on this row: the kit's sample list.
    int velTop(int ch, int row) const
    {
        const bank::Kit* k = kitAt(ch, row);
        return k != nullptr ? int(k->samples.size()) : 0;
    }
    /// D-UI-34: the label of a kit's sample `v` (1-based), the number past the list.
    static juce::String sampleLabel(const bank::Kit* k, int v)
    {
        if (k == nullptr || v < 1) return {};
        if (v > int(k->samples.size())) return ValueFormat::number(v);
        return juce::String(bank::kitSampleLabel(k->samples[size_t(v - 1)].name, v - 1));
    }
    /// The sample a kit note plays: the one mapped to the note, else the nearest
    /// (what Driver::startVoice picks); -1 with no samples.
    static int sampleOfNote(const bank::Kit* k, int note)
    {
        if (k == nullptr || k->samples.empty()) return -1;
        int best = -1, bestDist = 1000;
        for (int i = 0; i < int(k->samples.size()); ++i) { const int d = std::abs(int(k->samples[size_t(i)].note) - note); if (d < bestDist) { bestDist = d; best = i; } }
        return best;
    }
    /// The sample (1-based) whose label is `text`, ignoring case; 0 for none.
    static int sampleByLabel(const bank::Kit* k, const juce::String& text)
    {
        if (k == nullptr) return 0;
        const juce::String want = text.trim();
        if (want.isEmpty()) return 0;
        for (int i = 0; i < int(k->samples.size()); ++i)
            if (juce::String(bank::kitSampleLabel(k->samples[size_t(i)].name, i)).equalsIgnoreCase(want)) return i + 1;
        return 0;
    }

    void openGrooveMenu(int ch)
    {
        juce::PopupMenu m;
        m.addSectionHeader("Groove");
        const int cur = groove[size_t(ch)];
        if (cur > 0) {
            m.addItem(kMenuOpen, "Open groove " + ValueFormat::number(cur) + " in its tab");
            m.addSeparator();
        }
        m.addItem(1, "Straight (6/6)", true, cur == 0);
        if (song != nullptr)
            for (int g = 1; g <= int(song->grooves.size()); ++g) {
                const auto& gr = song->grooves[size_t(g - 1)];
                // "03 . hi-hat shuffle" with the ticks beside it (section 39).
                juce::PopupMenu::Item item(ValueFormat::slot(g) + (gr.named() ? juce::String(juce::CharPointer_UTF8("  \xc2\xb7  ")) + juce::String(juce::CharPointer_UTF8(gr.nameOf())) : juce::String()));
                item.itemID = 1 + g;
                item.shortcutKeyDescription = grooveTicks(g) + " ticks";
                item.isTicked = cur == g;
                m.addItem(item);
            }
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(owner).withTargetScreenArea(owner.localAreaToGlobal(grooveRects[size_t(ch)])),
                        [this, ch, cur, safe = juce::Component::SafePointer<juce::Component>(&owner)](int id) {
                            if (safe == nullptr || id == 0) return;
                            if (id == kMenuOpen) { openSlot(SlotKind::Groove, cur); return; }
                            groove[size_t(ch)] = id - 1;
                            owner.repaint();
                            if (owner.onGrooveChange) owner.onGrooveChange(ch, id - 1);
                        });
    }

    void paintHeaders(juce::Graphics& g, int width)
    {
        using namespace colours;
        const int h1 = kHead1;
        g.setColour(lineSoft);
        g.fillRect(0, core.headerH - 1, width, 1);
        for (int ch = 0; ch < 4; ++ch) {
            const int first = firstCol(ch);
            const int x = core.cols[size_t(first)].x;
            g.setColour(ch > 0 ? line : lineSoft);
            g.fillRect(ch > 0 ? x - 1 : x, 0, ch > 0 ? 2 : 1, core.headerH);
            // the arm: a red dot when this channel records (section 14)
            const auto ar = armRects[size_t(ch)];
            const bool on = armed[size_t(ch)];
            g.setColour(on ? accent : ledOff);
            g.fillEllipse(ar.toFloat().reduced(3.0f));
            g.setColour(on ? accentHi : (hoverArm == ch ? textMute : line));
            g.drawEllipse(ar.toFloat().reduced(2.5f), 1.0f);
            g.setFont(Fonts::pixel(10.0f));
            g.setColour(channel(ch));
            g.drawText(channelName(ch), juce::Rectangle<int>(ar.getRight() + 5, 0, 40, h1), juce::Justification::centredLeft, false);
            const bool focused = owner.hasKeyboardFocus(false);
            auto ring = [&g, focused](juce::Rectangle<int> r) {
                g.setColour(accentHi.withAlpha(focused ? 0.95f : 0.45f));
                g.drawRoundedRectangle(r.toFloat().reduced(0.5f), 3.0f, focused ? 2.0f : 1.0f);
            };
            const auto gr = grooveRects[size_t(ch)];
            if (gr.getWidth() >= 20) {
                draw::panel(g, gr, hoverGroove == ch ? raisedHi : raised, line, 3.0f);
                g.setFont(Fonts::mono(10.5f));
                g.setColour(hoverGroove == ch ? text : textMute);
                g.drawFittedText(grooveText(ch, gr.getWidth()), gr.reduced(4, 0), juce::Justification::centred, 1, 0.72f);
                if (headCh == ch && headField == 1) ring(gr);
            }
            // The chip row (D-UI-37): a caption at the left of each chip and
            // its number at the right, the four channels' chips in step.
            auto chip = [&g](juce::Rectangle<int> r, const char* cap, const juce::String& value, bool hover, bool readOnly) {
                if (r.getWidth() < 24) return;
                draw::panel(g, r, readOnly ? panel2 : hover ? raisedHi : raised, readOnly ? lineSoft : line, 3.0f);
                draw::caption(g, cap, r.withTrimmedLeft(3), juce::Justification::centredLeft, textDim, 7.0f);
                g.setFont(Fonts::mono(9.5f));
                g.setColour(readOnly ? textMute : hover ? text : textMute);
                g.drawText(value, r.withTrimmedRight(3), juce::Justification::centredRight, false);
            };
            const int slot = phraseSlot[size_t(ch)];
            chip(phraseRects[size_t(ch)], "Phr", slot ? ValueFormat::slot(slot) : juce::String("--"), hoverPhrase == ch, true);
            chip(tspRects[size_t(ch)], "Tsp", ValueFormat::transpose(tsp[size_t(ch)]), hoverTsp == ch, false);
            chip(stepsRects[size_t(ch)], "Steps", juce::String(length[size_t(ch)]), hoverSteps == ch, false);
            chip(ticksRects[size_t(ch)], "Ticks", juce::String(tickCount[size_t(ch)]), hoverTicks == ch, true);
            if (headCh == ch && headField == 0) ring(stepsRects[size_t(ch)]);
            if (headCh == ch && headField == 2) ring(tspRects[size_t(ch)]);
        }
        const int capY = kHead1 + kHead2, capH = core.headerH - capY;
        for (size_t i = 1; i < core.cols.size(); ++i)
            draw::caption(g, core.cols[i].title, juce::Rectangle<int>(core.cols[i].x + 6, capY, core.cols[i].w - 6, capH), juce::Justification::centredLeft, textDim, 10.0f);
        draw::caption(g, "Step", juce::Rectangle<int>(6, capY, 30, capH), juce::Justification::centredLeft, textDim, 10.0f);
    }
};

PhraseGrid::PhraseGrid() : impl_(std::make_unique<Impl>(*this))
{
    setWantsKeyboardFocus(true);
    setSize(980, preferredHeight());
}
PhraseGrid::~PhraseGrid() = default;

juce::String PhraseGrid::getTooltip() { return impl_->tooltip(); }

void PhraseGrid::setSong(std::shared_ptr<const tracker::Song> song, const int rows[4])
{
    auto& im = *impl_;
    for (int ch = 0; ch < 4; ++ch) {
        const int r = rows != nullptr ? juce::jmax(0, rows[ch]) : 0;
        if (r != im.chRow[size_t(ch)]) im.shadow[size_t(ch)].fill(0);
        im.chRow[size_t(ch)] = r;
    }
    im.song = std::move(song);
    im.refreshFromSong();
    im.buildColumns(getWidth());
    repaint();
}
void PhraseGrid::setBank(std::shared_ptr<const bank::Bank> bank)
{
    impl_->bank = std::move(bank);
}
void PhraseGrid::setVisibleRows(int rows)
{
    auto& im = *impl_;
    rows = juce::jlimit(1, kMaxRows, rows);
    if (rows == im.visibleRows) return;
    im.visibleRows = rows;
    im.refreshFromSong();
    im.buildColumns(getWidth());
    repaint();
}

int PhraseGrid::steps() const { return impl_->steps(); }
void PhraseGrid::setPlayingStep(int ch, int step)
{
    if (ch < 0 || ch > 3) return;
    auto& im = *impl_;
    if (im.playing[size_t(ch)] == step) return;
    im.playing[size_t(ch)] = step;
    if (step >= 0 && step < im.core.rows && im.rollNote[size_t(ch)] > 0) im.shadow[size_t(ch)][size_t(step)] = im.rollNote[size_t(ch)];
    repaint();
}
void PhraseGrid::setRollNote(int ch, int midiNote)
{
    if (ch < 0 || ch > 3) return;
    auto& im = *impl_;
    im.rollNote[size_t(ch)] = midiNote;
    const int step = im.playing[size_t(ch)];
    if (midiNote > 0 && step >= 0 && step < im.core.rows && im.shadow[size_t(ch)][size_t(step)] != midiNote) {
        im.shadow[size_t(ch)][size_t(step)] = midiNote;
        repaint(juce::Rectangle<int>(0, im.core.rowY(step), getWidth(), im.core.rowH));
    }
}
void PhraseGrid::resized() { impl_->buildColumns(getWidth()); }

void PhraseGrid::paint(juce::Graphics& g)
{
    auto& im = *impl_;
    auto& core = im.core;
    if (core.cols.empty()) im.buildColumns(getWidth());
    const bool focused = hasKeyboardFocus(false);
    im.paintHeaders(g, getWidth());
    core.paintBands(g, getWidth());
    // A MIDI or Hybrid channel's note column is washed: not what plays (42).
    for (const auto& c : core.cols)
        if (c.kind == Kind::Ghost) { g.setColour(juce::Colours::black.withAlpha(0.22f)); g.fillRect(c.x, core.headerH, c.w, core.rows * core.rowH); }
    for (int ch = 0; ch < 4; ++ch) {
        const int p = im.playing[size_t(ch)];
        if (p < 0 || p >= core.rows) continue;
        const int first = Impl::firstCol(ch), last = first + Impl::channelCols(ch) - 1;
        const int x = core.cols[size_t(first)].x, w = core.cols[size_t(last)].x + core.cols[size_t(last)].w - x;
        g.setColour(colours::playRow);
        g.fillRect(x, core.rowY(p), w, core.rowH);
    }
    core.paintRowLines(g, getWidth());
    for (int r = 0; r < core.rows; ++r) {
        bool anyPlaying = false;
        for (int ch = 0; ch < 4; ++ch) anyPlaying = anyPlaying || im.playing[size_t(ch)] == r;
        core.paintStep(g, r, anyPlaying);
        for (int c = 1; c < int(core.cols.size()); ++c) {
            if (core.cols[size_t(c)].kind == Kind::Cmd) {
                const auto& cell = im.cells[size_t(core.cols[size_t(c)].ch)][size_t(r)];
                core.paintCmdCell(g, r, c, im.cmdSlot(c) == 0 ? cell.cmd1 : cell.cmd2, focused);
                continue;
            }
            bool blank = false; juce::Colour colour;
            const auto text = im.cellText(r, c, blank, colour);
            core.paintCell(g, r, c, text, blank, colour, focused);
        }
    }
    // A step the row never plays -- one its H hops never reach (section
    // 102) -- is dimmed and stays a cell (D-UI-37); past the phrase's length
    // the rows that follow are previewed, dimmed alike, a line where each
    // new row begins (D-UI-40).
    for (int ch = 0; ch < 4; ++ch) {
        const int first = Impl::firstCol(ch), last = first + Impl::channelCols(ch) - 1;
        const int x = core.cols[size_t(first)].x, w = core.cols[size_t(last)].x + core.cols[size_t(last)].w - x;
        for (int r = 0; r < core.rows; ++r) {
            const bool own = r < im.length[size_t(ch)];
            if (own && im.reached[size_t(ch)][size_t(r)]) continue;
            g.setColour(juce::Colours::black.withAlpha(0.42f));
            g.fillRect(x, core.rowY(r), w, core.rowH);
            if (!own && im.boundary[size_t(ch)][size_t(r)]) { g.setColour(colours::line); g.fillRect(x, core.rowY(r), w, 1); }
        }
    }
}

void PhraseGrid::mouseMove(const juce::MouseEvent& e)
{
    auto& im = *impl_;
    auto& core = im.core;
    int r = -1, c = -1;
    if (!core.cellAt(e.getPosition(), r, c)) { r = -1; c = -1; }
    int hg = -1, ha = -1, hs = -1, ht = -1, hp = -1, hk = -1;
    for (int ch = 0; ch < 4; ++ch) {
        if (im.grooveRects[size_t(ch)].contains(e.getPosition())) hg = ch;
        if (im.armRects[size_t(ch)].contains(e.getPosition())) ha = ch;
        if (im.stepsRects[size_t(ch)].contains(e.getPosition())) hs = ch;
        if (im.tspRects[size_t(ch)].contains(e.getPosition())) ht = ch;
        if (im.phraseRects[size_t(ch)].contains(e.getPosition())) hp = ch;
        if (im.ticksRects[size_t(ch)].contains(e.getPosition())) hk = ch;
    }
    const bool letter = c >= 0 && core.onLetter(c, e.x);
    if (r != core.hoverRow || c != core.hoverCol || letter != core.hoverLetter || hg != im.hoverGroove || ha != im.hoverArm
        || hs != im.hoverSteps || ht != im.hoverTsp || hp != im.hoverPhrase || hk != im.hoverTicks) {
        core.hoverRow = r; core.hoverCol = c; core.hoverLetter = letter; im.hoverGroove = hg; im.hoverArm = ha;
        im.hoverSteps = hs; im.hoverTsp = ht; im.hoverPhrase = hp; im.hoverTicks = hk;
        repaint();
    }
}
void PhraseGrid::mouseExit(const juce::MouseEvent&)
{
    impl_->core.hoverRow = impl_->core.hoverCol = -1;
    impl_->core.hoverLetter = false;
    impl_->hoverGroove = impl_->hoverArm = impl_->hoverSteps = impl_->hoverTsp = impl_->hoverPhrase = impl_->hoverTicks = -1;
    repaint();
}
void PhraseGrid::mouseDown(const juce::MouseEvent& e)
{
    auto& im = *impl_;
    auto& core = im.core;
    grabKeyboardFocus();
    im.dragging = false;
    im.dragRow = im.dragCol = -1;
    // The head's fields: the arm toggles; STEPS, TSP and the groove chip
    // take the cursor, and the chip lists on a right click (section 35).
    for (int ch = 0; ch < 4; ++ch) {
        if (im.armRects[size_t(ch)].contains(e.getPosition())) { im.toggleArm(ch); return; }
        if (im.stepsRects[size_t(ch)].contains(e.getPosition())) { im.selectHead(ch, 0); return; }
        if (im.tspRects[size_t(ch)].contains(e.getPosition())) { im.selectHead(ch, 2); return; }
        if (im.grooveRects[size_t(ch)].contains(e.getPosition())) {
            im.selectHead(ch, 1);
            if (e.mods.isPopupMenu()) im.openGrooveMenu(ch);
            return;
        }
    }
    int r = 0, c = 0;
    if (!core.cellAt(e.getPosition(), r, c)) return;
    im.leaveHead();
    // A preview row (D-UI-40): not a cell to edit; the play head goes to the
    // row it previews on that channel.
    if (c > 0 && r >= im.length[size_t(core.cols[size_t(c)].ch)]) {
        const int ch = core.cols[size_t(c)].ch;
        if (onAdvance) onAdvance(ch, int(im.srcRow[size_t(ch)][size_t(r)]));
        return;
    }
    if (core.editable(c)) {
        // A click ends whatever value was being typed: what follows is a
        // new undo (UI_DESIGN section 2.1).
        if (onEntryEnd) onEntryEnd();
        core.setCursor(r, c);
        repaint();
    }
    // A click selects; the lists are the right click's and the boxes the
    // double click's (section 35).
    if (e.mods.isPopupMenu()) { im.openPalette(r, c); im.openSlotMenu(r, c); return; }
    // Every value takes a vertical drag (sections 30 and 38): a semitone
    // every six pixels on a note, one unit on the rest, from the letter's
    // right on a command.
    if (core.editable(c) && !core.onLetter(c, e.x)) {
        im.dragRow = r; im.dragCol = c;
        im.dragFrom = im.dragValue(r, c);
    }
}

void PhraseGrid::mouseDrag(const juce::MouseEvent& e)
{
    auto& im = *impl_;
    if (im.dragRow < 0 || im.dragCol < 0) return;
    const bool note = im.core.cols[size_t(im.dragCol)].kind == Kind::Note;
    if (note && (im.dragFrom == 0 || im.dragFrom == tracker::kNoteOff)) return;
    const int steps = -e.getDistanceFromDragStartY() / Impl::kDragPixels;
    if (steps == 0 && !im.dragging) return;
    im.dragging = true;
    const int unit = e.mods.isShiftDown() ? (note ? 12 : 16) : 1;
    im.setDragValue(im.dragRow, im.dragCol, im.dragFrom + steps * unit);
}

void PhraseGrid::mouseUp(const juce::MouseEvent&)
{
    auto& im = *impl_;
    // One drag is one undo (UI_DESIGN section 2.1).
    if (im.dragging && onEntryEnd) onEntryEnd();
    im.dragging = false;
    im.dragRow = im.dragCol = -1;
}

void PhraseGrid::mouseDoubleClick(const juce::MouseEvent& e)
{
    auto& im = *impl_;
    auto& core = im.core;
    // The double click is the box (section 35) -- LEN's, a note's, a
    // velocity's, a command's values, or its palette on the letter and where
    // the slot holds no command -- except on a slot field, where it opens
    // the item's own tab: a slot is typed at the selected cell already, and
    // the box would add nothing. An empty slot field opens the box.
    for (int ch = 0; ch < 4; ++ch) {
        if (im.grooveRects[size_t(ch)].contains(e.getPosition())) {
            if (im.groove[size_t(ch)] > 0) im.openSlot(SlotKind::Groove, im.groove[size_t(ch)]); else im.openGrooveEntry(ch);
            return;
        }
        if (im.stepsRects[size_t(ch)].contains(e.getPosition())) { im.openLengthEntry(ch); return; }
        if (im.tspRects[size_t(ch)].contains(e.getPosition())) { im.openTspEntry(ch); return; }
    }
    int r = 0, c = 0;
    if (!core.cellAt(e.getPosition(), r, c) || !core.editable(c)) return;
    if (r >= im.length[size_t(core.cols[size_t(c)].ch)]) return;   // a preview row (D-UI-40)
    // A blank cell fills itself first (section 38).
    if (im.fillBlank(r, c)) return;
    const auto kind = core.cols[size_t(c)].kind;
    if (kind == Kind::Note) { im.openNoteEntry(r, c); return; }
    if (kind == Kind::Inst || kind == Kind::Table) {
        const auto& cell = im.cells[size_t(core.cols[size_t(c)].ch)][size_t(r)];
        const int slot = kind == Kind::Inst ? int(cell.inst) : int(cell.table);
        if (slot > 0) { im.openSlot(kind == Kind::Inst ? SlotKind::Instrument : SlotKind::Table, slot); return; }
    }
    if (kind == Kind::Cmd) {
        const bank::Command* cmd = im.commandAt(r, c);
        if (core.onLetter(c, e.x) || cmd == nullptr || cmd->cmd == bank::Cmd::None) im.openPalette(r, c);
        else im.openValueEntry(r, c, core.argAt(*cmd, r, c, e.x));
        return;
    }
    im.openNumberEntry(r, c);
}

bool PhraseGrid::keyPressed(const juce::KeyPress& k)
{
    auto& im = *impl_;
    auto& core = im.core;
    if (k.getKeyCode() == juce::KeyPress::escapeKey) { core.entry.reset(); im.leaveHead(); return true; }
    // A selected head chip takes the keys first; the arrows fall through and
    // hand the cursor back to the cells (section 35).
    if (im.headField >= 0 && im.headKey(k)) return true;
    // Enter fills a blank cell (section 38), else opens the box the cell
    // has: the note's, a number's, a command's values (Shift: its palette).
    if (k.getKeyCode() == juce::KeyPress::returnKey) {
        if (!k.getModifiers().isShiftDown() && im.fillBlank(core.curRow, core.curCol)) return true;
        const auto kind = core.editable(core.curCol) ? core.cols[size_t(core.curCol)].kind : Kind::Step;
        if (kind == Kind::Note) im.openNoteEntry(core.curRow, core.curCol);
        else if (kind == Kind::Vel || kind == Kind::Inst || kind == Kind::Table) im.openNumberEntry(core.curRow, core.curCol);
        else if (k.getModifiers().isShiftDown() || !im.openValueEntry(core.curRow, core.curCol, core.entry.arg))
            im.openPalette(core.curRow, core.curCol);
        return true;
    }
    // Shift with the arrows moves the value: left and right by one (a
    // semitone), up and down by sixteen (an octave). Everything else
    // navigates (section 35).
    {
        const bool note = core.editable(core.curCol) && core.cols[size_t(core.curCol)].kind == Kind::Note;
        if (const int d = shiftDelta(k, note ? 12 : 16); d != 0 && im.nudge(core.curRow, core.curCol, d)) return true;
    }
    if (core.navigate(k)) {
        im.leaveHead();
        // A preview row is never the cursor's (D-UI-40).
        { const int ch = core.cols[size_t(core.curCol)].ch; core.curRow = juce::jlimit(0, juce::jmax(0, im.length[size_t(ch)] - 1), core.curRow); }
        repaint();
        if (onEntryEnd) onEntryEnd();
        // Past sixteen steps the grid is taller than its pane, so the tab
        // scrolls to wherever the cursor went (section 25).
        if (onCursorRow) onCursorRow(core.curRow);
        return true;
    }
    return impl_->edit(k);
}
void PhraseGrid::focusGained(FocusChangeType) { repaint(); }
void PhraseGrid::focusLost(FocusChangeType) { impl_->core.entry.reset(); impl_->headEntry.reset(); repaint(); }

// ===========================================================================
// ChainColumn -- the chain drawn in time (UI_DESIGN D-UI-35,
// docs/plan-chain-timeline.md, docs/COMMANDS_AND_TEMPO.md sections 222, 223)
// ===========================================================================

/// The time signature editor a double click in the gutter opens, in a
/// CallOutBox: beats per bar, the beat unit, ticks per beat unit, and
/// Delete for every signature but the one at tick 0 (section 222).
class SignatureEditor : public juce::Component {
public:
    SignatureEditor(tracker::TimeSignature sig, bool isNew, std::function<void(tracker::TimeSignature)> apply, std::function<void()> remove)
        : sig_(sig), apply_(std::move(apply)), remove_(std::move(remove)),
          ok_("OK"), cancel_("Cancel"), remove_btn_("Delete")
    {
        auto label = [](juce::Label& l, const juce::String& text, const juce::Font& f, juce::Colour c) {
            l.setText(text, juce::dontSendNotification); l.setFont(f); l.setColour(juce::Label::textColourId, c); l.setJustificationType(juce::Justification::centredLeft); l.setBorderSize({ 0, 0, 0, 0 });
        };
        label(title_, isNew ? "NEW TIME SIGNATURE" : "TIME SIGNATURE", Fonts::caption(10.0f, true), colours::textDim);
        label(beatsLabel_, "Beats per bar", Fonts::sans(12.0f), colours::textMute);
        label(unitLabel_, "Beat unit", Fonts::sans(12.0f), colours::textMute);
        label(ticksLabel_, "Ticks per beat unit", Fonts::sans(12.0f), colours::textMute);
        label(at_, {}, Fonts::mono(11.0f), colours::textDim);
        beats_.setRange(1, tracker::kMaxSignatureBeats, 4); beats_.setTyped(true); beats_.setValue(int(sig.beats), juce::dontSendNotification);
        unit_.setRange(1, tracker::kMaxSignatureUnit, 4); unit_.setTyped(true); unit_.setValue(int(sig.unit), juce::dontSendNotification);
        ticks_.setRange(1, tracker::kMaxTicksPerBeat, 24); ticks_.setTyped(true); ticks_.setValue(int(sig.ticksPerBeat), juce::dontSendNotification);
        beats_.setTooltip("How many beats a bar holds, 1-64.");
        unit_.setTooltip("What the beat is called: 4 a quarter, 8 an eighth. A name for the gutter; the ticks are what count.");
        ticks_.setTooltip("How many ticks one beat unit lasts, 1-192: 24 is a quarter at LSDj's grid, 12 an eighth, 6 a straight step.");
        auto bars = [this] { at_.setText("from tick " + juce::String(sig_.tick) + " " + juce::String(juce::CharPointer_UTF8("\xc2\xb7")) + " a bar is " + juce::String(beats_.value() * ticks_.value()) + " ticks", juce::dontSendNotification); };
        beats_.onChange = [bars](int) { bars(); }; ticks_.onChange = [bars](int) { bars(); }; unit_.onChange = [](int) {};
        bars();
        ok_.onClick = [this] {
            tracker::TimeSignature t = sig_;
            t.beats = uint8_t(beats_.value()); t.unit = uint8_t(unit_.value()); t.ticksPerBeat = uint8_t(ticks_.value());
            if (apply_) apply_(t);
            close();
        };
        cancel_.onClick = [this] { close(); };
        remove_btn_.onClick = [this] { if (remove_) remove_(); close(); };
        remove_btn_.setEnabled(!isNew && sig.tick != 0);
        remove_btn_.setTooltip(sig.tick == 0 ? "The signature at tick 0 can be changed but not deleted." : "Remove this signature; the one before it counts on.");
        for (auto* c : std::initializer_list<juce::Component*>{ &title_, &beatsLabel_, &beats_, &unitLabel_, &unit_, &ticksLabel_, &ticks_, &at_, &ok_, &cancel_, &remove_btn_ }) addAndMakeVisible(c);
        setSize(272, 176);
    }
    void resized() override
    {
        auto a = getLocalBounds().reduced(12, 10);
        title_.setBounds(a.removeFromTop(16));
        a.removeFromTop(6);
        auto row = [&a](juce::Label& l, Stepper& s) {
            auto r = a.removeFromTop(Stepper::kHeight);
            s.setBounds(r.removeFromRight(s.preferredWidth()));
            l.setBounds(r.withTrimmedRight(8));
            a.removeFromTop(6);
        };
        row(beatsLabel_, beats_); row(unitLabel_, unit_); row(ticksLabel_, ticks_);
        at_.setBounds(a.removeFromTop(16));
        a.removeFromTop(6);
        auto b = a.removeFromTop(24);
        remove_btn_.setBounds(b.removeFromLeft(64));
        ok_.setBounds(b.removeFromRight(56));
        b.removeFromRight(6);
        cancel_.setBounds(b.removeFromRight(64));
    }
    void paint(juce::Graphics& g) override { g.fillAll(colours::panel); }
private:
    void close() { if (auto* box = findParentComponentOfClass<juce::CallOutBox>()) box->dismiss(); }
    tracker::TimeSignature sig_;
    std::function<void(tracker::TimeSignature)> apply_;
    std::function<void()> remove_;
    juce::Label title_, beatsLabel_, unitLabel_, ticksLabel_, at_;
    Stepper beats_, unit_, ticks_;
    juce::TextButton ok_, cancel_, remove_btn_;
};

struct ChainColumn::Impl {
    ChainColumn& owner;
    std::shared_ptr<const tracker::Song> song;
    /// The play head, a tick (section 223); the transport's own tick while
    /// it plays, which is the same line while Follow is on.
    int64_t cursor = 0;
    bool playing = false;
    int64_t transport = 0;
    /// The zoom slider, 0..1, log-mapped to pixels per tick (D-UI-35); it
    /// opens where a grid line is 48 ticks, two quarters (D-UI-40).
    double zoomT = 0.40;
    /// The rows area's scroll, in pixels down the timeline.
    double scrollPx = 0.0;
    /// The cell cursor: channel * 2, + 1 on the transpose (section 48). The
    /// row is each channel's own at the play head, or its "+" block past the
    /// end while `endEdit` names it.
    int cursorCol = 0;
    int endEdit = -1;
    int hoverCh = -1, hoverRow = -1;   ///< hoverRow -2: the "+" block
    int hoverEnd = -1, hoverSig = -1;
    bool hoverGutter = false;
    Entry entry;
    TypedEntry box;
    enum class Drag { None, Head, Loop, Scroll } drag = Drag::None;
    int64_t dragAnchor = 0;
    double dragScroll = 0.0;
    bool dragged = false;
    /// The loop region shown, the panel's own state (section 223).
    bool loopOn = false;
    int64_t loopA = 0, loopB = -1;
    float wheelAcc = 0.0f;
    bool joinedTop = false;   ///< the transport card stands on this column (D-UI-39)

    /// 264 px across (UI_DESIGN section 7): a 4 px pad, a 54 px gutter --
    /// bar numbers at its left, beats at its right -- and four 50 px channel
    /// columns 2 px apart. The rows area starts under the 48 px head with an
    /// 18 px pad so the tick 0 signature's tag has room above its line.
    static constexpr int kPad = 4, kGutter = 54, kColW = 50, kGap = 2, kTopPad = 18, kPlusH = 20, kMinRow = 22;
    static constexpr double kTickEps = 1e-9;

    explicit Impl(ChainColumn& o) : owner(o) {}

    /* ---------------------------------------------------- the geometry */

    static int colX(int ch) { return kPad + kGutter + ch * (kColW + kGap); }
    static int chAt(int x) { const int c = (x - kPad - kGutter) / (kColW + kGap); return x >= kPad + kGutter && c >= 0 && c < 4 && (x - colX(c)) < kColW - 1 ? c : -1; }
    const tracker::TimeSignature& firstSig() const { static const tracker::TimeSignature d; return song != nullptr && !song->signatures.empty() ? song->signatures.front() : d; }
    int64_t songEnd() const { return song != nullptr ? juce::jmax<int64_t>(tracker::kEmptyRowTicks, tracker::songTicks(*song)) : tracker::kEmptyRowTicks; }
    double zoomMin() const { return double(kMinRow) / double(4 * firstSig().barTicks()); }
    double ppt() const { const double lo = zoomMin(), hi = double(kMinRow); return lo * std::pow(hi / lo, juce::jlimit(0.0, 1.0, zoomT)); }
    /// The divisions the grid can show: the first signature's beat unit and
    /// its divisors, a few multiples, then bars (section 222).
    std::vector<int> candidates() const
    {
        const auto& s = firstSig();
        const int tpb = juce::jmax(1, int(s.ticksPerBeat)), bar = s.barTicks();
        std::vector<int> c;
        for (int d = 1; d <= tpb; ++d) if (tpb % d == 0) c.push_back(d);
        for (int m : { 2, 3, 4, 8 }) if (tpb * m < bar) c.push_back(tpb * m);
        c.push_back(bar); c.push_back(bar * 2); c.push_back(bar * 4);
        std::sort(c.begin(), c.end());
        c.erase(std::unique(c.begin(), c.end()), c.end());
        return c;
    }
    int division() const
    {
        const auto c = candidates();
        const double k = ppt();
        for (int d : c) if (double(d) * k >= double(kMinRow) - kTickEps) return d;
        return c.back();
    }
    juce::String divisionName(int d) const
    {
        const auto& s = firstSig();
        const int tpb = juce::jmax(1, int(s.ticksPerBeat)), bar = s.barTicks();
        if (d == 1) return "1 tick";
        if (d % bar == 0) return juce::String(d / bar) + (d > bar ? " bars" : " bar");
        if (d % tpb == 0) return juce::String(d / tpb) + " x 1/" + juce::String(int(s.unit));
        if (tpb % d == 0) return "1/" + juce::String(int(s.unit) * (tpb / d));
        return juce::String(d) + " ticks";
    }
    int64_t snap(int64_t t) const { const int d = division(); return juce::jmax<int64_t>(0, (t / d) * d); }
    double totalH() const { return kTopPad + double(songEnd()) * ppt() + 2 * kPlusH + 8; }
    int rowsH() const { return juce::jmax(1, owner.getHeight() - kHeaderHeight); }
    double yOfTick(double t) const { return kHeaderHeight + kTopPad + t * ppt() - scrollPx; }
    double tickOfY(double y) const { return (y - kHeaderHeight - kTopPad + scrollPx) / ppt(); }
    int64_t tickAt(juce::Point<int> p) const { return juce::jlimit<int64_t>(0, songEnd(), int64_t(std::floor(tickOfY(double(p.y)) + kTickEps))); }
    void clampScroll() { scrollPx = juce::jlimit(0.0, juce::jmax(0.0, totalH() - double(rowsH())), scrollPx); }
    void keepInView(int64_t t)
    {
        const double y = yOfTick(double(t));
        const int top = kHeaderHeight + 24, bottom = owner.getHeight() - 30;
        if (y < top || y > bottom) scrollPx += y - (kHeaderHeight + double(rowsH()) * 0.4);
        clampScroll();
    }

    /* --------------------------------------------------- rows and cells */

    int rows(int ch) const { return song != nullptr ? song->rows(ch) : 0; }
    int64_t endTick(int ch) const { return song != nullptr ? tracker::rowStartTick(*song, ch, rows(ch)) : 0; }
    int64_t loopTicks(int ch) const { return song != nullptr ? tracker::chainLoopTicks(*song, ch) : 0; }
    /// The row a channel is in at a tick, wrapped for a looping chain (212).
    int rowAt(int ch, int64_t t, int* pass = nullptr) const
    {
        if (song == nullptr) return 0;
        int row = 0, in = 0;
        tracker::rowAtTick(*song, ch, t, row, in, pass);
        return row;
    }
    /// The row the cell cursor edits on a channel: its own at the play head,
    /// or the row past its end while the "+" block is selected.
    int editRow(int ch) const { return endEdit == ch ? rows(ch) : rowAt(ch, cursor); }
    int slotAt(int ch, int row) const { return song != nullptr ? song->phraseAt(ch, row) : 0; }
    int tspAt(int ch, int row) const { return song != nullptr ? int(song->rowTranspose(ch, row)) : 0; }
    bool loopsAt(int ch) const { return song == nullptr || song->chainEnd[size_t(ch & 3)] == tracker::ChainEnd::Loop; }
    juce::Rectangle<int> endRect(int ch) const { return { owner.getWidth() - 70 - (4 - ch) * 20, 4, 18, 18 }; }
    int endAt(juce::Point<int> p) const { for (int ch = 0; ch < 4; ++ch) if (endRect(ch).contains(p)) return ch; return -1; }
    /// A block's rectangle: the row's span in ticks, 1 px short at the foot.
    juce::Rectangle<int> blockRect(int ch, int row, int64_t offset = 0) const
    {
        const int64_t a = tracker::rowStartTick(*song, ch, row) + offset, b = tracker::rowStartTick(*song, ch, row + 1) + offset;
        const int y0 = juce::roundToInt(yOfTick(double(a))), y1 = juce::roundToInt(yOfTick(double(juce::jmin(b, songEnd()))));
        return { colX(ch), y0, kColW - 2, juce::jmax(1, y1 - y0 - 1) };
    }
    juce::Rectangle<int> plusRect(int ch) const
    {
        const int y = juce::roundToInt(yOfTick(double(endTick(ch)))) + 2;
        return { colX(ch), y, kColW - 2, kPlusH - 3 };
    }
    /// What is under a point in the rows area: a channel and its row, -2 for
    /// the "+" block, -1 for nothing.
    bool blockAt(juce::Point<int> p, int& ch, int& row) const
    {
        ch = chAt(p.x); row = -1;
        if (ch < 0 || p.y < kHeaderHeight || song == nullptr) return false;
        if (plusRect(ch).contains(p)) { row = -2; return true; }
        const int64_t t = tickAt(p);
        const int64_t loop = loopTicks(ch);
        if (loop <= 0 && t >= endTick(ch)) return false;
        if (rows(ch) == 0) return false;
        row = rowAt(ch, t);
        return row < rows(ch);
    }

    /// The value a cell shows and the range a nudge wraps in (D-UI-15).
    int valueAt(int col, int row) const { return (col & 1) ? tspAt(col >> 1, row) : slotAt(col >> 1, row); }
    static void rangeOf(int col, int& lo, int& hi) { if (col & 1) { lo = -128; hi = 127; } else { lo = 0; hi = tracker::kPhraseSlots; } }
    void setValue(int col, int row, int value)
    {
        if (col < 0 || col > 7 || row < 0) return;
        int lo = 0, hi = 0;
        rangeOf(col, lo, hi);
        const int v = juce::jlimit(lo, hi, value);
        if (v == valueAt(col, row)) return;
        const int ch = col >> 1;
        if (col & 1) { if (owner.onChainTransposeChange) owner.onChainTransposeChange(ch, row, v); }
        else if (owner.onChainChange) owner.onChainChange(ch, row, v);
        owner.repaint();
    }
    void nudge(int col, int row, int delta)
    {
        int lo = 0, hi = 0;
        rangeOf(col, lo, hi);
        setValue(col, row, wrapRange(valueAt(col, row) + delta, lo, hi));
    }

    /* ----------------------------------------------------- the play head */

    void selectTick(int64_t t, bool notify = true)
    {
        t = juce::jlimit<int64_t>(0, songEnd(), t);
        const bool changed = t != cursor || endEdit >= 0;
        cursor = t;
        endEdit = -1;
        if (changed) { entry.reset(); if (owner.onEntryEnd) owner.onEntryEnd(); }
        keepInView(cursor);
        owner.repaint();
        if (changed && notify && owner.onSelectTick) owner.onSelectTick(cursor);
    }
    /// Left and right: the previous or next step of any phrase under the
    /// play head, across the four channels (D-UI-35).
    int64_t stepBoundary(bool forward) const
    {
        if (song == nullptr) return cursor;
        int64_t best = forward ? songEnd() : 0;
        bool found = false;
        for (int ch = 0; ch < 4; ++ch) {
            int pass = 0;
            const int row = rowAt(ch, cursor, &pass);
            const int64_t off = int64_t(pass) * loopTicks(ch);
            for (int r = juce::jmax(0, row - 1); r <= row + 1; ++r) {
                const int64_t start = tracker::rowStartTick(*song, ch, r) + off;
                int st[tracker::kMaxPlaySteps + 1];
                tracker::GrooveWalk w = song->walkAt(ch, r);
                const int n = tracker::stepStartTicks(*song, song->phrase(song->phraseAt(ch, r)), w, st);
                for (int i = 0; i <= n; ++i) {
                    const int64_t t = start + st[i];
                    if (forward ? (t > cursor && t < best) : (t < cursor && t > best)) { best = t; found = true; }
                    else if (forward ? (t > cursor && !found) : (t < cursor && !found)) { best = t; found = true; }
                }
            }
        }
        return found ? best : cursor;
    }

    /* ----------------------------------------------------- the entries */

    juce::Rectangle<int> cellRect(int col, int row) const
    {
        const int ch = col >> 1;
        const auto b = row >= rows(ch) ? plusRect(ch) : blockRect(ch, row);
        return b.withHeight(juce::jmin(b.getHeight(), kMinRow));
    }
    void openEntry(int col, int row)
    {
        if (col < 0 || col > 7 || row < 0) return;
        const bool tsp = (col & 1) != 0;
        const int v = valueAt(col, row);
        const juce::String now = v == 0 ? juce::String() : tsp ? ValueFormat::transpose(v) : ValueFormat::slot(v);
        box.begin(owner, cellRect(col, row), now, juce::Justification::centred,
                  [this, col, row, tsp](const juce::String& text) {
                      const juce::String t = text.trim();
                      int nv = 0;
                      if (t.isNotEmpty()) {
                          if (tsp) { if (!detail::parseTransposeTyped(t, -128, 127, nv)) { owner.repaint(); return; } }
                          else if (!detail::parseSlotTyped(t, tracker::kPhraseSlots, nv)) { owner.repaint(); return; }
                      }
                      setValue(col, row, nv);
                  });
    }
    std::vector<SlotRow> phraseRows() const
    {
        std::vector<SlotRow> list;
        if (song == nullptr) return list;
        std::array<int, tracker::kPhraseSlots + 1> uses{};
        for (const auto& c : song->chain)
            for (uint8_t slot : c) if (slot >= 1) ++uses[size_t(slot)];
        for (int slot = 1; slot <= tracker::kPhraseSlots; ++slot) {
            if (song->phrase(slot) == nullptr) continue;
            SlotRow r;
            r.slot = slot;
            r.name = uses[size_t(slot)] > 0 ? "used in " + juce::String(uses[size_t(slot)]) + (uses[size_t(slot)] == 1 ? " row" : " rows") : juce::String("free");
            r.used = true;
            list.push_back(r);
        }
        return list;
    }
    void openPhraseMenu(int ch, int row)
    {
        const auto list = phraseRows();
        if (list.empty()) return;
        showSlotMenu(owner, cellRect(ch * 2, row), juce::String(colours::channelName(ch)) + " phrase, row " + ValueFormat::index(row),
                     list, slotAt(ch, row), [this, ch, row](int slot) { setValue(ch * 2, row, slot); });
    }
    /// Double-click in the gutter: the signature editor, on the tag's own
    /// signature or a new one at the snapped tick (section 222).
    void openSignatureEditor(int index, int64_t tick)
    {
        if (song == nullptr) return;
        const bool isNew = index < 0;
        tracker::TimeSignature sig = song->signatures[size_t(juce::jlimit(0, int(song->signatures.size()) - 1, isNew ? tracker::signatureAt(*song, tick) : index))];
        if (isNew) sig.tick = tick;
        // The box outlives a closed window: the column is reached through a
        // safe pointer, never assumed.
        auto list = song->signatures;
        juce::Component::SafePointer<ChainColumn> safe(&owner);
        auto apply = [safe, list, index, isNew](tracker::TimeSignature t) mutable {
            if (safe == nullptr) return;
            if (isNew) list.push_back(t); else if (index < int(list.size())) list[size_t(index)] = t;
            if (safe->onSignaturesChange) safe->onSignaturesChange(list);
        };
        auto remove = [safe, list, index, isNew]() mutable {
            if (safe == nullptr || isNew || index <= 0 || index >= int(list.size())) return;
            list.erase(list.begin() + index);
            if (safe->onSignaturesChange) safe->onSignaturesChange(list);
        };
        auto editor = std::make_unique<SignatureEditor>(sig, isNew, apply, remove);
        const int y = juce::jlimit(kHeaderHeight, owner.getHeight() - 4, juce::roundToInt(yOfTick(double(sig.tick))));
        juce::CallOutBox::launchAsynchronously(std::move(editor), owner.localAreaToGlobal(juce::Rectangle<int>(0, y - 10, kPad + kGutter, 20)), nullptr);
    }
    int signatureTagAt(juce::Point<int> p) const
    {
        if (song == nullptr || p.x >= kPad + kGutter) return -1;
        for (int i = 0; i < int(song->signatures.size()); ++i) {
            const double y = yOfTick(double(song->signatures[size_t(i)].tick));
            if (p.y >= y - 20 && p.y < y - 5) return i;
        }
        return -1;
    }

    /* --------------------------------------------------------- painting */

    void paintHead(juce::Graphics& g)
    {
        using namespace colours;
        const int w = owner.getWidth();
        g.setColour(lineSoft);
        g.fillRect(1, kHeaderHeight - 1, w - 2, 1);
        draw::caption(g, "Chain", { 6, 0, w - 12, 26 }, juce::Justification::centredLeft, textDim, 10.0f);
        g.setFont(Fonts::mono(9.0f));
        g.setColour(textDim);
        g.drawText(juce::String(songEnd()) + " t", juce::Rectangle<int>(w - 66, 0, 60, 26), juce::Justification::centredRight, false);
        // Section 212: a ring for a channel that plays its chain round again,
        // a square for one that stops at its end, in the channel's colour.
        for (int ch = 0; ch < 4; ++ch) {
            const auto r = endRect(ch);
            const bool loop = loopsAt(ch), hover = ch == hoverEnd;
            g.setColour(hover ? raised : panel2);
            g.fillRoundedRectangle(r.toFloat(), 3.0f);
            g.setColour(channel(ch).withAlpha(hover ? 1.0f : 0.85f));
            const auto c = r.toFloat().reduced(5.0f);
            if (loop) g.drawEllipse(c, 1.8f); else g.fillRect(c.reduced(0.5f));
        }
        draw::caption(g, "bar / beat", { kPad, 26, kGutter, kHeaderHeight - 26 }, juce::Justification::centredLeft, textDim, 7.0f);
        for (int ch = 0; ch < 4; ++ch) {
            g.setFont(Fonts::pixel(9.0f));
            g.setColour(channel(ch));
            g.drawText(channelName(ch), juce::Rectangle<int>(colX(ch), 26, kColW - 2, kHeaderHeight - 26), juce::Justification::centred, false);
        }
    }

    void paintRows(juce::Graphics& g)
    {
        using namespace colours;
        if (song == nullptr) return;
        const int w = owner.getWidth(), h = owner.getHeight();
        const double k = ppt();
        const int64_t end = songEnd();
        const int64_t tA = juce::jmax<int64_t>(0, int64_t(std::floor(tickOfY(kHeaderHeight))) - 1), tB = juce::jmin(end, int64_t(std::ceil(tickOfY(double(h)))) + 1);
        g.saveState();
        g.reduceClipRegion(1, kHeaderHeight, w - 2, h - kHeaderHeight - 1);
        // The loop region (section 223).
        if (loopOn && loopB > loopA) {
            const int y0 = juce::roundToInt(yOfTick(double(loopA))), y1 = juce::roundToInt(yOfTick(double(loopB)));
            g.setColour(accent.withAlpha(0.10f)); g.fillRect(1, y0, w - 2, y1 - y0);
            g.setColour(accent.withAlpha(0.7f)); g.fillRect(kPad, y0, kGutter - 8, 1); g.fillRect(kPad, y1, kGutter - 8, 1);
        }
        // The grid: the first signature's division, labelled by the one in force.
        const int d = division();
        std::vector<int64_t> barTicks;
        for (size_t i = 0; i < song->signatures.size(); ++i) {
            const auto& s = song->signatures[i];
            const int64_t next = i + 1 < song->signatures.size() ? song->signatures[i + 1].tick : end;
            const int64_t bar = s.barTicks();
            if (next <= tA || s.tick > tB) continue;
            // Every 2^n-th bar is numbered when bars come closer than 12 px.
            int every = 1;
            while (double(bar) * k * every < 12.0 && every < (1 << 16)) every *= 2;
            int n = 0;
            for (int64_t t = s.tick; t < next && t <= tB; t += bar, ++n) if (t >= tA - bar && n % every == 0) barTicks.push_back(t);
        }
        std::sort(barTicks.begin(), barTicks.end());
        auto isBar = [&barTicks](int64_t t) { return std::binary_search(barTicks.begin(), barTicks.end(), t); };
        const int labelR = kPad + kGutter - 6;
        for (int64_t t = (tA / d) * d; t <= tB; t += d) {
            const int y = juce::roundToInt(yOfTick(double(t)));
            const bool onBar = isBar(t);
            g.setColour(onBar ? line : lineSoft.withAlpha(0.6f));
            g.fillRect(kPad + kGutter - 4, y, w - kPad - kGutter + 3, 1);
            const auto q = tracker::barPositionAt(*song, t);
            if (onBar) {
                g.setFont(Fonts::mono(10.0f)); g.setColour(text);
                g.drawText(juce::String(q.bar), juce::Rectangle<int>(kPad + 2, y - 8, 24, 16), juce::Justification::centredLeft, false);
            } else {
                g.setFont(Fonts::mono(8.5f)); g.setColour(q.tick ? textDim.withAlpha(0.7f) : textDim);
                g.drawText(juce::String(q.beat) + (q.tick ? "+" + juce::String(q.tick) : juce::String()), juce::Rectangle<int>(kPad + 18, y - 7, labelR - kPad - 18, 14), juce::Justification::centredRight, false);
            }
        }
        // Bars of the signature in force that fall between grid lines.
        for (int64_t t : barTicks) {
            if (t < tA || t > tB || t % d == 0) continue;
            const int y = juce::roundToInt(yOfTick(double(t)));
            g.setColour(line); g.fillRect(kPad + kGutter - 4, y, w - kPad - kGutter + 3, 1);
            g.setFont(Fonts::mono(10.0f)); g.setColour(text);
            g.drawText(juce::String(tracker::barPositionAt(*song, t).bar), juce::Rectangle<int>(kPad + 2, y - 8, 24, 16), juce::Justification::centredLeft, false);
        }
        // Beats of the signature in force: marks in the gutter.
        for (size_t i = 0; i < song->signatures.size(); ++i) {
            const auto& s = song->signatures[i];
            const int64_t next = i + 1 < song->signatures.size() ? song->signatures[i + 1].tick : end;
            const int64_t beat = juce::jmax(1, int(s.ticksPerBeat));
            if (double(beat) * k < 5.0 || next <= tA || s.tick > tB) continue;
            int64_t t = s.tick + ((tA > s.tick ? (tA - s.tick) / beat : 0)) * beat;
            for (; t < next && t <= tB; t += beat) { if (t < tA) continue; g.setColour(line); g.fillRect(labelR - 6, juce::roundToInt(yOfTick(double(t))), 6, 1); }
        }
        // The blocks, per channel; a looping chain shorter than the song
        // repeats its rows dimmed (section 212).
        const bool focused = owner.hasKeyboardFocus(false);
        for (int ch = 0; ch < 4; ++ch) {
            const int64_t loop = loopTicks(ch);
            const int passes = loop > 0 && loop < end ? int(juce::jmin<int64_t>((end + loop - 1) / loop, 64)) : 1;
            const int cur = editRow(ch);
            int curPass = 0;
            rowAt(ch, cursor, &curPass);
            for (int pass = 0; pass < passes; ++pass) {
                const int64_t off = int64_t(pass) * loop;
                if (off >= end || off > tB) break;
                int row = 0, in = 0;
                tracker::rowAtTickLaid(*song, ch, juce::jmax<int64_t>(0, tA - off), row, in);
                for (; row < rows(ch); ++row) {
                    const int64_t start = tracker::rowStartTick(*song, ch, row) + off;
                    if (start >= end || start > tB) break;
                    paintBlock(g, ch, row, off, pass > 0, pass == curPass && row == cur && endEdit != ch, focused);
                }
            }
            // The "+" block past the chain's end, where a typed slot grows it.
            const auto pr = plusRect(ch);
            if (pr.getBottom() >= kHeaderHeight && pr.getY() <= h) {
                const bool sel = endEdit == ch, hov = hoverCh == ch && hoverRow == -2;
                g.setColour(hov ? raised : panel2); g.fillRoundedRectangle(pr.toFloat(), 3.0f);
                g.setColour(sel ? accentHi.withAlpha(focused ? 0.95f : 0.45f) : lineSoft);
                const float dashes[2] = { 3.0f, 2.0f };
                const auto fr = pr.toFloat().reduced(0.5f);
                if (sel) g.drawRoundedRectangle(fr, 3.0f, focused ? 2.0f : 1.0f);
                else { g.drawDashedLine({ fr.getTopLeft(), fr.getTopRight() }, dashes, 2); g.drawDashedLine({ fr.getTopRight(), fr.getBottomRight() }, dashes, 2);
                       g.drawDashedLine({ fr.getBottomRight(), fr.getBottomLeft() }, dashes, 2); g.drawDashedLine({ fr.getBottomLeft(), fr.getTopLeft() }, dashes, 2); }
                g.setFont(Fonts::mono(11.0f)); g.setColour(textDim);
                g.drawText("+", pr, juce::Justification::centred, false);
            }
        }
        // The signatures' tags, in the gutter above their bar line (222).
        for (size_t i = 0; i < song->signatures.size(); ++i) {
            const auto& s = song->signatures[i];
            if (s.tick < tA - 400 || s.tick > tB) continue;
            const int y = juce::roundToInt(yOfTick(double(s.tick)));
            const juce::String txt = juce::String(int(s.beats)) + "/" + juce::String(int(s.unit)) + juce::String(juce::CharPointer_UTF8("\xc2\xb7")) + juce::String(int(s.ticksPerBeat));
            g.setFont(Fonts::mono(8.5f));
            const int tw = juce::roundToInt(draw::textWidth(Fonts::mono(8.5f), txt)) + 8;
            const auto r = juce::Rectangle<int>(kPad + 1, y - 19, juce::jmin(tw, kGutter - 2), 13);
            g.setColour(hoverSig == int(i) ? warn.brighter(0.2f) : warn); g.fillRoundedRectangle(r.toFloat(), 2.0f);
            g.setColour(black); g.drawText(txt, r, juce::Justification::centred, false);
            g.setColour(warn.withAlpha(0.8f)); g.fillRect(kPad + kGutter - 4, y, w - kPad - kGutter + 3, 1);
        }
        // Section 214: the song's stop.
        if (song->stopTick >= 0 && song->stopTick >= tA && song->stopTick <= tB) {
            const int y = juce::roundToInt(yOfTick(double(song->stopTick)));
            g.setColour(warn); g.fillRect(kPad + kGutter - 4, y, w - kPad - kGutter + 3, 1);
            g.setFont(Fonts::mono(8.5f)); g.drawText("H F F", juce::Rectangle<int>(kPad + 2, y + 1, kGutter - 4, 12), juce::Justification::centredLeft, false);
        }
        // The play head: the transport's line while it plays, the cursor's
        // where they differ (Follow off), one line where they coincide.
        auto head = [&g, w](int y, juce::Colour c, int thick) {
            g.setColour(c); g.fillRect(1, y, w - 2, thick);
            juce::Path p; p.addTriangle(2.0f, float(y) - 4.0f, 8.0f, float(y) + float(thick) * 0.5f, 2.0f, float(y) + 4.0f + float(thick)); g.fillPath(p);
        };
        if (playing && transport != cursor) {
            head(juce::roundToInt(yOfTick(double(cursor))), accent.withAlpha(0.55f), 1);
            head(juce::roundToInt(yOfTick(double(transport))), accentHi, 2);
        } else head(juce::roundToInt(yOfTick(double(cursor))), accentHi, 2);
        g.restoreState();
    }

    void paintBlock(juce::Graphics& g, int ch, int row, int64_t off, bool dim, bool cur, bool focused)
    {
        using namespace colours;
        const auto r = blockRect(ch, row, off);
        if (r.getBottom() < kHeaderHeight || r.getY() > owner.getHeight()) return;
        const double k = ppt();
        const juce::Colour col = channel(ch);
        const bool hov = hoverCh == ch && hoverRow == row && off == 0;
        const float a = dim ? 0.4f : 1.0f;
        g.setColour((hov ? raisedHi : raised).withMultipliedAlpha(a));
        g.fillRect(r);
        // The steps as they play (section 102): the channel's colour over a
        // stretch played for the first time, dimmer over one an H replays.
        if (r.getHeight() >= 6 && song != nullptr) {
            const tracker::Phrase* p = song->phrase(song->phraseAt(ch, row));
            if (p != nullptr) {
                int st[tracker::kMaxPlaySteps + 1]; uint8_t step[tracker::kMaxPlaySteps + 1];
                tracker::GrooveWalk w = song->walkAt(ch, row);
                const int n = tracker::stepStartTicks(*song, p, w, st, step);
                const int len = int(tracker::rowStartTick(*song, ch, row + 1) - tracker::rowStartTick(*song, ch, row));
                bool seen[tracker::kMaxSteps] = {};
                for (int i = 0; i < n; ++i) {
                    if (st[i] >= len) break;
                    const int te = juce::jmin(len, st[i + 1]);
                    const bool first = !seen[step[i]];
                    seen[step[i]] = true;
                    const int y0 = r.getY() + juce::roundToInt(double(st[i]) * k), y1 = r.getY() + juce::roundToInt(double(te) * k);
                    g.setColour(col.withAlpha((first ? 0.30f : 0.10f) * a));
                    g.fillRect(r.getX() + 2, y0, r.getWidth() - 4, juce::jmax(1, y1 - y0));
                }
            }
        }
        g.setColour(col.withAlpha(0.55f * a));
        g.drawRect(r, 1);
        g.setColour(col.withMultipliedAlpha(a));
        g.fillRect(r.getX(), r.getY(), 2, r.getHeight());
        if (r.getHeight() >= 11) {
            const int slot = slotAt(ch, row), tsp = tspAt(ch, row);
            const int labelH = juce::jmin(16, r.getHeight());
            g.setFont(Fonts::mono(10.5f));
            g.setColour((slot ? text : textDim).withMultipliedAlpha(a));
            g.drawText(slot ? ValueFormat::slot(slot) : juce::String::charToString(0x00b7), juce::Rectangle<int>(r.getX() + 5, r.getY(), 26, labelH), juce::Justification::centredLeft, false);
            if (tsp) {
                g.setFont(Fonts::mono(9.0f)); g.setColour(textMute.withMultipliedAlpha(a));
                g.drawText(ValueFormat::transpose(tsp), juce::Rectangle<int>(r.getRight() - 24, r.getY(), 21, labelH), juce::Justification::centredRight, false);
            }
            if (r.getHeight() >= 30) {
                g.setFont(Fonts::mono(8.5f)); g.setColour(textDim.withMultipliedAlpha(a));
                g.drawText(juce::String(tracker::rowStartTick(*song, ch, row + 1) - tracker::rowStartTick(*song, ch, row)) + "t", juce::Rectangle<int>(r.getX() + 3, r.getBottom() - 13, r.getWidth() - 6, 12), juce::Justification::centredRight, false);
            }
        }
        if (cur) {
            g.setColour(accentHi.withAlpha(focused ? 0.95f : 0.45f));
            g.drawRect(r.reduced(1), focused ? 2 : 1);
            // Which of the two values the cursor is on: a mark under it.
            const bool onTsp = (cursorCol & 1) != 0 && (cursorCol >> 1) == ch;
            const bool onSlot = (cursorCol & 1) == 0 && (cursorCol >> 1) == ch;
            if (onTsp) g.fillRect(r.getRight() - 24, r.getY() + juce::jmin(15, r.getHeight() - 3), 21, 2);
            else if (onSlot) g.fillRect(r.getX() + 5, r.getY() + juce::jmin(15, r.getHeight() - 3), 20, 2);
        }
    }
};

ChainColumn::ChainColumn() : impl_(std::make_unique<Impl>(*this))
{
    setWantsKeyboardFocus(true);
    setSize(kWidth, kHeaderHeight + 16 * 22);
}
ChainColumn::~ChainColumn() = default;

void ChainColumn::setSong(std::shared_ptr<const tracker::Song> song, int64_t cursorTick)
{
    auto& im = *impl_;
    im.song = std::move(song);
    im.cursor = juce::jlimit<int64_t>(0, im.songEnd(), cursorTick);
    if (im.endEdit >= 0 && im.cursor != im.endTick(im.endEdit)) im.endEdit = -1;
    im.clampScroll();
    repaint();
}
void ChainColumn::setTransport(bool playing, int64_t tick, bool keepInView)
{
    auto& im = *impl_;
    if (playing == im.playing && tick == im.transport) return;
    im.playing = playing;
    im.transport = tick;
    if (keepInView && playing) im.keepInView(tick);
    repaint();
}
void ChainColumn::setZoom(double t)
{
    auto& im = *impl_;
    // The play head keeps its place on screen through a zoom.
    const double before = im.yOfTick(double(im.cursor));
    im.zoomT = juce::jlimit(0.0, 1.0, t);
    im.scrollPx += im.yOfTick(double(im.cursor)) - before;
    im.clampScroll();
    repaint();
}
double ChainColumn::zoom() const { return impl_->zoomT; }
int64_t ChainColumn::cursorTick() const { return impl_->cursor; }
void ChainColumn::setLoopRegion(bool on, int64_t from, int64_t to)
{
    auto& im = *impl_;
    im.loopOn = on; im.loopA = from; im.loopB = to;
    repaint();
}
juce::String ChainColumn::gridText() const
{
    const int d = impl_->division();
    return "grid " + juce::String(d) + " t " + juce::String(juce::CharPointer_UTF8("\xc2\xb7")) + " " + impl_->divisionName(d);
}
void ChainColumn::resized() { impl_->clampScroll(); }

juce::String ChainColumn::getTooltip()
{
    auto& im = *impl_;
    if (im.hoverEnd >= 0) {
        const bool loop = im.loopsAt(im.hoverEnd);
        return juce::String(colours::channelName(im.hoverEnd)) + (loop ? " plays its chain round again when it runs out, from its own row 0 on its own clock, as LSDj's channels do. Click to make it stop at its end instead (section 212)."
                                                                       : " stops when its chain runs out: empty rows from there on. Click to make it play its chain round again instead (section 212).");
    }
    if (im.song == nullptr) return {};
    if (im.hoverSig >= 0 && im.hoverSig < int(im.song->signatures.size())) {
        const auto& s = im.song->signatures[size_t(im.hoverSig)];
        return juce::String(int(s.beats)) + "/" + juce::String(int(s.unit)) + ", the beat unit at " + juce::String(int(s.ticksPerBeat)) + " ticks -- a " + juce::String(s.barTicks()) + "-tick bar -- from tick " + juce::String(s.tick)
               + ". Double-click to edit it" + (s.tick == 0 ? "; the one at tick 0 cannot be deleted." : " or delete it.");
    }
    if (im.hoverGutter) {
        const auto p = juce::Desktop::getInstance().getMainMouseSource().getScreenPosition().toInt();
        const int64_t t = im.snap(im.tickAt(getLocalPoint(nullptr, p)));
        const auto q = tracker::barPositionAt(*im.song, t);
        return "Tick " + juce::String(t) + ": bar " + juce::String(q.bar) + ", beat " + juce::String(q.beat) + (q.tick ? " + " + juce::String(q.tick) : juce::String())
               + ". Click or drag to put the play head here (it snaps to the grid: " + gridText().fromFirstOccurrenceOf("grid ", false, false) + "); Shift-drag for a loop region; double-click for a time signature.";
    }
    if (im.hoverCh >= 0 && im.hoverRow == -2)
        return juce::String("Past ") + colours::channelName(im.hoverCh) + "'s last row: type a phrase slot here to add a row to its chain.";
    if (im.hoverCh >= 0 && im.hoverRow >= 0) {
        const int ch = im.hoverCh, row = im.hoverRow;
        const int slot = im.slotAt(ch, row), tsp = im.tspAt(ch, row);
        const int64_t start = tracker::rowStartTick(*im.song, ch, row), ticks = tracker::rowStartTick(*im.song, ch, row + 1) - start;
        const auto q = tracker::barPositionAt(*im.song, start);
        juce::String s = juce::String(colours::channelName(ch)) + " row " + ValueFormat::index(row) + (slot ? ": phrase " + ValueFormat::slot(slot) : juce::String(": no phrase, sixteen empty steps"))
                         + (tsp ? " " + ValueFormat::signedNumber(tsp) + " semitones" : juce::String()) + ". Starts at tick " + juce::String(start) + " (bar " + juce::String(q.bar) + " beat " + juce::String(q.beat) + "), lasts " + juce::String(ticks) + " ticks";
        if (const auto* p = im.song->phrase(slot)) {
            uint8_t order[tracker::kMaxPlaySteps];
            const int n = tracker::phrasePlayOrder(p, order, tracker::kMaxPlaySteps);
            bool seen[tracker::kMaxSteps] = {}; int reached = 0;
            for (int i = 0; i < n; ++i) if (!seen[order[i]]) { seen[order[i]] = true; ++reached; }
            s += ": " + juce::String(p->length()) + " steps, " + juce::String(reached) + " reached";
            if (n > reached) s += ", H replays " + juce::String(n - reached);
        }
        s += ". Click to put the play head at its start and type a slot, Backspace blanks it, double-click for a box, right-click lists the phrases, Tab reaches the transpose.";
        return s;
    }
    return "The chain, drawn in time: a block per row of each channel, as tall as the row lasts. Click a block for its start, the gutter for anywhere; the wheel scrolls. "
           "With the keyboard: up and down a tick, left and right a step, PgUp and PgDn a grid line, Space plays.";
}

void ChainColumn::setJoinedTop(bool on) { impl_->joinedTop = on; repaint(); }

void ChainColumn::paint(juce::Graphics& g)
{
    if (impl_->joinedTop) {
        // Square top corners under the transport card, and no top line of
        // its own: the card's bottom edge is the joint (D-UI-39).
        juce::Path shape;
        shape.addRoundedRectangle(0.5f, -4.5f, float(getWidth()) - 1.0f, float(getHeight()) + 4.0f, 4.0f, 4.0f, false, false, true, true);
        g.setColour(colours::panel2); g.fillPath(shape);
        g.setColour(colours::line); g.strokePath(shape, juce::PathStrokeType(1.0f));
        g.setColour(colours::lineSoft); g.fillRect(1, 0, getWidth() - 2, 1);
    } else draw::panel(g, getLocalBounds(), colours::panel2, colours::line, 4.0f);
    impl_->paintRows(g);
    g.setColour(colours::panel2);
    g.fillRect(1, 1, getWidth() - 2, kHeaderHeight - 2);
    impl_->paintHead(g);
}

void ChainColumn::mouseMove(const juce::MouseEvent& e)
{
    auto& im = *impl_;
    int ch = -1, row = -1;
    if (!im.blockAt(e.getPosition(), ch, row)) { ch = -1; row = -1; }
    const int end = im.endAt(e.getPosition());
    const bool gutter = e.y >= kHeaderHeight && e.x < Impl::kPad + Impl::kGutter;
    const int sig = gutter ? im.signatureTagAt(e.getPosition()) : -1;
    if (ch != im.hoverCh || row != im.hoverRow || end != im.hoverEnd || gutter != im.hoverGutter || sig != im.hoverSig) {
        im.hoverCh = ch; im.hoverRow = row; im.hoverEnd = end; im.hoverGutter = gutter; im.hoverSig = sig;
        repaint();
    }
}
void ChainColumn::mouseExit(const juce::MouseEvent&) { auto& im = *impl_; im.hoverCh = im.hoverRow = im.hoverEnd = im.hoverSig = -1; im.hoverGutter = false; repaint(); }
void ChainColumn::mouseDown(const juce::MouseEvent& e)
{
    auto& im = *impl_;
    grabKeyboardFocus();
    im.drag = Impl::Drag::None;
    im.dragged = false;
    // Section 212: the head's end toggles.
    if (const int end = im.endAt(e.getPosition()); end >= 0) {
        if (onChainEndChange) onChainEndChange(end, !im.loopsAt(end));
        return;
    }
    if (e.y < kHeaderHeight || im.song == nullptr) return;
    if (e.getNumberOfClicks() > 1) return;
    if (e.x < Impl::kPad + Impl::kGutter) {
        // The gutter is the play head's strip: click or drag places it on
        // the grid; Shift-drag is a loop region (section 223).
        const int64_t t = im.snap(im.tickAt(e.getPosition()));
        if (e.mods.isShiftDown()) { im.drag = Impl::Drag::Loop; im.dragAnchor = t; im.loopOn = true; im.loopA = im.loopB = t; repaint(); }
        else { im.drag = Impl::Drag::Head; im.selectTick(t); }
        return;
    }
    int ch = -1, row = -1;
    if (im.blockAt(e.getPosition(), ch, row)) {
        if (onEntryEnd) onEntryEnd();
        im.entry.reset();
        // The right 40 % of a block is its transpose (section 48).
        im.cursorCol = ch * 2 + ((e.x - Impl::colX(ch)) > (Impl::kColW * 3) / 5 ? 1 : 0);
        if (row == -2) { im.cursor = juce::jlimit<int64_t>(0, im.songEnd(), im.endTick(ch)); im.endEdit = ch; repaint(); if (onSelectTick) onSelectTick(im.cursor); }
        else {
            int pass = 0; im.rowAt(ch, im.tickAt(e.getPosition()), &pass);
            im.selectTick(tracker::rowStartTick(*im.song, ch, row) + int64_t(pass) * im.loopTicks(ch));
        }
        if (e.mods.isPopupMenu()) im.openPhraseMenu(ch, row == -2 ? im.rows(ch) : row);
        return;
    }
    im.drag = Impl::Drag::Scroll;
    im.dragScroll = im.scrollPx;
}
void ChainColumn::mouseDoubleClick(const juce::MouseEvent& e)
{
    auto& im = *impl_;
    if (e.y < kHeaderHeight || im.song == nullptr) return;
    if (e.x < Impl::kPad + Impl::kGutter) {
        const int tag = im.signatureTagAt(e.getPosition());
        if (tag >= 0) { im.openSignatureEditor(tag, im.song->signatures[size_t(tag)].tick); return; }
        const int64_t t = im.snap(im.tickAt(e.getPosition()));
        for (size_t i = 0; i < im.song->signatures.size(); ++i) if (im.song->signatures[i].tick == t) { im.openSignatureEditor(int(i), t); return; }
        im.openSignatureEditor(-1, t);
        return;
    }
    int ch = -1, row = -1;
    if (!im.blockAt(e.getPosition(), ch, row)) return;
    im.openEntry(im.cursorCol, row == -2 ? im.rows(ch) : row);
}
void ChainColumn::mouseDrag(const juce::MouseEvent& e)
{
    auto& im = *impl_;
    if (im.drag == Impl::Drag::Head) { im.selectTick(im.snap(im.tickAt(e.getPosition()))); return; }
    if (im.drag == Impl::Drag::Loop) {
        const int64_t t = im.snap(im.tickAt(e.getPosition()));
        im.loopA = juce::jmin(im.dragAnchor, t); im.loopB = juce::jmax(im.dragAnchor, t);
        repaint();
        return;
    }
    if (im.drag != Impl::Drag::Scroll) return;
    const int dy = e.getDistanceFromDragStartY();
    if (std::abs(dy) < 4 && !im.dragged) return;
    im.dragged = true;
    im.scrollPx = im.dragScroll - dy;
    im.clampScroll();
    repaint();
}
void ChainColumn::mouseUp(const juce::MouseEvent&)
{
    auto& im = *impl_;
    if (im.drag == Impl::Drag::Loop) {
        if (im.loopB - im.loopA < im.division()) { im.loopOn = false; im.loopA = 0; im.loopB = -1; if (onLoopRegion) onLoopRegion(0, -1); }
        else if (onLoopRegion) onLoopRegion(im.loopA, im.loopB);
        repaint();
    }
    im.drag = Impl::Drag::None;
}
void ChainColumn::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    auto& im = *impl_;
    im.wheelAcc += w.deltaY;
    const int steps = w.isSmooth ? int(im.wheelAcc / 0.1f) : (w.deltaY > 0.0f ? 1 : w.deltaY < 0.0f ? -1 : 0);
    if (w.isSmooth) im.wheelAcc -= float(steps) * 0.1f; else im.wheelAcc = 0.0f;
    if (steps == 0) return;
    im.scrollPx -= double(steps) * 44.0;
    im.clampScroll();
    repaint();
}
bool ChainColumn::keyPressed(const juce::KeyPress& k)
{
    auto& im = *impl_;
    const int code = k.getKeyCode();
    const int col = im.cursorCol, ch = col >> 1;
    const int row = im.editRow(ch);
    // Shift with the arrows moves the cell's value (section 35), wrapping at
    // the ends (D-UI-15); the plain arrows move the play head (D-UI-35).
    if (const int d = shiftDelta(k); d != 0) {
        im.entry.restart();
        im.nudge(col, row, d);
        return true;
    }
    if (code == juce::KeyPress::spaceKey) { if (onPlayPause) onPlayPause(); return true; }
    if (code == juce::KeyPress::returnKey) { im.openEntry(col, row); return true; }
    if (code == juce::KeyPress::upKey) { im.selectTick(im.cursor - 1); return true; }
    if (code == juce::KeyPress::downKey) { im.selectTick(im.cursor + 1); return true; }
    if (code == juce::KeyPress::leftKey) { im.selectTick(im.stepBoundary(false)); return true; }
    if (code == juce::KeyPress::rightKey) { im.selectTick(im.stepBoundary(true)); return true; }
    if (code == juce::KeyPress::pageUpKey) { im.selectTick(im.snap(im.cursor - 1)); return true; }
    if (code == juce::KeyPress::pageDownKey) { im.selectTick(im.snap(im.cursor) + im.division()); return true; }
    if (code == juce::KeyPress::homeKey) { im.selectTick(0); return true; }
    if (code == juce::KeyPress::endKey) { im.selectTick(im.songEnd()); return true; }
    if (code == juce::KeyPress::tabKey) {
        const int d = k.getModifiers().isShiftDown() ? -1 : 1;
        im.cursorCol = juce::jlimit(0, 7, im.cursorCol + d);
        im.entry.reset();
        if (onEntryEnd) onEntryEnd();
        repaint();
        return true;
    }
    if (code == juce::KeyPress::escapeKey) { im.entry.reset(); return true; }
    const int cur = im.valueAt(col, row);
    if (col & 1) {
        // The transpose types like a table's column (section 35): digits are
        // a magnitude, "-" with nothing typed flips the sign, +/- move it.
        bool has = cur != 0; int8_t t = int8_t(cur);
        if (!editTranspose(has, t, k, im.entry, -128, 127)) return false;
        im.setValue(col, row, has ? int(t) : 0);
        return true;
    }
    // A phrase slot counts from 00 in Hex, like every slot (section 52).
    const bool hex = ValueFormat::hex();
    const int hi = hex ? tracker::kPhraseSlots - 1 : tracker::kPhraseSlots;
    auto fromShown = [hex](int m) { return hex ? m + 1 : m; };
    if (isBackspace(k)) { int m = 0; im.setValue(col, row, popDigit(im.entry, hex, m) ? fromShown(m) : 0); return true; }
    if (isDelete(k)) { im.entry.reset(); im.setValue(col, row, 0); return true; }
    if (isPlus(k)) { im.entry.reset(); im.nudge(col, row, 1); return true; }
    if (isMinus(k)) { im.entry.reset(); im.nudge(col, row, -1); return true; }
    int mag = 0;
    if (typeDigit(im.entry, k.getTextCharacter(), hex, hi, mag)) { if (mag >= 0) im.setValue(col, row, fromShown(mag)); return true; }
    return false;
}
void ChainColumn::focusGained(FocusChangeType) { repaint(); }
void ChainColumn::focusLost(FocusChangeType) { impl_->entry.reset(); repaint(); }

// ===========================================================================
// WaveGrid
// ===========================================================================
struct WaveGrid::Impl {
    bank::Frame frame;
    View view = View::Bars;
    int lastI = -1, lastV = -1;
    int hoverI = -1, hoverV = -1;       ///< the cell under the pointer, -1 outside
    int sel = 0;                        ///< the sample last clicked, which the arrow keys move (D-UI-24)
    static constexpr int kPad = 6;

    static juce::Rectangle<int> inner(const juce::Component& c) { return c.getLocalBounds().reduced(kPad); }

    /// The grid cell a point lands on, clamped into the 32 by 16.
    /// Section 107: the grid is drawn the way the DAC puts the frame out, which
    /// is the way LSDj's WAVE screen draws it -- sample **0 at the top**, 15 at
    /// the bottom, because the DMG's DACs invert. So the row under the pointer
    /// is the level straight off the y, not 15 less it.
    static void cellAt(const juce::Component& c, juce::Point<int> p, int& i, int& v)
    {
        const auto in = inner(c);
        i = juce::jlimit(0, 31, int(std::floor(float(p.x - in.getX()) / (float(in.getWidth()) / 32.0f))));
        v = juce::jlimit(0, 15, int(std::floor(float(p.y - in.getY()) / (float(in.getHeight()) / 16.0f))));
    }

    /// Sets one sample; drags interpolate between the previous and the new column.
    bool paintAt(const juce::Component& c, juce::Point<int> p, bool continuing)
    {
        const auto in = inner(c);
        if (in.getWidth() < 32 || in.getHeight() < 16) return false;
        int i = 0, v = 0;
        cellAt(c, p, i, v);
        bool changed = false;
        if (continuing && lastI >= 0 && std::abs(i - lastI) > 1) {
            const int dir = i > lastI ? 1 : -1;
            for (int k = lastI + dir; k != i; k += dir) {
                const float t = float(k - lastI) / float(i - lastI);
                const auto nv = uint8_t(juce::roundToInt(float(lastV) + t * float(v - lastV)));
                if (frame.s[size_t(k)] != nv) { frame.s[size_t(k)] = nv; changed = true; }
            }
        }
        if (frame.s[size_t(i)] != uint8_t(v)) { frame.s[size_t(i)] = uint8_t(v); changed = true; }
        lastI = i; lastV = v; sel = i;
        return changed;
    }

    /// D-UI-24: the arrows. Left and right walk the samples, up and down move
    /// the one that is selected. Up is a *smaller* level, because the grid is
    /// drawn the way the DAC puts it out (section 107) and up is louder.
    /// Returns whether the frame changed; the caller repaints either way.
    bool arrow(int dx, int dy)
    {
        sel = juce::jlimit(0, 31, sel + dx);
        if (dy == 0) return false;
        const int was = frame.s[size_t(sel)];
        const int now = juce::jlimit(0, 15, was - dy);
        if (now == was) return false;
        frame.s[size_t(sel)] = uint8_t(now);
        lastI = sel; lastV = now;
        return true;
    }

    bool hover(const juce::Component& c, juce::Point<int> p)
    {
        int i = -1, v = -1;
        if (inner(c).contains(p)) cellAt(c, p, i, v);
        if (i == hoverI && v == hoverV) return false;
        hoverI = i; hoverV = v;
        return true;
    }
};

WaveGrid::WaveGrid() : impl_(std::make_unique<Impl>())
{
    setMouseCursor(juce::MouseCursor::CrosshairCursor);
    setWantsKeyboardFocus(true);   // D-UI-24: the arrows move the selected sample
    setSize(640, 200);
}
WaveGrid::~WaveGrid() = default;

void WaveGrid::setFrame(const bank::Frame& f) { impl_->frame = f; repaint(); }
const bank::Frame& WaveGrid::frame() const { return impl_->frame; }
void WaveGrid::setView(View v) { if (v != impl_->view) { impl_->view = v; repaint(); } }
WaveGrid::View WaveGrid::view() const { return impl_->view; }

void WaveGrid::paint(juce::Graphics& g)
{
    using namespace colours;
    auto& im = *impl_;
    g.setColour(lcd);
    g.fillRect(getLocalBounds());
    g.setColour(scopeBorder);
    g.drawRect(getLocalBounds(), 1);
    const auto in = Impl::inner(*this);
    if (in.getWidth() < 32 || in.getHeight() < 16) return;
    const float cellW = float(in.getWidth()) / 32.0f;
    const float levelH = float(in.getHeight()) / 16.0f;
    const bool points = im.view == View::Points;
    // The level lines; in the Points view the columns are ruled too, so the
    // dots sit on a grid (section 36).
    g.setColour(lcdGrid);
    for (int k = 1; k < 16; ++k) g.fillRect(float(in.getX()), float(in.getBottom()) - float(k) * levelH - 1.0f, float(in.getWidth()), 1.0f);
    if (points) for (int i = 1; i < 32; ++i) g.fillRect(float(in.getX()) + float(i) * cellW, float(in.getY()), 1.0f, float(in.getHeight()));
    // The pointer's column and row, lit softly.
    if (im.hoverI >= 0) {
        g.setColour(wav.withAlpha(0.10f));
        g.fillRect(float(in.getX()) + float(im.hoverI) * cellW, float(in.getY()), cellW, float(in.getHeight()));
        g.fillRect(float(in.getX()), float(in.getY()) + float(im.hoverV) * levelH, float(in.getWidth()), levelH);
    }
    if (points) {
        // Each sample fills its grid box, the way LSDj's wave screen draws
        // them: one lit cell per column.
        for (int i = 0; i < 32; ++i) {
            const float x = float(in.getX()) + float(i) * cellW;
            const float y = float(in.getY()) + float(im.frame.s[size_t(i)]) * levelH;
            g.setColour(i == im.hoverI ? wav : wav.withAlpha(0.9f));
            g.fillRect(x + 1.0f, y + 1.0f, cellW - 1.0f, levelH - 1.0f);
        }
    } else {
        const float colW = (float(in.getWidth()) - 31.0f) / 32.0f;
        for (int i = 0; i < 32; ++i) {
            const float x = float(in.getX()) + float(i) * (colW + 1.0f);
            // Section 107 puts level 0 at the top and 15 at the bottom, and the
            // bar still grows **up** from the floor: it fills from the sample's
            // own row down, so a quiet sample is a short bar and a loud one a
            // tall one, as a level meter reads.
            const float y = float(in.getY()) + float(im.frame.s[size_t(i)]) * levelH;
            g.setColour(i == im.hoverI ? wav : wav.withAlpha(0.9f));
            g.fillRect(x + 1.0f, y, colW - 2.0f, float(in.getBottom()) - y);
        }
    }
    // D-UI-23: the middle of each axis, so a shape can be drawn against a centre
    // -- between levels 7 and 8, which is the DAC's own zero, and between samples
    // 15 and 16. Over the trace, or the Bars view would bury the level line.
    g.setColour(lcd.withAlpha(0.55f));
    g.fillRect(float(in.getX()), std::round(float(in.getY()) + 8.0f * levelH) - 1.0f, float(in.getWidth()), 2.0f);
    g.fillRect(std::round(float(in.getX()) + 16.0f * cellW) - 1.0f, float(in.getY()), 2.0f, float(in.getHeight()));
    g.setColour(textDim.withAlpha(0.55f));
    g.fillRect(float(in.getX()), std::round(float(in.getY()) + 8.0f * levelH) - 0.5f, float(in.getWidth()), 1.0f);
    g.fillRect(std::round(float(in.getX()) + 16.0f * cellW) - 0.5f, float(in.getY()), 1.0f, float(in.getHeight()));

    // D-UI-24: the sample last clicked, which the arrows move. Its column is
    // tinted, its own cell ringed; brighter while the grid has the keyboard, so
    // it is clear the arrows will land here.
    {
        const bool focused = hasKeyboardFocus(false);
        const int sv = im.frame.s[size_t(im.sel)];
        const float x = float(in.getX()) + float(im.sel) * cellW;
        const float y = float(in.getY()) + float(sv) * levelH;
        // An outline, not a tint: in the Bars view the selected column is filled
        // solid, and a tint over it would not read.
        g.setColour(text.withAlpha(focused ? 0.85f : 0.40f));
        g.drawRect(juce::Rectangle<float>(x, float(in.getY()), cellW, float(in.getHeight())).reduced(0.5f), 1.0f);
        g.setColour(focused ? text : text.withAlpha(0.75f));
        g.drawRect(juce::Rectangle<float>(x, y, cellW, levelH).reduced(0.5f), 2.0f);
    }

    // The coordinates, the way LSDj's wave screen shows them: the sample
    // under the pointer and its level, in a corner, out of the way. The number
    // is the **stored** level, 0-15, whichever way up the grid draws it.
    if (im.hoverI >= 0 || hasKeyboardFocus(false)) {
        const int ri = im.hoverI >= 0 ? im.hoverI : im.sel;
        const int rv = im.hoverI >= 0 ? im.hoverV : int(im.frame.s[size_t(im.sel)]);
        const juce::String text = "sample " + ValueFormat::number(ri) + "  level " + ValueFormat::number(rv)
                                + "  (" + ValueFormat::number(im.frame.s[size_t(ri)]) + ")";
        g.setFont(Fonts::mono(10.0f));
        const int w = juce::roundToInt(draw::textWidth(Fonts::mono(10.0f), text)) + 10;
        // In the corner away from the pointer, so it never sits under the hand.
        const bool left = ri >= 16;
        const juce::Rectangle<int> r(left ? in.getX() + 2 : in.getRight() - 2 - w, in.getY() + 2, w, 16);
        g.setColour(lcd.withAlpha(0.85f));
        g.fillRoundedRectangle(r.toFloat(), 3.0f);
        g.setColour(lcdGrid);
        g.drawRoundedRectangle(r.toFloat(), 3.0f, 1.0f);
        g.setColour(wav);
        g.drawText(text, r, juce::Justification::centred, false);
    }
}
bool WaveGrid::keyPressed(const juce::KeyPress& k)
{
    // D-UI-24. Up and down move the selected sample's level -- up is a smaller
    // level, because section 107 draws level 0 at the top -- and left and right
    // walk the samples. Nothing else is taken, so Tab still leaves the grid.
    int dx = 0, dy = 0;
    if (k.isKeyCode(juce::KeyPress::leftKey))       dx = -1;
    else if (k.isKeyCode(juce::KeyPress::rightKey)) dx = 1;
    else if (k.isKeyCode(juce::KeyPress::upKey))    dy = 1;
    else if (k.isKeyCode(juce::KeyPress::downKey))  dy = -1;
    else return false;
    if (impl_->arrow(dx, dy) && onChange) onChange(impl_->frame);
    repaint();
    return true;
}
void WaveGrid::focusGained(FocusChangeType) { repaint(); }
void WaveGrid::focusLost(FocusChangeType) { repaint(); }
void WaveGrid::mouseMove(const juce::MouseEvent& e) { if (impl_->hover(*this, e.getPosition())) repaint(); }
void WaveGrid::mouseExit(const juce::MouseEvent&) { impl_->hoverI = impl_->hoverV = -1; repaint(); }
void WaveGrid::mouseDown(const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    impl_->lastI = -1;
    impl_->hover(*this, e.getPosition());
    if (impl_->paintAt(*this, e.getPosition(), false)) { if (onChange) onChange(impl_->frame); }
    repaint();
}
void WaveGrid::mouseDrag(const juce::MouseEvent& e)
{
    impl_->hover(*this, e.getPosition());
    if (impl_->paintAt(*this, e.getPosition(), true)) { if (onChange) onChange(impl_->frame); }
    repaint();
}
void WaveGrid::mouseUp(const juce::MouseEvent&) { impl_->lastI = -1; }

} // namespace chipboy::ui
