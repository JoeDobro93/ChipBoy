// ChipBoy -- the grids: the table editor, the tracker lane, the chain
// column and the wave editor (UI_DESIGN sections 6-7; the mockup's .tracker,
// .chain and .wavegrid, with the keyboard of a tracker).
//
// Keyboard, in every grid: arrows / Tab move the cursor, digits type a value
// (multi-digit entry, decimal or hex with the display), Backspace and
// Delete blank the cell, + and - step it. Command columns are two parts, and
// they read as two: the letter, chosen from the palette a click on it opens
// or by typing the letter, and the arguments, typed as the base-10 numbers
// they are (spec 9.6) and refused when they fall outside what the letter
// takes. Comma moves to the next argument, "=" makes the letter its revert
// form ("E =": put the envelope back where the instrument left it, which is
// what the slot going to none does) and typing a value again clears that;
// Enter or a double-click opens the palette. Changing the letter clamps the
// values it kept into the new letter's ranges. The wheel never edits: it
// scrolls the pane the grid stands in (UI_DESIGN section 2.1). A right click
// on an INS or TBL cell lists the bank's slots by name, and on a chain cell
// the phrases the song uses. The note column is a piano: z s x d
// c v g b h n j m are C..B, comma l period continue into the next octave,
// q 2 w 3 e r 5 t 6 y 7 u are the octave above and i 9 o 0 p the one above
// that; minus enters note off; Ctrl/Alt with + or - changes the octave.
#include "plugin/shared/Parameters.h"
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

int cmdArg(const bank::Command& c, int i) { return i == 0 ? c.a : c.b; }
void setCmdArg(bank::Command& c, int i, int v)
{
    const auto v16 = int16_t(v);
    if (i == 0) c.a = v16; else c.b = v16;
    c.c = 0;                            // naming a value leaves the revert form
}
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

/// "4,6", "L", "-12" -- the cell's second part. Command arguments are base
/// 10 by definition (spec 9.6), so they stay decimal in hex display.
juce::String cmdValueText(const bank::Command& c)
{
    const auto* info = commandInfo(c.cmd);
    if (info == nullptr) return {};
    // The revert form has no arguments: it says "back to the instrument's".
    if (bank::isRevert(c)) return kRevertMark;
    juce::String s;
    for (int i = 0; i < info->nargs; ++i) {
        const int v = cmdArg(c, i);
        if (i > 0) s += ",";
        if (c.cmd == bank::Cmd::O) s += v == 0 ? juce::String::charToString(0x2013) : v == 1 ? juce::String("L") : v == 2 ? juce::String("R") : juce::String("LR");
        else if (c.cmd == bank::Cmd::P && i == 0) s += juce::String(v - 128);   // P is signed around 128
        else s += juce::String(v);
    }
    return s;
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
    s += "\nClick the letter for the palette; the values are typed and refused outside the range above.";
    return s;
}

// ---------------------------------------------------------------------------
// typed entry
// ---------------------------------------------------------------------------
struct Entry {
    int acc = 0, count = 0, arg = 0;
    bool negative = false;
    void reset() { acc = 0; count = 0; arg = 0; negative = false; }
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
int maxDigits(int hi, bool hex) { return hex ? (hi > 255 ? 3 : 2) : (hi >= 100 ? 3 : hi >= 10 ? 2 : 1); }

/// Types one digit into a magnitude in [0, hi]: a digit that would overflow
/// starts a new entry, and one the field cannot hold at all is refused --
/// the cell keeps what it had (UI_DESIGN section 2.1). False when `ch` is
/// not a digit in the display's base, or when it is refused.
bool typeDigit(Entry& e, juce::juce_wchar ch, bool hex, int hi, int& magnitude)
{
    const int d = digitValue(ch, hex);
    if (d < 0) return false;
    int v = e.acc * (hex ? 16 : 10) + d;
    if (v > hi) {
        if (d > hi) return false;
        v = d;
        e.count = 0;
    }
    e.acc = v;
    ++e.count;
    magnitude = v;
    if (e.count >= maxDigits(hi, hex)) { e.acc = 0; e.count = 0; }
    return true;
}

bool isBlankKey(const juce::KeyPress& k) { return k.getKeyCode() == juce::KeyPress::backspaceKey || k.getKeyCode() == juce::KeyPress::deleteKey; }
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
    if (isBlankKey(k)) { vol = -1; return true; }
    if (isPlus(k) || isMinus(k)) { vol = int8_t(juce::jlimit(0, 15, (vol < 0 ? (isPlus(k) ? -1 : 16) : int(vol)) + (isPlus(k) ? 1 : -1))); return true; }
    int mag = 0;
    if (!typeDigit(e, k.getTextCharacter(), ValueFormat::hex(), 15, mag)) return false;
    vol = int8_t(mag);
    return true;
}

bool editTranspose(bool& has, int8_t& t, const juce::KeyPress& k, Entry& e)
{
    if (isBlankKey(k)) { has = false; t = 0; return true; }
    if (isMinus(k) && e.count == 0) { has = true; t = int8_t(-int(t)); e.negative = t < 0 || t == 0; return true; }
    if (isPlus(k) && e.count == 0) { has = true; t = int8_t(std::abs(int(t))); e.negative = false; return true; }
    if (isMinus(k) || isPlus(k)) { has = true; t = int8_t(juce::jlimit(-60, 60, int(t) + (isPlus(k) ? 1 : -1))); return true; }
    if (e.count == 0) e.negative = t < 0 || (has && t == 0 && e.negative);
    int mag = 0;
    if (!typeDigit(e, k.getTextCharacter(), ValueFormat::hex(), 60, mag)) return false;
    has = true;
    t = int8_t(e.negative ? -mag : mag);
    return true;
}

bool editSlot(uint8_t& v, int hi, const juce::KeyPress& k, Entry& e)
{
    if (isBlankKey(k)) { v = 0; return true; }
    if (isPlus(k) || isMinus(k)) { v = uint8_t(juce::jlimit(0, hi, int(v) + (isPlus(k) ? 1 : -1))); return true; }
    int mag = 0;
    if (!typeDigit(e, k.getTextCharacter(), ValueFormat::hex(), hi, mag)) return false;
    v = uint8_t(mag);
    return true;
}

bool editCmd(bank::Command& c, const juce::KeyPress& k, Entry& e)
{
    if (isBlankKey(k)) { c = {}; e.reset(); return true; }
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
    if (info == nullptr) return false;
    if (ch == ',' || ch == '.') { e.arg = (e.arg + 1) % info->nargs; e.acc = 0; e.count = 0; return true; }
    const int a = juce::jlimit(0, info->nargs - 1, e.arg);
    const int lo = info->lo[a], hi = info->hi[a];
    const int cur = cmdArg(c, a);
    if (isMinus(k) && lo < 0 && e.count == 0) { setCmdArg(c, a, juce::jlimit(lo, hi, -cur)); e.negative = cur >= 0; return true; }
    if (isPlus(k) || isMinus(k)) { setCmdArg(c, a, juce::jlimit(lo, hi, cur + (isPlus(k) ? 1 : -1))); return true; }
    if (e.count == 0) e.negative = lo < 0 && cur < 0;
    int mag = 0;
    if (!typeDigit(e, ch, false, juce::jmax(hi, -lo), mag)) return false;
    setCmdArg(c, a, juce::jlimit(lo, hi, e.negative ? -mag : mag));
    return true;
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

/// The bank's instruments, by slot and name: the list a right click on an
/// INS cell opens. Only slots in use; the ones this channel plays come
/// first, the rest are marked with their type (UI_DESIGN section 2.1).
void showSlotMenu(juce::Component& target, juce::Rectangle<int> cellArea, const juce::String& heading,
                  const std::vector<SlotRow>& rows, int current, std::function<void(int slot)> done)
{
    juce::PopupMenu m;
    m.addSectionHeader(heading);
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
                    [done, safe = juce::Component::SafePointer<juce::Component>(&target)](int id) {
                        if (safe == nullptr || id == 0) return;
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
        const int widths[5] = { 34, 52, 76, 74, 74 };
        const Kind kinds[5] = { Kind::Step, Kind::Vol, Kind::Transpose, Kind::Cmd, Kind::Cmd };
        const char* titles[5] = { "Step", "Vol", "Transpose", "Cmd 1", "Cmd 2" };
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
        if (c.kind == Kind::Vol) return "Volume at this step, 0-15; blank leaves it alone";
        if (c.kind == Kind::Transpose) return "Semitones added to the note at this step; blank leaves it alone";
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
    // The letter is its own part of the cell: a click on it picks one.
    if (e.mods.isPopupMenu() || core.onLetter(c, e.x)) impl_->openPalette(r, c);
}
void TableGrid::mouseDoubleClick(const juce::MouseEvent& e)
{
    int r = 0, c = 0;
    if (impl_->core.cellAt(e.getPosition(), r, c)) impl_->openPalette(r, c);
}
bool TableGrid::keyPressed(const juce::KeyPress& k)
{
    auto& core = impl_->core;
    if (k.getKeyCode() == juce::KeyPress::escapeKey) { core.entry.reset(); return true; }
    if (k.getKeyCode() == juce::KeyPress::returnKey) { impl_->openPalette(core.curRow, core.curCol); return true; }
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
    /// The head, per channel: the record arm, the name, the PLAYS switch and
    /// the phrase's groove chip, all inside the 26 px name row.
    static constexpr int kArm = 14, kHead1 = 26;

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
    std::array<juce::Rectangle<int>, 4> grooveRects{}, armRects{}, playsRects{};
    Segmented source[4];
    GridCore core;
    int octave = 4;
    int hoverGroove = -1, hoverArm = -1;

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
    /// PLAYS caption with its switch, and what is left goes to the groove
    /// chip (UI_DESIGN section 7).
    void layoutHeader(int ch, int x, int w)
    {
        int cx = x + 4;
        armRects[size_t(ch)] = { cx, (kHead1 - kArm) / 2, kArm, kArm };
        cx += kArm + 4;
        const int nameW = juce::roundToInt(draw::textWidth(Fonts::pixel(10.0f), colours::channelName(ch))) + 4;
        cx += nameW + 5;
        const int playsW = juce::roundToInt(draw::textWidth(Fonts::caption(9.0f), "PLAYS")) + 4;
        playsRects[size_t(ch)] = { cx, 0, playsW, kHead1 };
        cx += playsW + 3;
        auto& seg = source[ch];
        seg.setBounds(cx, 3, seg.preferredWidth(), 20);
        const int gx = seg.getRight() + 5;
        grooveRects[size_t(ch)] = { gx, 3, juce::jmin(64, juce::jmax(0, x + w - gx - 3)), 20 };
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
        // cell it holds can be reached. The head's LEN is the next stage's.
        core.rows = PhraseGrid::kVisibleSteps;
        if (song != nullptr) { int n = 1; for (int ch = 0; ch < 4; ++ch) n = juce::jmax(n, song->stepsOfRow(ch, bar)); core.rows = n; }
        core.curRow = juce::jlimit(0, core.rows - 1, core.curRow);
        for (int ch = 0; ch < 4; ++ch) {
            const auto* p = song != nullptr ? song->phrase(song->phraseAt(ch, bar)) : nullptr;
            for (int i = 0; i < kMax; ++i) cells[size_t(ch)][size_t(i)] = p != nullptr ? p->cells[size_t(i)] : tracker::Cell{};
            groove[size_t(ch)] = p != nullptr ? p->groove : 0;
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
        showSlotMenu(owner, core.cellRect(row, col), instruments ? "Instrument" : "Table", rows,
                     instruments ? int(cell.inst) : int(cell.table),
                     [this, row, ch, instruments](int slot) {
                         auto& target = cells[size_t(ch)][size_t(row)];
                         (instruments ? target.inst : target.table) = uint8_t(juce::jmax(0, slot));
                         core.entry.reset();
                         changed(ch, row);
                     });
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
            return juce::String("Record arm for ") + colours::channelName(hoverArm)
                 + ": with Rec on and the transport running, this channel's incoming MIDI is written into its cells, whatever it plays. An unarmed channel never records.";
        if (hoverGroove >= 0)
            return "The groove this phrase runs on: how many ticks each step lasts. 0 is straight, six ticks a step; 1-16 are the song's, edited in the Grooves tab.";
        const int row = core.hoverRow, col = core.hoverCol;
        if (row < 0 || row >= core.rows || col < 0 || col >= int(core.cols.size())) return {};
        const auto& c = core.cols[size_t(col)];
        const auto& cell = cells[size_t(c.ch)][size_t(row)];
        if (c.kind == Kind::Ghost)
            return plays[size_t(c.ch)] == tracker::NoteSource::Hybrid
                       ? "The note the host sent as this bar played. This channel is Hybrid: its notes come from MIDI and its cells' notes are ignored, "
                         "but the instrument, table and command columns beside them still fire at their steps."
                       : juce::String("The note the host sent as this bar played. Set the channel to Trkr to type notes here.");
        if (c.kind == Kind::Note) return "The note this step plays; minus enters a note off";
        if (c.kind == Kind::Vel) return "How hard this step's note is played, 1-127; blank is the default "
                                        + juce::String(int(tracker::kDefaultVelocity)) + ". A recorded note keeps the velocity it arrived with.";
        if (c.kind == Kind::Inst) return "Instrument slot to load at this step; blank keeps the one in force. Right-click for the bank's instruments by name.";
        if (c.kind == Kind::Table) return "Table override for this step; blank keeps the instrument's own. Right-click for the bank's tables by name.";
        if (c.kind == Kind::Cmd) return cmdTooltip(cmdSlot(col) == 0 ? cell.cmd1 : cell.cmd2);
        return {};
    }

    void openGrooveMenu(int ch)
    {
        juce::PopupMenu m;
        m.addSectionHeader("Groove");
        m.addItem(1, "Straight (6/6)", true, groove[size_t(ch)] == 0);
        if (song != nullptr)
            for (int g = 1; g <= int(song->grooves.size()); ++g)
                m.addItem(1 + g, ValueFormat::number(g) + "  " + grooveTicks(g) + " ticks", true, groove[size_t(ch)] == g);
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(owner).withTargetScreenArea(owner.localAreaToGlobal(grooveRects[size_t(ch)])),
                        [this, ch, safe = juce::Component::SafePointer<juce::Component>(&owner)](int id) {
                            if (safe == nullptr || id == 0) return;
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
            draw::caption(g, "PLAYS", playsRects[size_t(ch)], juce::Justification::centredLeft, textDim, 9.0f);
            const auto gr = grooveRects[size_t(ch)];
            if (gr.getWidth() >= 20) {
                draw::panel(g, gr, hoverGroove == ch ? raisedHi : raised, line, 3.0f);
                g.setFont(Fonts::mono(10.5f));
                g.setColour(hoverGroove == ch ? text : textMute);
                g.drawFittedText(grooveText(ch, gr.getWidth()), gr.reduced(4, 0), juce::Justification::centred, 1, 0.72f);
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
    int hg = -1, ha = -1;
    for (int ch = 0; ch < 4; ++ch) {
        if (im.grooveRects[size_t(ch)].contains(e.getPosition())) hg = ch;
        if (im.armRects[size_t(ch)].contains(e.getPosition())) ha = ch;
    }
    const bool letter = c >= 0 && core.onLetter(c, e.x);
    if (r != core.hoverRow || c != core.hoverCol || letter != core.hoverLetter || hg != im.hoverGroove || ha != im.hoverArm) {
        core.hoverRow = r; core.hoverCol = c; core.hoverLetter = letter; im.hoverGroove = hg; im.hoverArm = ha;
        repaint();
    }
}
void PhraseGrid::mouseExit(const juce::MouseEvent&)
{
    impl_->core.hoverRow = impl_->core.hoverCol = -1;
    impl_->core.hoverLetter = false;
    impl_->hoverGroove = impl_->hoverArm = -1;
    repaint();
}
void PhraseGrid::mouseDown(const juce::MouseEvent& e)
{
    auto& im = *impl_;
    auto& core = im.core;
    grabKeyboardFocus();
    for (int ch = 0; ch < 4; ++ch) {
        if (im.armRects[size_t(ch)].contains(e.getPosition())) { im.toggleArm(ch); return; }
        if (im.grooveRects[size_t(ch)].contains(e.getPosition())) { im.openGrooveMenu(ch); return; }
    }
    int r = 0, c = 0;
    if (!core.cellAt(e.getPosition(), r, c)) return;
    if (core.editable(c)) {
        // A click ends whatever value was being typed: what follows is a
        // new undo (UI_DESIGN section 2.1).
        if (onEntryEnd) onEntryEnd();
        core.setCursor(r, c);
        repaint();
    }
    if (e.mods.isPopupMenu()) { im.openPalette(r, c); im.openSlotMenu(r, c); return; }
    if (core.onLetter(c, e.x)) im.openPalette(r, c);
}
void PhraseGrid::mouseDoubleClick(const juce::MouseEvent& e)
{
    int r = 0, c = 0;
    if (impl_->core.cellAt(e.getPosition(), r, c)) impl_->openPalette(r, c);
}
bool PhraseGrid::keyPressed(const juce::KeyPress& k)
{
    auto& core = impl_->core;
    if (k.getKeyCode() == juce::KeyPress::escapeKey) { core.entry.reset(); return true; }
    if (k.getKeyCode() == juce::KeyPress::returnKey) { impl_->openPalette(core.curRow, core.curCol); return true; }
    if (core.navigate(k)) {
        repaint();
        if (onEntryEnd) onEntryEnd();
        // Past sixteen steps the grid is taller than its pane, so the tab
        // scrolls to wherever the cursor went (section 11).
        if (onCursorRow) onCursorRow(core.curRow);
        return true;
    }
    return impl_->edit(k);
}
void PhraseGrid::focusGained(FocusChangeType) { repaint(); }
void PhraseGrid::focusLost(FocusChangeType) { impl_->core.entry.reset(); repaint(); }

// ===========================================================================
// ChainColumn
// ===========================================================================
struct ChainColumn::Impl {
    ChainColumn& owner;
    std::shared_ptr<const tracker::Song> song;
    int selectedBar = 0, playingBar = -1;
    int firstBar = 0;
    int cursorCol = 0;            ///< 0-3 a channel, 4 the bar's step count
    int hoverCol = -1, hoverBar = -1;
    Entry entry;
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
    int barCount() const { return juce::jmax(songBars(), juce::jmax(selectedBar, playingBar) + 1) + 1; }
    int slotAt(int ch, int bar) const { return song != nullptr ? song->phraseAt(ch, bar) : 0; }
    /// The fifth column is the row's phrase length: a phrase carries its own
    /// now (section 25), so it reads the first phrase this row holds.
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

    void openPhraseMenu(int col, int bar)
    {
        if (col < 0 || col >= kSteps) return;
        const auto rows = phraseRows();
        if (rows.empty()) return;
        showSlotMenu(owner, cellRect(col, bar), juce::String(colours::channelName(col)) + " phrase, bar " + juce::String(bar + 1),
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
        g.drawText(juce::String(bars) + (bars == 1 ? " bar" : " bars"), juce::Rectangle<int>(w - 66, 0, 60, 26), juce::Justification::centredRight, false);
        draw::caption(g, "Bar", { kPad, 26, gutter(), kHeaderHeight - 26 }, juce::Justification::centredLeft, textDim, 8.0f);
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
            if (bar == playingBar) { g.setColour(playRow); g.fillRect(rowRect(bar)); }
            g.setFont(Fonts::mono(10.0f));
            g.setColour(bar == playingBar ? accentHi : cur ? text : textDim);
            g.drawText(juce::String(bar + 1), juce::Rectangle<int>(kPad, rowRect(bar).getY(), gutter() - 4, kRowHeight), juce::Justification::centredRight, false);
            for (int col = 0; col < kCols; ++col) {
                const auto r = cellRect(col, bar);
                const int v = valueAt(col, bar);
                const bool empty = v == 0, hover = col == hoverCol && bar == hoverBar;
                const bool hasCursor = cur && col == cursorCol;
                g.setColour(cur ? accentSoft : hover ? raised : panel2);
                g.fillRoundedRectangle(r.toFloat(), 3.0f);
                g.setColour(cur ? accent : lineSoft);
                if (empty && !cur) {
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
                g.setColour(empty ? textDim : col == kSteps ? textMute : cur ? text : textMute);
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

void ChainColumn::setSong(std::shared_ptr<const tracker::Song> song, int selectedBar, int playingBar)
{
    auto& im = *impl_;
    im.song = std::move(song);
    im.selectedBar = juce::jmax(0, selectedBar);
    im.playingBar = playingBar;
    im.scrollToSelected();
    repaint();
}
void ChainColumn::resized() { impl_->clampScroll(); }

juce::String ChainColumn::getTooltip()
{
    auto& im = *impl_;
    if (im.hoverBar < 0) return "The chain: one row per bar, the four channels' phrases across it. Type a slot, blank it with Backspace, or step it with + and -. "
                                "Right-click a cell for the phrases the song uses; the wheel scrolls the bars.";
    const juce::String at = " Bar " + juce::String(im.hoverBar + 1) + ".";
    if (im.hoverCol == Impl::kSteps) {
        const int v = im.stepsAt(im.hoverBar);
        return v == 0 ? "This bar's own step count, 1-64. Blank: it takes the song's Steps / bar." + at
                      : "This bar runs " + juce::String(v) + " steps; Backspace puts it back on the song's." + at;
    }
    if (im.hoverCol < 0) return "Bar " + juce::String(im.hoverBar + 1) + ". Click a cell to edit that bar; the lane shows whichever bar is selected.";
    const int slot = im.slotAt(im.hoverCol, im.hoverBar);
    return juce::String(colours::channelName(im.hoverCol)) + (slot == 0 ? " has no phrase this bar -- it just plays its notes." : " plays phrase " + ValueFormat::number(slot) + ".") + at;
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
    if (isBlankKey(k)) { im.entry.reset(); im.setValue(col, bar, 0); return true; }
    if (isPlus(k)) { im.entry.reset(); im.setValue(col, bar, cur + 1); return true; }
    if (isMinus(k)) { im.entry.reset(); im.setValue(col, bar, cur - 1); return true; }
    // The step count is a plain number 1-64, as Steps / bar is; a phrase slot
    // follows the display's base like every other slot.
    const int hi = col == Impl::kSteps ? tracker::kMaxSteps : tracker::kPhraseSlots;
    int mag = 0;
    if (typeDigit(im.entry, k.getTextCharacter(), col != Impl::kSteps && ValueFormat::hex(), hi, mag)) { im.setValue(col, bar, mag); return true; }
    return false;
}
void ChainColumn::focusGained(FocusChangeType) { repaint(); }
void ChainColumn::focusLost(FocusChangeType) { impl_->entry.reset(); repaint(); }

// ===========================================================================
// WaveGrid
// ===========================================================================
struct WaveGrid::Impl {
    bank::Frame frame;
    int lastI = -1, lastV = -1;
    static constexpr int kPad = 6;

    static juce::Rectangle<int> inner(const juce::Component& c) { return c.getLocalBounds().reduced(kPad); }

    /// Sets one sample; drags interpolate between the previous and the new column.
    bool paintAt(const juce::Component& c, juce::Point<int> p, bool continuing)
    {
        const auto in = inner(c);
        if (in.getWidth() < 32 || in.getHeight() < 16) return false;
        const int i = juce::jlimit(0, 31, int(std::floor(float(p.x - in.getX()) / (float(in.getWidth()) / 32.0f))));
        const int v = juce::jlimit(0, 15, 15 - int(std::floor(float(p.y - in.getY()) / (float(in.getHeight()) / 16.0f))));
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
};

WaveGrid::WaveGrid() : impl_(std::make_unique<Impl>())
{
    setMouseCursor(juce::MouseCursor::CrosshairCursor);
    setSize(640, 200);
}
WaveGrid::~WaveGrid() = default;

void WaveGrid::setFrame(const bank::Frame& f) { impl_->frame = f; repaint(); }
const bank::Frame& WaveGrid::frame() const { return impl_->frame; }

void WaveGrid::paint(juce::Graphics& g)
{
    using namespace colours;
    g.setColour(lcd);
    g.fillRect(getLocalBounds());
    g.setColour(scopeBorder);
    g.drawRect(getLocalBounds(), 1);
    const auto in = Impl::inner(*this);
    if (in.getWidth() < 32 || in.getHeight() < 16) return;
    const float colW = (float(in.getWidth()) - 31.0f) / 32.0f;
    const float levelH = float(in.getHeight()) / 16.0f;
    g.setColour(lcdGrid);
    for (int k = 1; k < 16; ++k) g.fillRect(float(in.getX()), float(in.getBottom()) - float(k) * levelH - 1.0f, float(in.getWidth()), 1.0f);
    g.setColour(wav.withAlpha(0.9f));
    for (int i = 0; i < 32; ++i) {
        const float x = float(in.getX()) + float(i) * (colW + 1.0f);
        const float h = float(impl_->frame.s[size_t(i)] + 1) / 16.0f * float(in.getHeight());
        g.fillRect(x + 1.0f, float(in.getBottom()) - h, colW - 2.0f, h);
    }
}
void WaveGrid::mouseDown(const juce::MouseEvent& e)
{
    impl_->lastI = -1;
    if (impl_->paintAt(*this, e.getPosition(), false)) { repaint(); if (onChange) onChange(impl_->frame); }
}
void WaveGrid::mouseDrag(const juce::MouseEvent& e)
{
    if (impl_->paintAt(*this, e.getPosition(), true)) { repaint(); if (onChange) onChange(impl_->frame); }
}
void WaveGrid::mouseUp(const juce::MouseEvent&) { impl_->lastI = -1; }

} // namespace chipboy::ui
