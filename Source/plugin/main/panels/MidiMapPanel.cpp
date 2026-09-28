#include "plugin/main/panels/MidiMapPanel.h"

namespace chipboy::plugin {

using namespace juce;
using namespace chipboy::ui;

namespace {
constexpr int kListWidth = 220, kGap = 14, kListHeader = 28;
constexpr int kBarHeight = 30, kRowGap = 10;
String middot() { return String(CharPointer_UTF8(" \xc2\xb7 ")); }
const char* targetName(int t) { return t < 0 ? "\xe2\x80\x94" : colours::channelName(t); }
Colour targetColour(int t) { return t < 0 ? colours::textDim : colours::channel(t); }
}

/// The regions across 1-127: a band per region in the target's colour,
/// numbered, its `from` at the left. Drag a boundary to move it between its
/// neighbours; the drag is one undo.
class MidiMapPanel::VelocityBar : public Component, public SettableTooltipClient {
public:
    std::function<void(int region, int from)> onMove;   ///< region i's new `from`
    std::function<void(bool begin)> onGesture;
    void set(const std::vector<tracker::MidiRegion>& r, int target) { regions_ = r; target_ = target; repaint(); }
    void paint(Graphics& g) override
    {
        const auto area = getLocalBounds().toFloat();
        g.setColour(colours::well);
        g.fillRoundedRectangle(area, 4.0f);
        const int n = int(regions_.size());
        for (int i = 0; i < n; ++i) {
            const int from = int(regions_[size_t(i)].from), to = i + 1 < n ? int(regions_[size_t(i + 1)].from) - 1 : 127;
            const float x0 = xOf(from), x1 = xOf(to + 1);
            const Colour c = targetColour(target_);
            g.setColour(c.withAlpha(i % 2 ? 0.30f : 0.42f));
            g.fillRect(Rectangle<float>(x0, area.getY() + 3.0f, x1 - x0, area.getHeight() - 6.0f));
            g.setFont(Fonts::mono(10.0f));
            g.setColour(colours::text);
            const auto label = Rectangle<float>(x0 + 4.0f, area.getY(), jmax(0.0f, x1 - x0 - 8.0f), area.getHeight());
            g.drawText(String(i + 1) + "  " + String(from) + String::charToString(0x2013) + String(to), label, Justification::centredLeft, true);   // decimal in either display base (D-UI-43)
            if (i > 0) {
                g.setColour(i == hover_ || i == drag_ ? colours::accentHi : colours::line);
                g.fillRect(Rectangle<float>(x0 - 1.0f, area.getY() + 1.0f, 2.0f, area.getHeight() - 2.0f));
            }
        }
        g.setColour(colours::line);
        g.drawRoundedRectangle(area.reduced(0.5f), 4.0f, 1.0f);
    }
    void mouseMove(const MouseEvent& e) override { const int b = boundaryAt(e.x); if (b != hover_) { hover_ = b; setMouseCursor(b > 0 ? MouseCursor::LeftRightResizeCursor : MouseCursor::NormalCursor); repaint(); } }
    void mouseExit(const MouseEvent&) override { hover_ = -1; repaint(); }
    void mouseDown(const MouseEvent& e) override { drag_ = boundaryAt(e.x); if (drag_ > 0 && onGesture) onGesture(true); }
    void mouseDrag(const MouseEvent& e) override
    {
        if (drag_ <= 0 || drag_ >= int(regions_.size())) return;
        const int lo = int(regions_[size_t(drag_ - 1)].from) + 1;
        const int hi = drag_ + 1 < int(regions_.size()) ? int(regions_[size_t(drag_ + 1)].from) - 1 : 127;
        const int v = jlimit(lo, hi, velocityAt(e.x));
        if (v != int(regions_[size_t(drag_)].from) && onMove) onMove(drag_, v);
    }
    void mouseUp(const MouseEvent&) override { if (drag_ > 0 && onGesture) onGesture(false); drag_ = -1; repaint(); }
private:
    float xOf(int velocity) const { return 1.0f + (float(getWidth()) - 2.0f) * float(velocity - 1) / 127.0f; }
    int velocityAt(int x) const { return jlimit(1, 127, 1 + roundToInt(float(x - 1) / jmax(1.0f, float(getWidth()) - 2.0f) * 127.0f)); }
    int boundaryAt(int x) const
    {
        for (int i = 1; i < int(regions_.size()); ++i) if (std::abs(xOf(int(regions_[size_t(i)].from)) - float(x)) <= 5.0f) return i;
        return -1;
    }
    std::vector<tracker::MidiRegion> regions_;
    int target_ = -1, hover_ = -1, drag_ = -1;
};

MidiMapPanel::MidiMapPanel(ChipBoyProcessor& p)
    : EditorPanel(p),
      listTitle_("MIDI channels" + middot() + "16 a port", Fonts::sans(11.0f), colours::textMute),
      on_("Route MIDI through the map"),
      head_("MIDI channel 1", Fonts::sans(13.0f), colours::text),
      targetLabel_("plays", Fonts::sans(12.0f), colours::textMute),
      target_(StringArray{ "Off", "PU1", "PU2", "WAV", "NOI" }),
      addRegion_("+ region"), removeRegion_(String(CharPointer_UTF8("\xe2\x88\x92")) + " region"),
      bar_(std::make_unique<VelocityBar>())
{
    addAndMakeVisible(list_);
    addAndMakeVisible(listTitle_);
    addAndMakeVisible(on_);
    addAndMakeVisible(head_);
    addAndMakeVisible(targetLabel_);
    addAndMakeVisible(target_);
    addAndMakeVisible(addRegion_);
    addAndMakeVisible(removeRegion_);
    addAndMakeVisible(*bar_);
    gridView_.setViewedComponent(&grid_, false);
    gridView_.setScrollBarsShown(true, false, true, false);
    gridView_.setScrollBarThickness(8);
    addAndMakeVisible(gridView_);

    list_.setRenameable(false);
    list_.setKindColours([](int kind) { return targetColour(kind); });
    list_.onSelect = [this](int slot) { select(slot); };
    on_.setDescription("Off, each strip's Source menu routes as before. On, this map does: a MIDI channel plays the ChipBoy channel it is assigned to, and its velocity picks the region.");
    on_.onChange = [this](bool v) { editMap(v ? "MIDI map on" : "MIDI map off", [v](tracker::MidiMap& m) { m.on = v; }, true); };
    for (int i = 0; i < 4; ++i) target_.setOptionColour(i + 1, colours::channel(i));
    target_.setOptionTooltip(0, "This MIDI channel plays nothing.");
    target_.setTooltip("The ChipBoy channel this MIDI channel plays. Two MIDI channels may share one.");
    target_.onChange = [this](int i) {
        const int ch = midiCh_, t = i - 1;
        editMap("MIDI channel " + String(ch) + (t < 0 ? " off" : String(" to ") + colours::channelName(t)),
                [ch, t](tracker::MidiMap& m) { m.channels[size_t(ch - 1)].target = int8_t(t); }, true);
    };
    addRegion_.setTooltip("A region above the top one: the top half of what the top one had.");
    addRegion_.onClick = [this] {
        const int ch = midiCh_;
        editMap("MIDI channel " + String(ch) + " region added", [ch](tracker::MidiMap& m) {
            auto& r = m.channels[size_t(ch - 1)].regions;
            if (int(r.size()) >= tracker::kMaxRegions) return;
            const int lo = int(r.back().from), from = lo + (128 - lo) / 2;
            if (from <= lo || from > 127) return;
            tracker::MidiRegion n = r.back(); n.from = uint8_t(from);
            r.push_back(n);
        });
    };
    removeRegion_.setTooltip("The top region goes; the one under it runs to 127.");
    removeRegion_.onClick = [this] {
        const int ch = midiCh_;
        editMap("MIDI channel " + String(ch) + " region removed", [ch](tracker::MidiMap& m) {
            auto& r = m.channels[size_t(ch - 1)].regions;
            if (r.size() > 1) r.pop_back();
        });
    };
    bar_->setTooltip("The regions across the velocities 1-127. Drag a boundary to move it.");
    bar_->onMove = [this](int region, int from) {
        const int ch = midiCh_;
        editMap("MIDI channel " + String(ch) + " region " + String(region + 1), [ch, region, from](tracker::MidiMap& m) {
            auto& r = m.channels[size_t(ch - 1)].regions;
            if (region > 0 && region < int(r.size())) r[size_t(region)].from = uint8_t(from);
        });
    };
    bar_->onGesture = [this](bool begin) { if (begin) processor.history().beginGesture("MIDI channel " + String(midiCh_) + " regions"); else processor.history().endGesture(); };
    grid_.onChange = [this](const std::vector<tracker::MidiRegion>& regions) {
        const int ch = midiCh_;
        editMap("MIDI channel " + String(ch) + " regions", [ch, regions](tracker::MidiMap& m) { m.channels[size_t(ch - 1)].regions = regions; });
    };
    grid_.onOpenSlot = [this](SlotKind kind, int slot) { openSlot(kind, slot); };
    grid_.onEntryEnd = [this] { processor.history().endGesture(); };
    grid_.setBank(processor.bank());


    rebuildList();
    // The tab opens on the first channel the map assigns, else the first.
    int first = 1;
    if (const auto s = processor.song()) for (int m = 1; m <= tracker::kMidiChannels; ++m) if (s->midiMap.channels[size_t(m - 1)].target >= 0) { first = m; break; }
    select(first);
}

MidiMapPanel::~MidiMapPanel() = default;

tracker::MidiChannelMap MidiMapPanel::current() const
{
    const auto s = processor.song();
    if (!s || midiCh_ < 1 || midiCh_ > tracker::kMidiChannels) return {};
    return s->midiMap.channels[size_t(midiCh_ - 1)];
}

void MidiMapPanel::editMap(const String& what, const std::function<void(tracker::MidiMap&)>& fn, bool routingChanged)
{
    processor.editSong(what, [fn](tracker::Song& s) { fn(s.midiMap); tracker::normalizeMidiMap(s.midiMap); });
    if (routingChanged) processor.flushAllChannels();
    refresh();
    contextChanged();
}

void MidiMapPanel::rebuildList()
{
    std::vector<SlotRow> rows;
    const auto s = processor.song();
    for (int m = 1; m <= tracker::kMidiChannels; ++m) {
        const int t = s ? int(s->midiMap.channels[size_t(m - 1)].target) : -1;
        const int n = s ? int(s->midiMap.channels[size_t(m - 1)].regions.size()) : 1;
        // The target and its region count at the right; no type tag (the
        // list's kinds are instrument types), the colour is the target's.
        rows.push_back({ m, "Channel " + String(m), t >= 0, -1, t >= 0 ? String(CharPointer_UTF8(targetName(t))) + (n > 1 ? String(CharPointer_UTF8(" \xc3\x97")) + String(n) : String()) : String(CharPointer_UTF8("\xe2\x80\x94")) });
    }
    bool same = rows.size() == lastRows_.size();
    for (size_t k = 0; same && k < rows.size(); ++k)
        same = rows[k].slot == lastRows_[k].slot && rows[k].name == lastRows_[k].name && rows[k].note == lastRows_[k].note && rows[k].used == lastRows_[k].used && rows[k].kind == lastRows_[k].kind;
    if (same) return;
    lastRows_ = rows;
    list_.setRows(rows);
    list_.setSelected(midiCh_, dontSendNotification);
}

void MidiMapPanel::select(int midiCh)
{
    midiCh_ = std::clamp(midiCh, 1, tracker::kMidiChannels);
    if (list_.selected() != midiCh_) list_.setSelected(midiCh_, dontSendNotification);
    refresh();
    contextChanged();
}

void MidiMapPanel::refresh()
{
    const auto s = processor.song();
    const auto c = current();
    on_.setToggled(s && s->midiMap.on, dontSendNotification);
    head_.setText("MIDI channel " + String(midiCh_));
    target_.setSelected(c.target + 1, dontSendNotification);
    grid_.setRegions(c.regions, c.target);
    bar_->set(c.regions, c.target);
    addRegion_.setEnabled(int(c.regions.size()) < tracker::kMaxRegions && int(c.regions.back().from) < 126);
    removeRegion_.setEnabled(c.regions.size() > 1);
    rebuildList();
    resized();
    repaint();
}

void MidiMapPanel::bankChanged() { grid_.setBank(processor.bank()); }
void MidiMapPanel::songChanged() { lastRows_.clear(); refresh(); contextChanged(); }
void MidiMapPanel::hexChanged() { lastRows_.clear(); refresh(); repaint(); }

RichText MidiMapPanel::contextLine() const
{
    RichText r;
    const auto s = processor.song();
    int mapped = 0;
    if (s) for (const auto& c : s->midiMap.channels) if (c.target >= 0) ++mapped;
    r.plain("MIDI map ").bold(s && s->midiMap.on ? "on" : "off").plain(middot()).bold(String(mapped)).plain(" of 16 channels assigned");
    const auto c = current();
    r.plain(middot()).plain("channel ").bold(String(midiCh_)).plain(c.target < 0 ? " plays nothing" : String(" plays ") + colours::channelName(c.target) + ", " + String(c.regions.size()) + (c.regions.size() == 1 ? " region" : " regions"));
    return r;
}

void MidiMapPanel::paint(Graphics& g)
{
    // The pane the channel's controls sit in, as the other tabs' cards.
    g.setColour(colours::panel);
    g.fillRoundedRectangle(pane_.toFloat(), 6.0f);
    g.setColour(colours::line);
    g.drawRoundedRectangle(pane_.toFloat().reduced(0.5f), 6.0f, 1.0f);
}

void MidiMapPanel::resized()
{
    auto area = getLocalBounds();
    auto left = area.removeFromLeft(kListWidth);
    listTitle_.setBounds(left.removeFromTop(kListHeader).withTrimmedLeft(8));
    list_.setBounds(left.withTrimmedTop(4));
    area.removeFromLeft(kGap);

    auto col = area;                      // the pane runs to the right edge: the "the note is" column needs the room (D-UI-43a)
    on_.setBounds(col.removeFromTop(on_.preferredHeight(col.getWidth())));
    col.removeFromTop(kRowGap);
    pane_ = col;
    auto inner = col.reduced(12);
    auto headRow = inner.removeFromTop(26);
    head_.setBounds(headRow.removeFromLeft(head_.preferredWidth() + 8));
    targetLabel_.setBounds(headRow.removeFromLeft(targetLabel_.preferredWidth() + 10));
    target_.setBounds(headRow.removeFromLeft(target_.preferredWidth()).withHeight(target_.preferredHeight()));
    removeRegion_.setBounds(headRow.removeFromRight(84).withHeight(24));
    headRow.removeFromRight(6);
    addRegion_.setBounds(headRow.removeFromRight(84).withHeight(24));
    inner.removeFromTop(kRowGap);
    bar_->setBounds(inner.removeFromTop(kBarHeight));
    inner.removeFromTop(kRowGap);
    const int gh = std::min<int>(inner.getHeight(), grid_.preferredHeight());
    gridView_.setBounds(inner.removeFromTop(gh));
    grid_.setSize(gridView_.getMaximumVisibleWidth(), grid_.preferredHeight());
    pane_ = pane_.withBottom(gridView_.getBottom() + 12);
}

} // namespace chipboy::plugin
