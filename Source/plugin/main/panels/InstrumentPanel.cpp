#include "plugin/main/panels/InstrumentPanel.h"

#include <cmath>

namespace chipboy::plugin {

using namespace juce;
using namespace chipboy::ui;

namespace {
constexpr int kListWidth = 220, kGap = 14, kListHeader = 28, kListButtons = 26;
/// The cards go two to a row. The right column is fixed at the width two
/// 150 px field columns need with the card's padding; the left column takes
/// the rest, which is three of them. Keeping the tab down to two card rows
/// is what lets the window fit a 1080p screen (UI_DESIGN section 2).
constexpr int kNarrowCard = 380;
/// One second of envelope. It reads at this width, and at this width the
/// Result sits beside the Rate knob instead of taking a row of its own.
constexpr int kEnvPreview = 90;

String envMsText(int rate) { return rate > 0 ? String(rate * 15.625, 1) + " ms/step" : String("hold"); }
String dash() { return String(CharPointer_UTF8("\xe2\x80\x93")); }
String middot() { return String(CharPointer_UTF8(" \xc2\xb7 ")); }

String dutySeqText(const bank::Instrument& i)
{
    if (i.dutySeqLen == 0) return "none";
    String s;
    for (int k = 0; k < int(i.dutySeqLen) && k < 16; ++k) { if (k) s += " "; s += String(int(i.dutySeq[size_t(k)])); }
    return s;
}

void parseDutySeq(const String& text, bank::Instrument& i)
{
    int n = 0;
    for (const juce_wchar c : text) {
        if (c >= '0' && c <= '3' && n < 16) i.dutySeq[size_t(n++)] = uint8_t(c - '0');
    }
    i.dutySeqLen = uint8_t(n);
}

int panIndex(bank::Pan p) { return p == bank::Pan::Off ? 0 : p == bank::Pan::Left ? 1 : p == bank::Pan::Both ? 2 : 3; }
bank::Pan panFromIndex(int i) { return i == 0 ? bank::Pan::Off : i == 1 ? bank::Pan::Left : i == 2 ? bank::Pan::Both : bank::Pan::Right; }

String utf8(const char* s) { return String(CharPointer_UTF8(s)); }
String minus() { return utf8("\xe2\x88\x92"); }

/* --- what the pitch fields are worth, for the hints (COMMANDS_AND_TEMPO 7) --- */

/// V's y as semitones: LSDj's table, 0 = an eighth of a semitone, 15 = eight.
/// Written as the command slots write it, so a card and a strip agree.
String vibDepthText(int depth)
{
    static const char* const st[16] = { "1/8", "1/4", "3/8", "1/2", "3/4", "1", "1 1/2", "2",
                                        "2 1/2", "3", "3 1/2", "4", "5", "6", "7", "8" };
    return String(st[std::clamp(depth, 0, 15)]) + " st";
}

/// The same for the instrument's own depth, where 0 is no vibrato at all
/// (Bank.h) rather than the table's eighth of a semitone.
String vibDepthHint(int depth) { return depth == 0 ? String("off") : vibDepthText(depth); }

/// V's x: one cycle every 720/x pitch updates (x/2 Hz) in Fast, Step and
/// Drum, or 96/x ticks in Tick -- x cycles every four beats, with the tempo.
String vibSpeedText(int speed, bank::PitchSpeed ps)
{
    if (ps == bank::PitchSpeed::Tick) return String(speed) + " cycles/4 beats";
    return (speed % 2 == 0 ? String(speed / 2) : String(speed * 0.5, 1)) + " Hz";
}

/// What the chosen pitch speed does, short enough for a field caption.
String pitchSpeedHint(bank::PitchSpeed ps)
{
    switch (ps) {
        case bank::PitchSpeed::Fast: return "360 Hz";
        case bank::PitchSpeed::Tick: return "per tick";
        case bank::PitchSpeed::Step: return "P jumps";
        case bank::PitchSpeed::Drum: return "semitones";
    }
    return "360 Hz";
}

String cmdRateText(int rate) { return rate == 0 ? String("every tick") : "every " + String(rate + 1) + " ticks"; }
String tableModeHint(bank::TableMode m) { return m == bank::TableMode::Tick ? "row per tick" : "row per note"; }
String overlapHint(bank::Overlap o) { return o == bank::Overlap::Legato ? "only the pitch" : "starts it again"; }
String vibDirHint(bank::VibDir d) { return "to note " + (d == bank::VibDir::Up ? String("+") : minus()) + " depth"; }

/// Two small controls in one field cell, each at its own width: the mockup
/// pairs values this way, and it is what keeps the cards down to two rows.
class Pair : public Component {
public:
    Pair(std::unique_ptr<Component> a, int aw, std::unique_ptr<Component> b, int bw, int gap = 8)
        : a_(std::move(a)), b_(std::move(b)), aw_(aw), bw_(bw), gap_(gap)
    {
        addAndMakeVisible(*a_);
        addAndMakeVisible(*b_);
    }
    int width() const { return aw_ + gap_ + bw_; }
    void resized() override
    {
        int aw = aw_, bw = bw_;
        if (const int over = width() - getWidth(); over > 0) {   // squeeze both rather than cut one
            aw = std::max(1, aw - (over * aw_ + width() / 2) / std::max(1, aw_ + bw_));
            bw = std::max(1, getWidth() - gap_ - aw);
        }
        a_->setBounds(0, 0, aw, getHeight());
        b_->setBounds(aw + gap_, 0, bw, getHeight());
    }
private:
    std::unique_ptr<Component> a_, b_;
    int aw_, bw_, gap_;
};
} // namespace

/* ------------------------------------------------------- sub-widgets */

class InstrumentPanel::EnvPreview : public Component {
public:
    void set(int vol, bool up, int rate, Colour c)
    {
        if (vol == vol_ && up == up_ && rate == rate_ && c == colour_) return;
        vol_ = vol; up_ = up; rate_ = rate; colour_ = c;
        repaint();
    }
    void paint(Graphics& g) override
    {
        const float W = float(getWidth()), H = float(getHeight());
        g.fillAll(colours::lcd);
        g.setColour(colours::lcdGrid);
        for (int i = 0; i <= 15; i += 5) g.drawHorizontalLine(int(3.0f + std::round((H - 6.0f) * (1.0f - float(i) / 15.0f))), 0.0f, W);
        Path path;
        const double stepS = rate_ > 0 ? rate_ * 0.015625 : 0.0;
        for (int x = 0; x < int(W); ++x) {
            const double t = x / double(W);
            const int steps = stepS > 0.0 ? int(t / stepS) : 0;
            const int lv = std::clamp(vol_ + (up_ ? steps : -steps), 0, 15);
            const float y = 3.0f + (H - 6.0f) * (1.0f - float(lv) / 15.0f);
            if (x == 0) path.startNewSubPath(0.0f, y); else path.lineTo(float(x), y);
        }
        g.setColour(colour_);
        g.strokePath(path, PathStrokeType(1.5f));
        g.setColour(colours::textDim);
        g.setFont(Fonts::mono(10.0f));
        g.drawText("0", 3, int(H) - 14, 20, 12, Justification::centredLeft, false);
        g.drawText("1 s", int(W) - 27, int(H) - 14, 24, 12, Justification::centredRight, false);
        g.setColour(colours::scopeBorder);
        g.drawRect(getLocalBounds());
    }
private:
    int vol_ = 15; bool up_ = false; int rate_ = 0; Colour colour_ = colours::pu1;
};

class InstrumentPanel::HeadRow : public Block {
public:
    HeadRow() : type({ "Pulse", "Wave", "Kit", "Noise" }), usedOn({}, Fonts::sans(12.0f), colours::textDim)
    {
        type.setMini(true);
        type.setTooltip("The instrument's type. Switching resets it to that type's defaults, keeping the name.");
        addAndMakeVisible(name);
        addAndMakeVisible(type);
        addAndMakeVisible(usedOn);
    }
    int preferredHeight(int) override { return NameField::kHeight; }
    void resized() override
    {
        name.setBounds(0, 0, 220, NameField::kHeight);
        const int tw = type.preferredWidth(), th = type.preferredHeight();
        type.setBounds(230, (NameField::kHeight - th) / 2, tw, th);
        usedOn.setBounds(230 + tw + 12, 0, std::max(10, getWidth() - 242 - tw), NameField::kHeight);
    }
    NameField name;
    Segmented type;
    TextLine usedOn;
};

struct InstrumentPanel::Widgets {
    HeadRow* head = nullptr;
    Segmented* duty = nullptr; NameField* dutySeq = nullptr; Knob* sweepRate = nullptr; Segmented* sweepDir = nullptr; Knob* sweepShift = nullptr;
    Stepper* wave = nullptr; Knob* frameAdv = nullptr; Segmented* frameLoop = nullptr; Segmented* waveLevel = nullptr;
    Stepper* kit = nullptr; Segmented* kitLoop = nullptr; TextLine* kitRate = nullptr;
    Segmented* lfsr = nullptr; Segmented* pitchMode = nullptr; Knob* shift = nullptr; Knob* divisor = nullptr; Knob* noiseSweep = nullptr;
    Knob* envVol = nullptr; Segmented* envDir = nullptr; Knob* envRate = nullptr; Field* envResult = nullptr; EnvPreview* envPreview = nullptr;
    Segmented* vibShape = nullptr; Segmented* vibDir = nullptr; Knob* vibSpeed = nullptr; Knob* vibDepth = nullptr; Stepper* vibDelay = nullptr;
    Segmented* pitchSpeed = nullptr; Stepper* cmdRate = nullptr; Segmented* tableMode = nullptr;
    Stepper* table = nullptr; Segmented* transpose = nullptr; Segmented* noteOff = nullptr; Segmented* overlap = nullptr; Stepper* length = nullptr; Segmented* pan = nullptr;
    /// The fields whose hint says what the value is worth, refreshed on every edit.
    Field* vibF = nullptr; Field* vibSpeedF = nullptr; Field* vibDepthF = nullptr;
    Field* pitchSpeedF = nullptr; Field* cmdRateF = nullptr; Field* tableModeF = nullptr; Field* overlapF = nullptr;
};

/* ------------------------------------------------------------ panel */

InstrumentPanel::InstrumentPanel(ChipBoyProcessor& p)
    : EditorPanel(p),
      listTitle_("Instruments" + middot() + "128 slots", Fonts::sans(11.0f), colours::textMute),
      newBtn_("New"), dupBtn_("Dup")
{
    addAndMakeVisible(list_);
    addAndMakeVisible(listTitle_);
    addAndMakeVisible(newBtn_);
    addAndMakeVisible(dupBtn_);
    addAndMakeVisible(assignBtn_);
    addAndMakeVisible(scroll_);
    list_.setKindColours([](int kind) { return instrumentKindColour(kind); });
    // A click only picks what the editor shows; a double click hands the slot
    // to the channel, so browsing the bank never changes what is playing.
    list_.onSelect = [this](int slot) { showSlot(slot); };
    list_.onDoubleClick = [this](int slot) { assignSlot(slot); };
    list_.onRename = [this](int slot, const String& name) {
        const int s = std::clamp(slot, 1, bank::kInstrumentSlots);
        const int ch = channel;
        processor.mutateBank([s, ch, name](bank::Bank& b) {
            auto& i = b.instruments[size_t(s - 1)];
            if (!i.used) i = bank::Instrument::defaults(channelInstrumentType(ch), "");
            i.name = name.toStdString();
        });
        selfBank_ = processor.bank().get();
        if (s == slot_ && w_ && w_->head) w_->head->name.setText(name);
        rebuildList();
        contextChanged();
    };
    newBtn_.setTooltip("New instrument in the first empty slot (or the selected empty slot), of the current channel's type");
    newBtn_.onClick = [this] { newInstrument(); };
    dupBtn_.setTooltip("Duplicate the selected instrument into the first empty slot");
    dupBtn_.onClick = [this] { duplicate(); };
    assignBtn_.onClick = [this] { assignSlot(slot_); };

    const int v = paramValue(processor, channelParamId(channel, ids::instrument));
    slot_ = v >= 1 ? v : 1;
    rebuildList();
    rebuildEditor();
    refreshAssignButton();
}

InstrumentPanel::~InstrumentPanel() = default;

void InstrumentPanel::setChannel(int ch)
{
    EditorPanel::setChannel(ch);
    const int v = paramValue(processor, channelParamId(channel, ids::instrument));
    lastChannelInst_[size_t(channel)] = v;
    showSlot(v >= 1 ? v : slot_);
}

RichText InstrumentPanel::contextLine() const
{
    RichText r;
    r.plain("Editing ").bold(colours::channelName(channel)).plain(middot());
    const auto b = processor.bank();
    const bank::Instrument* inst = b ? b->instrument(slot_) : nullptr;
    if (inst) r.plain("instrument ").bold(slotAndName(slot_, inst->name));
    else r.plain("slot ").bold(ValueFormat::number(slot_)).plain(" is empty");
    return r;
}

void InstrumentPanel::bankChanged()
{
    const auto b = processor.bank();
    rebuildList();
    if (!b) return;
    const auto& inst = b->instruments[size_t(slot_ - 1)];
    if (inst.used != builtUsed_ || (inst.used && int(inst.type) != builtType_) || slot_ != builtSlot_) rebuildEditor();
    else if (b.get() != selfBank_) syncValues();
    refreshAssignButton();
    updateUsedOn();
    contextChanged();
}

void InstrumentPanel::songChanged() { rebuildList(); }

void InstrumentPanel::hexChanged()
{
    lastRows_.clear();
    rebuildList();
    updateUsedOn();
    scroll_.repaint();
    repaint();
}

void InstrumentPanel::tick()
{
    bool changed = false;
    for (int ch = 0; ch < 4; ++ch) {
        const int v = paramValue(processor, channelParamId(ch, ids::instrument));
        if (v == lastChannelInst_[size_t(ch)]) continue;
        lastChannelInst_[size_t(ch)] = v;
        changed = true;
        if (ch == channel && v >= 1 && v != slot_) showSlot(v);
    }
    if (changed) { updateUsedOn(); rebuildList(); }
}

void InstrumentPanel::resized()
{
    auto area = getLocalBounds();
    auto left = area.removeFromLeft(kListWidth);
    auto head = left.removeFromTop(kListHeader);
    listTitle_.setBounds(head.withTrimmedLeft(8));
    auto buttons = left.removeFromTop(kListButtons).reduced(0, 2);
    dupBtn_.setBounds(buttons.removeFromRight(40).reduced(2, 0));
    newBtn_.setBounds(buttons.removeFromRight(44).reduced(2, 0));
    assignBtn_.setBounds(buttons);
    list_.setBounds(left.withTrimmedTop(4));
    area.removeFromLeft(kGap);
    scroll_.setBounds(area);
}

/* ------------------------------------------------------------- list */

int InstrumentPanel::firstEmptySlot(const bank::Bank& b)
{
    for (int k = 0; k < bank::kInstrumentSlots; ++k) if (!b.instruments[size_t(k)].used) return k + 1;
    return 0;
}

std::vector<int> InstrumentPanel::computeUses(const bank::Bank& b, const tracker::Song* song) const
{
    std::vector<int> uses(size_t(bank::kInstrumentSlots + 1), 0);
    for (int ch = 0; ch < 4; ++ch) {
        const int v = paramValue(processor, channelParamId(ch, ids::instrument));
        if (v >= 1 && v <= bank::kInstrumentSlots && b.instruments[size_t(v - 1)].used) ++uses[size_t(v)];
    }
    if (song)
        for (const auto& ph : song->phrases) {
            if (!ph.used) continue;
            for (const auto& c : ph.steps) if (c.inst >= 1 && c.inst <= bank::kInstrumentSlots) ++uses[c.inst];
        }
    return uses;
}

void InstrumentPanel::rebuildList()
{
    const auto b = processor.bank();
    if (!b) return;
    const auto s = processor.song();
    const std::vector<int> uses = computeUses(*b, s.get());
    std::vector<SlotRow> rows;
    rows.resize(size_t(bank::kInstrumentSlots));
    for (int k = 0; k < bank::kInstrumentSlots; ++k) {
        const auto& i = b->instruments[size_t(k)];
        auto& r = rows[size_t(k)];
        r.slot = k + 1;
        r.used = i.used;
        r.name = i.used ? String(i.name) : String();
        r.kind = i.used ? int(i.type) : -1;
        r.note = uses[size_t(k + 1)] > 0 ? "x" + String(uses[size_t(k + 1)]) : String();
    }
    bool same = rows.size() == lastRows_.size();
    for (size_t k = 0; same && k < rows.size(); ++k)
        same = rows[k].slot == lastRows_[k].slot && rows[k].used == lastRows_[k].used && rows[k].name == lastRows_[k].name && rows[k].kind == lastRows_[k].kind && rows[k].note == lastRows_[k].note;
    if (!same) { lastRows_ = rows; list_.setRows(rows); }
    if (list_.selected() != slot_) list_.setSelected(slot_, dontSendNotification);
}

void InstrumentPanel::showSlot(int slot)
{
    slot_ = std::clamp(slot, 1, bank::kInstrumentSlots);
    if (list_.selected() != slot_) list_.setSelected(slot_, dontSendNotification);
    rebuildEditor();
    refreshAssignButton();
    contextChanged();
}

void InstrumentPanel::assignSlot(int slot)
{
    showSlot(slot);
    const auto b = processor.bank();
    const bank::Instrument* inst = b ? b->instrument(slot_) : nullptr;
    if (inst && instrumentFitsChannel(inst->type, channel)) {
        lastChannelInst_[size_t(channel)] = slot_;
        setParam(param(processor, channelParamId(channel, ids::instrument)), float(slot_));
        updateUsedOn();
        rebuildList();
    }
}

void InstrumentPanel::refreshAssignButton()
{
    const String ch = colours::channelName(channel);
    assignBtn_.setButtonText("Assign to " + ch);
    const auto b = processor.bank();
    const bank::Instrument* inst = b ? b->instrument(slot_) : nullptr;
    const bool fits = inst != nullptr && instrumentFitsChannel(inst->type, channel);
    assignBtn_.setEnabled(fits);
    assignBtn_.setTooltip(fits ? "Give slot " + ValueFormat::number(slot_) + " to " + ch + ". Double-clicking the row does the same."
                               : inst == nullptr ? "The selected slot is empty" : "A " + instrumentTypeName(inst->type) + " instrument does not fit " + ch);
}

void InstrumentPanel::newInstrument()
{
    const auto b = processor.bank();
    if (!b) return;
    int slot = !b->instruments[size_t(slot_ - 1)].used ? slot_ : firstEmptySlot(*b);
    if (slot == 0) return;
    const bank::InstrumentType t = channelInstrumentType(channel);
    const String name = instrumentTypeName(t) + " " + String(slot);
    processor.mutateBank([slot, t, name](bank::Bank& bk) { bk.instruments[size_t(slot - 1)] = bank::Instrument::defaults(t, name.toRawUTF8()); });
    selfBank_ = processor.bank().get();
    rebuildList();
    assignSlot(slot);
}

void InstrumentPanel::duplicate()
{
    const auto b = processor.bank();
    if (!b) return;
    const auto& src = b->instruments[size_t(slot_ - 1)];
    if (!src.used) return;
    const int slot = firstEmptySlot(*b);
    if (slot == 0) return;
    bank::Instrument copy = src;
    copy.name += " copy";
    processor.mutateBank([slot, copy](bank::Bank& bk) { bk.instruments[size_t(slot - 1)] = copy; });
    selfBank_ = processor.bank().get();
    rebuildList();
    showSlot(slot);
}

/* ----------------------------------------------------------- editor */

void InstrumentPanel::edit(const std::function<void(bank::Instrument&)>& fn)
{
    const int slot = slot_;
    const int ch = channel;
    processor.mutateBank([&fn, slot, ch](bank::Bank& b) {
        auto& i = b.instruments[size_t(slot - 1)];
        if (!i.used) i = bank::Instrument::defaults(channelInstrumentType(ch), ("Inst " + String(slot)).toRawUTF8());
        fn(i);
    });
    selfBank_ = processor.bank().get();
    updateUsedOn();
    refreshDerived();
    rebuildList();
    contextChanged();
}

void InstrumentPanel::rebuildEditor()
{
    const auto b = processor.bank();
    w_ = std::make_unique<Widgets>();
    auto stack = std::make_unique<Stack>(12);
    builtSlot_ = slot_;
    builtChannel_ = channel;
    if (!b) { scroll_.setContent(std::move(stack)); return; }
    const bank::Instrument& inst = b->instruments[size_t(slot_ - 1)];
    builtUsed_ = inst.used;
    builtType_ = inst.used ? int(inst.type) : -1;

    if (!inst.used) {
        RichText t;
        t.plain("Slot ").bold(ValueFormat::number(slot_)).plain(" is empty. ").bold("New").plain(" puts a " + instrumentTypeName(channelInstrumentType(channel)) + " instrument here, the current channel's type; ")
         .bold("Dup").plain(" copies the selected instrument into the first empty slot. Double-click a row to name it into being.");
        stack->add(std::make_unique<HelpText>(t, 12.5f));
        scroll_.setContent(std::move(stack));
        return;
    }

    const Colour accent = colours::channel(channel);
    auto knob = [this, accent](FlowGrid& g, const String& label, const String& hint, int lo, int hi, int def, std::function<void(bank::Instrument&, int)> set, std::function<String(int)> text = {}, Field** field = nullptr) {
        auto k = std::make_unique<Knob>();
        k->setRange(lo, hi, def);
        k->setAccent(accent);
        if (text) k->setTextFunction(std::move(text));
        k->onChange = [this, set](int v) { edit([set, v](bank::Instrument& i) { set(i, v); }); };
        Knob* raw = k.get();
        Field* f = g.add(std::make_unique<Field>(label, hint, std::move(k), Knob::kHeight, Knob::kWidth));
        if (field) *field = f;
        return raw;
    };
    // seg and stepper hand back the Field as well as the control: the fields
    // whose hint says what the value is worth have to reach it again.
    auto seg = [this](FlowGrid& g, const String& label, const String& hint, const StringArray& opts, std::function<void(bank::Instrument&, int)> set, int columns = 1, Field** field = nullptr) {
        auto s = std::make_unique<Segmented>(opts);
        s->setMini(true);
        s->onChange = [this, set](int v) { edit([set, v](bank::Instrument& i) { set(i, v); }); };
        Segmented* raw = s.get();
        const int h = s->preferredHeight(), w = s->preferredWidth();
        Field* f = g.add(std::make_unique<Field>(label, hint, std::move(s), h, w, columns));
        if (field) *field = f;
        return raw;
    };
    auto stepper = [this](FlowGrid& g, const String& label, const String& hint, int lo, int hi, int def, std::function<String(int)> text, std::function<void(bank::Instrument&, int)> set, int width, Field** field = nullptr) {
        auto s = std::make_unique<Stepper>();
        s->setRange(lo, hi, def);
        if (text) s->setTextFunction(std::move(text));
        s->onChange = [this, set](int v) { edit([set, v](bank::Instrument& i) { set(i, v); }); };
        Stepper* raw = s.get();
        Field* f = g.add(std::make_unique<Field>(label, hint, std::move(s), Stepper::kHeight, width));
        if (field) *field = f;
        return raw;
    };

    // --- head: name, type, where it is used --------------------------------
    auto head = std::make_unique<HeadRow>();
    w_->head = head.get();
    head->name.onChange = [this](const String& n) { edit([n](bank::Instrument& i) { i.name = n.toStdString(); }); };
    head->type.onChange = [this](int t) {
        // Only the type changes: every type's fields live in the instrument, so
        // switching back finds them as they were (nothing is reset).
        edit([t](bank::Instrument& i) { i.type = bank::InstrumentType(std::clamp(t, 0, 3)); });
        rebuildEditor();
    };
    stack->add(std::move(head));

    // --- sound ------------------------------------------------------------
    auto sound = std::make_unique<FlowGrid>();
    const auto type = inst.type;
    if (type == bank::InstrumentType::Pulse) {
        w_->duty = seg(*sound, "Duty", "NRx1 7-6", { "12.5", "25", "50", "75" }, [](bank::Instrument& i, int v) { i.duty = uint8_t(v); });
        auto f = std::make_unique<NameField>();
        f->onChange = [this](const String& t) { edit([t](bank::Instrument& i) { parseDutySeq(t, i); }); };
        w_->dutySeq = sound->addField("Duty sequence", "one NRx1 write per tick, looped", std::move(f), NameField::kHeight, 0, 2);
        w_->sweepRate = knob(*sound, "Sweep rate", "NR10 6-4" + middot() + "PU1 only", 0, 7, 0, [](bank::Instrument& i, int v) { i.sweepRate = uint8_t(v); });
        w_->sweepDir = seg(*sound, "Sweep dir", "NR10 bit 3", { "Up", "Down" }, [](bank::Instrument& i, int v) { i.sweepDown = v == 1; });
        w_->sweepShift = knob(*sound, "Sweep shift", "NR10 2-0", 0, 7, 0, [](bank::Instrument& i, int v) { i.sweepShift = uint8_t(v); });
    } else if (type == bank::InstrumentType::Wave) {
        w_->wave = stepper(*sound, "Wave", "the wave RAM source" + middot() + "W overrides", 1, bank::kWaveSlots, 1,
                           [this](int v) { const auto bk = processor.bank(); const bank::Wave* wv = bk ? bk->wave(v) : nullptr; return wv ? slotAndName(v, wv->name) + middot() + String(int(wv->frames.size())) + " fr" : slotAndName(v, "empty"); },
                           [](bank::Instrument& i, int v) { i.wave = uint8_t(v); }, 0);
        w_->frameAdv = knob(*sound, "Frame advance", "ticks per frame; 0 holds", 0, 15, 0, [](bank::Instrument& i, int v) { i.frameAdvance = uint8_t(v); });
        // three long words: it takes two field columns rather than squeeze
        w_->frameLoop = seg(*sound, "Frame loop", "how the frames run", { "Loop", "One-shot", "Ping-pong" }, [](bank::Instrument& i, int v) { i.frameLoop = bank::FrameLoop(std::clamp(v, 0, 2)); }, 2);
        w_->waveLevel = seg(*sound, "Volume", "NR32 6-5" + middot() + "no envelope here", { "mute", "25", "50", "100" }, [](bank::Instrument& i, int v) { i.waveLevel = uint8_t(v); });
    } else if (type == bank::InstrumentType::Kit) {
        w_->kit = stepper(*sound, "Kit", "streamed through wave RAM", 1, bank::kKitSlots, 1,
                          [this](int v) { const auto bk = processor.bank(); const bank::Kit* k = bk ? bk->kit(v) : nullptr; return k ? slotAndName(v, k->name) + middot() + String(int(k->samples.size())) + " samples" : slotAndName(v, "empty"); },
                          [](bank::Instrument& i, int v) { i.kit = uint8_t(v); }, 0);
        // three long words, as the wave's frame loop: two field columns
        w_->kitLoop = seg(*sound, "Loop", "per note", { "One-shot", "Loop", "Loop from point" }, [](bank::Instrument& i, int v) { i.kitLoop = bank::KitLoop(std::clamp(v, 0, 2)); }, 2);
        auto rate = std::make_unique<TextLine>(String(), Fonts::mono(12.0f), colours::text);
        w_->kitRate = sound->addField("Rate", "NR33/34" + middot() + "one register does pitch and rate", std::move(rate), Stepper::kHeight, 0, 2);
    } else {
        w_->lfsr = seg(*sound, "LFSR width", "NR43 bit 3", { "15-bit noise", "7-bit metallic" }, [](bank::Instrument& i, int v) { i.lfsr7 = v == 1; });
        w_->pitchMode = seg(*sound, "Pitch", "the noise grid is coarse; the map picks the nearest pair per note", { "Note map", "Manual" }, [](bank::Instrument& i, int v) { i.noiseManual = v == 1; });
        w_->shift = knob(*sound, "Clock shift", "NR43 7-4 (manual)", 0, 13, 5, [](bank::Instrument& i, int v) { i.noiseShift = uint8_t(v); });
        w_->divisor = knob(*sound, "Divisor", "NR43 2-0 (manual)", 0, 7, 1, [](bank::Instrument& i, int v) { i.noiseDivisor = uint8_t(v); });
        w_->noiseSweep = knob(*sound, "Noise sweep", "shift steps per tick, repeated NR43 writes", -7, 7, 0, [](bank::Instrument& i, int v) { i.noiseSweep = int8_t(v); },
                              [](int v) { return ValueFormat::signedNumber(v); });
    }
    // Pan is NR51, so it belongs with the other registers, and the row it
    // leaves behind is what the table card's new mode field takes.
    w_->pan = seg(*sound, "Pan", "NR51 default for this instrument", { dash(), "L", "LR", "R" }, [](bank::Instrument& i, int v) { i.pan = panFromIndex(v); });
    std::vector<std::unique_ptr<Block>> cards;
    cards.push_back(std::make_unique<Card>("Sound", std::move(sound)));

    // --- envelope ---------------------------------------------------------
    const bool hasEnvelope = type == bank::InstrumentType::Pulse || type == bank::InstrumentType::Noise;
    if (hasEnvelope) {
        const String nr = type == bank::InstrumentType::Noise ? "NR42" : "NR12/22";
        auto env = std::make_unique<FlowGrid>();
        w_->envVol = knob(*env, "Volume", nr + " 7-4", 0, 15, 15, [](bank::Instrument& i, int v) { i.envVol = uint8_t(v); });
        w_->envDir = seg(*env, "Direction", nr + " bit 3", { String(CharPointer_UTF8("\xe2\x86\x93")), String(CharPointer_UTF8("\xe2\x86\x91")) }, [](bank::Instrument& i, int v) { i.envDir = v == 1 ? bank::EnvDir::Up : bank::EnvDir::Down; });
        w_->envRate = knob(*env, "Rate", nr + " 2-0: 0 = hold, 1-7 = n x 15.6 ms", 0, 7, 0, [](bank::Instrument& i, int v) { i.envRate = uint8_t(v); });
        auto preview = std::make_unique<EnvPreview>();
        w_->envPreview = preview.get();
        w_->envResult = env->add(std::make_unique<Field>("Result", envMsText(inst.envRate), std::move(preview), 64, kEnvPreview));
        cards.push_back(std::make_unique<Card>("Envelope", std::move(env), String(CharPointer_UTF8("\xe2\x80\x94")) + " one " + nr + " write, then the chip runs it"));
    }

    // --- pitch and modulation ---------------------------------------------
    // One group: the vibrato's shape and direction share a cell, then its
    // three numbers, then the two fields that set how fast everything steps.
    auto mod = std::make_unique<FlowGrid>();
    {
        auto shape = std::make_unique<Segmented>(StringArray { "Tri", "Saw", "Sq" });
        shape->setMini(true);
        shape->setTooltip("The waveform of V and of the instrument's own vibrato.");
        shape->onChange = [this](int v) { edit([v](bank::Instrument& i) { i.vib.shape = bank::VibShape(std::clamp(v, 0, 2)); }); };
        auto dir = std::make_unique<Segmented>(StringArray { utf8("\xe2\x86\x93"), utf8("\xe2\x86\x91") });
        dir->setMini(true);
        dir->setTooltip("Down swings between the note and the note minus the depth; up, between the note and the note plus it.");
        dir->onChange = [this](int v) { edit([v](bank::Instrument& i) { i.vib.dir = v == 1 ? bank::VibDir::Up : bank::VibDir::Down; }); };
        w_->vibShape = shape.get();
        w_->vibDir = dir.get();
        const int sw = shape->preferredWidth(), dw = dir->preferredWidth(), h = shape->preferredHeight();
        auto pair = std::make_unique<Pair>(std::move(shape), sw, std::move(dir), dw);
        const int pw = pair->width();
        w_->vibF = mod->add(std::make_unique<Field>("Vibrato", vibDirHint(inst.vib.dir), std::move(pair), h, pw));
    }
    w_->vibSpeed = knob(*mod, "Speed", vibSpeedText(inst.vib.speed, inst.pitchSpeed), 1, 15, 8, [](bank::Instrument& i, int v) { i.vib.speed = uint8_t(v); }, {}, &w_->vibSpeedF);
    w_->vibSpeed->setTooltip("V's x, 1-15: one cycle every 720/x pitch updates -- x/2 Hz in Fast, Step and Drum -- or every 96/x ticks in Tick, which is x cycles every four beats and follows the tempo.");
    w_->vibDepth = knob(*mod, "Depth", vibDepthHint(inst.vib.depth), 0, 15, 0, [](bank::Instrument& i, int v) { i.vib.depth = uint8_t(v); }, {}, &w_->vibDepthF);
    w_->vibDepth->setTooltip("V's y: LSDj's semitone table, a quarter of a semitone at 1 up to eight semitones at 15. 0 leaves the instrument without a vibrato of its own.");
    w_->vibDelay = stepper(*mod, "Delay", "ticks before it starts", 0, 255, 0, {}, [](bank::Instrument& i, int v) { i.vib.delay = uint8_t(v); }, 100);
    w_->vibDelay->setTooltip("Ticks after the note before the vibrato starts.");
    if (type != bank::InstrumentType::Noise) {
        w_->pitchSpeed = seg(*mod, "Pitch speed", pitchSpeedHint(inst.pitchSpeed), { "Fast", "Tick", "Step", "Drum" },
                             [](bank::Instrument& i, int v) { i.pitchSpeed = bank::PitchSpeed(std::clamp(v, 0, 3)); }, 1, &w_->pitchSpeedF);
        w_->pitchSpeed->setTooltip("How P, L and V move.");
        w_->pitchSpeed->setOptionTooltip(0, "Fast: 360 updates a second, tempo-independent.");
        w_->pitchSpeed->setOptionTooltip(1, "Tick: one update per tracker tick (24 a beat), so the effect follows the tempo.");
        w_->pitchSpeed->setOptionTooltip(2, "Step: as Fast, except P is an immediate offset instead of a bend.");
        w_->pitchSpeed->setOptionTooltip(3, type == bank::InstrumentType::Kit ? "Drum is not available on kits; a kit plays as Fast."
                                                                             : "Drum: as Fast, but P and L move in semitones, so a P kick falls logarithmically.");
        if (type == bank::InstrumentType::Kit) w_->pitchSpeed->setOptionEnabled(3, false);
    }
    w_->cmdRate = stepper(*mod, "Cmd rate", cmdRateText(inst.cmdRate), 0, 15, 0, {}, [](bank::Instrument& i, int v) { i.cmdRate = uint8_t(v); }, 80, &w_->cmdRateF);
    w_->cmdRate->setTooltip("0-15: C and R step every rate + 1 ticks, and so do P and V when the pitch speed is Tick. Nothing else is slowed.");
    cards.push_back(std::make_unique<Card>("Pitch & modulation", std::move(mod), utf8("\xe2\x80\x94") + " how fast the pitch effects step"));

    // --- table & note behaviour -------------------------------------------
    auto tab = std::make_unique<FlowGrid>();
    w_->table = stepper(*tab, "Table", "runs from note-on", 0, bank::kTableSlots, 0,
                        [this](int v) { if (v == 0) return String("none"); const auto bk = processor.bank(); const bank::Table* t = bk ? bk->table(v) : nullptr; return t ? slotAndName(v, t->name) : slotAndName(v, "empty"); },
                        [](bank::Instrument& i, int v) { i.table = uint8_t(v); }, 0);
    w_->table->setTooltip("The table this instrument runs from note-on, unless an A command overrides it.");
    w_->tableMode = seg(*tab, "Table mode", tableModeHint(inst.tableMode), { "Tick", "Step" },
                        [](bank::Instrument& i, int v) { i.tableMode = v == 1 ? bank::TableMode::Step : bank::TableMode::Tick; }, 1, &w_->tableModeF);
    w_->tableMode->setTooltip("Tick: the table runs one row per tick, or per its own G. Step: it advances one row every time the instrument is triggered.");
    w_->transpose = seg(*tab, "Transpose", "whether the table's transpose column applies", { "On", "Off" }, [](bank::Instrument& i, int v) { i.transpose = v == 0; });
    w_->noteOff = seg(*tab, "Note-off", "Kill clears the DAC; on this hardware that holds the level", { "Kill", "Release", "Ignore" }, [](bank::Instrument& i, int v) { i.noteOff = bank::NoteOff(std::clamp(v, 0, 2)); });
    w_->overlap = seg(*tab, "Overlap", overlapHint(inst.overlap), { "Legato", "Retrig" },
                      [](bank::Instrument& i, int v) { i.overlap = v == 1 ? bank::Overlap::Retrig : bank::Overlap::Legato; }, 1, &w_->overlapF);
    w_->overlap->setTooltip("A note arriving over a held one: legato writes only the period, so the envelope and the table keep running; retrig starts the instrument again.");
    const bool longLength = type == bank::InstrumentType::Wave || type == bank::InstrumentType::Kit;
    w_->length = stepper(*tab, "Length", longLength ? "NR31" + middot() + "off or 1-256" : "NRx1 5-0" + middot() + "off or 1-64", 0, longLength ? 256 : 64, 0,
                         [](int v) { return v == 0 ? String("off") : ValueFormat::number(v); }, [](bank::Instrument& i, int v) { i.length = uint16_t(v); }, 100);
    cards.push_back(std::make_unique<Card>("Table & note behaviour", std::move(tab)));

    // Two cards to a row, each row as tall as the taller of its two: Sound
    // beside Envelope, Modulation beside Table, and the whole tab fits.
    for (size_t i = 0; i < cards.size(); i += 2) {
        auto row = std::make_unique<Columns>(12);
        row->setWidths({ 0, kNarrowCard });
        row->add(std::move(cards[i]));
        if (i + 1 < cards.size()) row->add(std::move(cards[i + 1]));
        else row->add(std::make_unique<Space>(1));
        stack->add(std::move(row));
    }

    scroll_.setContent(std::move(stack));
    syncValues();
}

void InstrumentPanel::syncValues()
{
    if (!w_) return;
    const auto b = processor.bank();
    if (!b) return;
    const auto& i = b->instruments[size_t(slot_ - 1)];
    if (!i.used) return;
    auto& w = *w_;
    auto K = [](Knob* k, int v) { if (k) k->setValue(v, dontSendNotification); };
    auto S = [](Segmented* s, int v) { if (s) s->setSelected(v, dontSendNotification); };
    auto T = [](Stepper* s, int v) { if (s) s->setValue(v, dontSendNotification); };
    if (w.head) {
        if (w.head->name.text() != String(i.name)) w.head->name.setText(String(i.name));
        w.head->type.setSelected(int(i.type), dontSendNotification);
    }
    S(w.duty, i.duty);
    if (w.dutySeq && w.dutySeq->text() != dutySeqText(i)) w.dutySeq->setText(dutySeqText(i));
    K(w.sweepRate, i.sweepRate); S(w.sweepDir, i.sweepDown ? 1 : 0); K(w.sweepShift, i.sweepShift);
    T(w.wave, i.wave); K(w.frameAdv, i.frameAdvance); S(w.frameLoop, int(i.frameLoop)); S(w.waveLevel, i.waveLevel);
    T(w.kit, i.kit); S(w.kitLoop, int(i.kitLoop));
    if (w.kitRate) {
        const bank::Kit* k = b->kit(i.kit);
        const uint16_t period = k ? k->period : uint16_t(1865);
        w.kitRate->setText(withThousands(int(std::lround(bank::sampleRateForPeriod(period)))) + " Hz (period " + String(int(period)) + ")");
    }
    S(w.lfsr, i.lfsr7 ? 1 : 0); S(w.pitchMode, i.noiseManual ? 1 : 0);
    K(w.shift, i.noiseShift); K(w.divisor, i.noiseDivisor); K(w.noiseSweep, i.noiseSweep);
    if (w.shift) w.shift->setEnabled(i.noiseManual);
    if (w.divisor) w.divisor->setEnabled(i.noiseManual);
    K(w.envVol, i.envVol); S(w.envDir, int(i.envDir)); K(w.envRate, i.envRate);
    S(w.vibShape, int(i.vib.shape)); S(w.vibDir, int(i.vib.dir)); K(w.vibSpeed, i.vib.speed); K(w.vibDepth, i.vib.depth); T(w.vibDelay, i.vib.delay);
    S(w.pitchSpeed, int(i.pitchSpeed)); T(w.cmdRate, i.cmdRate); S(w.tableMode, int(i.tableMode));
    T(w.table, i.table); S(w.transpose, i.transpose ? 0 : 1); S(w.noteOff, int(i.noteOff)); S(w.overlap, i.overlap == bank::Overlap::Retrig ? 1 : 0);
    T(w.length, i.length); S(w.pan, panIndex(i.pan));
    refreshDerived();
    updateUsedOn();
}

/// Everything the editor shows that is computed from a field rather than
/// held in one: the envelope picture, and the hints that say what a value is
/// worth (the vibrato in Hz and semitones, the rates in ticks).
void InstrumentPanel::refreshDerived()
{
    if (!w_) return;
    const auto b = processor.bank();
    if (!b) return;
    const auto& i = b->instruments[size_t(slot_ - 1)];
    auto& w = *w_;
    if (w.envPreview) w.envPreview->set(i.envVol, i.envDir == bank::EnvDir::Up, i.envRate, colours::channel(channel));
    if (w.envResult) w.envResult->setHint(envMsText(i.envRate));
    if (w.shift) w.shift->setEnabled(i.noiseManual);
    if (w.divisor) w.divisor->setEnabled(i.noiseManual);
    if (w.vibF) w.vibF->setHint(vibDirHint(i.vib.dir));
    if (w.vibSpeedF) w.vibSpeedF->setHint(vibSpeedText(i.vib.speed, i.pitchSpeed));
    if (w.vibDepthF) w.vibDepthF->setHint(vibDepthHint(i.vib.depth));
    if (w.pitchSpeedF) w.pitchSpeedF->setHint(pitchSpeedHint(i.pitchSpeed));
    if (w.cmdRateF) w.cmdRateF->setHint(cmdRateText(i.cmdRate));
    if (w.tableModeF) w.tableModeF->setHint(tableModeHint(i.tableMode));
    if (w.overlapF) w.overlapF->setHint(overlapHint(i.overlap));
}

void InstrumentPanel::updateUsedOn()
{
    if (!w_ || !w_->head) return;
    String on;
    for (int ch = 0; ch < 4; ++ch)
        if (paramValue(processor, channelParamId(ch, ids::instrument)) == slot_) { if (on.isNotEmpty()) on += ", "; on += colours::channelName(ch); }
    w_->head->usedOn.setText("slot " + ValueFormat::number(slot_) + middot() + "used on " + (on.isEmpty() ? String("no channel") : on));
}

} // namespace chipboy::plugin
