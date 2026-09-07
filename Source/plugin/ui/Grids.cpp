// ChipBoy -- the grids: the table editor, the phrases lane, the bar chain
// and the wave editor (UI_DESIGN sections 6-7; the mockup's .tracker,
// .chain and .wavegrid, with the keyboard of a tracker).
//
// Keyboard, in every grid: arrows / Tab move the cursor, digits type a value
// (multi-digit entry, decimal or hex with the display), Backspace and
// Delete blank the cell, + and - step it. Command columns: a letter picks
// the command with its default argument, digits then edit the argument,
// comma moves to the next argument, Enter or a double-click opens the
// palette. The note column is a piano: z s x d c v g b h n j m are C..B,
// comma l period continue into the next octave, q 2 w 3 e r 5 t 6 y 7 u are
// the octave above and i 9 o 0 p the one above that; minus enters note off;
// Ctrl/Alt with + or - changes the octave.
#include "plugin/ui/Widgets.h"

#include <cmath>

namespace chipboy::ui {

namespace {

// ---------------------------------------------------------------------------
// commands (spec 9.6): argument counts, ranges and the palette defaults
// ---------------------------------------------------------------------------
struct CmdInfo { char letter; const char* name; const char* args; int nargs; int lo[3]; int hi[3]; int def[3]; };

constexpr CmdInfo kCmds[15] = {
    { 'A', "Envelope",      "vol, rate, dir",           3, { 0, 0, 0 },     { 15, 7, 1 },   { 12, 3, 0 } },
    { 'C', "Chord",         "a, b semitones",           2, { -60, -60, 0 }, { 60, 60, 0 },  { 3, 7, 0 } },
    { 'D', "Delay",         "ticks",                    1, { 0, 0, 0 },     { 255, 0, 0 },  { 3, 0, 0 } },
    { 'F', "Frame",         "1-16",                     1, { 1, 0, 0 },     { 16, 0, 0 },   { 2, 0, 0 } },
    { 'H', "Hop",           "step 1-16, 0 stops",       1, { 0, 0, 0 },     { 16, 0, 0 },   { 1, 0, 0 } },
    { 'K', "Kill",          "after ticks",              1, { 0, 0, 0 },     { 255, 0, 0 },  { 4, 0, 0 } },
    { 'L', "Slide",         "rate 0-15",                1, { 0, 0, 0 },     { 15, 0, 0 },   { 8, 0, 0 } },
    { 'M', "Master vol",    "L, R 0-7",                 2, { 0, 0, 0 },     { 7, 7, 0 },    { 5, 5, 0 } },
    { 'O', "Pan",           "off / L / R / LR",         1, { 0, 0, 0 },     { 3, 0, 0 },    { 1, 0, 0 } },
    { 'P', "Pitch offset",  "-128..127 period",         1, { -128, 0, 0 },  { 127, 0, 0 },  { -12, 0, 0 } },
    { 'R', "Retrigger",     "every N ticks",            1, { 0, 0, 0 },     { 255, 0, 0 },  { 3, 0, 0 } },
    { 'S', "Sweep / shift", "signed",                   1, { -128, 0, 0 },  { 127, 0, 0 },  { -2, 0, 0 } },
    { 'V', "Vibrato",       "speed 1-15, depth 0-15",   2, { 1, 0, 0 },     { 15, 15, 0 },  { 4, 6, 0 } },
    { 'W', "Wave",          "1-64",                     1, { 1, 0, 0 },     { 64, 0, 0 },   { 2, 0, 0 } },
    { 'Z', "Random arg",    "max, for the last command",1, { 0, 0, 0 },     { 255, 0, 0 },  { 15, 0, 0 } },
};

const CmdInfo* cmdInfo(bank::Cmd c)
{
    const int i = int(c) - 1;   // Cmd::A == 1 .. Cmd::Z == 15, the table's order
    return i >= 0 && i < 15 ? &kCmds[i] : nullptr;
}

int cmdArg(const bank::Command& c, int i) { return i == 0 ? c.a : i == 1 ? c.b : c.c; }
void setCmdArg(bank::Command& c, int i, int v)
{
    const auto v16 = int16_t(v);
    if (i == 0) c.a = v16; else if (i == 1) c.b = v16; else c.c = v16;
}

bank::Command defaultCommand(bank::Cmd cmd)
{
    bank::Command c;
    c.cmd = cmd;
    if (const auto* info = cmdInfo(cmd)) for (int i = 0; i < 3; ++i) setCmdArg(c, i, info->def[i]);
    return c;
}

/// "V 4,6", "O L", "A 12,3,down-arrow". Command arguments are base 10 by
/// definition (spec 9.6), so they stay decimal in hex display.
juce::String cmdText(const bank::Command& c)
{
    const auto* info = cmdInfo(c.cmd);
    if (info == nullptr) return {};
    juce::String s = juce::String::charToString(juce::juce_wchar(info->letter)) + " ";
    for (int i = 0; i < info->nargs; ++i) {
        const int v = cmdArg(c, i);
        if (i > 0) s += ",";
        if (c.cmd == bank::Cmd::O) s += v == 0 ? juce::String::charToString(0x2013) : v == 1 ? juce::String("L") : v == 2 ? juce::String("R") : juce::String("LR");
        else if (c.cmd == bank::Cmd::A && i == 2) s += juce::String::charToString(v != 0 ? juce::juce_wchar(0x2191) : juce::juce_wchar(0x2193));
        else s += juce::String(v);
    }
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
/// starts a new entry. False if `ch` is not a digit in the display's base.
bool typeDigit(Entry& e, juce::juce_wchar ch, bool hex, int hi, int& magnitude)
{
    const int d = digitValue(ch, hex);
    if (d < 0) return false;
    int v = e.acc * (hex ? 16 : 10) + d;
    if (v > hi) { v = juce::jmin(hi, d); e.count = 0; }
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
bool wheelNote(uint8_t& note, int delta)
{
    if (note == 0) { if (delta > 0) { note = 60; return true; } return false; }
    if (note == tracker::kNoteOff) return false;
    note = uint8_t(juce::jlimit(1, 127, int(note) + delta));
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
    const auto upper = juce::juce_wchar(ch >= 'a' && ch <= 'z' ? ch - 'a' + 'A' : ch);
    if (upper >= 'A' && upper <= 'Z') {
        const auto cmd = bank::cmdFromLetter(char(upper));
        if (cmd == bank::Cmd::None) return false;
        c = defaultCommand(cmd);
        e.reset();
        return true;
    }
    const auto* info = cmdInfo(c.cmd);
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
    return a.note == b.note && a.inst == b.inst && a.table == b.table && sameCommand(a.cmd1, b.cmd1) && sameCommand(a.cmd2, b.cmd2);
}

bool wheelCmd(bank::Command& c, int delta, int arg)
{
    const auto* info = cmdInfo(c.cmd);
    if (info == nullptr) return false;
    const int a = juce::jlimit(0, info->nargs - 1, arg);
    setCmdArg(c, a, juce::jlimit(info->lo[a], info->hi[a], cmdArg(c, a) + delta));
    return true;
}

/// The palette: every command with its argument description, then Clear.
void showCommandPalette(juce::Component& target, juce::Rectangle<int> cellArea, std::function<void(bool clear, bank::Cmd)> done)
{
    juce::PopupMenu m;
    for (int i = 0; i < 15; ++i) {
        juce::PopupMenu::Item item(juce::String::charToString(juce::juce_wchar(kCmds[i].letter)) + "   " + kCmds[i].name);
        item.itemID = i + 1;
        item.shortcutKeyDescription = kCmds[i].args;
        m.addItem(item);
    }
    m.addSeparator();
    m.addItem(100, "Clear");
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(target).withTargetScreenArea(target.localAreaToGlobal(cellArea)).withMinimumWidth(260),
                    [done, safe = juce::Component::SafePointer<juce::Component>(&target)](int id) {
                        if (safe == nullptr || id == 0) return;
                        if (id == 100) done(true, bank::Cmd::None);
                        else done(false, bank::Cmd(id));
                    });
}

// ---------------------------------------------------------------------------
// the grid core: columns, cursor, hover, navigation, painting
// ---------------------------------------------------------------------------
enum class Kind { Step, Vol, Transpose, Cmd, Note, Inst, Table, Ghost, Info };

struct Column { Kind kind = Kind::Step; int ch = 0; int x = 0, w = 0; juce::String title; };

bool editableKind(Kind k)
{
    return k == Kind::Vol || k == Kind::Transpose || k == Kind::Cmd || k == Kind::Note || k == Kind::Inst || k == Kind::Table;
}

struct GridCore {
    std::vector<Column> cols;
    int rows = 16, rowH = 22, headerH = 22;
    int curRow = 0, curCol = 1, hoverRow = -1, hoverCol = -1;
    Entry entry;
    float wheelAcc = 0.0f;

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
};

const juce::String kBlank2 = "--", kBlank3 = "---";

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
        if (k == Kind::Cmd) { const auto& c = core.cols[size_t(col)].ch == 0 ? s.cmd1 : s.cmd2; blank = c.cmd == bank::Cmd::None; return blank ? kBlank2 : cmdText(c); }
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

    bool wheel(int row, int col, int delta)
    {
        auto& s = table.steps[size_t(row)];
        const auto& c = core.cols[size_t(col)];
        bool done = false;
        if (c.kind == Kind::Vol) { const int v = juce::jlimit(0, 15, (s.vol < 0 ? (delta > 0 ? -1 : 16) : int(s.vol)) + delta); done = v != s.vol; s.vol = int8_t(v); }
        else if (c.kind == Kind::Transpose) { const int v = juce::jlimit(-60, 60, (s.hasTranspose ? int(s.transpose) : 0) + delta); done = !s.hasTranspose || v != s.transpose; s.hasTranspose = true; s.transpose = int8_t(v); }
        else if (c.kind == Kind::Cmd) done = wheelCmd(c.ch == 0 ? s.cmd1 : s.cmd2, delta, core.entry.arg);
        if (done) changed(row);
        return done;
    }

    void openPalette(int row, int col)
    {
        if (core.cols[size_t(col)].kind != Kind::Cmd) return;
        const int which = core.cols[size_t(col)].ch;
        showCommandPalette(owner, core.cellRect(row, col), [this, row, which](bool clear, bank::Cmd cmd) {
            auto& c = which == 0 ? table.steps[size_t(row)].cmd1 : table.steps[size_t(row)].cmd2;
            c = clear ? bank::Command{} : defaultCommand(cmd);
            core.entry.reset();
            changed(row);
        });
    }
};

TableGrid::TableGrid() : impl_(std::make_unique<Impl>(*this))
{
    setWantsKeyboardFocus(true);
    setSize(420, preferredHeight());
}
TableGrid::~TableGrid() = default;

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
            bool blank = false;
            const auto text = im.cellText(r, c, blank);
            if (core.cols[size_t(c)].kind == Kind::Info) {
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
    if (r != core.hoverRow || c != core.hoverCol) { core.hoverRow = r; core.hoverCol = c; repaint(); }
}
void TableGrid::mouseExit(const juce::MouseEvent&) { impl_->core.hoverRow = impl_->core.hoverCol = -1; repaint(); }
void TableGrid::mouseDown(const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    int r = 0, c = 0;
    if (impl_->core.cellAt(e.getPosition(), r, c) && impl_->core.editable(c)) { impl_->core.setCursor(r, c); repaint(); }
    if (e.mods.isPopupMenu()) impl_->openPalette(r, c);
}
void TableGrid::mouseDoubleClick(const juce::MouseEvent& e)
{
    int r = 0, c = 0;
    if (impl_->core.cellAt(e.getPosition(), r, c)) impl_->openPalette(r, c);
}
void TableGrid::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    auto& core = impl_->core;
    int r = 0, c = 0;
    if (!core.cellAt(e.getPosition(), r, c) || !core.editable(c)) return;
    core.wheelAcc += w.deltaY;
    const int steps = w.isSmooth ? int(core.wheelAcc / 0.1f) : (w.deltaY > 0.0f ? 1 : -1);
    if (w.isSmooth) core.wheelAcc -= float(steps) * 0.1f; else core.wheelAcc = 0.0f;
    if (steps == 0) return;
    core.setCursor(r, c);
    impl_->wheel(r, c, steps);
    repaint();
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
    PhraseGrid& owner;
    std::shared_ptr<const tracker::Song> song;
    int bar = 0;
    std::array<std::array<tracker::Cell, tracker::kSteps>, 4> cells{};
    std::array<int, 4> playing { -1, -1, -1, -1 };
    std::array<int, 4> rollNote { -1, -1, -1, -1 };
    std::array<std::array<int, tracker::kSteps>, 4> shadow{};   ///< the roll's notes as the bar played, greyed
    std::array<int, 4> groove{};
    std::array<bool, 4> trackerSource{};
    std::array<juce::Rectangle<int>, 4> grooveRects{};
    Segmented source[4];
    GridCore core;
    int octave = 4;
    int hoverGroove = -1;

    explicit Impl(PhraseGrid& o) : owner(o)
    {
        core.rows = tracker::kSteps; core.rowH = kRowHeight; core.headerH = kHeaderHeight;
        for (int ch = 0; ch < 4; ++ch) {
            source[ch].setOptions({ "Roll", "Trk" });
            source[ch].setMini(true);
            source[ch].setOptionTooltip(0, "The piano roll's notes, greyed. Commands sit next to the note they will hit.");
            source[ch].setOptionTooltip(1, "The tracker's own notes; incoming MIDI is ignored on this channel.");
            source[ch].onChange = [this, ch](int i) {
                trackerSource[size_t(ch)] = i == 1;
                buildColumns(owner.getWidth());
                owner.repaint();
                if (owner.onSourceChange) owner.onSourceChange(ch, i == 1 ? tracker::NoteSource::Tracker : tracker::NoteSource::PianoRoll);
            };
            owner.addAndMakeVisible(source[ch]);
        }
    }

    static const char* colTitle(int i) { static const char* t[5] = { "note", "ins", "tbl", "cmd", "cmd" }; return t[i]; }

    void buildColumns(int width)
    {
        auto& cols = core.cols;
        cols.clear();
        cols.push_back({ Kind::Step, 0, 0, 34, "Step" });
        const float weights[5] = { 1.15f, 0.9f, 0.9f, 1.5f, 1.5f };
        float total = 0.0f;
        for (float w : weights) total += w;
        const float unit = juce::jmax(36.0f, float(width - 34) / (4.0f * total));
        float x = 34.0f;
        for (int ch = 0; ch < 4; ++ch) {
            const float groupX = x;
            for (int i = 0; i < 5; ++i) {
                const Kind kinds[5] = { trackerSource[size_t(ch)] ? Kind::Note : Kind::Ghost, Kind::Inst, Kind::Table, Kind::Cmd, Kind::Cmd };
                const float w = unit * weights[i];
                cols.push_back({ kinds[i], ch, juce::roundToInt(x), juce::roundToInt(x + w) - juce::roundToInt(x), colTitle(i) });
                x += w;
            }
            layoutHeader(ch, juce::roundToInt(groupX), juce::roundToInt(x) - juce::roundToInt(groupX));
        }
        core.ensureEditableCursor();
    }
    void layoutHeader(int ch, int x, int w)
    {
        const int nameW = juce::roundToInt(draw::textWidth(Fonts::pixel(10.0f), colours::channelName(ch))) + 6;
        auto& seg = source[ch];
        seg.setBounds(x + 6 + nameW, 3, seg.preferredWidth(), 20);
        const int gx = seg.getRight() + 6;
        grooveRects[size_t(ch)] = { gx, 3, juce::jmin(64, juce::jmax(0, x + w - gx - 4)), 20 };
    }

    int cmdSlot(int col) const   // 0: cmd1, 1: cmd2
    {
        const auto& c = core.cols[size_t(col)];
        return (col >= 1 && c.kind == Kind::Cmd && core.cols[size_t(col - 1)].kind == Kind::Cmd) ? 1 : 0;
    }
    juce::String grooveText(int ch) const
    {
        const int g = groove[size_t(ch)];
        if (g <= 0 || song == nullptr || g > int(song->grooves.size())) return "6/6";
        const auto& gr = song->grooves[size_t(g - 1)];
        return ValueFormat::number(g) + juce::String::charToString(0x00b7) + juce::String(gr.a) + "/" + juce::String(gr.b);
    }

    void refreshFromSong()
    {
        for (int ch = 0; ch < 4; ++ch) {
            const auto* p = song != nullptr ? song->phrase(song->phraseAt(ch, bar)) : nullptr;
            cells[size_t(ch)] = p != nullptr ? p->steps : std::array<tracker::Cell, tracker::kSteps>{};
            groove[size_t(ch)] = p != nullptr ? p->groove : 0;
            trackerSource[size_t(ch)] = song != nullptr && song->noteSource[size_t(ch)] == tracker::NoteSource::Tracker;
            source[ch].setSelected(trackerSource[size_t(ch)] ? 1 : 0, juce::dontSendNotification);
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
        if (c.kind == Kind::Inst) { blank = cell.inst == 0; return blank ? kBlank2 : ValueFormat::number(cell.inst); }
        if (c.kind == Kind::Table) { blank = cell.table == 0; return blank ? kBlank2 : ValueFormat::number(cell.table); }
        if (c.kind == Kind::Cmd) { const auto& cmd = cmdSlot(col) == 0 ? cell.cmd1 : cell.cmd2; blank = cmd.cmd == bank::Cmd::None; return blank ? kBlank2 : cmdText(cmd); }
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
        else if (col.kind == Kind::Inst) done = editSlot(cell.inst, bank::kInstrumentSlots, k, core.entry);
        else if (col.kind == Kind::Table) done = editSlot(cell.table, bank::kTableSlots, k, core.entry);
        else if (col.kind == Kind::Cmd) done = editCmd(cmdSlot(core.curCol) == 0 ? cell.cmd1 : cell.cmd2, k, core.entry);
        if (done && !sameCell(before, cell)) changed(col.ch, core.curRow);
        return done;
    }

    bool wheel(int row, int col, int delta)
    {
        const auto& c = core.cols[size_t(col)];
        auto& cell = cells[size_t(c.ch)][size_t(row)];
        bool done = false;
        if (c.kind == Kind::Note) done = wheelNote(cell.note, delta);
        else if (c.kind == Kind::Inst) { const int v = juce::jlimit(0, bank::kInstrumentSlots, int(cell.inst) + delta); done = v != cell.inst; cell.inst = uint8_t(v); }
        else if (c.kind == Kind::Table) { const int v = juce::jlimit(0, bank::kTableSlots, int(cell.table) + delta); done = v != cell.table; cell.table = uint8_t(v); }
        else if (c.kind == Kind::Cmd) done = wheelCmd(cmdSlot(col) == 0 ? cell.cmd1 : cell.cmd2, delta, core.entry.arg);
        if (done) changed(c.ch, row);
        return done;
    }

    void openPalette(int row, int col)
    {
        const auto& c = core.cols[size_t(col)];
        if (c.kind != Kind::Cmd) return;
        const int ch = c.ch, slot = cmdSlot(col);
        showCommandPalette(owner, core.cellRect(row, col), [this, row, ch, slot](bool clear, bank::Cmd cmd) {
            auto& cell = cells[size_t(ch)][size_t(row)];
            (slot == 0 ? cell.cmd1 : cell.cmd2) = clear ? bank::Command{} : defaultCommand(cmd);
            core.entry.reset();
            changed(ch, row);
        });
    }

    void openGrooveMenu(int ch)
    {
        juce::PopupMenu m;
        m.addSectionHeader("Groove");
        m.addItem(1, "Straight (6/6)", true, groove[size_t(ch)] == 0);
        if (song != nullptr)
            for (int g = 1; g <= int(song->grooves.size()); ++g) {
                const auto& gr = song->grooves[size_t(g - 1)];
                m.addItem(1 + g, ValueFormat::number(g) + "  " + juce::String(gr.a) + " / " + juce::String(gr.b) + " ticks", true, groove[size_t(ch)] == g);
            }
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
        const int h1 = 26, h2 = core.headerH - h1;
        g.setColour(lineSoft);
        g.fillRect(0, core.headerH - 1, width, 1);
        for (int ch = 0; ch < 4; ++ch) {
            const int first = 1 + ch * 5;
            const int x = core.cols[size_t(first)].x;
            g.setColour(lineSoft);
            g.fillRect(x, 0, 1, core.headerH);
            g.setFont(Fonts::pixel(10.0f));
            g.setColour(channel(ch));
            g.drawText(channelName(ch), juce::Rectangle<int>(x + 6, 0, 40, h1), juce::Justification::centredLeft, false);
            const auto gr = grooveRects[size_t(ch)];
            if (gr.getWidth() > 24) {
                draw::panel(g, gr, hoverGroove == ch ? raisedHi : raised, line, 3.0f);
                g.setFont(Fonts::mono(10.5f));
                g.setColour(hoverGroove == ch ? text : textMute);
                g.drawText(grooveText(ch), gr.reduced(4, 0), juce::Justification::centred, false);
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
    setSize(1050, preferredHeight());
}
PhraseGrid::~PhraseGrid() = default;

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
int PhraseGrid::bar() const { return impl_->bar; }
void PhraseGrid::setPlayingStep(int ch, int step)
{
    if (ch < 0 || ch > 3) return;
    auto& im = *impl_;
    if (im.playing[size_t(ch)] == step) return;
    im.playing[size_t(ch)] = step;
    if (step >= 0 && step < tracker::kSteps && im.rollNote[size_t(ch)] > 0) im.shadow[size_t(ch)][size_t(step)] = im.rollNote[size_t(ch)];
    repaint();
}
void PhraseGrid::setRollNote(int ch, int midiNote)
{
    if (ch < 0 || ch > 3) return;
    auto& im = *impl_;
    im.rollNote[size_t(ch)] = midiNote;
    const int step = im.playing[size_t(ch)];
    if (midiNote > 0 && step >= 0 && step < tracker::kSteps && im.shadow[size_t(ch)][size_t(step)] != midiNote) {
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
        const int first = 1 + ch * 5;
        const int x = core.cols[size_t(first)].x, w = core.cols[size_t(first + 4)].x + core.cols[size_t(first + 4)].w - x;
        g.setColour(colours::playRow);
        g.fillRect(x, core.rowY(p), w, core.rowH);
    }
    core.paintRowLines(g, getWidth());
    for (int r = 0; r < core.rows; ++r) {
        bool anyPlaying = false;
        for (int ch = 0; ch < 4; ++ch) anyPlaying = anyPlaying || im.playing[size_t(ch)] == r;
        core.paintStep(g, r, anyPlaying);
        for (int c = 1; c < int(core.cols.size()); ++c) {
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
    int hg = -1;
    for (int ch = 0; ch < 4; ++ch) if (im.grooveRects[size_t(ch)].contains(e.getPosition())) hg = ch;
    if (r != core.hoverRow || c != core.hoverCol || hg != im.hoverGroove) { core.hoverRow = r; core.hoverCol = c; im.hoverGroove = hg; repaint(); }
}
void PhraseGrid::mouseExit(const juce::MouseEvent&) { impl_->core.hoverRow = impl_->core.hoverCol = -1; impl_->hoverGroove = -1; repaint(); }
void PhraseGrid::mouseDown(const juce::MouseEvent& e)
{
    auto& im = *impl_;
    grabKeyboardFocus();
    for (int ch = 0; ch < 4; ++ch)
        if (im.grooveRects[size_t(ch)].contains(e.getPosition())) { im.openGrooveMenu(ch); return; }
    int r = 0, c = 0;
    if (im.core.cellAt(e.getPosition(), r, c) && im.core.editable(c)) { im.core.setCursor(r, c); repaint(); }
    if (e.mods.isPopupMenu()) im.openPalette(r, c);
}
void PhraseGrid::mouseDoubleClick(const juce::MouseEvent& e)
{
    int r = 0, c = 0;
    if (impl_->core.cellAt(e.getPosition(), r, c)) impl_->openPalette(r, c);
}
void PhraseGrid::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    auto& core = impl_->core;
    int r = 0, c = 0;
    if (!core.cellAt(e.getPosition(), r, c) || !core.editable(c)) return;
    core.wheelAcc += w.deltaY;
    const int steps = w.isSmooth ? int(core.wheelAcc / 0.1f) : (w.deltaY > 0.0f ? 1 : -1);
    if (w.isSmooth) core.wheelAcc -= float(steps) * 0.1f; else core.wheelAcc = 0.0f;
    if (steps == 0) return;
    core.setCursor(r, c);
    impl_->wheel(r, c, steps);
    repaint();
}
bool PhraseGrid::keyPressed(const juce::KeyPress& k)
{
    auto& core = impl_->core;
    if (k.getKeyCode() == juce::KeyPress::escapeKey) { core.entry.reset(); return true; }
    if (k.getKeyCode() == juce::KeyPress::returnKey) { impl_->openPalette(core.curRow, core.curCol); return true; }
    if (core.navigate(k)) { repaint(); return true; }
    return impl_->edit(k);
}
void PhraseGrid::focusGained(FocusChangeType) { repaint(); }
void PhraseGrid::focusLost(FocusChangeType) { impl_->core.entry.reset(); repaint(); }

// ===========================================================================
// ChainStrip
// ===========================================================================
struct ChainStrip::Impl {
    ChainStrip& owner;
    std::shared_ptr<const tracker::Song> song;
    int selectedBar = 0, playingBar = -1;
    int firstBar = 0;
    int cursorCh = 0;
    int hoverCh = -1, hoverBar = -1;
    Entry entry;
    int dragStartFirst = 0;
    bool dragged = false;
    float wheelAcc = 0.0f;

    static constexpr int kLabelW = 60, kGap = 2, kMinCell = 40;

    explicit Impl(ChainStrip& o) : owner(o) {}

    int visibleBars() const { return juce::jmax(1, (owner.getWidth() - kLabelW) / (kMinCell + kGap)); }
    int cellW() const { const int n = visibleBars(); return (owner.getWidth() - kLabelW - kGap * (n - 1)) / n; }
    int songBars() const
    {
        size_t n = 0;
        if (song != nullptr) for (const auto& c : song->chain) n = juce::jmax(n, c.size());
        return int(n);
    }
    int barCount() const { return juce::jmax(songBars(), juce::jmax(selectedBar, playingBar) + 1) + 1; }
    int slotAt(int ch, int bar) const { return song != nullptr ? song->phraseAt(ch, bar) : 0; }

    juce::Rectangle<int> cellRect(int ch, int bar) const
    {
        const int i = bar - firstBar;
        return { kLabelW + i * (cellW() + kGap), kHeaderHeight + ch * (kRowHeight + kGap), cellW(), kRowHeight };
    }
    bool cellAt(juce::Point<int> p, int& ch, int& bar) const
    {
        if (p.x < kLabelW) return false;
        const int i = (p.x - kLabelW) / (cellW() + kGap);
        if (i < 0 || i >= visibleBars()) return false;
        bar = firstBar + i;
        if (bar >= barCount()) return false;
        if (p.y < kHeaderHeight) { ch = -1; return true; }
        ch = (p.y - kHeaderHeight) / (kRowHeight + kGap);
        return ch >= 0 && ch < 4;
    }
    void clampScroll()
    {
        firstBar = juce::jlimit(0, juce::jmax(0, barCount() - visibleBars()), firstBar);
    }
    void scrollToSelected()
    {
        if (selectedBar < firstBar) firstBar = selectedBar;
        else if (selectedBar >= firstBar + visibleBars()) firstBar = selectedBar - visibleBars() + 1;
        clampScroll();
    }
    void selectBar(int bar)
    {
        bar = juce::jmax(0, bar);
        const bool changed = bar != selectedBar;
        selectedBar = bar;
        entry.reset();
        scrollToSelected();
        owner.repaint();
        if (changed && owner.onSelectBar) owner.onSelectBar(bar);
    }
    void setSlot(int ch, int bar, int slot)
    {
        slot = juce::jlimit(0, tracker::kPhraseSlots, slot);
        if (slot == slotAt(ch, bar)) return;
        if (owner.onChainChange) owner.onChainChange(ch, bar, slot);
        owner.repaint();
    }
};

ChainStrip::ChainStrip() : impl_(std::make_unique<Impl>(*this))
{
    setWantsKeyboardFocus(true);
    setSize(600, preferredHeight());
}
ChainStrip::~ChainStrip() = default;

void ChainStrip::setSong(std::shared_ptr<const tracker::Song> song, int selectedBar, int playingBar)
{
    auto& im = *impl_;
    im.song = std::move(song);
    im.selectedBar = juce::jmax(0, selectedBar);
    im.playingBar = playingBar;
    im.scrollToSelected();
    repaint();
}
void ChainStrip::resized() { impl_->clampScroll(); }

void ChainStrip::paint(juce::Graphics& g)
{
    using namespace colours;
    auto& im = *impl_;
    const int n = im.visibleBars(), bars = im.barCount();
    const bool focused = hasKeyboardFocus(false);
    g.setFont(Fonts::mono(11.0f));
    for (int i = 0; i < n; ++i) {
        const int bar = im.firstBar + i;
        if (bar >= bars) break;
        const auto hr = im.cellRect(-1, bar).withY(0).withHeight(kHeaderHeight);
        g.setColour(bar == im.playingBar ? accentHi : textDim);
        g.setFont(Fonts::mono(11.0f));
        g.drawText("bar " + ValueFormat::number(bar + 1), hr, juce::Justification::centred, false);
    }
    for (int ch = 0; ch < 4; ++ch) {
        const auto lr = juce::Rectangle<int>(0, kHeaderHeight + ch * (kRowHeight + Impl::kGap), Impl::kLabelW, kRowHeight);
        g.setFont(Fonts::pixel(10.0f));
        g.setColour(channel(ch));
        g.drawText(channelName(ch), lr, juce::Justification::centredLeft, false);
        for (int i = 0; i < n; ++i) {
            const int bar = im.firstBar + i;
            if (bar >= bars) break;
            const auto r = im.cellRect(ch, bar);
            const int slot = im.slotAt(ch, bar);
            const bool cur = bar == im.selectedBar, empty = slot == 0, hover = ch == im.hoverCh && bar == im.hoverBar;
            const bool hasCursor = cur && ch == im.cursorCh;
            g.setColour(cur ? accentSoft : hover ? raised : panel2);
            g.fillRoundedRectangle(r.toFloat(), 3.0f);
            if (bar == im.playingBar && !cur) { g.setColour(playRow); g.fillRoundedRectangle(r.toFloat(), 3.0f); }
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
            g.setColour(empty ? textDim : cur ? text : textMute);
            g.drawText(empty ? juce::String::charToString(0x00b7) : ValueFormat::number(slot), r, juce::Justification::centred, false);
        }
    }
}

void ChainStrip::mouseMove(const juce::MouseEvent& e)
{
    auto& im = *impl_;
    int ch = -1, bar = -1;
    if (!im.cellAt(e.getPosition(), ch, bar) || ch < 0) { ch = -1; bar = -1; }
    if (ch != im.hoverCh || bar != im.hoverBar) { im.hoverCh = ch; im.hoverBar = bar; repaint(); }
}
void ChainStrip::mouseExit(const juce::MouseEvent&) { impl_->hoverCh = impl_->hoverBar = -1; repaint(); }
void ChainStrip::mouseDown(const juce::MouseEvent& e)
{
    auto& im = *impl_;
    grabKeyboardFocus();
    im.dragStartFirst = im.firstBar;
    im.dragged = false;
    int ch = -1, bar = -1;
    if (im.cellAt(e.getPosition(), ch, bar)) {
        if (ch >= 0) im.cursorCh = ch;
        im.selectBar(bar);
    }
}
void ChainStrip::mouseDrag(const juce::MouseEvent& e)
{
    auto& im = *impl_;
    const int dx = e.getDistanceFromDragStartX();
    if (std::abs(dx) < 6 && !im.dragged) return;
    im.dragged = true;
    const int first = im.dragStartFirst - dx / (im.cellW() + Impl::kGap);
    if (first != im.firstBar) { im.firstBar = first; im.clampScroll(); repaint(); }
}
void ChainStrip::mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    auto& im = *impl_;
    const float d = std::abs(w.deltaX) > 1.0e-6f ? -w.deltaX : w.deltaY;
    im.wheelAcc += d;
    const int steps = w.isSmooth ? int(im.wheelAcc / 0.1f) : (d > 0.0f ? 1 : d < 0.0f ? -1 : 0);
    if (w.isSmooth) im.wheelAcc -= float(steps) * 0.1f; else im.wheelAcc = 0.0f;
    if (steps == 0) return;
    im.firstBar -= steps;
    im.clampScroll();
    repaint();
}
bool ChainStrip::keyPressed(const juce::KeyPress& k)
{
    auto& im = *impl_;
    const int code = k.getKeyCode();
    if (code == juce::KeyPress::leftKey) { im.selectBar(im.selectedBar - 1); return true; }
    if (code == juce::KeyPress::rightKey) { im.selectBar(im.selectedBar + 1); return true; }
    if (code == juce::KeyPress::homeKey) { im.selectBar(0); return true; }
    if (code == juce::KeyPress::endKey) { im.selectBar(juce::jmax(0, im.songBars() - 1)); return true; }
    if (code == juce::KeyPress::upKey) { im.cursorCh = juce::jmax(0, im.cursorCh - 1); im.entry.reset(); repaint(); return true; }
    if (code == juce::KeyPress::downKey) { im.cursorCh = juce::jmin(3, im.cursorCh + 1); im.entry.reset(); repaint(); return true; }
    if (code == juce::KeyPress::escapeKey) { im.entry.reset(); return true; }
    const int cur = im.slotAt(im.cursorCh, im.selectedBar);
    if (isBlankKey(k)) { im.setSlot(im.cursorCh, im.selectedBar, 0); return true; }
    if (isPlus(k)) { im.setSlot(im.cursorCh, im.selectedBar, cur + 1); return true; }
    if (isMinus(k)) { im.setSlot(im.cursorCh, im.selectedBar, cur - 1); return true; }
    int mag = 0;
    if (typeDigit(im.entry, k.getTextCharacter(), ValueFormat::hex(), tracker::kPhraseSlots, mag)) { im.setSlot(im.cursorCh, im.selectedBar, mag); return true; }
    return false;
}
void ChainStrip::focusGained(FocusChangeType) { repaint(); }
void ChainStrip::focusLost(FocusChangeType) { impl_->entry.reset(); repaint(); }

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
