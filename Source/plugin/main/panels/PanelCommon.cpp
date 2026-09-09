#include "plugin/main/panels/PanelCommon.h"

#include <algorithm>
#include <cmath>

namespace chipboy::plugin {

using namespace juce;
using namespace chipboy::ui;

/* ------------------------------------------------------------- text */

String RichText::toString() const
{
    String s;
    for (const auto& r : runs) s += r.text;
    return s;
}

AttributedString RichText::attributed(float px, Colour normal, Colour strong, Justification j) const
{
    AttributedString a;
    for (const auto& r : runs) a.append(r.text, r.bold ? Fonts::sans(px, true) : Fonts::sans(px), r.bold ? strong : normal);
    a.setJustification(j);
    a.setWordWrap(AttributedString::byWord);
    return a;
}

bool RichText::operator==(const RichText& o) const
{
    if (runs.size() != o.runs.size()) return false;
    for (size_t i = 0; i < runs.size(); ++i)
        if (runs[i].bold != o.runs[i].bold || runs[i].text != o.runs[i].text) return false;
    return true;
}

TextLine::TextLine(const String& text, Font font, Colour colour, Justification j)
    : text_(text), font_(font), colour_(colour), just_(j)
{
    setInterceptsMouseClicks(false, false);
}

TextLine* TextLine::label(const String& text)
{
    auto* t = new TextLine(text, Fonts::caption(10.0f), colours::textDim);
    t->setUpperCase(true);
    return t;
}

void TextLine::setText(const String& text)
{
    if (text == text_) return;
    text_ = text;
    repaint();
}

int TextLine::preferredWidth() const
{
    return int(std::ceil(GlyphArrangement::getStringWidth(font_, upper_ ? text_.toUpperCase() : text_))) + 2;
}

void TextLine::paint(Graphics& g)
{
    g.setFont(font_);
    g.setColour(colour_);
    g.drawText(upper_ ? text_.toUpperCase() : text_, getLocalBounds(), just_, false);
}

/* ----------------------------------------------------------- blocks */

HelpText::HelpText(const RichText& text, float px, Colour colour) : text_(text), px_(px), colour_(colour)
{
    setInterceptsMouseClicks(false, false);
}

void HelpText::setText(const RichText& text)
{
    if (text == text_) return;
    text_ = text;
    repaint();
}

int HelpText::preferredHeight(int width)
{
    const int w = maxWidth_ > 0 ? std::min(width, maxWidth_) : width;
    if (w <= 0 || text_.runs.empty()) return 0;
    TextLayout tl;
    tl.createLayout(text_.attributed(px_, colour_, colours::text), float(w));
    return int(std::ceil(tl.getHeight())) + 2;
}

void HelpText::paint(Graphics& g)
{
    const int w = maxWidth_ > 0 ? std::min(getWidth(), maxWidth_) : getWidth();
    text_.attributed(px_, colour_, colours::text).draw(g, Rectangle<float>(0.0f, 0.0f, float(w), float(getHeight())));
}

Hold::Hold(Component& c, int height, int maxWidth) : c_(c), height_(height), maxWidth_(maxWidth)
{
    addAndMakeVisible(c_);
}

void Hold::resized()
{
    const int w = maxWidth_ > 0 ? std::min(getWidth(), maxWidth_) : getWidth();
    c_.setBounds(0, 0, w, getHeight());
}

Field::Field(const String& label, const String& hint, std::unique_ptr<Component> control, int controlHeight, int controlWidth, int columns)
    : label_(label), hint_(hint), control_(std::move(control)), controlHeight_(controlHeight), controlWidth_(controlWidth), columns_(columns)
{
    if (control_) addAndMakeVisible(*control_);
}

Field::~Field() = default;

void Field::setHint(const String& h)
{
    if (h == hint_) return;
    hint_ = h;
    repaint();
}

int Field::preferredHeight(int) { return kCaption + controlHeight_; }

void Field::resized()
{
    if (!control_) return;
    const int w = controlWidth_ > 0 ? std::min(getWidth(), controlWidth_) : getWidth();
    control_->setBounds(0, kCaption, w, controlHeight_);
}

void Field::paint(Graphics& g)
{
    auto area = getLocalBounds().withHeight(kCaption).toFloat();
    const Font lf = Fonts::caption(10.0f);
    g.setFont(lf);
    g.setColour(colours::textDim);
    const String up = label_.toUpperCase();
    g.drawText(up, area, Justification::centredLeft, false);
    if (hint_.isNotEmpty()) {
        const float x = GlyphArrangement::getStringWidth(lf, up) + 8.0f;
        g.setFont(Fonts::mono(10.0f));
        g.drawText(hint_, area.withTrimmedLeft(x), Justification::centredLeft, true);
    }
}

FlowGrid::~FlowGrid() = default;

Field* FlowGrid::add(std::unique_ptr<Field> f)
{
    Field* raw = f.get();
    addAndMakeVisible(*raw);
    fields_.push_back(std::move(f));
    return raw;
}

void FlowGrid::clear()
{
    fields_.clear();
}

int FlowGrid::layout(int width, bool apply)
{
    constexpr int gapX = 16, gapY = 12, minCol = 150;
    const int ncols = std::max(1, (width + gapX) / (minCol + gapX));
    const int colW = std::max(1, (width - gapX * (ncols - 1)) / ncols);
    int col = 0, y = 0, rowH = 0;
    for (auto& f : fields_) {
        const int span = std::clamp(f->columns(), 1, ncols);
        if (col + span > ncols) { y += rowH + gapY; col = 0; rowH = 0; }
        const int h = f->preferredHeight(colW);
        if (apply) f->setBounds(col * (colW + gapX), y, span * colW + (span - 1) * gapX, h);
        rowH = std::max(rowH, h);
        col += span;
    }
    return fields_.empty() ? 0 : y + rowH;
}

int FlowGrid::preferredHeight(int width) { return layout(width, false); }
void FlowGrid::resized() { layout(getWidth(), true); }

/* ------------------------------------------------------------- form */

FormRow::FormRow(const String& label, std::unique_ptr<Component> control, int controlWidth, int controlHeight, int labelWidth)
    : label_(label), control_(std::move(control)), controlW_(controlWidth), controlH_(controlHeight), labelW_(labelWidth)
{
    addAndMakeVisible(*control_);
}
FormRow::~FormRow() = default;

int FormRow::preferredHeight(int) { return std::max(FormGroup::kRow, controlH_ + 2); }

void FormRow::resized()
{
    const int w = controlW_ > 0 ? std::min(controlW_, std::max(20, getWidth() - labelW_)) : std::max(20, getWidth() - labelW_);
    control_->setBounds(labelW_, (getHeight() - controlH_) / 2, w, controlH_);
}

void FormRow::paint(Graphics& g)
{
    g.setFont(ui::Fonts::sans(12.0f));
    g.setColour(ui::colours::textMute);
    g.drawText(label_, 0, 0, labelW_ - 8, getHeight(), Justification::centredLeft, false);
}

FormGroup::FormGroup(const String& caption, int labelWidth) : caption_(caption), labelWidth_(labelWidth) {}
FormGroup::~FormGroup() = default;

void FormGroup::addRow(std::unique_ptr<FormRow> row, const String& tip)
{
    if (tip.isNotEmpty()) row->setTooltip(tip);
    addAndMakeVisible(*row);
    rows_.push_back(std::move(row));
}

void FormGroup::addWide(std::unique_ptr<Block> b)
{
    addAndMakeVisible(*b);
    rows_.push_back(std::move(b));
}

int FormGroup::preferredHeight(int width)
{
    int h = kCaption;
    for (auto& r : rows_) h += r->preferredHeight(width);
    return h;
}

void FormGroup::resized()
{
    int y = kCaption;
    for (auto& r : rows_) {
        const int h = r->preferredHeight(getWidth());
        r->setBounds(0, y, getWidth(), h);
        y += h;
    }
}

void FormGroup::paint(Graphics& g)
{
    ui::draw::caption(g, caption_, { 0, 0, getWidth(), kCaption - 5 }, Justification::centredLeft, ui::colours::textDim, 10.0f);
    g.setColour(ui::colours::lineSoft);
    g.fillRect(0, kCaption - 5, getWidth(), 1);
}

Card::Card(const String& heading, std::unique_ptr<Block> content, const String& note)
    : heading_(heading), note_(note), content_(std::move(content))
{
    if (content_) addAndMakeVisible(*content_);
}

Card::~Card() = default;

void Card::setHeading(const String& h)
{
    if (h == heading_) return;
    heading_ = h;
    repaint();
}

int Card::preferredHeight(int width)
{
    const int inner = std::max(1, width - 2 * kPad);
    return kPad + kHeading + (content_ ? content_->preferredHeight(inner) : 0) + kPad;
}

void Card::resized()
{
    if (content_) content_->setBounds(kPad, kPad + kHeading, std::max(1, getWidth() - 2 * kPad), std::max(1, getHeight() - 2 * kPad - kHeading));
}

void Card::paint(Graphics& g)
{
    draw::panel(g, getLocalBounds(), colours::panel2, colours::lineSoft, 5.0f);
    auto area = getLocalBounds().reduced(kPad).withHeight(kHeading - 6);
    draw::heading(g, heading_, area);
    if (note_.isNotEmpty()) {
        const float w = draw::textWidth(Fonts::caption(11.0f, true), heading_.toUpperCase());
        g.setFont(Fonts::sans(11.0f));
        g.setColour(colours::textDim);
        g.drawText(note_, area.withTrimmedLeft(int(w) + 10), Justification::centredLeft, true);
    }
}

Stack::~Stack() = default;

Block* Stack::add(std::unique_ptr<Block> b)
{
    Block* raw = b.get();
    addAndMakeVisible(*raw);
    items_.push_back(std::move(b));
    return raw;
}

void Stack::clear() { items_.clear(); }

int Stack::preferredHeight(int width)
{
    int h = 0;
    bool first = true;
    for (auto& b : items_) {
        if (!b->isVisible()) continue;
        if (!first) h += gap_;
        first = false;
        h += b->preferredHeight(width);
    }
    return h;
}

void Stack::resized()
{
    int y = 0;
    bool first = true;
    for (auto& b : items_) {
        if (!b->isVisible()) continue;
        if (!first) y += gap_;
        first = false;
        const int h = b->preferredHeight(getWidth());
        b->setBounds(0, y, getWidth(), h);
        y += h;
    }
}

Columns::~Columns() = default;

Block* Columns::add(std::unique_ptr<Block> b)
{
    Block* raw = b.get();
    addAndMakeVisible(*raw);
    items_.push_back(std::move(b));
    return raw;
}

void Columns::setWidths(std::vector<int> fixedWidths) { widths_ = std::move(fixedWidths); }

int Columns::columnWidth(int index, int total) const
{
    const int n = int(items_.size());
    if (n == 0) return total;
    int fixed = 0, flexible = 0;
    for (int i = 0; i < n; ++i) {
        const int w = i < int(widths_.size()) ? widths_[size_t(i)] : 0;
        if (w > 0) fixed += w; else ++flexible;
    }
    const int mine = index < int(widths_.size()) ? widths_[size_t(index)] : 0;
    if (mine > 0) return mine;
    const int rest = total - gap_ * (n - 1) - fixed;
    return std::max(1, flexible > 0 ? rest / flexible : rest);
}

int Columns::preferredHeight(int width)
{
    int h = 0;
    for (int i = 0; i < int(items_.size()); ++i) h = std::max(h, items_[size_t(i)]->preferredHeight(columnWidth(i, width)));
    return h;
}

void Columns::resized()
{
    int x = 0;
    for (int i = 0; i < int(items_.size()); ++i) {
        const int w = columnWidth(i, getWidth());
        items_[size_t(i)]->setBounds(x, 0, w, getHeight());
        x += w + gap_;
    }
}

ScrollBlock::ScrollBlock()
{
    viewport_.setScrollBarsShown(true, false, true, false);
    addAndMakeVisible(viewport_);
}

ScrollBlock::~ScrollBlock()
{
    viewport_.setViewedComponent(nullptr, false);
}

void ScrollBlock::setContent(std::unique_ptr<Block> b)
{
    viewport_.setViewedComponent(nullptr, false);
    content_ = std::move(b);
    if (content_) viewport_.setViewedComponent(content_.get(), false);
    relayout();
}

void ScrollBlock::scrollToKeepVisible(int y, int height)
{
    const auto view = viewport_.getViewArea();
    if (view.getHeight() <= 0) return;
    if (y < view.getY()) viewport_.setViewPosition(view.getX(), std::max(0, y));
    else if (y + height > view.getBottom()) viewport_.setViewPosition(view.getX(), std::max(0, y + height - view.getHeight()));
}

void ScrollBlock::relayout()
{
    if (!content_) return;
    int w = std::max(1, getWidth());
    int h = content_->preferredHeight(w);
    if (h > getHeight()) { w = std::max(1, getWidth() - viewport_.getScrollBarThickness()); h = content_->preferredHeight(w); }
    content_->setSize(w, std::max(h, 1));
    content_->resized();
}

void ScrollBlock::resized()
{
    viewport_.setBounds(getLocalBounds());
    relayout();
}

TipBox::TipBox(Component& child)
{
    child.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(child);
}

void TipBox::resized()
{
    for (auto* c : getChildren()) c->setBounds(getLocalBounds());
}

void TipBox::mouseUp(const MouseEvent& e)
{
    if (onClick && e.mouseWasClicked() && getLocalBounds().contains(e.getPosition())) onClick();
}

/* ------------------------------------------------------- parameters */

RangedAudioParameter& param(ChipBoyProcessor& p, const String& id)
{
    auto* r = p.apvts.getParameter(id);
    jassert(r != nullptr);
    return *r;
}

int paramValue(const ChipBoyProcessor& p, const String& id)
{
    if (auto* v = p.apvts.getRawParameterValue(id)) return int(std::lround(v->load()));
    return 0;
}

float paramFloat(const ChipBoyProcessor& p, const String& id)
{
    if (auto* v = p.apvts.getRawParameterValue(id)) return v->load();
    return 0.0f;
}

String paramText(ChipBoyProcessor& p, const String& id)
{
    if (auto* r = p.apvts.getParameter(id)) return r->getCurrentValueAsText();
    return {};
}

void setParam(const Component& owner, RangedAudioParameter& p, float denormalised)
{
    if (auto* history = ui::historyFor(owner)) { history->setParameter(p, denormalised); return; }
    p.beginChangeGesture();
    p.setValueNotifyingHost(p.convertTo0to1(denormalised));
    p.endChangeGesture();
}

ToggleParam::ToggleParam(Button& b, RangedAudioParameter& p) : button_(b), param_(p)
{
    att_ = std::make_unique<ParameterAttachment>(p, [this](float v) { button_.setToggleState(v >= 0.5f, dontSendNotification); });
    att_->sendInitialUpdate();
    button_.onClick = [this] {
        const float v = button_.getToggleState() ? 1.0f : 0.0f;
        if (auto* history = ui::historyFor(button_)) history->setParameter(param_, v);
        else att_->setValueAsCompleteGesture(v);
    };
}

ToggleParam::~ToggleParam() { button_.onClick = nullptr; }

SegmentedParam::SegmentedParam(Segmented& seg, RangedAudioParameter& p, std::vector<int> valueForOption)
    : seg_(seg), p_(p), map_(std::move(valueForOption))
{
    att_ = std::make_unique<ParameterAttachment>(p, [this](float v) {
        const int iv = int(std::lround(v));
        for (int i = 0; i < int(map_.size()); ++i)
            if (map_[size_t(i)] == iv) { seg_.setSelected(i, dontSendNotification); return; }
    });
    seg_.onChange = [this](int i) {
        if (i < 0 || i >= int(map_.size())) return;
        const float v = float(map_[size_t(i)]);
        if (auto* history = ui::historyFor(seg_)) history->setParameter(p_, v);
        else att_->setValueAsCompleteGesture(v);
    };
    att_->sendInitialUpdate();
}

SegmentedParam::~SegmentedParam() { seg_.onChange = nullptr; }

StepperParam::StepperParam(Stepper& stepper, RangedAudioParameter& p, float scale, int lo, int hi)
    : stepper_(stepper), p_(p), scale_(scale)
{
    stepper_.setRange(lo, hi, int(std::lround(p.convertFrom0to1(p.getDefaultValue()) / scale_)));
    att_ = std::make_unique<ParameterAttachment>(p, [this](float v) { stepper_.setValue(int(std::lround(v / scale_)), dontSendNotification); });
    stepper_.onChange = [this](int v) {
        const float d = float(v) * scale_;
        if (auto* history = ui::historyFor(stepper_)) history->setParameter(p_, d);
        else att_->setValueAsCompleteGesture(d);
    };
    att_->sendInitialUpdate();
}

StepperParam::~StepperParam() { stepper_.onChange = nullptr; }

ParamWatch::ParamWatch(RangedAudioParameter& p, std::function<void(float)> fn)
{
    att_ = std::make_unique<ParameterAttachment>(p, std::move(fn));
    att_->sendInitialUpdate();
}

/* --------------------------------------------------------- the tracker */

TrackerPosition trackerPosition(const ChipBoyProcessor& p)
{
    TrackerPosition t;
    t.playing = p.transportPlaying();
    t.tick = std::max<int64_t>(0, p.trackerTick());
    const auto s = p.song();
    // Each channel's rows lie end to end on its own prefix table, so two
    // channels are in different rows at the same tick (section 25).
    for (int ch = 0; ch < 4; ++ch) {
        if (s) tracker::rowAtTick(*s, ch, t.tick, t.row[ch], t.inRow[ch]);
        else { t.row[ch] = int(t.tick / tracker::kEmptyRowTicks); t.inRow[ch] = int(t.tick % tracker::kEmptyRowTicks); }
    }
    return t;
}

int playingStepOf(const ChipBoyProcessor& p, const tracker::Song& s, int ch, int row, int inRow)
{
    const tracker::Phrase* phrase = s.phrase(s.phraseAt(ch, row));
    const int steps = s.stepsOfRow(ch, row);
    int start[tracker::kMaxSteps + 1];
    tracker::stepStartTicks(s, phrase, p.player().groove(ch), start);
    const int length = tracker::phraseTicks(s, phrase);
    int step = -1;
    for (int i = 0; i < steps; ++i) {
        if (start[i] >= length || start[i] > inRow) break;     // that step never fires, or has not come yet
        step = i;
    }
    return step;
}

/* ----------------------------------------------------------- lookups */

int modelIndex(const ChipBoyProcessor& p) { return std::clamp(paramValue(p, ids::model), 0, 2); }

double analogCornerHz(const ChipBoyProcessor& p)
{
    const int m = modelIndex(p);
    if (m == 2) return 0.0;
    if (m == 0) return 25.0;
    const int mod = std::clamp(paramValue(p, ids::bassMod), 0, 2);
    return 338.0 / (mod == 0 ? 1.0 : mod == 1 ? 10.0 : 47.0);
}

String modelName(int index) { return index == 1 ? "CGB" : index == 2 ? "RAW" : "DMG"; }
Colour modelColour(int index) { return index == 1 ? colours::cgb : index == 2 ? colours::textMute : colours::wav; }

String channelSourceText(ChipBoyProcessor& p, int ch, bool* voiceOwned)
{
    const bool owned = (p.voiceOwnedMask() & (1u << ch)) != 0;
    if (voiceOwned) *voiceOwned = owned;
    if (owned) {
        String n = p.voiceName(ch);
        if (n.isEmpty()) n = "Voice";
        return "Voice: " + n;
    }
    return paramText(p, channelParamId(ch, ids::source));
}

Colour instrumentKindColour(int kind)
{
    return kind == 0 ? colours::pu1 : kind == 1 ? colours::wav : kind == 2 ? colours::pu2 : kind == 3 ? colours::noi : colours::textDim;
}

String instrumentTypeName(bank::InstrumentType t)
{
    const int k = int(t);
    return k == 0 ? "Pulse" : k == 1 ? "Wave" : k == 2 ? "Kit" : "Noise";
}

bank::InstrumentType channelInstrumentType(int ch)
{
    return ch == 2 ? bank::InstrumentType::Wave : ch == 3 ? bank::InstrumentType::Noise : bank::InstrumentType::Pulse;
}

bool instrumentFitsChannel(bank::InstrumentType t, int ch)
{
    if (ch == 2) return t == bank::InstrumentType::Wave || t == bank::InstrumentType::Kit;
    if (ch == 3) return t == bank::InstrumentType::Noise;
    return t == bank::InstrumentType::Pulse;
}

String shortUuid(const String& uuid)
{
    const String s = uuid.removeCharacters("-{}");
    if (s.length() <= 9) return s;
    return s.substring(0, 4) + juce::String(CharPointer_UTF8("\xe2\x80\xa6")) + s.substring(s.length() - 4);
}

String withThousands(int v)
{
    const bool neg = v < 0;
    String digits(std::abs(v));
    String out;
    int count = 0;
    for (int i = digits.length() - 1; i >= 0; --i) {
        out = digits.substring(i, i + 1) + out;
        if (++count % 3 == 0 && i > 0) out = " " + out;
    }
    return neg ? "-" + out : out;
}

String slotAndName(int slot, const std::string& name)
{
    return ValueFormat::slot(slot).paddedLeft('0', 2) + " " + String(name);
}

} // namespace chipboy::plugin
