#include "plugin/main/panels/InstrumentPanel.h"

#include "plugin/shared/Presets.h"

#include <cmath>
#include <vector>

namespace chipboy::plugin {

using namespace juce;
using namespace chipboy::ui;

namespace {
/// The right-click list's first entry: open the item in its own tab (section 35).
constexpr int kMenuOpen = 1000;
constexpr int kListWidth = 220, kGap = 14, kListHeader = 28, kListButtons = 26;
constexpr int kOpenFlags = FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles;
constexpr int kSaveFlags = FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::warnAboutOverwriting;
String ellipsis() { return String(CharPointer_UTF8("\xe2\x80\xa6")); }
String arrow() { return String(CharPointer_UTF8(" \xe2\x86\x92 ")); }
String middot() { return String(CharPointer_UTF8(" \xc2\xb7 ")); }
String utf8(const char* s) { return String(CharPointer_UTF8(s)); }

/// The form (docs/COMMANDS_AND_TEMPO.md section 29): labels down the left,
/// controls down the right, a thin caption over each group and no card
/// chrome anywhere. Two columns of groups fill the pane.
constexpr int kGroupGap = 10, kColumnGap = 20;
/// The envelope picture over its fields. It is the one thing on the tab that
/// is not a label and a value, because an envelope is a shape.
constexpr int kGraphHeight = 66;

/// What placing a preset did, in one line: "Pluck -> slot 3; table 5 -> 9
/// (renumbered); wave 2 reused" (docs/COMMANDS_AND_TEMPO.md section 15).
String placeText(const bank::PlaceReport& r, const String& name)
{
    if (!r.ok) return "Preset not placed: " + String(r.error != nullptr ? r.error : "something is full");
    String s = name + arrow() + "slot " + ValueFormat::number(r.instrumentSlot);
    for (const auto& m : r.moves) {
        const char* kind = m.kind == bank::PlaceReport::Kind::Table ? "table" : m.kind == bank::PlaceReport::Kind::Wave ? "wave" : "kit";
        s += "; " + String(kind) + " " + ValueFormat::number(m.from);
        if (m.to != m.from) s += arrow() + ValueFormat::number(m.to) + (m.reused ? " (reused)" : " (renumbered)");
        else if (m.reused) s += " reused";
    }
    return s;
}

String envMsText(int rate) { return rate > 0 ? String(rate * 15.625, 1) + " ms" : String("hold"); }

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

/* --- what the pitch fields are worth, on the control (COMMANDS_AND_TEMPO 7) --- */

/// V's y as semitones: LSDj's table, 0 = an eighth of a semitone, 15 = eight.
String vibDepthText(int depth)
{
    static const char* const st[16] = { "1/8", "1/4", "3/8", "1/2", "3/4", "1", "1 1/2", "2",
                                        "2 1/2", "3", "3 1/2", "4", "5", "6", "7", "8" };
    return String(st[std::clamp(depth, 0, 15)]) + " st";
}
/// The same for the instrument's own depth, where 0 is no vibrato at all.
String vibDepthHint(int depth) { return depth == 0 ? String("off") : vibDepthText(depth); }

/// V's x: one cycle every 720/x pitch updates (x/2 Hz) in Fast, Step and
/// Drum, or 96/x ticks in Tick -- x cycles every four beats, with the tempo.
String vibSpeedText(int speed, bank::PitchSpeed ps)
{
    if (ps == bank::PitchSpeed::Tick) return String(speed) + " cyc/4b";
    return (speed % 2 == 0 ? String(speed / 2) : String(speed * 0.5, 1)) + " Hz";
}

String cmdRateText(int rate) { return rate == 0 ? String("every tick") : "every " + String(rate + 1); }
String tickText(int t) { return String(t) + " t"; }

/// Two small controls in one cell, each at its own width: the vibrato's
/// shape and direction, and a segment's ticks beside its curve.
class Pair : public Component {
public:
    Pair(std::unique_ptr<Component> a, int aw, int ah, std::unique_ptr<Component> b, int bw, int bh, int gap = 8)
        : a_(std::move(a)), b_(std::move(b)), aw_(aw), bw_(bw), ah_(ah), bh_(bh), gap_(gap)
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
        a_->setBounds(0, (getHeight() - ah_) / 2, aw, ah_);
        b_->setBounds(aw + gap_, (getHeight() - bh_) / 2, bw, bh_);
    }
private:
    std::unique_ptr<Component> a_, b_;
    int aw_, bw_, ah_, bh_, gap_;
};

/// A block that holds one component at a fixed height: the graph.
class GraphHold : public Block {
public:
    GraphHold(std::unique_ptr<Component> c, int height) : c_(std::move(c)), h_(height) { addAndMakeVisible(*c_); }
    int preferredHeight(int) override { return h_ + 6; }
    void resized() override { c_->setBounds(0, 3, getWidth(), h_); }
private:
    std::unique_ptr<Component> c_;
    int h_;
};
} // namespace

/* ------------------------------------------------------- sub-widgets */

/// The envelope, drawn from the fields (section 29): the chip's NRx2 ramp
/// over a second, or the shaped ADSR with its curves -- rendered through
/// bank::envSegmentLevel, so the picture is the level list the driver really
/// writes (section 27).
class InstrumentPanel::EnvPreview : public Component, public SettableTooltipClient {
public:
    void set(const bank::Instrument& i, Colour c, bool fourLevels)
    {
        if (i.env.mode == mode_ && c == colour_ && fourLevels == four_ && sameFields(i)) return;
        env_ = i.env;
        mode_ = i.env.mode;
        vol_ = i.envVol; up_ = i.envDir == bank::EnvDir::Up; rate_ = i.envRate;
        colour_ = c; four_ = fourLevels;
        setTooltip(mode_ == bank::EnvMode::Shaped
                       ? String("Attack to the peak, decay to the sustain, which is held while the note is, then the release. Each segment has its own curve.")
                       : String("The chip's own envelope: one NRx2 write, then it runs -- a start level, a direction and one of seven rates."));
        repaint();
    }
    void paint(Graphics& g) override
    {
        const float W = float(getWidth()), H = float(getHeight());
        g.fillAll(colours::lcd);
        g.setColour(colours::lcdGrid);
        for (int i = 0; i <= 15; i += 5) g.drawHorizontalLine(int(3.0f + std::round((H - 6.0f) * (1.0f - float(i) / 15.0f))), 0.0f, W);
        Path path;
        std::vector<Point<float>> pts;
        const int top = four_ ? 3 : 15;
        auto point = [&](float x, int lv) {
            const float y = 3.0f + (H - 6.0f) * (1.0f - float(std::clamp(lv, 0, top)) / float(top));
            if (path.isEmpty()) path.startNewSubPath(x, y); else path.lineTo(x, y);
            pts.push_back({ x, y });
        };
        String right;
        if (mode_ == bank::EnvMode::Shaped) {
            const int peak = std::clamp<int>(env_.peak, 0, top), sus = std::clamp<int>(env_.sustain, 0, top);
            const int a = env_.attackTicks, d = env_.decayTicks, r = env_.releaseTicks;
            const int hold = std::max(6, (a + d + r) / 3);
            const int total = std::max(1, a + d + hold + r);
            const auto x = [&](int t) { return W * float(t) / float(total); };
            for (int t = 0; t <= a; ++t) point(x(t), bank::envSegmentLevel(0, peak, a, t, env_.attackCurve));
            for (int t = 0; t <= d; ++t) point(x(a + t), bank::envSegmentLevel(peak, sus, d, t, env_.decayCurve));
            point(x(a + d + hold), sus);
            for (int t = 0; t <= r; ++t) point(x(a + d + hold + t), bank::envSegmentLevel(sus, 0, r, t, env_.releaseCurve));
            right = String(a + d + r) + " t";
        } else {
            const double stepS = rate_ > 0 ? rate_ * 0.015625 : 0.0;
            for (int px = 0; px < int(W); ++px) {
                const double t = px / double(W);
                const int steps = stepS > 0.0 ? int(t / stepS) : 0;
                point(float(px), std::clamp(vol_ + (up_ ? steps : -steps), 0, 15));
            }
            right = "1 s";
        }
        // The area under the line, softly: a level of 13 held for a second is
        // a thin line near the top, and a picture reads better than a line.
        if (pts.size() > 1) {
            Path fill;
            fill.startNewSubPath(pts.front().x, H - 3.0f);
            for (const auto& p : pts) fill.lineTo(p.x, p.y);
            fill.lineTo(pts.back().x, H - 3.0f);
            fill.closeSubPath();
            g.setColour(colour_.withAlpha(0.16f));
            g.fillPath(fill);
        }
        g.setColour(colour_);
        g.strokePath(path, PathStrokeType(1.5f));
        g.setColour(colours::textDim);
        g.setFont(Fonts::mono(10.0f));
        g.drawText("0", 3, int(H) - 14, 24, 12, Justification::centredLeft, false);
        g.drawText(right, int(W) - 48, int(H) - 14, 44, 12, Justification::centredRight, false);
        g.setColour(colours::scopeBorder);
        g.drawRect(getLocalBounds());
    }
private:
    bool sameFields(const bank::Instrument& i) const
    {
        return i.envVol == vol_ && (i.envDir == bank::EnvDir::Up) == up_ && i.envRate == rate_
               && i.env.attackTicks == env_.attackTicks && i.env.peak == env_.peak && i.env.decayTicks == env_.decayTicks
               && i.env.sustain == env_.sustain && i.env.releaseTicks == env_.releaseTicks
               && i.env.attackCurve == env_.attackCurve && i.env.decayCurve == env_.decayCurve && i.env.releaseCurve == env_.releaseCurve;
    }
    bank::Envelope env_;
    bank::EnvMode mode_ = bank::EnvMode::Chip;
    int vol_ = 15; bool up_ = false; int rate_ = 0;
    Colour colour_ = colours::pu1;
    bool four_ = false;
};

/// The name, the type and where the instrument is used, on one row.
class InstrumentPanel::HeadRow : public Block {
public:
    HeadRow() : type({ "Pulse", "Wave", "Kit", "Noise" }), usedOn({}, Fonts::sans(12.0f), colours::textDim)
    {
        type.setMini(true);
        type.setTooltip("The instrument's type. Every type's fields are kept, so switching back finds them as they were.");
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
    // sound
    Segmented* duty = nullptr; NameField* dutySeq = nullptr; Stepper* sweepRate = nullptr; Segmented* sweepDir = nullptr; Stepper* sweepShift = nullptr;
    Stepper* wave = nullptr; Stepper* frameAdv = nullptr; Segmented* frameLoop = nullptr; Segmented* waveLevel = nullptr;
    Stepper* kit = nullptr; Segmented* kitLoop = nullptr; TextLine* kitRate = nullptr;
    Segmented* lfsr = nullptr; Segmented* pitchMode = nullptr; Stepper* shift = nullptr; Stepper* divisor = nullptr; Stepper* noiseSweep = nullptr;
    Segmented* pan = nullptr;
    // envelope (section 27)
    EnvPreview* graph = nullptr;
    Segmented* envMode = nullptr;
    Stepper* envVol = nullptr; Segmented* envDir = nullptr; Stepper* envRate = nullptr;
    Stepper* attack = nullptr; Stepper* peak = nullptr; Stepper* decay = nullptr; Stepper* sustain = nullptr; Stepper* release = nullptr;
    Segmented* attackCurve = nullptr; Segmented* decayCurve = nullptr; Segmented* releaseCurve = nullptr;
    // pitch and modulation
    Segmented* vibShape = nullptr; Segmented* vibDir = nullptr; Stepper* vibSpeed = nullptr; Stepper* vibDepth = nullptr; Stepper* vibDelay = nullptr;
    Segmented* pitchSpeed = nullptr; Stepper* cmdRate = nullptr; Stepper* chordRate = nullptr; Segmented* tableMode = nullptr;
    // table and note behaviour
    Stepper* table = nullptr; Segmented* transpose = nullptr; Segmented* noteOff = nullptr; Segmented* overlap = nullptr; Stepper* length = nullptr;
};

/* ------------------------------------------------------------ panel */

InstrumentPanel::InstrumentPanel(ChipBoyProcessor& p)
    : EditorPanel(p),
      listTitle_("Instruments" + middot() + "128 slots", Fonts::sans(11.0f), colours::textMute),
      newBtn_("New"), dupBtn_("Dup"),
      savePresetBtn_("Save preset" + ellipsis()), loadPresetBtn_("Load preset" + ellipsis())
{
    addAndMakeVisible(list_);
    addAndMakeVisible(listTitle_);
    addAndMakeVisible(newBtn_);
    addAndMakeVisible(dupBtn_);
    addAndMakeVisible(assignBtn_);
    addAndMakeVisible(savePresetBtn_);
    addAndMakeVisible(loadPresetBtn_);
    addAndMakeVisible(scroll_);
    list_.setKindColours([](int kind) { return instrumentKindColour(kind); });
    // A click only picks what the editor shows; a double click hands the slot
    // to the channel, so browsing the bank never changes what is playing.
    list_.onSelect = [this](int slot) { showSlot(slot); };
    list_.onDoubleClick = [this](int slot) { assignSlot(slot); };
    list_.onRename = [this](int slot, const String& name) {
        const int s = std::clamp(slot, 1, bank::kInstrumentSlots);
        const int ch = channel;
        processor.editBank("Instrument " + ValueFormat::number(s) + " named " + name, [s, ch, name](bank::Bank& b) {
            auto& i = b.instruments[size_t(s - 1)];
            if (!i.used) i = bank::Instrument::defaults(channelInstrumentType(ch), "");
            i.name = name.toStdString();
        });
        selfBank_ = processor.bank().get();
        if (s == slot_ && w_ && w_->head) w_->head->name.setText(name);
        rebuildList();
        contextChanged();
    };
    newBtn_.setTooltip("New instrument in the first empty slot, of this channel's type");
    newBtn_.onClick = [this] { newInstrument(); };
    dupBtn_.setTooltip("Duplicate the selected instrument into the first empty slot");
    dupBtn_.onClick = [this] { duplicate(); };
    assignBtn_.onClick = [this] { assignSlot(slot_); };
    // A preset is the instrument and everything it references, as a file
    // (docs/COMMANDS_AND_TEMPO.md section 15).
    savePresetBtn_.setTooltip("Write this instrument to a .cbi with every table, wave and kit it uses.");
    savePresetBtn_.onClick = [this] { savePreset(); };
    loadPresetBtn_.setTooltip("Read a .cbi into this slot; its tables, waves and kit take free slots and every reference is renumbered.");
    loadPresetBtn_.onClick = [this] { loadPreset(); };

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
    // The envelope's mode changes which fields the group holds, so it is one
    // of the things a rebuild watches (section 27).
    if (inst.used != builtUsed_ || (inst.used && int(inst.type) != builtType_) || slot_ != builtSlot_
        || (inst.used && int(inst.env.mode) != builtEnvMode_)) rebuildEditor();
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
    // The preset pair takes the row under them: two 220 px buttons do not
    // fit beside New and Dup (UI_DESIGN section 6).
    auto presets = left.removeFromTop(kListButtons).reduced(0, 2);
    savePresetBtn_.setBounds(presets.removeFromLeft(presets.getWidth() / 2).reduced(2, 0));
    loadPresetBtn_.setBounds(presets.reduced(2, 0));
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
            for (const auto& c : ph.cells) if (c.inst >= 1 && c.inst <= bank::kInstrumentSlots) ++uses[c.inst];
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
        setParam(*this, param(processor, channelParamId(channel, ids::instrument)), float(slot_));
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
    processor.editBank("New instrument " + ValueFormat::number(slot), [slot, t, name](bank::Bank& bk) { bk.instruments[size_t(slot - 1)] = bank::Instrument::defaults(t, name.toRawUTF8()); });
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
    processor.editBank("Duplicate into instrument " + ValueFormat::number(slot), [slot, copy](bank::Bank& bk) { bk.instruments[size_t(slot - 1)] = copy; });
    selfBank_ = processor.bank().get();
    rebuildList();
    showSlot(slot);
}

/* ---------------------------------------------------------- presets */

void InstrumentPanel::savePreset()
{
    const auto b = processor.bank();
    if (!b) return;
    const bank::Instrument* inst = b->instrument(slot_);
    if (inst == nullptr) { message("Slot " + ValueFormat::number(slot_) + " is empty: nothing to save."); return; }
    const String name = File::createLegalFileName(String(inst->name).trim());
    auto preset = std::make_shared<bank::Preset>(bank::collectPreset(*b, slot_));
    chooser_ = std::make_unique<FileChooser>("Save instrument preset",
                                             presetsFolder().getChildFile((name.isEmpty() ? String("Instrument") : name) + kPresetExtension),
                                             String("*") + kPresetExtension, true, false, this);
    chooser_->launchAsync(kSaveFlags, [safe = Component::SafePointer<InstrumentPanel>(this), preset](const FileChooser& fc) {
        if (safe == nullptr) return;
        File file = fc.getResult();
        if (file == File()) return;
        if (!file.hasFileExtension(kPresetExtension)) file = file.withFileExtension(kPresetExtension);
        const bool ok = chipboy::plugin::savePreset(*preset, file);
        safe->message(ok ? "Saved " + file.getFileName() + " to " + file.getParentDirectory().getFullPathName()
                         : "Could not write " + file.getFileName());
    });
}

void InstrumentPanel::loadPreset()
{
    chooser_ = std::make_unique<FileChooser>("Open instrument preset", presetsFolder(), String("*") + kPresetExtension, true, false, this);
    chooser_->launchAsync(kOpenFlags, [safe = Component::SafePointer<InstrumentPanel>(this)](const FileChooser& fc) {
        if (safe == nullptr) return;
        const File file = fc.getResult();
        if (!file.existsAsFile()) return;
        auto preset = std::make_shared<bank::Preset>();
        if (!chipboy::plugin::loadPreset(file, *preset)) { safe->message("Could not read " + file.getFileName()); return; }
        InstrumentPanel& panel = *safe;
        const int slot = panel.slot_;
        bank::PlaceReport report;
        panel.processor.editBank("Preset " + String(preset->instrument.name) + " into slot " + ValueFormat::number(slot),
                                 [&preset, slot, &report](bank::Bank& b) { bank::placePreset(b, *preset, slot, report); });
        panel.selfBank_ = panel.processor.bank().get();
        panel.rebuildList();
        panel.rebuildEditor();
        panel.refreshAssignButton();
        panel.updateUsedOn();
        panel.contextChanged();
        panel.message(placeText(report, String(preset->instrument.name)));
    });
}

/* ----------------------------------------------------------- editor */

void InstrumentPanel::edit(const String& what, const std::function<void(bank::Instrument&)>& fn)
{
    const int slot = slot_;
    const int ch = channel;
    processor.editBank("Instrument " + ValueFormat::number(slot) + middot() + what, [&fn, slot, ch](bank::Bank& b) {
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
    builtEnvMode_ = inst.used ? int(inst.env.mode) : -1;

    if (!inst.used) {
        RichText t;
        t.plain("Slot ").bold(ValueFormat::number(slot_)).plain(" is empty. ").bold("New").plain(" puts a " + instrumentTypeName(channelInstrumentType(channel)) + " instrument here; ")
         .bold("Dup").plain(" copies the selected one. Double-click a row to name it into being.");
        stack->add(std::make_unique<HelpText>(t, 12.5f));
        scroll_.setContent(std::move(stack));
        return;
    }

    const auto type = inst.type;
    const bool fourLevels = type == bank::InstrumentType::Wave || type == bank::InstrumentType::Kit;

    // --- the two makers every row uses ------------------------------------
    auto stepper = [this](FormGroup& g, const String& label, const String& tip, int lo, int hi, int def,
                          std::function<String(int)> text, std::function<void(bank::Instrument&, int)> set, int width = 92) {
        auto s = std::make_unique<Stepper>();
        s->setRange(lo, hi, def);
        const auto show = text;
        if (text) s->setTextFunction(std::move(text));
        s->onChange = [this, set, label, show](int v) { edit(label + " " + (show ? show(v) : ValueFormat::number(v)), [set, v](bank::Instrument& i) { set(i, v); }); };
        if (tip.isNotEmpty()) s->setTooltip(tip);
        return g.add(label, std::move(s), width, Stepper::kHeight, tip);
    };
    auto seg = [this](FormGroup& g, const String& label, const String& tip, const StringArray& opts,
                      std::function<void(bank::Instrument&, int)> set) {
        auto s = std::make_unique<Segmented>(opts);
        s->setMini(true);
        s->onChange = [this, set, label, opts](int v) { edit(label + " " + opts[std::clamp(v, 0, opts.size() - 1)], [set, v](bank::Instrument& i) { set(i, v); }); };
        if (tip.isNotEmpty()) s->setTooltip(tip);
        const int w = s->preferredWidth(), h = s->preferredHeight();
        return g.add(label, std::move(s), w, h, tip);
    };
    /// A segment's ticks beside its curve: one row for each of A, D and R.
    auto segmentRow = [this](FormGroup& g, const String& label, const String& tip,
                             std::function<void(bank::Instrument&, int)> setTicks,
                             std::function<void(bank::Instrument&, bank::EnvCurve)> setCurve,
                             Stepper** outTicks, Segmented** outCurve) {
        auto ticks = std::make_unique<Stepper>();
        ticks->setRange(0, 255, 0);
        ticks->setTextFunction([](int v) { return tickText(v); });
        ticks->setTooltip(tip);
        ticks->onChange = [this, setTicks, label](int v) { edit(label + " " + tickText(v), [setTicks, v](bank::Instrument& i) { setTicks(i, v); }); };
        auto curve = std::make_unique<Segmented>(StringArray{ "Lin", "Exp", "Log" });
        curve->setMini(true);
        curve->setTooltip("The shape of the segment: even, fast at the start, or slow at it.");
        curve->onChange = [this, setCurve, label](int v) {
            const auto c = bank::EnvCurve(std::clamp(v, 0, 2));
            edit(label + " curve", [setCurve, c](bank::Instrument& i) { setCurve(i, c); });
        };
        *outTicks = ticks.get();
        *outCurve = curve.get();
        const int cw = curve->preferredWidth(), chh = curve->preferredHeight();
        auto pair = std::make_unique<Pair>(std::move(ticks), 84, Stepper::kHeight, std::move(curve), cw, chh);
        const int pw = pair->width();
        g.add(label, std::move(pair), pw, Stepper::kHeight, tip);
    };

    // --- head -------------------------------------------------------------
    auto head = std::make_unique<HeadRow>();
    w_->head = head.get();
    head->name.onChange = [this](const String& n) { edit("named " + n, [n](bank::Instrument& i) { i.name = n.toStdString(); }); };
    head->type.onChange = [this](int t) {
        // Only the type changes: every type's fields live in the instrument, so
        // switching back finds them as they were (nothing is reset).
        edit("type " + instrumentTypeName(bank::InstrumentType(std::clamp(t, 0, 3))), [t](bank::Instrument& i) { i.type = bank::InstrumentType(std::clamp(t, 0, 3)); });
        rebuildEditor();
    };
    stack->add(std::move(head));

    // --- SOUND ------------------------------------------------------------
    auto sound = std::make_unique<FormGroup>("Sound");
    if (type == bank::InstrumentType::Pulse) {
        w_->duty = seg(*sound, "Duty", "NRx1 bits 7-6: the pulse width.", { "12.5", "25", "50", "75" }, [](bank::Instrument& i, int v) { i.duty = uint8_t(v); });
        auto f = std::make_unique<NameField>();
        f->onChange = [this](const String& t) { edit("duty sequence " + t, [t](bank::Instrument& i) { parseDutySeq(t, i); }); };
        w_->dutySeq = sound->add("Duty sequence", std::move(f), 150, NameField::kHeight, "One NRx1 write per tick, looped. Digits 0-3.");
        w_->sweepRate = stepper(*sound, "Sweep rate", "NR10 bits 6-4. PU1 only.", 0, 7, 0, {}, [](bank::Instrument& i, int v) { i.sweepRate = uint8_t(v); });
        w_->sweepDir = seg(*sound, "Sweep dir", "NR10 bit 3.", { "Up", "Down" }, [](bank::Instrument& i, int v) { i.sweepDown = v == 1; });
        w_->sweepShift = stepper(*sound, "Sweep shift", "NR10 bits 2-0.", 0, 7, 0, {}, [](bank::Instrument& i, int v) { i.sweepShift = uint8_t(v); });
    } else if (type == bank::InstrumentType::Wave) {
        w_->wave = stepper(*sound, "Wave", "The wave RAM source; a W command overrides it. Right-click lists the bank, double-click opens it.", 1, bank::kWaveSlots, 1,
                           [this](int v) { const auto bk = processor.bank(); const bank::Wave* wv = bk ? bk->wave(v) : nullptr; return wv ? slotAndName(v, wv->name) : slotAndName(v, "empty"); },
                           [](bank::Instrument& i, int v) { i.wave = uint8_t(v); }, 190);
        w_->wave->onList = [this] { showWaveMenu(); };
        w_->frameAdv = stepper(*sound, "Frame advance", "Ticks per frame; 0 holds the frame.", 0, 15, 0, {}, [](bank::Instrument& i, int v) { i.frameAdvance = uint8_t(v); });
        w_->frameLoop = seg(*sound, "Frame loop", "How the frames run.", { "Loop", "One-shot", "Ping-pong" }, [](bank::Instrument& i, int v) { i.frameLoop = bank::FrameLoop(std::clamp(v, 0, 2)); });
        w_->waveLevel = seg(*sound, "Level", "NR32 bits 6-5: four levels, and no envelope unit on this channel.", { "mute", "25", "50", "100" }, [](bank::Instrument& i, int v) { i.waveLevel = uint8_t(v); });
    } else if (type == bank::InstrumentType::Kit) {
        w_->kit = stepper(*sound, "Kit", "Streamed through wave RAM. Right-click lists the bank, double-click opens it.", 1, bank::kKitSlots, 1,
                          [this](int v) { const auto bk = processor.bank(); const bank::Kit* k = bk ? bk->kit(v) : nullptr; return k ? slotAndName(v, k->name) : slotAndName(v, "empty"); },
                          [](bank::Instrument& i, int v) { i.kit = uint8_t(v); }, 190);
        w_->kit->onList = [this] { showKitMenu(); };
        w_->kitLoop = seg(*sound, "Loop", "Per note.", { "One-shot", "Loop", "From point" }, [](bank::Instrument& i, int v) { i.kitLoop = bank::KitLoop(std::clamp(v, 0, 2)); });
        auto rate = std::make_unique<TextLine>(String(), Fonts::mono(12.0f), colours::text);
        w_->kitRate = sound->add("Rate", std::move(rate), 190, Stepper::kHeight, "NR33/34: one register does the pitch and the rate.");
    } else {
        w_->lfsr = seg(*sound, "LFSR width", "NR43 bit 3.", { "15-bit", "7-bit" }, [](bank::Instrument& i, int v) { i.lfsr7 = v == 1; });
        w_->pitchMode = seg(*sound, "Pitch", "The noise grid is coarse; the map picks the nearest pair per note.", { "Note map", "Manual" }, [](bank::Instrument& i, int v) { i.noiseManual = v == 1; });
        w_->shift = stepper(*sound, "Clock shift", "NR43 bits 7-4, in Manual.", 0, 13, 5, {}, [](bank::Instrument& i, int v) { i.noiseShift = uint8_t(v); });
        w_->divisor = stepper(*sound, "Divisor", "NR43 bits 2-0, in Manual.", 0, 7, 1, {}, [](bank::Instrument& i, int v) { i.noiseDivisor = uint8_t(v); });
        w_->noiseSweep = stepper(*sound, "Noise sweep", "Shift steps per tick: repeated NR43 writes.", -7, 7, 0,
                                 [](int v) { return ValueFormat::signedNumber(v); }, [](bank::Instrument& i, int v) { i.noiseSweep = int8_t(v); });
    }
    // Pan is NR51, so it belongs with the other registers.
    w_->pan = seg(*sound, "Pan", "The NR51 default for this instrument. There is no pan law.", { utf8("\xe2\x80\x93"), "L", "LR", "R" }, [](bank::Instrument& i, int v) { i.pan = panFromIndex(v); });

    // --- ENVELOPE (section 27) --------------------------------------------
    auto env = std::make_unique<FormGroup>("Envelope");
    {
        auto graph = std::make_unique<EnvPreview>();
        w_->graph = graph.get();
        env->addWide(std::make_unique<GraphHold>(std::move(graph), kGraphHeight));
    }
    w_->envMode = seg(*env, "Mode", "Chip is the chip's own NRx2 envelope; Shaped is an ADSR the driver renders a level a tick and writes through zombie mode.",
                      { "Chip", "Shaped" }, [](bank::Instrument& i, int v) { i.env.mode = v == 1 ? bank::EnvMode::Shaped : bank::EnvMode::Chip; });
    w_->envMode->onChange = [this](int v) {
        edit(v == 1 ? String("envelope Shaped") : String("envelope Chip"), [v](bank::Instrument& i) { i.env.mode = v == 1 ? bank::EnvMode::Shaped : bank::EnvMode::Chip; });
        rebuildEditor();
    };
    const int topLevel = fourLevels ? 3 : 15;
    if (inst.env.mode == bank::EnvMode::Shaped) {
        segmentRow(*env, "Attack", "Ticks from silence to the peak.",
                   [](bank::Instrument& i, int v) { i.env.attackTicks = uint8_t(std::clamp(v, 0, 255)); },
                   [](bank::Instrument& i, bank::EnvCurve c) { i.env.attackCurve = c; }, &w_->attack, &w_->attackCurve);
        w_->peak = stepper(*env, "Peak", fourLevels ? "The level the attack reaches, of the four NR32 levels." : "The level the attack reaches, 0-15.",
                           0, topLevel, topLevel, {}, [topLevel](bank::Instrument& i, int v) { i.env.peak = uint8_t(std::clamp(v, 0, topLevel)); });
        segmentRow(*env, "Decay", "Ticks from the peak to the sustain.",
                   [](bank::Instrument& i, int v) { i.env.decayTicks = uint8_t(std::clamp(v, 0, 255)); },
                   [](bank::Instrument& i, bank::EnvCurve c) { i.env.decayCurve = c; }, &w_->decay, &w_->decayCurve);
        w_->sustain = stepper(*env, "Sustain", "The level held while the note is held.", 0, topLevel, topLevel, {},
                              [topLevel](bank::Instrument& i, int v) { i.env.sustain = uint8_t(std::clamp(v, 0, topLevel)); });
        segmentRow(*env, "Release", "Ticks from the level at note-off to silence, when Note-off is Release.",
                   [](bank::Instrument& i, int v) { i.env.releaseTicks = uint8_t(std::clamp(v, 0, 255)); },
                   [](bank::Instrument& i, bank::EnvCurve c) { i.env.releaseCurve = c; }, &w_->release, &w_->releaseCurve);
    } else if (!fourLevels) {
        const String nr = type == bank::InstrumentType::Noise ? "NR42" : "NR12/22";
        w_->envVol = stepper(*env, "Volume", nr + " bits 7-4: the level the note starts at.", 0, 15, 15, {}, [](bank::Instrument& i, int v) { i.envVol = uint8_t(v); });
        w_->envDir = seg(*env, "Direction", nr + " bit 3.", { utf8("\xe2\x86\x93"), utf8("\xe2\x86\x91") }, [](bank::Instrument& i, int v) { i.envDir = v == 1 ? bank::EnvDir::Up : bank::EnvDir::Down; });
        w_->envRate = stepper(*env, "Rate", nr + " bits 2-0: 0 holds, 1-7 step every n x 15.6 ms.", 0, 7, 0,
                              [](int v) { return envMsText(v); }, [](bank::Instrument& i, int v) { i.envRate = uint8_t(v); }, 110);
    } else {
        auto note = std::make_unique<TextLine>("the Level in Sound", Fonts::sans(12.0f), colours::textDim);
        env->add("Level", std::move(note), 190, Stepper::kHeight, "This channel has no envelope unit: Chip holds the NR32 level in Sound, and Shaped renders one instead.");
    }

    // --- PITCH & MODULATION -----------------------------------------------
    auto mod = std::make_unique<FormGroup>("Pitch & modulation");
    {
        auto shape = std::make_unique<Segmented>(StringArray{ "Tri", "Saw", "Sq" });
        shape->setMini(true);
        shape->setTooltip("The waveform of V and of the instrument's own vibrato.");
        shape->onChange = [this](int v) { edit("vibrato shape", [v](bank::Instrument& i) { i.vib.shape = bank::VibShape(std::clamp(v, 0, 2)); }); };
        auto dir = std::make_unique<Segmented>(StringArray{ utf8("\xe2\x86\x93"), utf8("\xe2\x86\x91") });
        dir->setMini(true);
        dir->setTooltip("Down swings between the note and the note minus the depth; up, between it and the note plus it.");
        dir->onChange = [this](int v) { edit("vibrato direction", [v](bank::Instrument& i) { i.vib.dir = v == 1 ? bank::VibDir::Up : bank::VibDir::Down; }); };
        w_->vibShape = shape.get();
        w_->vibDir = dir.get();
        const int sw = shape->preferredWidth(), dw = dir->preferredWidth(), h = shape->preferredHeight();
        auto pair = std::make_unique<Pair>(std::move(shape), sw, h, std::move(dir), dw, h);
        const int pw = pair->width();
        mod->add("Vibrato", std::move(pair), pw, h, "The shape the vibrato swings in, and which way it goes.");
    }
    w_->vibSpeed = stepper(*mod, "Speed", "V's x, 1-15: one cycle every 720/x pitch updates -- x/2 Hz in Fast, Step and Drum -- or every 96/x ticks in Tick.",
                           1, 15, 8, [ps = inst.pitchSpeed](int v) { return vibSpeedText(v, ps); }, [](bank::Instrument& i, int v) { i.vib.speed = uint8_t(v); }, 106);
    w_->vibDepth = stepper(*mod, "Depth", "V's y: LSDj's semitone table, an eighth of a semitone at 0 up to eight at 15. 0 leaves the instrument without a vibrato of its own.",
                           0, 15, 0, [](int v) { return vibDepthHint(v); }, [](bank::Instrument& i, int v) { i.vib.depth = uint8_t(v); }, 106);
    w_->vibDelay = stepper(*mod, "Delay", "Ticks after the note before the vibrato starts.", 0, 255, 0,
                           [](int v) { return tickText(v); }, [](bank::Instrument& i, int v) { i.vib.delay = uint8_t(v); });
    if (type != bank::InstrumentType::Noise) {
        w_->pitchSpeed = seg(*mod, "Pitch speed", "How P, L and V move.", { "Fast", "Tick", "Step", "Drum" },
                             [](bank::Instrument& i, int v) { i.pitchSpeed = bank::PitchSpeed(std::clamp(v, 0, 3)); });
        w_->pitchSpeed->setOptionTooltip(0, "Fast: 360 updates a second, tempo-independent.");
        w_->pitchSpeed->setOptionTooltip(1, "Tick: one update per tracker tick (24 a beat), so the effect follows the tempo.");
        w_->pitchSpeed->setOptionTooltip(2, "Step: as Fast, except P is an immediate offset instead of a bend.");
        w_->pitchSpeed->setOptionTooltip(3, type == bank::InstrumentType::Kit ? "Drum is not available on kits; a kit plays as Fast."
                                                                             : "Drum: as Fast, but P and L move in semitones, so a P kick falls logarithmically.");
        if (type == bank::InstrumentType::Kit) w_->pitchSpeed->setOptionEnabled(3, false);
    }
    w_->cmdRate = stepper(*mod, "Cmd rate", "0-15: R steps every rate + 1 ticks, and so do P and V when the pitch speed is Tick.",
                          0, 15, 0, [](int v) { return cmdRateText(v); }, [](bank::Instrument& i, int v) { i.cmdRate = uint8_t(v); }, 120);
    // The chord's own rate (docs/COMMANDS_AND_TEMPO.md section 37): at 0 a C
    // steps every tick, LSDj's speed, and a slower chord no longer slows R.
    w_->chordRate = stepper(*mod, "Chord rate", "0-15: a C chord steps every rate + 1 ticks. 0 is LSDj's one step a tick.",
                            0, 15, 0, [](int v) { return cmdRateText(v); }, [](bank::Instrument& i, int v) { i.chordRate = uint8_t(v); }, 120);

    // --- TABLE & NOTE BEHAVIOUR -------------------------------------------
    auto tab = std::make_unique<FormGroup>("Table & note behaviour");
    w_->table = stepper(*tab, "Table", "The table this instrument runs from note-on, unless an A overrides it. Right-click lists the bank, double-click opens it.",
                        0, bank::kTableSlots, 0,
                        [this](int v) { if (v == 0) return String("none"); const auto bk = processor.bank(); const bank::Table* t = bk ? bk->table(v) : nullptr; return t ? slotAndName(v, t->name) : slotAndName(v, "empty"); },
                        [](bank::Instrument& i, int v) { i.table = uint8_t(v); }, 190);
    w_->table->onList = [this] { showTableMenu(); };
    w_->tableMode = seg(*tab, "Table mode", "Tick: one row a tick, or per its own G. Step: one row every time the instrument is triggered.",
                        { "Tick", "Step" }, [](bank::Instrument& i, int v) { i.tableMode = v == 1 ? bank::TableMode::Step : bank::TableMode::Tick; });
    w_->transpose = seg(*tab, "Transpose", "Whether the table's transpose column applies.", { "On", "Off" }, [](bank::Instrument& i, int v) { i.transpose = v == 0; });
    w_->noteOff = seg(*tab, "Note-off", "Kill clears the DAC, which holds the level on this hardware; Release runs the shaped release.",
                      { "Kill", "Release", "Ignore" }, [](bank::Instrument& i, int v) { i.noteOff = bank::NoteOff(std::clamp(v, 0, 2)); });
    w_->overlap = seg(*tab, "Overlap", "A note over a held one: legato writes only the period; retrig starts the instrument again.",
                      { "Legato", "Retrig" }, [](bank::Instrument& i, int v) { i.overlap = v == 1 ? bank::Overlap::Retrig : bank::Overlap::Legato; });
    const bool longLength = type == bank::InstrumentType::Wave || type == bank::InstrumentType::Kit;
    w_->length = stepper(*tab, "Length", longLength ? "NR31: off, or 1-256." : "NRx1 bits 5-0: off, or 1-64.", 0, longLength ? 256 : 64, 0,
                         [](int v) { return v == 0 ? String("off") : ValueFormat::number(v); }, [](bank::Instrument& i, int v) { i.length = uint16_t(v); });

    // Two columns of groups: Sound over Pitch, Envelope over Table. The
    // envelope's picture is what makes the right column the taller one, and
    // the whole form fits the pane without scrolling (section 29).
    auto columns = std::make_unique<Columns>(kColumnGap);
    auto left = std::make_unique<Stack>(kGroupGap);
    left->add(std::move(sound));
    left->add(std::move(mod));
    auto right = std::make_unique<Stack>(kGroupGap);
    right->add(std::move(env));
    right->add(std::move(tab));
    columns->add(std::move(left));
    columns->add(std::move(right));
    stack->add(std::move(columns));

    scroll_.setContent(std::move(stack));
    syncValues();
}

/* ------------------------------------------------------- slot lists */

void InstrumentPanel::showTableMenu()
{
    const auto b = processor.bank();
    if (!b || !w_ || w_->table == nullptr) return;
    PopupMenu m;
    m.addSectionHeader("Table");
    const int current = w_->table->value();
    // The item's own tab, first (docs/COMMANDS_AND_TEMPO.md section 35).
    if (current > 0) {
        const auto* cur = b->table(current);
        m.addItem(kMenuOpen, "Open table " + (cur ? slotAndName(current, cur->name) : ValueFormat::number(current)) + " in its tab");
        m.addSeparator();
    }
    m.addItem(1, utf8("\xe2\x80\x93") + "   none", true, current == 0);
    for (int slot = 1; slot <= bank::kTableSlots; ++slot)
        if (const auto* t = b->table(slot)) m.addItem(slot + 1, slotAndName(slot, t->name), true, slot == current);
    Component::SafePointer<InstrumentPanel> safe(this);
    m.showMenuAsync(PopupMenu::Options().withTargetComponent(w_->table), [safe, current](int r) {
        if (safe == nullptr || r < 1 || safe->w_ == nullptr || safe->w_->table == nullptr) return;
        if (r == kMenuOpen) { safe->openSlot(ui::SlotKind::Table, current); return; }
        safe->w_->table->setValue(r - 1);
    });
}

void InstrumentPanel::showWaveMenu()
{
    const auto b = processor.bank();
    if (!b || !w_ || w_->wave == nullptr) return;
    PopupMenu m;
    m.addSectionHeader("Wave");
    const int current = w_->wave->value();
    if (const auto* cur = b->wave(current)) {
        m.addItem(kMenuOpen, "Open wave " + slotAndName(current, cur->name) + " in its tab");
        m.addSeparator();
    }
    for (int slot = 1; slot <= bank::kWaveSlots; ++slot)
        if (const auto* wv = b->wave(slot)) {
            PopupMenu::Item item(slotAndName(slot, wv->name));
            item.itemID = slot;
            item.isTicked = slot == current;
            item.shortcutKeyDescription = String(int(wv->frames.size())) + " fr";
            m.addItem(std::move(item));
        }
    Component::SafePointer<InstrumentPanel> safe(this);
    m.showMenuAsync(PopupMenu::Options().withTargetComponent(w_->wave), [safe, current](int r) {
        if (safe == nullptr || r < 1 || safe->w_ == nullptr || safe->w_->wave == nullptr) return;
        if (r == kMenuOpen) { safe->openSlot(ui::SlotKind::Wave, current); return; }
        safe->w_->wave->setValue(r);
    });
}

void InstrumentPanel::showKitMenu()
{
    const auto b = processor.bank();
    if (!b || !w_ || w_->kit == nullptr) return;
    PopupMenu m;
    m.addSectionHeader("Kit");
    const int current = w_->kit->value();
    if (const auto* cur = b->kit(current)) {
        m.addItem(kMenuOpen, "Open kit " + slotAndName(current, cur->name) + " in its tab");
        m.addSeparator();
    }
    for (int slot = 1; slot <= bank::kKitSlots; ++slot)
        if (const auto* k = b->kit(slot)) m.addItem(slot, slotAndName(slot, k->name), true, slot == current);
    Component::SafePointer<InstrumentPanel> safe(this);
    m.showMenuAsync(PopupMenu::Options().withTargetComponent(w_->kit), [safe, current](int r) {
        if (safe == nullptr || r < 1 || safe->w_ == nullptr || safe->w_->kit == nullptr) return;
        if (r == kMenuOpen) { safe->openSlot(ui::SlotKind::Kit, current); return; }
        safe->w_->kit->setValue(r);
    });
}

/* ------------------------------------------------------------- sync */

void InstrumentPanel::syncValues()
{
    if (!w_) return;
    const auto b = processor.bank();
    if (!b) return;
    const auto& i = b->instruments[size_t(slot_ - 1)];
    if (!i.used) return;
    auto& w = *w_;
    auto S = [](Segmented* s, int v) { if (s) s->setSelected(v, dontSendNotification); };
    auto T = [](Stepper* s, int v) { if (s) s->setValue(v, dontSendNotification); };
    if (w.head) {
        if (w.head->name.text() != String(i.name)) w.head->name.setText(String(i.name));
        w.head->type.setSelected(int(i.type), dontSendNotification);
    }
    S(w.duty, i.duty);
    if (w.dutySeq && w.dutySeq->text() != dutySeqText(i)) w.dutySeq->setText(dutySeqText(i));
    T(w.sweepRate, i.sweepRate); S(w.sweepDir, i.sweepDown ? 1 : 0); T(w.sweepShift, i.sweepShift);
    T(w.wave, i.wave); T(w.frameAdv, i.frameAdvance); S(w.frameLoop, int(i.frameLoop)); S(w.waveLevel, i.waveLevel);
    T(w.kit, i.kit); S(w.kitLoop, int(i.kitLoop));
    if (w.kitRate) {
        const bank::Kit* k = b->kit(i.kit);
        const uint16_t period = k ? k->period : uint16_t(1865);
        w.kitRate->setText(withThousands(int(std::lround(bank::sampleRateForPeriod(period)))) + " Hz");
    }
    S(w.lfsr, i.lfsr7 ? 1 : 0); S(w.pitchMode, i.noiseManual ? 1 : 0);
    T(w.shift, i.noiseShift); T(w.divisor, i.noiseDivisor); T(w.noiseSweep, i.noiseSweep);
    S(w.envMode, int(i.env.mode));
    T(w.envVol, i.envVol); S(w.envDir, int(i.envDir)); T(w.envRate, i.envRate);
    T(w.attack, i.env.attackTicks); T(w.peak, i.env.peak); T(w.decay, i.env.decayTicks); T(w.sustain, i.env.sustain); T(w.release, i.env.releaseTicks);
    S(w.attackCurve, int(i.env.attackCurve)); S(w.decayCurve, int(i.env.decayCurve)); S(w.releaseCurve, int(i.env.releaseCurve));
    S(w.vibShape, int(i.vib.shape)); S(w.vibDir, int(i.vib.dir)); T(w.vibSpeed, i.vib.speed); T(w.vibDepth, i.vib.depth); T(w.vibDelay, i.vib.delay);
    S(w.pitchSpeed, int(i.pitchSpeed)); T(w.cmdRate, i.cmdRate); T(w.chordRate, i.chordRate); S(w.tableMode, int(i.tableMode));
    T(w.table, i.table); S(w.transpose, i.transpose ? 0 : 1); S(w.noteOff, int(i.noteOff)); S(w.overlap, i.overlap == bank::Overlap::Retrig ? 1 : 0);
    T(w.length, i.length); S(w.pan, panIndex(i.pan));
    refreshDerived();
    updateUsedOn();
}

/// Everything the editor shows that is computed from a field rather than
/// held in one: the envelope picture, the readouts that say what a value is
/// worth, and the fields a mode greys out.
void InstrumentPanel::refreshDerived()
{
    if (!w_) return;
    const auto b = processor.bank();
    if (!b) return;
    const auto& i = b->instruments[size_t(slot_ - 1)];
    auto& w = *w_;
    const bool fourLevels = i.type == bank::InstrumentType::Wave || i.type == bank::InstrumentType::Kit;
    if (w.graph) w.graph->set(i, colours::channel(channel), fourLevels);
    if (w.shift) w.shift->setEnabled(i.noiseManual);
    if (w.divisor) w.divisor->setEnabled(i.noiseManual);
    // The vibrato speed reads in Hz or in cycles a bar, with the pitch speed.
    if (w.vibSpeed) {
        const auto ps = i.pitchSpeed;
        w.vibSpeed->setTextFunction([ps](int v) { return vibSpeedText(v, ps); });
    }
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
