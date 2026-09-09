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
// types, a double click opens the box, and a right click lists the bank's
// slots by name with, at the top, the entry that opens that item's own tab.
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
bool editVol(int8_t& vol, const juce::KeyPress& k, Entry& e)
{
    const bool hex = ValueFormat::hex();
    if (isBackspace(k)) { int m = 0; vol = popDigit(e, hex, m) ? int8_t(m) : int8_t(-1); return true; }
    if (isDelete(k)) { e.restart(); vol = -1; return true; }
    if (isPlus(k) || isMinus(k)) { e.restart(); vol = int8_t(juce::jlimit(0, 15, (vol < 0 ? (isPlus(k) ? -1 : 16) : int(vol)) + (isPlus(k) ? 1 : -1))); return true; }
    int mag = 0;
    if (!typeDigit(e, k.getTextCharacter(), hex, 15, mag)) return false;
    if (mag >= 0) vol = int8_t(mag);
    return true;
}

bool editTranspose(bool& has, int8_t& t, const juce::KeyPress& k, Entry& e)
{
    const bool hex = ValueFormat::hex();
    if (isBackspace(k)) {
        int m = 0;
        if (popDigit(e, hex, m)) { has = true; t = int8_t(e.negative ? -m : m); }
        else { has = false; t = 0; }
        return true;
    }
    if (isDelete(k)) { e.restart(); has = false; t = 0; return true; }
    if (isMinus(k) && e.count == 0) { has = true; t = int8_t(-int(t)); e.negative = t < 0 || t == 0; return true; }
    if (isPlus(k) && e.count == 0) { has = true; t = int8_t(std::abs(int(t))); e.negative = false; return true; }
    if (isMinus(k) || isPlus(k)) { e.restart(); has = true; t = int8_t(juce::jlimit(-60, 60, int(t) + (isPlus(k) ? 1 : -1))); return true; }
    if (e.count == 0) e.negative = t < 0 || (has && t == 0 && e.negative);
    int mag = 0;
    if (!typeDigit(e, k.getTextCharacter(), hex, 60, mag)) return false;
    if (mag < 0) return true;
    has = true;
    t = int8_t(e.negative ? -mag : mag);
    return true;
}

bool editSlot(uint8_t& v, int hi, const juce::KeyPress& k, Entry& e)
{
    const bool hex = ValueFormat::hex();
    if (isBackspace(k)) { int m = 0; v = popDigit(e, hex, m) ? uint8_t(m) : uint8_t(0); return true; }
    if (isDelete(k)) { e.restart(); v = 0; return true; }
    if (isPlus(k) || isMinus(k)) { e.restart(); v = uint8_t(juce::jlimit(0, hi, int(v) + (isPlus(k) ? 1 : -1))); return true; }
    int mag = 0;
    if (!typeDigit(e, k.getTextCharacter(), hex, hi, mag)) return false;
    if (mag >= 0) v = uint8_t(mag);
    return true;
}

/// Shift with the arrows on a command cell (section 35): the argument the
/// cursor is on moves by `delta`, or the whole byte in Hex. False when the
/// slot holds no command.
bool nudgeCommand(bank::Command& c, int arg, int delta)
{
    const auto* info = commandInfo(c.cmd);
    if (info == nullptr) return false;
    if (ValueFormat::hex()) return plugin::setCommandByte(c, juce::jlimit(0, 255, plugin::commandByte(c) + delta));
    const int a = juce::jlimit(0, info->nargs - 1, arg);
    int lo = 0, hi = 0;
    plugin::commandShownRange(c.cmd, a, lo, hi);
    return plugin::setCommandShownValue(c, a, juce::jlimit(lo, hi, plugin::commandShownValue(c, a) + delta));
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
bool parseNoteText(const juce::String& text, uint8_t& out)
{
    juce::String t = text.trim().toLowerCase().removeCharacters(" -\xe2\x88\x92");
    if (text.trim().isEmpty()) { out = 0; return true; }
    if (t.isEmpty()) { out = tracker::kNoteOff; return true; }          // "-" and "---"
    if (t == "off" || t == "o") { out = tracker::kNoteOff; return true; }
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
        m.addItem(kMenuOpen, "Open " + heading.toLowerCase() + " " + ValueFormat::number(current)
                                 + (name.isNotEmpty() ? juce::String(juce::CharPointer_UTF8(" \xc2\xb7 ")) + name : juce::String()) + " in its tab");
        m.addSeparator();
    }
    m.addItem(1, juce::String(juce::CharPointer_UTF8("\xe2\x80\x93   none")), true, current == 0);
    bool separated = false;
    for (const auto& r : rows) {
        if (!separated && r.note.isNotEmpty()) { m.addSeparator(); separated = true; }
        juce::PopupMenu::Item item(ValueFormat::number(r.slot) + juce::String(juce::CharPointer_UTF8("  \xc2\xb7  ")) + (r.name.isEmpty() ? juce::String("(unnamed)") : r.name));
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
enum class Kind { Step, Vol, Transpose, Cmd, Note, Vel, Inst, Table, Ghost, Info };

struct Column { Kind kind = Kind::Step; int ch = 0; int x = 0, w = 0; juce::String title; };

bool editableKind(Kind k)
{
    return k == Kind::Vol || k == Kind::Transpose || k == Kind::Cmd || k == Kind::Note || k == Kind::Vel || k == Kind::Inst || k == Kind::Table;
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
    void paintRowLines(juce::Graphics& g, int width) const
    {
        g.setColour(colours::lineSoft);
        for (int r = 0; r < rows; ++r) g.fillRect(0, rowY(r) + rowH - 1, width, 1);
        for (size_t i = 1; i < cols.size(); ++i) g.fillRect(cols[i].x, headerH, 1, rows * rowH);
    }
    void paintStep(juce::Graphics& g, int row, bool playing) const
    {
        const auto r = cellRect(row, 0);
        g.setFont(Fonts::mono(12.0f));
        g.setColour(playing ? colours::accentHi : (row % 4 == 0 ? colours::textMute : colours::textDim));
        g.drawText(ValueFormat::number(row + 1), r.withTrimmedLeft(8), juce::Justification::centredLeft, false);
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
    int playing = -1;

    explicit Impl(TableGrid& o) : owner(o)
    {
        core.rows = bank::kTableSteps; core.rowH = kRowHeight; core.headerH = kHeaderHeight;
        core.curCol = 1;
    }

    void buildColumns(int width)
    {
        auto& cols = core.cols;
        cols.clear();
        const int widths[5] = { 38, 52, 72, 74, 74 };
        const Kind kinds[5] = { Kind::Step, Kind::Vol, Kind::Transpose, Kind::Cmd, Kind::Cmd };
        const char* titles[5] = { "Step", "Vol", "Trans", "Cmd 1", "Cmd 2" };
        int x = 0;
        for (int i = 0; i < 5; ++i) { cols.push_back({ kinds[i], i == 4 ? 1 : 0, x, widths[i], titles[i] }); x += widths[i]; }
        if (width - x >= 70) cols.push_back({ Kind::Info, 0, x, width - x, juce::String::charToString(0x2192) + " written as" });
    }

    juce::String cellText(int row, int col, bool& blank) const
    {
        const auto& s = table.steps[size_t(row)];
        const auto k = core.cols[size_t(col)].kind;
        blank = false;
        if (k == Kind::Vol) { blank = s.vol < 0; return blank ? kBlank2 : ValueFormat::number(s.vol); }
        if (k == Kind::Transpose) { blank = !s.hasTranspose; return blank ? kBlank2 : ValueFormat::signedNumber(s.transpose); }
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
        owner.repaint(juce::Rectangle<int>(0, core.rowY(row), owner.getWidth(), core.rowH));
        if (owner.onChange) owner.onChange(table);
    }

    bool edit(const juce::KeyPress& k)
    {
        auto& s = table.steps[size_t(core.curRow)];
        const auto& col = core.cols[size_t(core.curCol)];
        bool done = false;
        if (col.kind == Kind::Vol) done = editVol(s.vol, k, core.entry);
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
        if (kind == Kind::Transpose && s.hasTranspose) now = ValueFormat::signedNumber(s.transpose);
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
                      const bool ok = kind == Kind::Vol ? detail::parseTypedInt(text, 0, 15, v) : detail::parseTypedInt(text, -60, 60, v);
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
        if (c.kind == Kind::Vol) s.vol = int8_t(juce::jlimit(0, 15, (s.vol < 0 ? 0 : int(s.vol)) + delta));
        else if (c.kind == Kind::Transpose) { s.hasTranspose = true; s.transpose = int8_t(juce::jlimit(-60, 60, int(s.transpose) + delta)); }
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
void TableGrid::setPlayingStep(int step)
{
    if (step == impl_->playing) return;
    impl_->playing = step;
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
    if (im.playing >= 0 && im.playing < core.rows) { g.setColour(colours::playRow); g.fillRect(0, core.rowY(im.playing), getWidth(), core.rowH); }
    core.paintRowLines(g, getWidth());
    for (int r = 0; r < core.rows; ++r) {
        core.paintStep(g, r, r == im.playing);
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
    auto& core = impl_->core;
    grabKeyboardFocus();
    int r = 0, c = 0;
    if (!core.cellAt(e.getPosition(), r, c)) return;
    if (core.editable(c)) { core.setCursor(r, c); repaint(); }
    // A click selects; the palette is the right click's and the box the
    // double click's (section 35).
    if (e.mods.isPopupMenu()) impl_->openPalette(r, c);
}
void TableGrid::mouseDoubleClick(const juce::MouseEvent& e)
{
    auto& im = *impl_;
    auto& core = im.core;
    int r = 0, c = 0;
    if (!core.cellAt(e.getPosition(), r, c) || !core.editable(c)) return;
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
    // Enter opens the box: the values of a command (Shift: its palette), or
    // the number a volume or transpose cell holds.
    if (k.getKeyCode() == juce::KeyPress::returnKey) {
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
    static constexpr int kChannelCols = 6;   ///< note, vel, ins, tbl and the two commands
    static constexpr int kStepWidth = 34;
    /// The head, per channel: the record arm, the name, the playback switch,
    /// the phrase's LEN and its groove chip, all inside the 26 px name row.
    /// The "PLAYS" caption went with the rest of the window's spare words
    /// (docs/COMMANDS_AND_TEMPO.md section 30); the switch's tooltip says it.
    static constexpr int kArm = 14, kHead1 = 26, kLenWidth = 44;

    PhraseGrid& owner;
    std::shared_ptr<const tracker::Song> song;
    std::shared_ptr<const bank::Bank> bank;
    int bar = 0;
    /// A phrase holds sixty-four cells and the bar says how many of them play
    /// (docs/COMMANDS_AND_TEMPO.md section 11); the grid is that many rows.
    static constexpr int kMax = tracker::kMaxSteps;
    std::array<std::array<tracker::Cell, kMax>, 4> cells{};
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
    std::array<juce::Rectangle<int>, 4> grooveRects{}, armRects{}, lenRects{};
    std::array<int, 4> length{ { 16, 16, 16, 16 } };
    Segmented source[4];
    GridCore core;
    TypedEntry box;
    int octave = 4;
    int hoverGroove = -1, hoverArm = -1, hoverLen = -1;
    /// The head chip the cursor is on, if any (section 35): a click on LEN
    /// or the groove chip selects it, digits type into it, a double click
    /// opens its box, and the arrows hand the cursor back to the cells.
    int headCh = -1, headField = -1;   ///< headField: 0 LEN, 1 groove
    Entry headEntry;
    /// A vertical drag on a note: a semitone every six pixels, octaves with
    /// Shift (docs/COMMANDS_AND_TEMPO.md section 30).
    static constexpr int kDragPixels = 6;
    int dragRow = -1, dragCol = -1, dragFrom = 0;
    bool dragging = false;

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

    static const char* colTitle(int i) { static const char* t[kChannelCols] = { "note", "vel", "ins", "tbl", "cmd", "cmd" }; return t[i]; }

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
        const float weights[kChannelCols] = { 1.12f, 0.88f, 0.88f, 0.82f, 1.50f, 1.50f };
        float total = 0.0f;
        for (float w : weights) total += w;
        const float unit = juce::jmax(30.0f, float(width - kStepWidth) / (4.0f * total));
        float x = float(kStepWidth);
        for (int ch = 0; ch < 4; ++ch) {
            const float groupX = x;
            for (int i = 0; i < kChannelCols; ++i) {
                const Kind kinds[kChannelCols] = { trackerSource[size_t(ch)] ? Kind::Note : Kind::Ghost, Kind::Vel, Kind::Inst, Kind::Table, Kind::Cmd, Kind::Cmd };
                const float w = unit * weights[i];
                cols.push_back({ kinds[i], ch, juce::roundToInt(x), juce::roundToInt(x + w) - juce::roundToInt(x), colTitle(i) });
                x += w;
            }
            layoutHeader(ch, juce::roundToInt(groupX), juce::roundToInt(x) - juce::roundToInt(groupX));
        }
        core.ensureEditableCursor();
    }
    /// The head row, left to right: the arm dot, the channel's name, the
    /// playback switch, the phrase's LEN and the groove chip, which takes
    /// what is left (UI_DESIGN section 7).
    void layoutHeader(int ch, int x, int w)
    {
        int cx = x + 3;
        armRects[size_t(ch)] = { cx, (kHead1 - kArm) / 2, kArm, kArm };
        cx += kArm + 3;
        const int nameW = juce::roundToInt(draw::textWidth(Fonts::pixel(10.0f), colours::channelName(ch))) + 3;
        cx += nameW + 4;
        auto& seg = source[ch];
        seg.setBounds(cx, 3, seg.preferredWidth(), 20);
        int gx = seg.getRight() + 4;
        const int room = juce::jmax(0, x + w - gx - 2);
        // LEN first: a phrase's length is what its channel runs on, so it
        // keeps its width and the chip takes the rest (section 25).
        const int lenW = juce::jmin(kLenWidth, room);
        lenRects[size_t(ch)] = { gx, 3, lenW, 20 };
        gx += lenW + 3;
        grooveRects[size_t(ch)] = { gx, 3, juce::jmin(64, juce::jmax(0, x + w - gx - 2)), 20 };
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
        const juce::String full = ValueFormat::number(g) + juce::String::charToString(0x00b7) + ticks;
        const auto fits = [width](const juce::String& s) { return draw::textWidth(Fonts::mono(10.5f), s) <= float(width - 8); };
        if (fits(full)) return full;
        if (fits(ticks)) return ticks;
        return ValueFormat::number(g);
    }

    void refreshFromSong()
    {
        // Each channel's phrase has its own length now (section 25); the grid
        // shows as many steps as the longest of the four in this row, so every
        // cell it holds can be reached, and each head's LEN says how far its
        // own channel really runs.
        core.rows = PhraseGrid::kVisibleSteps;
        if (song != nullptr) { int n = 1; for (int ch = 0; ch < 4; ++ch) n = juce::jmax(n, song->stepsOfRow(ch, bar)); core.rows = n; }
        core.curRow = juce::jlimit(0, core.rows - 1, core.curRow);
        for (int ch = 0; ch < 4; ++ch) {
            const auto* p = song != nullptr ? song->phrase(song->phraseAt(ch, bar)) : nullptr;
            for (int i = 0; i < kMax; ++i) cells[size_t(ch)][size_t(i)] = p != nullptr ? p->cells[size_t(i)] : tracker::Cell{};
            groove[size_t(ch)] = p != nullptr ? p->groove : 0;
            length[size_t(ch)] = p != nullptr ? p->length() : (song != nullptr ? song->stepsOfRow(ch, bar) : 16);
            plays[size_t(ch)] = song != nullptr ? song->noteSource[size_t(ch)] : tracker::NoteSource::PianoRoll;
            trackerSource[size_t(ch)] = plays[size_t(ch)] == tracker::NoteSource::Tracker;
            armed[size_t(ch)] = song == nullptr || song->recordArm[size_t(ch)];
            source[ch].setSelected(plays[size_t(ch)] == tracker::NoteSource::Tracker ? 1
                                   : plays[size_t(ch)] == tracker::NoteSource::Hybrid ? 2 : 0, juce::dontSendNotification);
        }
    }

    juce::String cellText(int row, int col, bool& blank, juce::Colour& colour) const
    {
        const auto& c = core.cols[size_t(col)];
        const auto& cell = cells[size_t(c.ch)][size_t(row)];
        blank = false; colour = colours::text;
        if (c.kind == Kind::Ghost) {
            const int n = shadow[size_t(c.ch)][size_t(row)];
            blank = true;
            return n > 0 ? ValueFormat::noteName(n) : juce::String::charToString(0x00b7);
        }
        if (c.kind == Kind::Note) {
            blank = cell.note == 0;
            if (cell.note != 0 && cell.note != tracker::kNoteOff) colour = colours::channel(c.ch);
            return ValueFormat::noteName(cell.note);
        }
        if (c.kind == Kind::Vel) { blank = cell.vel == 0; return blank ? kBlank2 : ValueFormat::number(cell.vel); }
        if (c.kind == Kind::Inst) { blank = cell.inst == 0; return blank ? kBlank2 : ValueFormat::number(cell.inst); }
        if (c.kind == Kind::Table) { blank = cell.table == 0; return blank ? kBlank2 : ValueFormat::number(cell.table); }
        // A command cell is drawn in two parts by paintCmdCell, not here.
        return {};
    }

    void changed(int ch, int row)
    {
        owner.repaint(juce::Rectangle<int>(0, core.rowY(row), owner.getWidth(), core.rowH));
        if (owner.onCellChange) owner.onCellChange(ch, row, cells[size_t(ch)][size_t(row)]);
    }

    bool edit(const juce::KeyPress& k)
    {
        const auto& col = core.cols[size_t(core.curCol)];
        auto& cell = cells[size_t(col.ch)][size_t(core.curRow)];
        const tracker::Cell before = cell;
        bool done = false;
        if (col.kind == Kind::Note) done = editNote(cell.note, k, octave);
        else if (col.kind == Kind::Vel) done = editSlot(cell.vel, 127, k, core.entry);
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
        const auto& cell = cells[size_t(ch)][size_t(row)];
        const int cur = kind == Kind::Vel ? int(cell.vel) : kind == Kind::Inst ? int(cell.inst) : int(cell.table);
        const int hi = kind == Kind::Vel ? 127 : kind == Kind::Inst ? bank::kInstrumentSlots : bank::kTableSlots;
        core.setCursor(row, col);
        box.begin(owner, core.cellRect(row, col).reduced(1), cur == 0 ? juce::String() : ValueFormat::number(cur), juce::Justification::centredLeft,
                  [this, row, ch, kind, hi](const juce::String& text) {
                      int v = 0;
                      if (!text.trim().isEmpty() && !detail::parseTypedInt(text, 0, hi, v)) { owner.repaint(); return; }
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
        if (c.kind == Kind::Vel) cell.vel = uint8_t(juce::jlimit(0, 127, int(cell.vel) + delta));
        else if (c.kind == Kind::Inst) cell.inst = uint8_t(juce::jlimit(0, bank::kInstrumentSlots, int(cell.inst) + delta));
        else if (c.kind == Kind::Table) cell.table = uint8_t(juce::jlimit(0, bank::kTableSlots, int(cell.table) + delta));
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
        const juce::String now = cell.note == 0 ? juce::String() : ValueFormat::noteName(cell.note);
        core.setCursor(row, col);
        box.begin(owner, core.cellRect(row, col).reduced(1), now, juce::Justification::centredLeft,
                  [this, row, ch](const juce::String& text) {
                      uint8_t note = 0;
                      if (!parseNoteText(text, note)) { owner.repaint(); return; }   // refused: the cell keeps what it had
                      auto& target = cells[size_t(ch)][size_t(row)];
                      if (target.note == note) { owner.repaint(); return; }
                      target.note = note;
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

    /// A click on LEN or the groove chip puts the cursor there (section 35).
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

    /// The head's LEN: the length of the phrase this channel plays in the row
    /// on show, 1-64, typed (section 25).
    void openLengthEntry(int ch)
    {
        const auto r = lenRects[size_t(ch)];
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
        if (hoverLen >= 0)
            return juce::String("LEN: the steps this phrase holds, 1-64. Click and type it, or double-click for a box.");
        if (hoverGroove >= 0)
            return "Groove: the ticks each step lasts. Click and type a slot, double-click for a box, right-click to list them or open the one on show.";
        const int row = core.hoverRow, col = core.hoverCol;
        if (row < 0 || row >= core.rows || col < 0 || col >= int(core.cols.size())) return {};
        const auto& c = core.cols[size_t(col)];
        const auto& cell = cells[size_t(c.ch)][size_t(row)];
        if (c.kind == Kind::Ghost)
            return plays[size_t(c.ch)] == tracker::NoteSource::Hybrid
                       ? juce::String("The note the host sent here. Hybrid takes its notes from MIDI; the columns beside them still fire.")
                       : juce::String("The note the host sent here. Set the channel to Trkr to type notes.");
        if (c.kind == Kind::Note) return "The note this step plays. Shift+arrows move it (left/right a semitone, up/down an octave), a drag moves it, a double click types it; minus is a note off.";
        if (c.kind == Kind::Vel) return "Velocity, 1-127; blank is " + juce::String(int(tracker::kDefaultVelocity)) + ". Type it, double-click for a box, Shift+arrows move it.";
        if (c.kind == Kind::Inst) return "Instrument at this step. Type it, double-click for a box; right-click lists the bank and opens the one on show.";
        if (c.kind == Kind::Table) return "Table override at this step. Type it, double-click for a box; right-click lists the bank and opens the one on show.";
        if (c.kind == Kind::Cmd) return cmdTooltip(cmdSlot(col) == 0 ? cell.cmd1 : cell.cmd2);
        return {};
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
            for (int g = 1; g <= int(song->grooves.size()); ++g)
                m.addItem(1 + g, ValueFormat::number(g) + "  " + grooveTicks(g) + " ticks", true, cur == g);
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
        const int h1 = kHead1, h2 = core.headerH - h1;
        g.setColour(lineSoft);
        g.fillRect(0, core.headerH - 1, width, 1);
        for (int ch = 0; ch < 4; ++ch) {
            const int first = 1 + ch * kChannelCols;
            const int x = core.cols[size_t(first)].x;
            g.setColour(lineSoft);
            g.fillRect(x, 0, 1, core.headerH);
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
            // LEN, then the groove chip: the two numbers that say how long
            // this channel's row is and how its steps are laid out (25).
            const bool focused = owner.hasKeyboardFocus(false);
            auto ring = [&g, focused](juce::Rectangle<int> r) {
                g.setColour(accentHi.withAlpha(focused ? 0.95f : 0.45f));
                g.drawRoundedRectangle(r.toFloat().reduced(0.5f), 3.0f, focused ? 2.0f : 1.0f);
            };
            const auto lr = lenRects[size_t(ch)];
            if (lr.getWidth() >= 24) {
                draw::panel(g, lr, hoverLen == ch ? raisedHi : raised, line, 3.0f);
                // "LEN 16" in a chip: the caption takes exactly what it needs
                // and the number takes the rest.
                const int capW = juce::roundToInt(draw::textWidth(Fonts::caption(8.0f), "LEN")) + 2;
                draw::caption(g, "LEN", lr.withWidth(capW + 3).withTrimmedLeft(3), juce::Justification::centredLeft, textDim, 8.0f);
                g.setFont(Fonts::mono(10.5f));
                g.setColour(hoverLen == ch ? text : textMute);
                g.drawText(juce::String(length[size_t(ch)]), lr.withTrimmedLeft(capW + 4).withTrimmedRight(3), juce::Justification::centredRight, false);
                if (headCh == ch && headField == 0) ring(lr);
            }
            const auto gr = grooveRects[size_t(ch)];
            if (gr.getWidth() >= 20) {
                draw::panel(g, gr, hoverGroove == ch ? raisedHi : raised, line, 3.0f);
                g.setFont(Fonts::mono(10.5f));
                g.setColour(hoverGroove == ch ? text : textMute);
                g.drawFittedText(grooveText(ch, gr.getWidth()), gr.reduced(4, 0), juce::Justification::centred, 1, 0.72f);
                if (headCh == ch && headField == 1) ring(gr);
            }
        }
        for (size_t i = 1; i < core.cols.size(); ++i)
            draw::caption(g, core.cols[i].title, juce::Rectangle<int>(core.cols[i].x + 6, h1, core.cols[i].w - 6, h2), juce::Justification::centredLeft, textDim, 10.0f);
        draw::caption(g, "Step", juce::Rectangle<int>(6, 0, 30, core.headerH), juce::Justification::centredLeft, textDim, 10.0f);
    }
};

PhraseGrid::PhraseGrid() : impl_(std::make_unique<Impl>(*this))
{
    setWantsKeyboardFocus(true);
    setSize(980, preferredHeight());
}
PhraseGrid::~PhraseGrid() = default;

juce::String PhraseGrid::getTooltip() { return impl_->tooltip(); }

void PhraseGrid::setSong(std::shared_ptr<const tracker::Song> song, int bar)
{
    auto& im = *impl_;
    if (bar != im.bar) for (auto& s : im.shadow) s.fill(0);
    im.song = std::move(song);
    im.bar = juce::jmax(0, bar);
    im.refreshFromSong();
    im.buildColumns(getWidth());
    repaint();
}
void PhraseGrid::setBank(std::shared_ptr<const bank::Bank> bank)
{
    impl_->bank = std::move(bank);
}
int PhraseGrid::bar() const { return impl_->bar; }
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
    for (int ch = 0; ch < 4; ++ch) {
        const int p = im.playing[size_t(ch)];
        if (p < 0 || p >= core.rows) continue;
        const int first = 1 + ch * Impl::kChannelCols, last = first + Impl::kChannelCols - 1;
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
}

void PhraseGrid::mouseMove(const juce::MouseEvent& e)
{
    auto& im = *impl_;
    auto& core = im.core;
    int r = -1, c = -1;
    if (!core.cellAt(e.getPosition(), r, c)) { r = -1; c = -1; }
    int hg = -1, ha = -1, hl = -1;
    for (int ch = 0; ch < 4; ++ch) {
        if (im.grooveRects[size_t(ch)].contains(e.getPosition())) hg = ch;
        if (im.armRects[size_t(ch)].contains(e.getPosition())) ha = ch;
        if (im.lenRects[size_t(ch)].contains(e.getPosition())) hl = ch;
    }
    const bool letter = c >= 0 && core.onLetter(c, e.x);
    if (r != core.hoverRow || c != core.hoverCol || letter != core.hoverLetter || hg != im.hoverGroove || ha != im.hoverArm || hl != im.hoverLen) {
        core.hoverRow = r; core.hoverCol = c; core.hoverLetter = letter; im.hoverGroove = hg; im.hoverArm = ha; im.hoverLen = hl;
        repaint();
    }
}
void PhraseGrid::mouseExit(const juce::MouseEvent&)
{
    impl_->core.hoverRow = impl_->core.hoverCol = -1;
    impl_->core.hoverLetter = false;
    impl_->hoverGroove = impl_->hoverArm = impl_->hoverLen = -1;
    repaint();
}
void PhraseGrid::mouseDown(const juce::MouseEvent& e)
{
    auto& im = *impl_;
    auto& core = im.core;
    grabKeyboardFocus();
    im.dragging = false;
    im.dragRow = im.dragCol = -1;
    // The head's three fields: the arm toggles; LEN and the groove chip take
    // the cursor, and the chip lists on a right click (section 35).
    for (int ch = 0; ch < 4; ++ch) {
        if (im.armRects[size_t(ch)].contains(e.getPosition())) { im.toggleArm(ch); return; }
        if (im.lenRects[size_t(ch)].contains(e.getPosition())) { im.selectHead(ch, 0); return; }
        if (im.grooveRects[size_t(ch)].contains(e.getPosition())) {
            im.selectHead(ch, 1);
            if (e.mods.isPopupMenu()) im.openGrooveMenu(ch);
            return;
        }
    }
    int r = 0, c = 0;
    if (!core.cellAt(e.getPosition(), r, c)) return;
    im.leaveHead();
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
    // A note takes a vertical drag: a semitone every six pixels (section 30).
    if (core.cols[size_t(c)].kind == Kind::Note) {
        im.dragRow = r; im.dragCol = c;
        im.dragFrom = int(im.cells[size_t(core.cols[size_t(c)].ch)][size_t(r)].note);
    }
}

void PhraseGrid::mouseDrag(const juce::MouseEvent& e)
{
    auto& im = *impl_;
    if (im.dragRow < 0 || im.dragCol < 0) return;
    const int note = im.dragFrom;
    if (note == 0 || note == tracker::kNoteOff) return;
    const int steps = -e.getDistanceFromDragStartY() / Impl::kDragPixels;
    if (steps == 0 && !im.dragging) return;
    im.dragging = true;
    const int semis = e.mods.isShiftDown() ? steps * 12 : steps;
    const int want = juce::jlimit(1, 127, note + semis);
    const int ch = im.core.cols[size_t(im.dragCol)].ch;
    auto& cell = im.cells[size_t(ch)][size_t(im.dragRow)];
    if (int(cell.note) == want) return;
    cell.note = uint8_t(want);
    im.changed(ch, im.dragRow);
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
    // The double click is the box, everywhere (section 35): a head chip's,
    // a note's, a number's, a command's values -- or its palette on the
    // letter and where the slot holds no command.
    for (int ch = 0; ch < 4; ++ch) {
        if (im.grooveRects[size_t(ch)].contains(e.getPosition())) { im.openGrooveEntry(ch); return; }
        if (im.lenRects[size_t(ch)].contains(e.getPosition())) { im.openLengthEntry(ch); return; }
    }
    int r = 0, c = 0;
    if (!core.cellAt(e.getPosition(), r, c) || !core.editable(c)) return;
    const auto kind = core.cols[size_t(c)].kind;
    if (kind == Kind::Note) { im.openNoteEntry(r, c); return; }
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
    // Enter opens the box the cell has: the note's, a number's, a command's
    // values (Shift: its palette).
    if (k.getKeyCode() == juce::KeyPress::returnKey) {
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
// ChainColumn
// ===========================================================================
struct ChainColumn::Impl {
    ChainColumn& owner;
    std::shared_ptr<const tracker::Song> song;
    int selectedBar = 0;
    /// Each channel's own playing row: they drift apart by design, so the
    /// column lights one cell per channel rather than a whole row (25).
    std::array<int, 4> playingRow{ { -1, -1, -1, -1 } };
    int firstBar = 0;
    int cursorCol = 0;            ///< 0-3 a channel, 4 the bar's step count
    int hoverCol = -1, hoverBar = -1;
    Entry entry;
    TypedEntry box;
    int dragStartFirst = 0;
    bool dragged = false;
    float wheelAcc = 0.0f;

    /// 164 px across: five 25 px cells with 2 px between them, and what is
    /// left is the gutter the bar number stands in.
    static constexpr int kPad = 3, kMinGutter = 22, kGap = 2, kCols = 5, kSteps = 4;

    explicit Impl(ChainColumn& o) : owner(o) {}

    int cellW() const { return juce::jmax(14, (owner.getWidth() - 2 * kPad - kMinGutter - kGap * (kCols - 1)) / kCols); }
    int gutter() const { return juce::jmax(kMinGutter, owner.getWidth() - 2 * kPad - kCols * cellW() - kGap * (kCols - 1)); }
    int visibleBars() const { return juce::jmax(1, (owner.getHeight() - kHeaderHeight) / kRowHeight); }
    int songBars() const { return song != nullptr ? song->rows() : 0; }
    /// One row past the song, so typing there grows it (UI_DESIGN section 7).
    int playingMax() const { int m = -1; for (int v : playingRow) m = juce::jmax(m, v); return m; }
    bool anyPlaying(int bar) const { for (int v : playingRow) if (v == bar) return true; return false; }
    int barCount() const { return juce::jmax(songBars(), juce::jmax(selectedBar, playingMax()) + 1) + 1; }
    int slotAt(int ch, int bar) const { return song != nullptr ? song->phraseAt(ch, bar) : 0; }
    /// The fifth column is the row's LEN: a phrase carries its own length now
    /// (section 25), so it reads the first phrase this row holds and typing
    /// there sets the length of every phrase in the row.
    int stepsAt(int bar) const
    {
        if (song == nullptr || bar < 0) return 0;
        for (int ch = 0; ch < 4; ++ch)
            if (const auto* p = song->phrase(song->phraseAt(ch, bar))) return p->length();
        return 0;
    }
    int valueAt(int col, int bar) const { return col == kSteps ? stepsAt(bar) : slotAt(col, bar); }

    juce::Rectangle<int> cellRect(int col, int bar) const
    {
        const int w = cellW();
        return { kPad + gutter() + col * (w + kGap), kHeaderHeight + (bar - firstBar) * kRowHeight, w, kRowHeight - 1 };
    }
    juce::Rectangle<int> rowRect(int bar) const
    {
        return { 1, kHeaderHeight + (bar - firstBar) * kRowHeight, owner.getWidth() - 2, kRowHeight };
    }
    bool cellAt(juce::Point<int> p, int& col, int& bar) const
    {
        if (p.y < kHeaderHeight) return false;
        const int row = (p.y - kHeaderHeight) / kRowHeight;
        if (row < 0 || row >= visibleBars()) return false;
        bar = firstBar + row;
        if (bar >= barCount()) return false;
        const int w = cellW(), g = gutter();
        const int i = (p.x - kPad - g) / (w + kGap);
        col = p.x < kPad + g ? -1 : (i >= 0 && i < kCols ? i : -1);
        return true;
    }
    void clampScroll() { firstBar = juce::jlimit(0, juce::jmax(0, barCount() - visibleBars()), firstBar); }
    void scrollToSelected()
    {
        if (selectedBar < firstBar) firstBar = selectedBar;
        else if (selectedBar >= firstBar + visibleBars()) firstBar = selectedBar - visibleBars() + 1;
        clampScroll();
    }
    void selectBar(int bar)
    {
        bar = juce::jlimit(0, juce::jmax(0, barCount() - 1), bar);
        const bool changed = bar != selectedBar;
        selectedBar = bar;
        if (changed && owner.onEntryEnd) owner.onEntryEnd();
        entry.reset();
        scrollToSelected();
        owner.repaint();
        if (changed && owner.onSelectBar) owner.onSelectBar(bar);
    }
    /// Writes one cell: a phrase slot, or the bar's own step count.
    void setValue(int col, int bar, int value)
    {
        if (col < 0 || col > kSteps || bar < 0) return;
        const int v = juce::jlimit(0, col == kSteps ? tracker::kMaxSteps : tracker::kPhraseSlots, value);
        if (v == valueAt(col, bar)) return;
        if (col == kSteps) { if (owner.onRowLengthChange) owner.onRowLengthChange(bar, v); }
        else if (owner.onChainChange) owner.onChainChange(col, bar, v);
        owner.repaint();
    }

    static const char* colName(int col) { return col == kSteps ? "LEN" : colours::channelName(col); }

    /// The phrases the song uses, by slot, with how many bars play each:
    /// the list a right click on a chain cell opens (UI_DESIGN section 2.1).
    std::vector<SlotRow> phraseRows() const
    {
        std::vector<SlotRow> rows;
        if (song == nullptr) return rows;
        std::array<int, tracker::kPhraseSlots + 1> uses{};
        for (const auto& c : song->chain)
            for (uint8_t slot : c) if (slot >= 1) ++uses[size_t(slot)];
        for (int slot = 1; slot <= tracker::kPhraseSlots; ++slot) {
            if (song->phrase(slot) == nullptr) continue;
            SlotRow r;
            r.slot = slot;
            r.name = uses[size_t(slot)] > 0 ? "used in " + juce::String(uses[size_t(slot)]) + (uses[size_t(slot)] == 1 ? " bar" : " bars") : juce::String("free");
            r.used = true;
            rows.push_back(r);
        }
        return rows;
    }

    /// The double click on a cell: the inline box holding it (section 35).
    /// A phrase slot follows the display's base; LEN is a plain number.
    void openEntry(int col, int bar)
    {
        if (col < 0 || col > kSteps || bar < 0) return;
        const bool len = col == kSteps;
        const int v = valueAt(col, bar);
        box.begin(owner, cellRect(col, bar), v == 0 ? juce::String() : len ? juce::String(v) : ValueFormat::number(v), juce::Justification::centred,
                  [this, col, bar, len](const juce::String& text) {
                      const juce::String t = text.trim();
                      int nv = 0;
                      if (t.isNotEmpty()) {
                          if (len) { if (!t.containsOnly("0123456789")) { owner.repaint(); return; } nv = t.getIntValue(); }
                          else if (!detail::parseTypedInt(t, 0, tracker::kPhraseSlots, nv)) { owner.repaint(); return; }
                      }
                      setValue(col, bar, nv);
                  });
    }

    void openPhraseMenu(int col, int bar)
    {
        if (col < 0 || col >= kSteps) return;
        const auto rows = phraseRows();
        if (rows.empty()) return;
        showSlotMenu(owner, cellRect(col, bar), juce::String(colours::channelName(col)) + " phrase, row " + juce::String(bar + 1),
                     rows, slotAt(col, bar), [this, col, bar](int slot) { setValue(col, bar, slot); });
    }

    void paintHead(juce::Graphics& g)
    {
        using namespace colours;
        const int w = owner.getWidth();
        g.setColour(lineSoft);
        g.fillRect(1, kHeaderHeight - 1, w - 2, 1);
        draw::caption(g, "Chain", { 6, 0, w - 12, 26 }, juce::Justification::centredLeft, textDim, 10.0f);
        const int bars = songBars();
        g.setFont(Fonts::mono(10.0f));
        g.setColour(textDim);
        g.drawText(juce::String(bars) + (bars == 1 ? " row" : " rows"), juce::Rectangle<int>(w - 66, 0, 60, 26), juce::Justification::centredRight, false);
        draw::caption(g, "Row", { kPad, 26, gutter(), kHeaderHeight - 26 }, juce::Justification::centredLeft, textDim, 8.0f);
        for (int col = 0; col < kCols; ++col) {
            const auto r = cellRect(col, firstBar).withY(26).withHeight(kHeaderHeight - 26);
            g.setFont(Fonts::pixel(9.0f));
            g.setColour(col == kSteps ? textDim : channel(col));
            g.drawText(colName(col), r, juce::Justification::centred, false);
        }
    }

    void paintRows(juce::Graphics& g)
    {
        using namespace colours;
        const int n = visibleBars(), bars = barCount();
        const bool focused = owner.hasKeyboardFocus(false);
        for (int i = 0; i < n; ++i) {
            const int bar = firstBar + i;
            if (bar >= bars) break;
            const bool cur = bar == selectedBar;
            const bool playing = anyPlaying(bar);
            g.setFont(Fonts::mono(10.0f));
            g.setColour(playing ? accentHi : cur ? text : textDim);
            g.drawText(juce::String(bar + 1), juce::Rectangle<int>(kPad, rowRect(bar).getY(), gutter() - 4, kRowHeight), juce::Justification::centredRight, false);
            for (int col = 0; col < kCols; ++col) {
                const auto r = cellRect(col, bar);
                const int v = valueAt(col, bar);
                const bool empty = v == 0, hover = col == hoverCol && bar == hoverBar;
                const bool hasCursor = cur && col == cursorCol;
                // Each channel lights its own playing row, so two channels a
                // row apart both show where they are (section 25).
                const bool lit = col < kSteps && playingRow[size_t(col)] == bar;
                g.setColour(lit ? playRow : cur ? accentSoft : hover ? raised : panel2);
                g.fillRoundedRectangle(r.toFloat(), 3.0f);
                g.setColour(lit ? accentHi : cur ? accent : lineSoft);
                if (empty && !cur && !lit) {
                    const float dashes[2] = { 3.0f, 2.0f };
                    const auto fr = r.toFloat().reduced(0.5f);
                    g.drawDashedLine({ fr.getTopLeft(), fr.getTopRight() }, dashes, 2);
                    g.drawDashedLine({ fr.getTopRight(), fr.getBottomRight() }, dashes, 2);
                    g.drawDashedLine({ fr.getBottomRight(), fr.getBottomLeft() }, dashes, 2);
                    g.drawDashedLine({ fr.getBottomLeft(), fr.getTopLeft() }, dashes, 2);
                }
                else g.drawRoundedRectangle(r.toFloat().reduced(0.5f), 3.0f, 1.0f);
                if (hasCursor) { g.setColour(accentHi.withAlpha(focused ? 0.95f : 0.45f)); g.drawRoundedRectangle(r.toFloat().reduced(1.5f), 2.0f, focused ? 2.0f : 1.0f); }
                g.setFont(Fonts::mono(11.0f));
                g.setColour(empty ? textDim : lit ? text : col == kSteps ? textMute : cur ? text : textMute);
                g.drawText(empty ? juce::String::charToString(0x00b7) : col == kSteps ? juce::String(v) : ValueFormat::number(v),
                           r, juce::Justification::centred, false);
            }
        }
    }
};

ChainColumn::ChainColumn() : impl_(std::make_unique<Impl>(*this))
{
    setWantsKeyboardFocus(true);
    setSize(kWidth, kHeaderHeight + 16 * kRowHeight);
}
ChainColumn::~ChainColumn() = default;

void ChainColumn::setSong(std::shared_ptr<const tracker::Song> song, int selectedRow, const int playingRow[4])
{
    auto& im = *impl_;
    im.song = std::move(song);
    im.selectedBar = juce::jmax(0, selectedRow);
    for (int ch = 0; ch < 4; ++ch) im.playingRow[size_t(ch)] = playingRow != nullptr ? playingRow[ch] : -1;
    im.scrollToSelected();
    repaint();
}
void ChainColumn::resized() { impl_->clampScroll(); }

juce::String ChainColumn::getTooltip()
{
    auto& im = *impl_;
    if (im.hoverBar < 0) return "The chain: one row per row of the song, the four channels' phrases across it and the row's LEN at the end. "
                                "Type a slot, Backspace blanks it, double-click for a box, right-click lists the phrases, Shift+arrows move it; the wheel scrolls.";
    const juce::String at = " Row " + juce::String(im.hoverBar + 1) + ".";
    if (im.hoverCol == Impl::kSteps) {
        const int v = im.stepsAt(im.hoverBar);
        return v == 0 ? "LEN: the steps the phrases in this row hold, 1-64." + at
                      : "LEN " + juce::String(v) + ": every phrase in this row runs that many steps." + at;
    }
    if (im.hoverCol < 0) return "Row " + juce::String(im.hoverBar + 1) + ". Click a cell to edit that row; the lane shows whichever row is selected.";
    const int slot = im.slotAt(im.hoverCol, im.hoverBar);
    return juce::String(colours::channelName(im.hoverCol)) + (slot == 0 ? " has no phrase here -- it just plays its notes." : " plays phrase " + ValueFormat::number(slot) + ".") + at;
}

void ChainColumn::paint(juce::Graphics& g)
{
    draw::panel(g, getLocalBounds(), colours::panel2, colours::line, 4.0f);
    impl_->paintHead(g);
    impl_->paintRows(g);
}

void ChainColumn::mouseMove(const juce::MouseEvent& e)
{
    auto& im = *impl_;
    int col = -1, bar = -1;
    if (!im.cellAt(e.getPosition(), col, bar)) { col = -1; bar = -1; }
    if (col != im.hoverCol || bar != im.hoverBar) { im.hoverCol = col; im.hoverBar = bar; repaint(); }
}
void ChainColumn::mouseExit(const juce::MouseEvent&) { impl_->hoverCol = impl_->hoverBar = -1; repaint(); }
void ChainColumn::mouseDown(const juce::MouseEvent& e)
{
    auto& im = *impl_;
    grabKeyboardFocus();
    im.dragStartFirst = im.firstBar;
    im.dragged = false;
    int col = -1, bar = -1;
    if (!im.cellAt(e.getPosition(), col, bar)) return;
    if (col >= 0) {
        if (onEntryEnd) onEntryEnd();
        im.cursorCol = col;
        im.entry.reset();
    }
    im.selectBar(bar);
    if (e.mods.isPopupMenu()) im.openPhraseMenu(col, bar);
}
void ChainColumn::mouseDoubleClick(const juce::MouseEvent& e)
{
    auto& im = *impl_;
    int col = -1, bar = -1;
    if (!im.cellAt(e.getPosition(), col, bar) || col < 0) return;
    im.openEntry(col, bar);
}
void ChainColumn::mouseDrag(const juce::MouseEvent& e)
{
    auto& im = *impl_;
    const int dy = e.getDistanceFromDragStartY();
    if (std::abs(dy) < 6 && !im.dragged) return;
    im.dragged = true;
    const int first = im.dragStartFirst - dy / kRowHeight;
    if (first != im.firstBar) { im.firstBar = first; im.clampScroll(); repaint(); }
}
void ChainColumn::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    auto& im = *impl_;
    im.wheelAcc += w.deltaY;
    const int steps = w.isSmooth ? int(im.wheelAcc / 0.1f) : (w.deltaY > 0.0f ? 1 : w.deltaY < 0.0f ? -1 : 0);
    if (w.isSmooth) im.wheelAcc -= float(steps) * 0.1f; else im.wheelAcc = 0.0f;
    if (steps == 0) return;
    im.firstBar -= steps;
    im.clampScroll();
    repaint();
}
bool ChainColumn::keyPressed(const juce::KeyPress& k)
{
    auto& im = *impl_;
    const int code = k.getKeyCode();
    // Shift with the arrows moves the cell's value (section 35); the plain
    // arrows move the cursor.
    if (const int d = shiftDelta(k); d != 0) {
        im.entry.restart();
        im.setValue(im.cursorCol, im.selectedBar, im.valueAt(im.cursorCol, im.selectedBar) + d);
        return true;
    }
    if (code == juce::KeyPress::returnKey) { im.openEntry(im.cursorCol, im.selectedBar); return true; }
    if (code == juce::KeyPress::upKey) { im.selectBar(im.selectedBar - 1); return true; }
    if (code == juce::KeyPress::downKey) { im.selectBar(im.selectedBar + 1); return true; }
    if (code == juce::KeyPress::pageUpKey) { im.selectBar(im.selectedBar - im.visibleBars()); return true; }
    if (code == juce::KeyPress::pageDownKey) { im.selectBar(im.selectedBar + im.visibleBars()); return true; }
    if (code == juce::KeyPress::homeKey) { im.selectBar(0); return true; }
    if (code == juce::KeyPress::endKey) { im.selectBar(juce::jmax(0, im.songBars() - 1)); return true; }
    if (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey || code == juce::KeyPress::tabKey) {
        const int d = code == juce::KeyPress::leftKey || (code == juce::KeyPress::tabKey && k.getModifiers().isShiftDown()) ? -1 : 1;
        im.cursorCol = juce::jlimit(0, Impl::kCols - 1, im.cursorCol + d);
        im.entry.reset();
        if (onEntryEnd) onEntryEnd();
        repaint();
        return true;
    }
    if (code == juce::KeyPress::escapeKey) { im.entry.reset(); return true; }
    const int col = im.cursorCol, bar = im.selectedBar;
    const int cur = im.valueAt(col, bar);
    // LEN is a plain number 1-64; a phrase slot follows the display's base
    // like every other slot.
    const bool hex = col != Impl::kSteps && ValueFormat::hex();
    const int hi = col == Impl::kSteps ? tracker::kMaxSteps : tracker::kPhraseSlots;
    if (isBackspace(k)) { int m = 0; im.setValue(col, bar, popDigit(im.entry, hex, m) ? m : 0); return true; }
    if (isDelete(k)) { im.entry.reset(); im.setValue(col, bar, 0); return true; }
    if (isPlus(k)) { im.entry.reset(); im.setValue(col, bar, cur + 1); return true; }
    if (isMinus(k)) { im.entry.reset(); im.setValue(col, bar, cur - 1); return true; }
    int mag = 0;
    if (typeDigit(im.entry, k.getTextCharacter(), hex, hi, mag)) { if (mag >= 0) im.setValue(col, bar, mag); return true; }
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
    static constexpr int kPad = 6;

    static juce::Rectangle<int> inner(const juce::Component& c) { return c.getLocalBounds().reduced(kPad); }

    /// The grid cell a point lands on, clamped into the 32 by 16.
    static void cellAt(const juce::Component& c, juce::Point<int> p, int& i, int& v)
    {
        const auto in = inner(c);
        i = juce::jlimit(0, 31, int(std::floor(float(p.x - in.getX()) / (float(in.getWidth()) / 32.0f))));
        v = juce::jlimit(0, 15, 15 - int(std::floor(float(p.y - in.getY()) / (float(in.getHeight()) / 16.0f))));
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
        lastI = i; lastV = v;
        return changed;
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
        g.fillRect(float(in.getX()), float(in.getBottom()) - float(im.hoverV + 1) * levelH, float(in.getWidth()), levelH);
    }
    if (points) {
        const float d = juce::jmax(3.0f, juce::jmin(cellW, levelH) * 0.55f);
        for (int i = 0; i < 32; ++i) {
            const float cx = float(in.getX()) + (float(i) + 0.5f) * cellW;
            const float cy = float(in.getBottom()) - (float(im.frame.s[size_t(i)]) + 0.5f) * levelH;
            g.setColour(i == im.hoverI ? wav : wav.withAlpha(0.9f));
            g.fillEllipse(cx - d * 0.5f, cy - d * 0.5f, d, d);
        }
    } else {
        const float colW = (float(in.getWidth()) - 31.0f) / 32.0f;
        for (int i = 0; i < 32; ++i) {
            const float x = float(in.getX()) + float(i) * (colW + 1.0f);
            const float h = float(im.frame.s[size_t(i)] + 1) / 16.0f * float(in.getHeight());
            g.setColour(i == im.hoverI ? wav : wav.withAlpha(0.9f));
            g.fillRect(x + 1.0f, float(in.getBottom()) - h, colW - 2.0f, h);
        }
    }
    // The coordinates, the way LSDj's wave screen shows them: the sample
    // under the pointer and its level, in a corner, out of the way.
    if (im.hoverI >= 0) {
        const juce::String text = "sample " + ValueFormat::number(im.hoverI) + "  level " + ValueFormat::number(im.hoverV)
                                + "  (" + ValueFormat::number(im.frame.s[size_t(im.hoverI)]) + ")";
        g.setFont(Fonts::mono(10.0f));
        const int w = juce::roundToInt(draw::textWidth(Fonts::mono(10.0f), text)) + 10;
        // In the corner away from the pointer, so it never sits under the hand.
        const bool left = im.hoverI >= 16;
        const juce::Rectangle<int> r(left ? in.getX() + 2 : in.getRight() - 2 - w, in.getY() + 2, w, 16);
        g.setColour(lcd.withAlpha(0.85f));
        g.fillRoundedRectangle(r.toFloat(), 3.0f);
        g.setColour(lcdGrid);
        g.drawRoundedRectangle(r.toFloat(), 3.0f, 1.0f);
        g.setColour(wav);
        g.drawText(text, r, juce::Justification::centred, false);
    }
}
void WaveGrid::mouseMove(const juce::MouseEvent& e) { if (impl_->hover(*this, e.getPosition())) repaint(); }
void WaveGrid::mouseExit(const juce::MouseEvent&) { impl_->hoverI = impl_->hoverV = -1; repaint(); }
void WaveGrid::mouseDown(const juce::MouseEvent& e)
{
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
