#include "plugin/ui/GrooveEditor.h"

#include <cmath>

namespace chipboy::ui {

namespace {

constexpr int kMaxTick = 48;          ///< a groove entry is 1-48, 0 unused (section 9.2)
constexpr int kCellX = 4, kCellW = 38;
constexpr int kBarX = 48, kBarRight = 118;
constexpr int kStartX = 124, kStartW = 34;
constexpr int kNudgeW = 20, kNudgeH = 18, kNudgeY = 28;
constexpr int kHeadRow = 26;          ///< as the grid's header splits: name row, caption row
constexpr float kPixelsPerTick = 6.0f;   ///< a value drag, as the knob's travel reads

/// Whole steps from a wheel, as the other controls read it: a notch is a
/// step, a trackpad accumulates.
int wheelSteps(const juce::MouseWheelDetails& w, float& acc)
{
    if (std::abs(w.deltaY) < 1.0e-6f) return 0;
    if (!w.isSmooth) { acc = 0.0f; return w.deltaY > 0.0f ? 1 : -1; }
    acc += w.deltaY;
    const int steps = int(acc / 0.1f);
    acc -= float(steps) * 0.1f;
    return steps;
}

/// Multi-digit typing, the grids' convention: a digit that would overflow
/// starts a new entry, and two digits fill a cell in either display base.
struct Entry {
    int acc = 0, count = 0;
    void reset() { acc = 0; count = 0; }
    bool type(juce::juce_wchar ch, int hi, int& out)
    {
        const bool hex = ValueFormat::hex();
        int d = -1;
        if (ch >= '0' && ch <= '9') d = int(ch - '0');
        else if (hex && ch >= 'a' && ch <= 'f') d = int(ch - 'a') + 10;
        else if (hex && ch >= 'A' && ch <= 'F') d = int(ch - 'A') + 10;
        if (d < 0) return false;
        int v = acc * (hex ? 16 : 10) + d;
        if (v > hi) { v = juce::jmin(hi, d); count = 0; }
        acc = v;
        ++count;
        out = v;
        if (count >= 2) reset();
        return true;
    }
};

const juce::String kBlank2 = "--";

} // namespace

struct GrooveEditor::Impl {
    GrooveEditor& owner;
    std::shared_ptr<const tracker::Song> song;
    Stepper slotStepper;
    int slot = 0;                 ///< 0 straight (read-only), 1-16 the song's
    int barTicks = driver::kTicksPerBeat * 4;
    int playing = -1;
    int cursor = 0, hover = -1, hoverNudge = -1;
    int dragRow = -1, dragFrom = 0;
    Entry entry;
    float wheelAcc = 0.0f;

    explicit Impl(GrooveEditor& o) : owner(o)
    {
        slotStepper.setRange(0, 16, 0);
        slotStepper.setTooltip("Which groove the editor is showing: 0 is straight and cannot be edited, 1-16 are the song's. It follows the selected channel's phrase until you browse.");
        slotStepper.setTextFunction([](int v) { return v == 0 ? juce::String("0 str") : ValueFormat::number(v); });
        slotStepper.onChange = [this](int v) { slot = juce::jlimit(0, 16, v); entry.reset(); owner.repaint(); };
        owner.addAndMakeVisible(slotStepper);
    }

    bool editable() const { return slot >= 1 && slot <= 16; }
    int steps() const { return song != nullptr ? song->steps() : tracker::kSteps; }
    /// Eight steps per bar doubles every entry, so a groove swings the same
    /// way whichever the song is set to (section 9.2).
    int scale() const { return steps() <= 8 ? 2 : 1; }

    tracker::Groove groove() const
    {
        if (song != nullptr && editable()) return song->grooves[size_t(slot - 1)];
        return tracker::Groove{};
    }
    /// The grid the Player runs on, from the core, so the editor cannot
    /// disagree with playback about where a step starts.
    void startTicks(int* start) const
    {
        if (song != nullptr) { tracker::stepStartTicks(*song, nullptr, uint8_t(slot), start); return; }
        for (int i = 0; i <= tracker::kSteps; ++i) start[i] = i * 6;
    }
    int total() const { int start[tracker::kSteps + 1]; startTicks(start); return start[steps()]; }
    /// LSDj's swing: what the first of the pair takes of the two (section 10).
    int swingPercent() const
    {
        const auto g = groove();
        if (g.length() < 2) return -1;
        const int a = g.ticks[0], b = g.ticks[1];
        return a + b > 0 ? 100 * a / (a + b) : -1;
    }

    juce::Rectangle<int> cellRect(int row) const { return { kCellX, kHeaderHeight + row * kRowHeight, kCellW, kRowHeight }; }
    juce::Rectangle<int> nudgeRect(int i) const { return { owner.getWidth() - 6 - (2 - i) * kNudgeW - (1 - i) * 2, kNudgeY, kNudgeW, kNudgeH }; }
    int rowAt(juce::Point<int> p) const
    {
        if (p.y < kHeaderHeight) return -1;
        const int row = (p.y - kHeaderHeight) / kRowHeight;
        return row >= 0 && row < tracker::kSteps ? row : -1;
    }

    void write(const tracker::Groove& g)
    {
        if (!editable()) return;
        if (owner.onChange) owner.onChange(slot, g);
        owner.repaint();
    }
    bool setTick(int row, int value)
    {
        if (!editable() || row < 0 || row >= tracker::kSteps) return false;
        auto g = groove();
        const auto v = uint8_t(juce::jlimit(0, kMaxTick, value));
        if (g.ticks[size_t(row)] == v) return true;
        g.ticks[size_t(row)] = v;
        write(g);
        return true;
    }
    /// One tick across every pair, the total of each pair kept: 6 6 -> 7 5 ->
    /// 8 4, and back the other way; no entry goes below one (section 10).
    void nudge(int direction)
    {
        if (!editable()) return;
        auto g = groove();
        const int n = g.length();
        bool moved = false;
        for (int i = 0; i + 1 < n; i += 2) {
            const int a = g.ticks[size_t(i)] + direction, b = g.ticks[size_t(i + 1)] - direction;
            if (a < 1 || b < 1 || a > kMaxTick || b > kMaxTick) continue;
            g.ticks[size_t(i)] = uint8_t(a);
            g.ticks[size_t(i + 1)] = uint8_t(b);
            moved = true;
        }
        if (moved) write(g);
    }

    void paintHead(juce::Graphics& g)
    {
        using namespace colours;
        const int w = owner.getWidth();
        g.setColour(lineSoft);
        g.fillRect(1, kHeaderHeight - 1, w - 2, 1);
        draw::caption(g, "Groove", { 6, 0, w - 12, kHeadRow }, juce::Justification::centredLeft, textDim, 10.0f);

        const int t = total(), bars = juce::jmax(1, barTicks);
        g.setFont(Fonts::mono(11.0f));
        g.setColour(t == bars ? ok : warn);
        const juce::String totalText = ValueFormat::number(t) + " / " + ValueFormat::number(bars);
        g.drawFittedText(totalText, juce::Rectangle<int>(6, kHeadRow, 52, kRowHeight), juce::Justification::centredLeft, 1, 0.8f);
        g.setColour(textMute);
        const int swing = swingPercent();
        g.drawFittedText(editable() ? (swing >= 0 ? juce::String(swing) + " %" : juce::String("--")) : juce::String("straight"),
                         juce::Rectangle<int>(60, kHeadRow, 54, kRowHeight), juce::Justification::centredLeft, 1, 0.8f);

        for (int i = 0; i < 2; ++i) {
            const auto r = nudgeRect(i);
            const bool live = editable();
            draw::panel(g, r, hoverNudge == i && live ? raisedHi : raised, line, 3.0f);
            g.setColour((hoverNudge == i && live ? text : textMute).withMultipliedAlpha(live ? 1.0f : 0.4f));
            g.setFont(Fonts::mono(9.0f));
            g.drawText(juce::String::charToString(i == 0 ? 0x25c0 : 0x25b6), r, juce::Justification::centred, false);
        }
    }

    void paintRows(juce::Graphics& g)
    {
        using namespace colours;
        const auto gr = groove();
        const int n = gr.length(), st = steps(), sc = scale(), w = owner.getWidth();
        int start[tracker::kSteps + 1];
        startTicks(start);
        int peak = 1;
        for (int i = 0; i < st; ++i) peak = juce::jmax(peak, gr.at(i) * sc);
        const bool live = editable();
        const bool focused = owner.hasKeyboardFocus(false);

        for (int row = 0; row < tracker::kSteps; ++row) {
            const int y = kHeaderHeight + row * kRowHeight;
            if (row == playing) { g.setColour(playRow); g.fillRect(1, y, w - 2, kRowHeight); }
            g.setColour(lineSoft);
            g.fillRect(1, y + kRowHeight - 1, w - 2, 1);

            const bool inPattern = row < n;
            const bool plays = row < st && start[row] < barTicks;
            const auto cell = cellRect(row);
            if (hover == row && live) { g.setColour(raised); g.fillRect(cell.withTrimmedBottom(1)); }
            g.setFont(Fonts::mono(12.0f));
            g.setColour(inPattern ? (live ? text : textMute) : textDim);
            g.drawText(inPattern ? ValueFormat::number(gr.ticks[size_t(row)]).paddedLeft('0', 2) : kBlank2, cell.withTrimmedLeft(6), juce::Justification::centredLeft, false);
            if (row == cursor && live) {
                g.setColour(accentHi.withAlpha(focused ? 0.95f : 0.45f));
                g.drawRect(cell.reduced(1), focused ? 2 : 1);
            }

            if (row >= st) continue;                       // that step is not in the bar's grid
            const int ticks = gr.at(row) * sc;
            const auto track = juce::Rectangle<int>(kBarX, y + kRowHeight / 2 - 4, kBarRight - kBarX, 7);
            g.setColour(well);
            g.fillRoundedRectangle(track.toFloat(), 2.0f);
            if (plays) {
                const int len = juce::jmax(2, juce::roundToInt(float(track.getWidth()) * float(ticks) / float(peak)));
                g.setColour((inPattern ? textMute : textDim).withAlpha(inPattern ? 0.75f : 0.45f));
                g.fillRoundedRectangle(track.withWidth(len).toFloat(), 2.0f);
            }
            g.setFont(Fonts::mono(10.0f));
            g.setColour(plays ? textDim : warn);
            g.drawText(ValueFormat::number(start[row]), juce::Rectangle<int>(kStartX, y, kStartW, kRowHeight), juce::Justification::centredRight, false);
        }
    }
};

GrooveEditor::GrooveEditor() : impl_(std::make_unique<Impl>(*this))
{
    setWantsKeyboardFocus(true);
    setSize(kWidth, preferredHeight());
}
GrooveEditor::~GrooveEditor() = default;

void GrooveEditor::setSong(std::shared_ptr<const tracker::Song> song)
{
    impl_->song = std::move(song);
    repaint();
}
void GrooveEditor::setSlot(int slot)
{
    const int v = juce::jlimit(0, 16, slot);
    if (v == impl_->slot) return;
    impl_->slot = v;
    impl_->entry.reset();
    impl_->slotStepper.setValue(v, juce::dontSendNotification);
    repaint();
}
int GrooveEditor::slot() const { return impl_->slot; }
void GrooveEditor::setBarTicks(int ticks)
{
    const int t = juce::jmax(1, ticks);
    if (t == impl_->barTicks) return;
    impl_->barTicks = t;
    repaint();
}
void GrooveEditor::setPlayingStep(int step)
{
    const int s = step >= 0 && step < tracker::kSteps ? step : -1;
    if (s == impl_->playing) return;
    impl_->playing = s;
    repaint();
}

juce::String GrooveEditor::getTooltip()
{
    auto& im = *impl_;
    if (im.hoverNudge >= 0)
        return "Move one tick between the entries of every pair, keeping each pair's total: 6 6, 7 5, 8 4. The left and right arrow keys do the same.";
    if (im.hover >= 0) {
        if (!im.editable()) return "Groove 0 is straight, six ticks a step, and cannot be edited. Browse to 1-16 to edit one of the song's.";
        const auto g = im.groove();
        const int row = im.hover;
        juce::String s = "How many ticks step " + ValueFormat::number(row + 1) + " lasts, 1-48. ";
        s += row < g.length() ? juce::String("Type two digits, or use the wheel, a drag, or + and -; Backspace ends the groove here.")
                              : "Blank: the groove is " + juce::String(g.length()) + " entries long and repeats from the top here.";
        return s;
    }
    return "The groove: how many ticks each step of a phrase lasts (docs/COMMANDS_AND_TEMPO.md 9.2). The total is measured against the bar's ticks.";
}

void GrooveEditor::resized()
{
    auto& im = *impl_;
    im.slotStepper.setBounds(getWidth() - 6 - im.slotStepper.preferredWidth(), 1, im.slotStepper.preferredWidth(), Stepper::kHeight);
}

void GrooveEditor::paint(juce::Graphics& g)
{
    draw::panel(g, getLocalBounds(), colours::panel2, colours::line, 4.0f);
    impl_->paintHead(g);
    impl_->paintRows(g);
}

void GrooveEditor::mouseMove(const juce::MouseEvent& e)
{
    auto& im = *impl_;
    const int row = im.rowAt(e.getPosition());
    int nudge = -1;
    for (int i = 0; i < 2; ++i) if (im.nudgeRect(i).contains(e.getPosition())) nudge = i;
    if (row != im.hover || nudge != im.hoverNudge) { im.hover = row; im.hoverNudge = nudge; repaint(); }
}
void GrooveEditor::mouseExit(const juce::MouseEvent&)
{
    impl_->hover = impl_->hoverNudge = -1;
    repaint();
}
void GrooveEditor::mouseDown(const juce::MouseEvent& e)
{
    auto& im = *impl_;
    grabKeyboardFocus();
    im.dragRow = -1;
    for (int i = 0; i < 2; ++i)
        if (im.nudgeRect(i).contains(e.getPosition())) { im.nudge(i == 0 ? -1 : 1); return; }
    const int row = im.rowAt(e.getPosition());
    if (row < 0) return;
    if (row != im.cursor) { im.cursor = row; im.entry.reset(); }
    im.dragRow = row;
    im.dragFrom = im.groove().ticks[size_t(row)];
    repaint();
}
void GrooveEditor::mouseDrag(const juce::MouseEvent& e)
{
    auto& im = *impl_;
    if (im.dragRow < 0 || !im.editable()) return;
    im.entry.reset();
    im.setTick(im.dragRow, im.dragFrom + juce::roundToInt(-float(e.getDistanceFromDragStartY()) / kPixelsPerTick));
}
void GrooveEditor::mouseDoubleClick(const juce::MouseEvent& e)
{
    // The cell takes typed digits as the grids do, so a double click is the
    // invitation to type rather than a second gesture of its own.
    auto& im = *impl_;
    const int row = im.rowAt(e.getPosition());
    if (row < 0) return;
    im.cursor = row;
    im.entry.reset();
    grabKeyboardFocus();
    repaint();
}
void GrooveEditor::mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    auto& im = *impl_;
    const int row = im.rowAt(e.getPosition());
    if (row < 0 || !im.editable()) return;
    const int steps = wheelSteps(w, im.wheelAcc);
    if (steps == 0) return;
    im.cursor = row;
    im.entry.reset();
    im.setTick(row, im.groove().ticks[size_t(row)] + steps);
}

bool GrooveEditor::keyPressed(const juce::KeyPress& k)
{
    auto& im = *impl_;
    const int code = k.getKeyCode();
    if (code == juce::KeyPress::escapeKey) { im.entry.reset(); return true; }
    if (code == juce::KeyPress::downKey || code == juce::KeyPress::tabKey) {
        const int dir = code == juce::KeyPress::tabKey && k.getModifiers().isShiftDown() ? -1 : 1;
        im.cursor = juce::jlimit(0, tracker::kSteps - 1, im.cursor + dir);
        im.entry.reset(); repaint(); return true;
    }
    if (code == juce::KeyPress::upKey) { im.cursor = juce::jmax(0, im.cursor - 1); im.entry.reset(); repaint(); return true; }
    if (code == juce::KeyPress::homeKey) { im.cursor = 0; im.entry.reset(); repaint(); return true; }
    if (code == juce::KeyPress::endKey) { im.cursor = tracker::kSteps - 1; im.entry.reset(); repaint(); return true; }
    if (!im.editable()) return false;
    // Nothing to move through sideways in one column, so the arrows are the
    // nudge the head's buttons make.
    if (code == juce::KeyPress::leftKey) { im.nudge(-1); return true; }
    if (code == juce::KeyPress::rightKey) { im.nudge(1); return true; }
    const int cur = im.groove().ticks[size_t(im.cursor)];
    if (code == juce::KeyPress::backspaceKey || code == juce::KeyPress::deleteKey) { im.entry.reset(); return im.setTick(im.cursor, 0); }
    const auto ch = k.getTextCharacter();
    if (ch == '+' || ch == '=') { im.entry.reset(); return im.setTick(im.cursor, cur + 1); }
    if (ch == '-') { im.entry.reset(); return im.setTick(im.cursor, cur - 1); }
    int typed = 0;
    if (im.entry.type(ch, kMaxTick, typed)) return im.setTick(im.cursor, typed);
    return false;
}
void GrooveEditor::focusGained(FocusChangeType) { repaint(); }
void GrooveEditor::focusLost(FocusChangeType) { impl_->entry.reset(); repaint(); }

} // namespace chipboy::ui
