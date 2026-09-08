#include "plugin/main/panels/LinkPanel.h"

#include <cmath>

namespace chipboy::plugin {

using namespace juce;
using namespace chipboy::ui;

namespace {
constexpr int kSlotRow = 32, kSlotGap = 6;
String middot() { return String(CharPointer_UTF8(" \xc2\xb7 ")); }
String emdash() { return String(CharPointer_UTF8("\xe2\x80\x94")); }
}

/// One channel line (the mockup's .slot-row): name, what drives it, a status pill.
class LinkPanel::SlotRow : public Block {
public:
    SlotRow() { addAndMakeVisible(pill_); }
    void set(const String& name, Colour nameColour, const RichText& text, const String& pill, Pill::Tone tone)
    {
        name_ = name; nameColour_ = nameColour; text_ = text;
        pill_.set(pill, tone);
        resized();
        repaint();
    }
    int preferredHeight(int) override { return kSlotRow; }
    void resized() override
    {
        const int pw = pill_.preferredWidth();
        pill_.setBounds(getWidth() - 8 - pw, (getHeight() - 18) / 2, pw, 18);
    }
    void paint(Graphics& g) override
    {
        draw::panel(g, getLocalBounds(), colours::well, colours::lineSoft, 4.0f);
        g.setFont(Fonts::pixel(11.0f));
        g.setColour(nameColour_);
        g.drawText(name_, 8, 0, 48, getHeight(), Justification::centredLeft, false);
        const int right = pill_.getX() - 10;
        text_.attributed(12.0f, colours::textMute, colours::text).draw(g, Rectangle<float>(62.0f, 0.0f, float(std::max(10, right - 62)), float(getHeight())));
    }
private:
    String name_; Colour nameColour_ = colours::textMute; RichText text_; Pill pill_;
};

/// The instance card: name, short UUID, the linked count, the four
/// channels, and link mode with its latency.
class LinkPanel::InstanceBlock : public Block {
public:
    explicit InstanceBlock(ChipBoyProcessor& p)
        : processor_(p), uuid_(shortUuid(p.instanceUuid()), Fonts::mono(12.0f), colours::textDim), link_("Link mode")
    {
        addAndMakeVisible(name_);
        addAndMakeVisible(uuid_);
        addAndMakeVisible(linked_);
        for (auto& s : slots_) { s = std::make_unique<SlotRow>(); addAndMakeVisible(*s); }
        addAndMakeVisible(link_);
        name_.setText(processor_.instanceName());
        name_.onChange = [this](const String& n) { processor_.setInstanceName(n.trim().isEmpty() ? String("ChipBoy") : n.trim()); };
        link_.setTooltip("Link mode: this instance renders one block behind the host so notes from Voice tracks land on time, and reports that block as latency");
        link_.attach(param(processor_, ids::linkMode));
    }
    void set(const String& instanceName, const String& uuid, int voices, const std::array<RichText, 4>& texts, const std::array<bool, 4>& owned, const String& latency)
    {
        if (name_.text() != instanceName) name_.setText(instanceName);
        uuid_.setText(shortUuid(uuid));
        linked_.set(voices == 0 ? String("no voices linked") : String(voices) + (voices == 1 ? " voice linked" : " voices linked"), voices > 0 ? Pill::Tone::Ok : Pill::Tone::Neutral);
        for (int ch = 0; ch < 4; ++ch)
            slots_[size_t(ch)]->set(colours::channelName(ch), colours::channel(ch), texts[size_t(ch)], owned[size_t(ch)] ? "linked" : "free", owned[size_t(ch)] ? Pill::Tone::Ok : Pill::Tone::Neutral);
        link_.setDescription("reports one block of latency so Voice tracks line up" + middot() + latency);
        resized();
    }
    int preferredHeight(int width) override
    {
        return NameField::kHeight + 10 + 4 * (kSlotRow + kSlotGap) + 4 + link_.preferredHeight(width);
    }
    void resized() override
    {
        int y = 0;
        name_.setBounds(0, y, 220, NameField::kHeight);
        uuid_.setBounds(230, y, 110, NameField::kHeight);
        const int pw = linked_.preferredWidth();
        linked_.setBounds(std::min(getWidth() - pw, 350), y + (NameField::kHeight - 18) / 2, pw, 18);
        y += NameField::kHeight + 10;
        for (auto& s : slots_) { s->setBounds(0, y, getWidth(), kSlotRow); y += kSlotRow + kSlotGap; }
        y += 4;
        link_.setBounds(0, y, getWidth(), link_.preferredHeight(getWidth()));
    }
private:
    ChipBoyProcessor& processor_;
    NameField name_;
    TextLine uuid_;
    Pill linked_;
    std::array<std::unique_ptr<SlotRow>, 4> slots_;
    Toggle link_;
};

/// Voice plugins holding a channel of this instance.
class LinkPanel::VoicesBlock : public Block {
public:
    VoicesBlock() : none_(RichText("None. Put a ChipBoy Voice on another track and pick this instance; it claims one channel and keeps it across reloads.")) { addAndMakeVisible(none_); }
    void set(const std::vector<std::pair<int, String>>& claims)
    {
        rows_.clear();
        for (const auto& c : claims) {
            auto r = std::make_unique<SlotRow>();
            RichText t;
            t.plain("Track ").bold(c.second.isEmpty() ? String("(unnamed)") : c.second);
            r->set(colours::channelName(c.first), colours::channel(c.first), t, "linked", Pill::Tone::Ok);
            addAndMakeVisible(*r);
            rows_.push_back(std::move(r));
        }
        none_.setVisible(rows_.empty());
        resized();
    }
    int preferredHeight(int width) override
    {
        return rows_.empty() ? none_.preferredHeight(width) : int(rows_.size()) * (kSlotRow + kSlotGap) - kSlotGap;
    }
    void resized() override
    {
        none_.setBounds(0, 0, getWidth(), none_.preferredHeight(getWidth()));
        int y = 0;
        for (auto& r : rows_) { r->setBounds(0, y, getWidth(), kSlotRow); y += kSlotRow + kSlotGap; }
    }
private:
    std::vector<std::unique_ptr<SlotRow>> rows_;
    HelpText none_;
};

/// The mockup's figure: three tracks, shared memory, the ChipBoy instance,
/// the return path and the legend, drawn in its 520 x 300 box.
class LinkPanel::Diagram : public Block {
public:
    int preferredHeight(int width) override { return int(std::lround(width * 300.0 / 520.0)) + 24; }
    void paint(Graphics& g) override
    {
        const float s = float(getWidth()) / 520.0f;
        g.saveState();
        g.addTransform(AffineTransform::scale(s));
        const Colour cur = colours::textMute;
        auto text = [&](const String& t, float x, float y, float px, Colour c, Justification j = Justification::left, bool bold = false) {
            g.setFont(Fonts::sans(px, bold));
            g.setColour(c);
            const float w = draw::textWidth(g.getCurrentFont(), t);
            const float x0 = j == Justification::centred ? x - w / 2.0f : x;
            g.drawText(t, Rectangle<float>(x0, y - px, w + 4.0f, px + 4.0f), Justification::bottomLeft, false);
        };
        auto box = [&](float x, float y, float w, float h, Colour c, float thick, bool dashed) {
            if (!dashed) { g.setColour(c); g.drawRoundedRectangle(x, y, w, h, 4.0f, thick); return; }
            Path p; p.addRoundedRectangle(x, y, w, h, 4.0f);
            const float d[] = { 4.0f, 3.0f };
            Path out; PathStrokeType(thick).createDashedStroke(out, p, d, 2);
            g.setColour(c); g.fillPath(out);
        };
        auto arrow = [&](float x1, float y1, float x2, float y2, Colour c, float thick) { g.setColour(c); g.drawArrow(Line<float>(x1, y1, x2, y2), thick, 7.0f, 8.0f); };

        // tracks
        box(10, 20, 150, 46, cur, 1.0f, false);
        text("Track 2" + middot() + "Bass", 20, 38, 11.5f, colours::text, Justification::left, true);
        text("ChipBoy Voice " + String(CharPointer_UTF8("\xe2\x86\x92")) + " PU2", 20, 54, 11.5f, cur);
        box(10, 84, 150, 46, cur, 1.0f, false);
        text("Track 3" + middot() + "Drums", 20, 102, 11.5f, colours::text, Justification::left, true);
        text("ChipBoy Voice " + String(CharPointer_UTF8("\xe2\x86\x92")) + " NOI", 20, 118, 11.5f, cur);
        box(10, 148, 150, 46, cur, 1.0f, true);
        text("Track 1" + middot() + "ChipBoy", 20, 166, 11.5f, colours::text, Justification::left, true);
        text("MIDI ch 3 " + String(CharPointer_UTF8("\xe2\x86\x92")) + " WAV, omni " + String(CharPointer_UTF8("\xe2\x86\x92")) + " PU1", 20, 182, 11.5f, cur);
        // shared memory
        box(212, 52, 96, 60, cur, 1.0f, false);
        text("shared", 260, 78, 11.5f, colours::text, Justification::centred, true);
        text("memory", 260, 94, 11.5f, colours::text, Justification::centred, true);
        arrow(160, 43, 210, 70, cur, 1.0f);
        arrow(160, 107, 210, 94, cur, 1.0f);
        text("notes + params", 176, 40, 10.0f, cur);
        text("one block late", 176, 122, 10.0f, cur);
        // main
        box(360, 30, 150, 104, colours::accent, 1.5f, false);
        text("ChipBoy", 435, 52, 11.5f, colours::accentHi, Justification::centred, true);
        text("APU" + middot() + "bank", 435, 70, 11.5f, cur, Justification::centred);
        text("analog stage", 435, 86, 11.5f, cur, Justification::centred);
        text("one mixer, one cap", 435, 102, 11.5f, cur, Justification::centred);
        arrow(308, 82, 358, 82, cur, 1.0f);
        g.setColour(cur);
        g.drawLine(160, 171, 435, 171, 1.0f);
        arrow(435, 171, 435, 136, cur, 1.0f);
        text("MIDI on its own track, no link needed", 240, 167, 10.0f, cur);
        // audio out
        arrow(435, 30, 435, 12, colours::accent, 1.5f);
        text("stereo out " + emdash() + " the only audio", 447, 16, 10.0f, colours::accentHi);
        // return path
        {
            Path p;
            p.startNewSubPath(360, 120);
            p.cubicTo(300, 150, 240, 150, 168, 140);
            const float d[] = { 3.0f, 3.0f };
            Path out; PathStrokeType(1.0f).createDashedStroke(out, p, d, 2);
            g.setColour(cur.withAlpha(0.6f));
            g.fillPath(out);
            g.drawArrow(Line<float>(176.0f, 141.0f, 168.0f, 140.0f), 1.0f, 6.0f, 6.0f);
        }
        text("scope + registers, display only", 196, 148, 10.0f, cur.withAlpha(0.7f));
        // legend
        text("A Voice claims one channel of one instance by UUID and keeps it across reloads.", 10, 232, 10.5f, cur);
        text("Two Voices claiming the same channel: the first keeps it, the second shows " + String(CharPointer_UTF8("\xe2\x80\x9c")) + "channel busy" + String(CharPointer_UTF8("\xe2\x80\x9d")) + ".", 10, 248, 10.5f, cur);
        text("Voice tracks report one block of latency so the host lines everything up.", 10, 264, 10.5f, cur);
        g.restoreState();
        g.setFont(Fonts::sans(11.5f));
        g.setColour(colours::textDim);
        g.drawText("Notes and parameters flow one way; audio leaves from one place.", 0, getHeight() - 20, getWidth(), 18, Justification::centredLeft, false);
    }
};

/* ------------------------------------------------------------ panel */

LinkPanel::LinkPanel(ChipBoyProcessor& p) : EditorPanel(p)
{
    addAndMakeVisible(scroll_);
    auto cols = std::make_unique<Columns>(14);
    auto left = std::make_unique<Stack>(12);
    auto inst = std::make_unique<InstanceBlock>(processor);
    instance_ = inst.get();
    left->add(std::make_unique<Card>("This instance", std::move(inst)));
    auto voices = std::make_unique<VoicesBlock>();
    voices_ = voices.get();
    left->add(std::make_unique<Card>("Voice plugins in this session", std::move(voices)));
    cols->add(std::move(left));
    cols->add(std::make_unique<Card>("How the link works", std::make_unique<Diagram>()));
    scroll_.setContent(std::move(cols));
    tick();
}

LinkPanel::~LinkPanel() = default;

RichText LinkPanel::contextLine() const
{
    RichText r;
    r.plain("Instance ").bold(processor.instanceName());
    return r;
}

void LinkPanel::tick()
{
    const uint32_t owned = processor.voiceOwnedMask() & 15u;
    const bool linkOn = paramValue(processor, ids::linkMode) != 0;
    const int latency = processor.getLatencySamples();
    const double sr = processor.currentSampleRate();
    String key = processor.instanceName() + "|" + processor.instanceUuid() + "|" + String(int(owned)) + "|" + String(linkOn ? 1 : 0) + "|" + String(latency) + "|";
    std::array<RichText, 4> texts;
    std::array<bool, 4> isOwned{};
    std::vector<std::pair<int, String>> claims;
    for (int ch = 0; ch < 4; ++ch) {
        bool o = false;
        const String src = channelSourceText(processor, ch, &o);
        isOwned[size_t(ch)] = o;
        key += src + ";";
        RichText t;
        if (o) { const String n = src.fromFirstOccurrenceOf(": ", false, false); t.plain("Claimed by ").bold(n).plain(" (Voice plugin)"); claims.emplace_back(ch, n); }
        else if (src == "Off") t.plain("Off " + emdash() + " the tracker lane and tables only");
        else t.bold(src).plain(" on this track");
        texts[size_t(ch)] = t;
    }
    if (key == lastKey_) return;
    lastKey_ = key;
    const String lat = linkOn ? "now " + String(latency) + " samples (" + String(sr > 0.0 ? latency * 1000.0 / sr : 0.0, 1) + " ms)" : "off: no extra latency, Voice notes arrive a block late";
    if (instance_) instance_->set(processor.instanceName(), processor.instanceUuid(), int(claims.size()), texts, isOwned, lat);
    if (voices_) voices_->set(claims);
    scroll_.relayout();
    contextChanged();
}

void LinkPanel::resized()
{
    scroll_.setBounds(getLocalBounds());
}

} // namespace chipboy::plugin
