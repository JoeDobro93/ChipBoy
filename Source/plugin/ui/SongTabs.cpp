// ChipBoy -- the song tabs (docs/COMMANDS_AND_TEMPO.md section 18,
// UI_DESIGN section 7): the strip that stands where the Tracker tab's
// summary line was, one tab per loaded song.
//
// A tab is a song and the bank it plays through, and only the active one is
// live: it is what plays and records, and it is what the Instrument, Tables,
// Grooves, Waves and Kits tabs show. The widget itself knows none of that --
// it draws a list of names, says which one was clicked, and leaves the
// processor to the panel.
#include "plugin/ui/Widgets.h"

#include <algorithm>

namespace chipboy::ui {

namespace {
/// A tab is as wide as its name asks, between these; the + is square. When
/// more tabs are open than the strip can hold at kMin they share it evenly
/// and the names are squeezed, which is still a row of readable tabs.
constexpr int kMinTab = 76, kMaxTab = 190, kPlus = 26, kGap = 3;
constexpr int kNamePad = 10, kDot = 6, kClose = 14;
}

struct SongTabStrip::Impl {
    SongTabStrip& owner;
    std::vector<SongTabInfo> tabs;
    std::vector<juce::Rectangle<int>> rects;   ///< one per tab, then the +
    int active = 0;
    int hover = -1;         ///< index of the tab under the mouse, tabs.size() = the +
    bool hoverClose = false;

    explicit Impl(SongTabStrip& o) : owner(o) {}

    juce::Rectangle<int> closeRect(int i) const
    {
        if (i < 0 || i >= int(rects.size()) || i >= int(tabs.size())) return {};
        const auto r = rects[size_t(i)];
        return { r.getRight() - kClose - 5, r.getY() + (r.getHeight() - kClose) / 2, kClose, kClose };
    }

    void layout()
    {
        rects.clear();
        const int n = int(tabs.size());
        const int total = owner.getWidth();
        const int room = total - kPlus - kGap;
        int w = kMaxTab;
        if (n > 0) {
            int wanted = 0;
            for (const auto& t : tabs)
                wanted = std::max(wanted, juce::roundToInt(draw::textWidth(Fonts::sans(12.0f), t.name)) + kNamePad * 2 + kDot + 4 + kClose + 6);
            w = std::clamp(wanted, kMinTab, kMaxTab);
            const int fits = (room - (n - 1) * kGap) / std::max(1, n);
            w = std::min(w, std::max(28, fits));
        }
        int x = 0;
        for (int i = 0; i < n; ++i) { rects.push_back({ x, 0, w, owner.getHeight() }); x += w + kGap; }
        rects.push_back({ x, 0, kPlus, owner.getHeight() });   // the + tab, right after the last one
    }

    int indexAt(juce::Point<int> p) const
    {
        for (int i = 0; i < int(rects.size()); ++i) if (rects[size_t(i)].contains(p)) return i;
        return -1;
    }
};

SongTabStrip::SongTabStrip() : impl_(std::make_unique<Impl>(*this))
{
    setSize(600, kHeight);
}
SongTabStrip::~SongTabStrip() = default;

void SongTabStrip::setTabs(const std::vector<SongTabInfo>& tabs, int active)
{
    auto& im = *impl_;
    if (tabs == im.tabs && active == im.active) return;
    const bool count = tabs.size() != im.tabs.size();
    im.tabs = tabs;
    im.active = active;
    if (count) { im.hover = -1; im.hoverClose = false; }
    im.layout();
    repaint();
}

juce::String SongTabStrip::getTooltip()
{
    auto& im = *impl_;
    if (im.hover < 0) return "The songs open in this window. Only the active tab plays, records and is what the other tabs edit.";
    if (im.hover >= int(im.tabs.size()))
        return "A new song: sixteen empty bars on the factory bank, in a tab of its own.";
    const auto& t = im.tabs[size_t(im.hover)];
    if (im.hoverClose) return "Close " + t.name + (t.dirty ? ". It has unsaved work, so this asks first." : "");
    return t.name + (t.tip.isEmpty() ? juce::String() : juce::String(juce::CharPointer_UTF8(" \xe2\x80\x94 ")) + t.tip)
         + (t.dirty ? juce::String(juce::CharPointer_UTF8(" \xe2\x80\x94 ")) + "edited since it was saved" : juce::String());
}

void SongTabStrip::paint(juce::Graphics& g)
{
    using namespace colours;
    auto& im = *impl_;
    if (im.rects.empty()) im.layout();
    const int n = int(im.tabs.size());
    g.setColour(lineSoft);
    g.fillRect(0, getHeight() - 1, getWidth(), 1);

    for (int i = 0; i < n && i < int(im.rects.size()); ++i) {
        const auto r = im.rects[size_t(i)];
        const bool on = i == im.active, hov = i == im.hover;
        auto body = r.withTrimmedBottom(1).toFloat();
        g.setColour(on ? panel : (hov ? raised : panel2));
        g.fillRoundedRectangle(body, 4.0f);
        g.setColour(on ? accent : lineSoft);
        g.drawRoundedRectangle(body.reduced(0.5f), 4.0f, 1.0f);
        if (on) g.fillRect(float(r.getX()) + 4.0f, float(r.getBottom()) - 2.0f, float(r.getWidth()) - 8.0f, 2.0f);

        int tx = r.getX() + kNamePad;
        if (im.tabs[size_t(i)].dirty) {                       // unsaved work
            g.setColour(on ? accentHi : textMute);
            g.fillEllipse(float(tx), float(r.getCentreY() - kDot / 2), float(kDot), float(kDot));
            tx += kDot + 5;
        }
        const auto close = im.closeRect(i);
        g.setColour(on ? text : textMute);
        g.setFont(Fonts::sans(12.0f, on));
        g.drawFittedText(im.tabs[size_t(i)].name, juce::Rectangle<int>(tx, r.getY(), close.getX() - 4 - tx, r.getHeight()),
                         juce::Justification::centredLeft, 1, 0.7f);
        const bool hovClose = hov && im.hoverClose;
        if (hovClose) { g.setColour(raisedHi); g.fillRoundedRectangle(close.toFloat(), 3.0f); }
        g.setColour(hovClose ? accentHi : (on ? textMute : textDim));
        g.setFont(Fonts::sans(12.0f));
        g.drawText(juce::String::charToString(0x00d7), close, juce::Justification::centred, false);
    }

    // the + tab: a new empty song on the factory bank
    if (!im.rects.empty()) {
        const auto r = im.rects.back();
        const bool hov = im.hover == n;
        auto body = r.withTrimmedBottom(1).toFloat();
        g.setColour(hov ? raised : panel2);
        g.fillRoundedRectangle(body, 4.0f);
        g.setColour(lineSoft);
        g.drawRoundedRectangle(body.reduced(0.5f), 4.0f, 1.0f);
        g.setColour(hov ? text : textMute);
        g.setFont(Fonts::sans(14.0f));
        g.drawText("+", r, juce::Justification::centred, false);
    }
}

void SongTabStrip::resized() { impl_->layout(); }

void SongTabStrip::mouseMove(const juce::MouseEvent& e)
{
    auto& im = *impl_;
    const int i = im.indexAt(e.getPosition());
    const bool close = i >= 0 && i < int(im.tabs.size()) && im.closeRect(i).contains(e.getPosition());
    if (i == im.hover && close == im.hoverClose) return;
    im.hover = i;
    im.hoverClose = close;
    repaint();
}

void SongTabStrip::mouseExit(const juce::MouseEvent&)
{
    auto& im = *impl_;
    if (im.hover < 0 && !im.hoverClose) return;
    im.hover = -1;
    im.hoverClose = false;
    repaint();
}

void SongTabStrip::mouseDown(const juce::MouseEvent& e)
{
    auto& im = *impl_;
    const int i = im.indexAt(e.getPosition());
    if (i < 0) return;
    if (i >= int(im.tabs.size())) { if (onNew) onNew(); return; }
    if (im.closeRect(i).contains(e.getPosition())) { if (onClose) onClose(i); return; }
    if (onActivate) onActivate(i);
}

} // namespace chipboy::ui
