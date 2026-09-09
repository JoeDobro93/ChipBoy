#include "plugin/main/panels/HardwarePanel.h"

#include <cmath>

namespace chipboy::plugin {

using namespace juce;
using namespace chipboy::ui;

namespace {
constexpr int kRowPad = 8, kRowInset = 10;
String middot() { return String(CharPointer_UTF8(" \xc2\xb7 ")); }
String emdash() { return String(CharPointer_UTF8("\xe2\x80\x94")); }
String minus() { return String(CharPointer_UTF8("\xe2\x88\x92")); }
String times() { return String(CharPointer_UTF8("\xc3\x97")); }
String arrow() { return String(CharPointer_UTF8("\xe2\x86\x92")); }
const Identifier kTraceProp("ui_trace"), kPeriodsProp("ui_periods");

int textHeight(const String& text, float px, int width)
{
    if (width <= 0 || text.isEmpty()) return 0;
    AttributedString a;
    a.append(text, Fonts::sans(px), colours::textMute);
    a.setWordWrap(AttributedString::byWord);
    TextLayout tl;
    tl.createLayout(a, float(width));
    return int(std::ceil(tl.getHeight())) + 2;
}
}

/* ------------------------------------------------------ model cards */

class HardwarePanel::ModelCard : public Button {
public:
    ModelCard(const String& title, Colour colour, const String& description) : Button(title), name_(title), colour_(colour), text_(description) {}
    int preferredHeight(int width) const { return 12 + 16 + 6 + textHeight(text_, 12.0f, width - 24) + 12; }
    void paintButton(Graphics& g, bool over, bool down) override
    {
        const bool on = getToggleState();
        draw::panel(g, getLocalBounds(), over || down ? colours::panel : colours::panel2, on ? colours::accent : colours::lineSoft, 5.0f);
        if (on) { g.setColour(colours::accent); g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(1.5f), 4.0f, 1.0f); }
        g.setFont(Fonts::pixel(12.0f));
        g.setColour(colour_);
        g.drawText(name_, 12, 12, getWidth() - 24, 16, Justification::centredLeft, false);
        AttributedString a;
        a.append(text_, Fonts::sans(12.0f), colours::textMute);
        a.setWordWrap(AttributedString::byWord);
        a.draw(g, Rectangle<float>(12.0f, 34.0f, float(getWidth() - 24), float(getHeight() - 46)));
    }
private:
    String name_; Colour colour_; String text_;
};

class HardwarePanel::ModelRow : public Block {
public:
    explicit ModelRow(std::function<void(int)> onPick)
    {
        const char* texts[] = {
            "The brick. Wave RAM locked while playing, trigger corruption, DACs that hold. Coupling at 25 Hz \xe2\x80\x94 the fat one. Measured 2026-09-07.",
            "Live wave RAM, no corruption bug. Coupling at 338 Hz \xe2\x80\x94 thin, with a louder LCD line. Measured on one unit; its board revision matters.",
            "The DMG chip with no analog stage: the clean digital mix a gaming emulator makes. DAC-off is silence, no coupling, no noise, no DC clicks.",
        };
        for (int i = 0; i < 3; ++i) {
            cards_[size_t(i)] = std::make_unique<ModelCard>(modelName(i), modelColour(i), String(CharPointer_UTF8(texts[i])));
            cards_[size_t(i)]->setTooltip("Emulate the " + modelName(i) + (i == 2 ? " mix: chip only, analog stage bypassed" : ""));
            cards_[size_t(i)]->onClick = [onPick, i] { onPick(i); };
            addAndMakeVisible(*cards_[size_t(i)]);
        }
    }
    void setModel(int m) { for (int i = 0; i < 3; ++i) cards_[size_t(i)]->setToggleState(i == m, dontSendNotification); }
    int preferredHeight(int width) override
    {
        const int colW = (width - 20) / 3;
        int h = 0;
        for (auto& c : cards_) h = std::max(h, c->preferredHeight(colW));
        return h;
    }
    void resized() override
    {
        const int colW = (getWidth() - 20) / 3;
        for (int i = 0; i < 3; ++i) cards_[size_t(i)]->setBounds(i * (colW + 10), 0, colW, getHeight());
    }
private:
    std::array<std::unique_ptr<ModelCard>, 3> cards_;
};

/* ------------------------------------------------------------- rows */

/// A switch row: the Toggle carries the title and the fact line as its
/// description; the explanation sits under it (the mockup's .toggle-row).
class HardwarePanel::ToggleRow : public Block {
public:
    ToggleRow(const String& title, const String& desc, const String& fact) : toggle(title), desc_(RichText(desc)), fact_(fact)
    {
        toggle.setDescription(fact);
        addAndMakeVisible(toggle);
        addAndMakeVisible(desc_);
    }
    void setFact(const String& f)
    {
        if (f == fact_) return;
        fact_ = f;
        toggle.setDescription(f);
    }
    void setExtra(std::unique_ptr<Component> c, int width)
    {
        extra_ = std::move(c);
        extraW_ = width;
        addAndMakeVisible(*extra_);
    }
    void setDimmed(bool d)
    {
        if (d == dimmed_) return;
        dimmed_ = d;
        setAlpha(d ? 0.45f : 1.0f);
        toggle.setEnabled(!d);
        if (extra_) extra_->setEnabled(!d);
    }
    int toggleWidth(int width) const { return width - 2 * kRowInset - (extra_ ? extraW_ + 10 : 0); }
    int preferredHeight(int width) override
    {
        return kRowPad + toggle.preferredHeight(toggleWidth(width)) + 2 + desc_.preferredHeight(width - 2 * kRowInset) + kRowPad;
    }
    void resized() override
    {
        const int tw = toggleWidth(getWidth());
        const int th = toggle.preferredHeight(tw);
        toggle.setBounds(kRowInset, kRowPad, tw, th);
        if (extra_) extra_->setBounds(getWidth() - kRowInset - extraW_, kRowPad, extraW_, Stepper::kHeight);
        desc_.setBounds(kRowInset, kRowPad + th + 2, getWidth() - 2 * kRowInset, std::max(0, getHeight() - th - 2 * kRowPad - 2));
    }
    void paint(Graphics& g) override { draw::panel(g, getLocalBounds(), colours::well, colours::lineSoft, 4.0f); }
    Toggle toggle;
private:
    HelpText desc_;
    String fact_;
    std::unique_ptr<Component> extra_;
    int extraW_ = 0;
    bool dimmed_ = false;
};

/// A row with a Segmented at the right: title, explanation, fact line.
class HardwarePanel::ChoiceRow : public Block {
public:
    ChoiceRow(const String& title, const String& desc, const String& fact, const StringArray& options)
        : seg(options), title_(title), desc_(RichText(desc)), fact_(fact)
    {
        seg.setMini(true);
        addAndMakeVisible(seg);
        addAndMakeVisible(desc_);
    }
    void setFact(const String& f) { if (f == fact_) return; fact_ = f; repaint(); }
    void setDimmed(bool d)
    {
        if (d == dimmed_) return;
        dimmed_ = d;
        setAlpha(d ? 0.45f : 1.0f);
        seg.setEnabled(!d);
    }
    int textWidth(int width) const { return width - 2 * kRowInset - seg.preferredWidth() - 12; }
    int preferredHeight(int width) override
    {
        return kRowPad + 18 + desc_.preferredHeight(textWidth(width)) + (fact_.isNotEmpty() ? 15 : 0) + kRowPad;
    }
    void resized() override
    {
        const int sw = seg.preferredWidth(), sh = seg.preferredHeight();
        seg.setBounds(getWidth() - kRowInset - sw, (getHeight() - sh) / 2, sw, sh);
        const int tw = textWidth(getWidth());
        desc_.setBounds(kRowInset, kRowPad + 18, tw, desc_.preferredHeight(tw));
    }
    void paint(Graphics& g) override
    {
        draw::panel(g, getLocalBounds(), colours::well, colours::lineSoft, 4.0f);
        g.setFont(Fonts::sans(13.0f, true));
        g.setColour(colours::text);
        g.drawText(title_, kRowInset, kRowPad, textWidth(getWidth()), 18, Justification::centredLeft, false);
        if (fact_.isNotEmpty()) {
            g.setFont(Fonts::mono(10.5f));
            g.setColour(colours::textDim);
            g.drawText(fact_, kRowInset, getHeight() - kRowPad - 14, textWidth(getWidth()), 14, Justification::centredLeft, true);
        }
    }
    Segmented seg;
private:
    String title_; HelpText desc_; String fact_; bool dimmed_ = false;
};

/* ------------------------------------------------------------ panel */

ScopeView::Trace HardwarePanel::storedTrace(const ChipBoyProcessor& p)
{
    const int t = std::clamp(int(p.apvts.state.getProperty(kTraceProp, 0)), 0, 2);
    return t == 1 ? ScopeView::Trace::Analog : t == 2 ? ScopeView::Trace::Both : ScopeView::Trace::Digital;
}

int HardwarePanel::storedPeriods(const ChipBoyProcessor& p)
{
    const int v = int(p.apvts.state.getProperty(kPeriodsProp, 2));
    return v == 1 || v == 2 || v == 4 || v == 8 ? v : 2;
}

HardwarePanel::HardwarePanel(ChipBoyProcessor& p) : EditorPanel(p)
{
    addAndMakeVisible(scroll_);
    auto stack = std::make_unique<Stack>(14);

    auto models = std::make_unique<ModelRow>([this](int i) { setParam(*this, param(processor, ids::model), float(i)); });
    models_ = models.get();
    stack->add(std::move(models));
    modelWatch_ = std::make_unique<ParamWatch>(param(processor, ids::model), [this](float v) { if (models_) models_->setModel(int(std::lround(v))); });

    auto groups = std::make_unique<Columns>(12);

    // --- hardware states -------------------------------------------------------
    auto states = std::make_unique<Stack>(8);
    {
        auto r = std::make_unique<ToggleRow>("Headphone Noise", "The broadband hiss and the 59.7 Hz frame hum, at the measured levels. The display's line has its own switch.", "");
        r->toggle.attach(param(processor, ids::noise));
        noise_ = r.get();
        states->add(std::move(r));
    }
    {
        auto r = std::make_unique<ToggleRow>("LCD Whine", "The 9198 Hz line the display puts into the headphones, and its harmonic. Off removes it the way switching the display off does; it is independent of Headphone Noise. \"Disable the whine\" is this.", "");
        r->toggle.attach(param(processor, ids::lcd));
        lcd_ = r.get();
        states->add(std::move(r));
    }
    {
        // Section 26: a program on the Game Boy cannot wait for a pulse's low
        // half, so neither does ChipBoy. The switch has no effect and stays
        // only so that a saved project's parameter list is the one it wrote.
        auto r = std::make_unique<ToggleRow>("Volume writes at edges", "No effect. Level changes are zombie-mode NRx2 writes now, which a driver on the hardware can really make; waiting for the pulse's low half is something no program on the console can do.",
                                             "no effect");
        r->toggle.attach(param(processor, ids::volEdges));
        edges_ = r.get();
        states->add(std::move(r));
    }
    {
        auto r = std::make_unique<ChoiceRow>("CGB bass mod", "The common capacitor swap on the output. Corner scales with the capacitor. Approximation " + emdash() + " no modded unit measured.",
                                             "stock 338 Hz" + middot() + times() + "10 " + arrow() + " 34 Hz" + middot() + times() + "47 " + arrow() + " 7 Hz",
                                             StringArray{ "stock", times() + "10", times() + "47" });
        r->seg.attach(param(processor, ids::bassMod));
        bassMod_ = r.get();
        states->add(std::move(r));
    }
    {
        auto r = std::make_unique<ToggleRow>("Pro Sound tap", "Output taken before the amplifier and volume wheel. A later version.", "post-v1");
        r->setDimmed(true);
        proSound_ = r.get();
        states->add(std::move(r));
    }
    groups->add(std::make_unique<Card>("Hardware states " + emdash() + " legal on a real unit", std::move(states)));

    // --- departures and display ---------------------------------------------------
    auto right = std::make_unique<Stack>(12);
    auto departures = std::make_unique<Stack>(8);
    {
        auto r = std::make_unique<ToggleRow>("Tame DAC clicks", "Crossfades each DAC-on step instead of stepping it. Not what any Game Boy does.", "");
        r->toggle.attach(param(processor, ids::declick));
        auto ms = std::make_unique<Stepper>();
        ms->setTooltip("Crossfade length, 0.5-5 ms");
        ms->setTextFunction([](int v) { return String(v * 0.5, 1) + " ms"; });
        tameMs_ = std::make_unique<StepperParam>(*ms, param(processor, ids::declickMs), 0.5f, 1, 10);
        r->setExtra(std::move(ms), 96);
        tame_ = r.get();
        departures->add(std::move(r));
    }
    {
        auto r = std::make_unique<ToggleRow>("Soften master pops", "Ramps NR50 changes instead of stepping the DC offset the held DACs sit on.", "");
        r->toggle.attach(param(processor, ids::soften));
        soften_ = r.get();
        departures->add(std::move(r));
    }
    right->add(std::make_unique<Card>("Departures " + emdash() + " not what any Game Boy does; the header reads MODIFIED while one is on", std::move(departures)));

    auto display = std::make_unique<Stack>(8);
    {
        auto r = std::make_unique<ChoiceRow>("Scope trace", "Digital is the staircase into the DAC; analog is what leaves the machine.", "", StringArray{ "Digital", "Analog", "Both" });
        const auto t = storedTrace(processor);
        r->seg.setSelected(t == ScopeView::Trace::Analog ? 1 : t == ScopeView::Trace::Both ? 2 : 0, dontSendNotification);
        r->seg.onChange = [this](int i) { processor.apvts.state.setProperty(kTraceProp, i, nullptr); announceDisplay(); };
        trace_ = r.get();
        display->add(std::move(r));
    }
    {
        auto r = std::make_unique<ChoiceRow>("Periods shown", "How many periods of the channel's own frequency register fill a scope, so a held note is a still picture. Noise uses a fixed window.", "", StringArray{ "1", "2", "4", "8" });
        const int pv = storedPeriods(processor);
        r->seg.setSelected(pv == 1 ? 0 : pv == 2 ? 1 : pv == 4 ? 2 : 3, dontSendNotification);
        r->seg.onChange = [this](int i) { processor.apvts.state.setProperty(kPeriodsProp, i == 0 ? 1 : i == 1 ? 2 : i == 2 ? 4 : 8, nullptr); announceDisplay(); };
        periods_ = r.get();
        display->add(std::move(r));
    }
    {
        auto r = std::make_unique<ChoiceRow>("Values", "Decimal everywhere, or hex for the LSDj habit. Registers are always hex.", "", StringArray{ "Decimal", "Hex" });
        r->seg.attach(param(processor, ids::hexDisplay));
        values_ = r.get();
        display->add(std::move(r));
    }
    right->add(std::make_unique<Card>("Display", std::move(display)));
    groups->add(std::move(right));
    stack->add(std::move(groups));
    scroll_.setContent(std::move(stack));
    refreshFacts();
}

HardwarePanel::~HardwarePanel() = default;

RichText HardwarePanel::contextLine() const
{
    RichText r;
    r.plain("Model ").bold(modelName(modelIndex(processor)));
    return r;
}

void HardwarePanel::announceDisplay()
{
    if (onDisplaySettings) onDisplaySettings(storedTrace(processor), storedPeriods(processor));
}

void HardwarePanel::refreshFacts()
{
    const int model = modelIndex(processor);
    const int noiseOn = paramValue(processor, ids::noise) != 0 ? 1 : 0;
    if (model == lastModel_ && noiseOn == lastNoise_) return;
    lastModel_ = model;
    lastNoise_ = noiseOn;
    const bool raw = model == 2, cgb = model == 1;
    if (noise_) {
        noise_->setFact(raw ? String("no analog stage in RAW") : "measured: floor " + minus() + "58 dB, frame hum at 59.7 Hz");
        noise_->setDimmed(raw);
    }
    if (lcd_) {
        lcd_->setFact(cgb ? "measured +43 dB over the floor; this CGB unit kept its line with the display off (+41 dB) " + emdash() + " modelled as measured"
                          : "measured +26 dB over the floor; DMG: the line drops 24 dB with LCDC bit 7 clear");
        // The whine is its own switch now (section 21): it does not follow
        // the hiss, so only RAW -- which has no analog stage -- dims it.
        lcd_->setDimmed(raw);
    }
    if (bassMod_) bassMod_->setDimmed(!cgb);
    if (tame_) { tame_->setFact(raw ? String("RAW has no DC steps to tame") : String()); tame_->setDimmed(raw); }
    if (soften_) soften_->setDimmed(raw);
    scroll_.relayout();
    contextChanged();
}

void HardwarePanel::tick()
{
    refreshFacts();
}

void HardwarePanel::resized()
{
    scroll_.setBounds(getLocalBounds());
}

} // namespace chipboy::plugin
