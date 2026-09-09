#include "plugin/ui/CommandSlot.h"

#include <algorithm>
#include <cmath>

namespace chipboy::ui {

namespace {

constexpr int kTypeWidth = 54, kGap = 4, kMinArg = 52;

/// What one argument reads as on its stepper. The letter decides: O is a
/// pan position, P is a byte read as two's complement (section 34), E's y
/// carries the direction.
juce::String argText(bank::Cmd cmd, int arg, int v)
{
    if (cmd == bank::Cmd::O && arg == 0) { static const char* n[] = { "off", "L", "R", "LR" }; return n[v & 3]; }
    if (cmd == bank::Cmd::P && arg == 0) { const int s = (v & 255) >= 128 ? (v & 255) - 256 : (v & 255); return (s > 0 ? "+" : "") + juce::String(s); }
    if (cmd == bank::Cmd::E && arg == 1) return juce::String::charToString((v & 8) ? 0x2191 : 0x2193) + juce::String(v & 7);
    return juce::String(v);
}

juce::String hexByte(int v) { return juce::String::toHexString(v & 255).paddedLeft('0', 2).toUpperCase(); }

} // namespace

struct CommandSlot::Impl {
    CommandSlot& owner;
    juce::String label;
    plugin::ChannelKind kind;

    juce::ComboBox type;
    Stepper arg[2];
    /// Hex shows the byte instead of the arguments (section 34). It is not
    /// attached to a parameter: it writes both of them at once.
    Stepper byteArg;
    juce::RangedAudioParameter* xParam = nullptr;
    juce::RangedAudioParameter* yParam = nullptr;
    std::unique_ptr<juce::ParameterAttachment> typeAtt, argAtt[2];
    int choice = 0, value[2] = { 0, 0 };
    bool asByte = false;
    juce::String caption;
    bool inert = false;
    juce::String inertWhy;

    Impl(CommandSlot& o, const juce::String& l, plugin::ChannelKind k) : owner(o), label(l), kind(k) {}

    bank::Cmd cmd() const { return plugin::cmdFromChoice(choice); }
    bank::Command command() const { bank::Command c; c.cmd = cmd(); c.a = int16_t(value[0]); c.b = int16_t(value[1]); return c; }

    /// The letters, greyed where this channel cannot use them, with each
    /// letter's name beside it.
    void buildMenu()
    {
        const auto choices = plugin::commandChoices();
        type.clear(juce::dontSendNotification);
        auto* menu = type.getRootMenu();
        for (int i = 0; i < choices.size(); ++i) {
            const auto c = plugin::cmdFromChoice(i);
            const auto* info = plugin::commandInfo(c);
            juce::PopupMenu::Item item(choices[i]);
            item.itemID = i + 1;
            if (info != nullptr) item.shortcutKeyDescription = info->name;
            item.isEnabled = i == 0 || plugin::commandAppliesTo(c, kind);
            menu->addItem(std::move(item));
        }
        type.setSelectedId(choice + 1, juce::dontSendNotification);
    }

    /// The letter changed, or the display did: the steppers take its ranges,
    /// its readouts and its tooltips, and Hex folds them into one byte
    /// (section 34).
    void applyLetter()
    {
        const auto c = cmd();
        const auto* info = plugin::commandInfo(c);
        const int n = info != nullptr ? info->nargs : 0;
        asByte = ValueFormat::hex() && n > 0;
        byteArg.setVisible(asByte);
        for (int i = 0; i < 2; ++i) {
            const bool used = !asByte && i < n;
            arg[i].setVisible(used);
            if (i >= n) continue;
            // The slot's arguments are host parameters, so they are bytes: a
            // letter that reaches past one (T's 295 BPM) is typed in a cell.
            const int hi = std::min(info->hi[i], 255);
            arg[i].setRange(info->lo[i], hi, std::min(info->def[i], hi));
            value[i] = arg[i].value();       // setRange may have clamped it into the letter's range
            arg[i].setTextFunction([c, i](int v) { return argText(c, i, v); });
            // P is stored two's complement, so the box takes "-73" and puts
            // the byte back (section 34).
            if (c == bank::Cmd::P && i == 0)
                arg[i].setEntryFormat([](int v) { return juce::String((v & 255) >= 128 ? (v & 255) - 256 : (v & 255)); },
                                      [](const juce::String& t, int& out) {
                                          const juce::String s = t.trim().replaceCharacter(juce::juce_wchar(0x2212), '-');
                                          if (s.isEmpty()) return false;
                                          const int v = s.getIntValue();
                                          if (v < -128 || v > 127) return false;
                                          out = v & 255;
                                          return true;
                                      });
            else arg[i].setEntryFormat({}, {});
            arg[i].setTooltip(juce::String(i == 0 ? "x" : "y") + juce::String(juce::CharPointer_UTF8(" \xe2\x80\x94 ")) + info->args);
        }
        if (asByte) {
            byteArg.setRange(0, 255, 0);
            byteArg.setTextFunction([](int v) { return hexByte(v); });
            byteArg.setEntryFormat([](int v) { return hexByte(v); },
                                   [](const juce::String& t, int& out) {
                                       const juce::String s = t.trim();
                                       if (s.isEmpty() || s.length() > 2) return false;
                                       for (int i = 0; i < s.length(); ++i) {
                                           const auto d = s[i];
                                           if (!((d >= '0' && d <= '9') || (d >= 'a' && d <= 'f') || (d >= 'A' && d <= 'F'))) return false;
                                       }
                                       out = s.getHexValue32() & 255;
                                       return true;
                                   });
            byteArg.setTooltip(juce::String(info->name) + juce::String(juce::CharPointer_UTF8(" \xe2\x80\x94 ")) + info->args
                               + ".  The byte a playback ROM carries; two digits set it.");
        }
        type.setTooltip(info != nullptr ? juce::String(info->name) + juce::String(juce::CharPointer_UTF8(" \xe2\x80\x94 ")) + info->args
                                        : juce::String("No command in this slot. A letter fires at the next tick and again at every note-on."));
        refreshCaption();
        owner.resized();
    }

    /// One typed byte becomes both arguments, as one gesture on the history.
    void writeByte(int byte)
    {
        bank::Command c = command();
        if (!plugin::setCommandByte(c, byte)) { byteArg.setValue(plugin::commandByte(command()), juce::dontSendNotification); return; }
        auto* history = historyFor(owner);
        if (history != nullptr) history->beginGesture(label + " " + hexByte(byte));
        const auto set = [&](juce::RangedAudioParameter* p, juce::ParameterAttachment& att, float v) {
            if (p == nullptr) return;
            if (history != nullptr) history->setParameter(*p, v);
            else att.setValueAsCompleteGesture(v);
        };
        set(xParam, *argAtt[0], float(std::clamp<int>(c.a, 0, 255)));
        set(yParam, *argAtt[1], float(std::clamp<int>(c.b, 0, 255)));
        if (history != nullptr) history->endGesture();
    }

    void refreshCaption()
    {
        const bank::Command c = command();
        if (asByte) byteArg.setValue(plugin::commandByte(c), juce::dontSendNotification);
        const juce::String text = plugin::commandArgText(c);
        if (text == caption) return;
        caption = text;
        owner.repaint();
    }
};

CommandSlot::CommandSlot(const juce::String& label, plugin::ChannelKind kind)
    : impl_(std::make_unique<Impl>(*this, label, kind))
{
    auto& im = *impl_;
    // The wheel never edits: a letter is chosen, not scrolled past
    // (UI_DESIGN section 2.1). The arguments are Steppers, which are typed.
    im.type.setScrollWheelEnabled(false);
    addAndMakeVisible(im.type);
    for (auto& a : im.arg) addAndMakeVisible(a);
    addChildComponent(im.byteArg);
    im.byteArg.onChange = [this](int v) { impl_->writeByte(v); };
    im.buildMenu();
    im.applyLetter();
}

CommandSlot::~CommandSlot() = default;

void CommandSlot::attach(juce::RangedAudioParameter& type, juce::RangedAudioParameter& x, juce::RangedAudioParameter& y)
{
    auto& im = *impl_;
    im.xParam = &x;
    im.yParam = &y;
    juce::RangedAudioParameter* args[2] = { &x, &y };
    for (int i = 0; i < 2; ++i) {
        im.arg[i].attach(*args[i]);
        // The parameter, not the stepper: a parameter's listeners run in an
        // order we do not control, so the caption reads what the host holds.
        im.argAtt[i] = std::make_unique<juce::ParameterAttachment>(*args[i], [this, i](float v) {
            impl_->value[i] = juce::roundToInt(v);
            impl_->refreshCaption();
        });
        im.argAtt[i]->sendInitialUpdate();
    }
    im.typeAtt = std::make_unique<juce::ParameterAttachment>(type, [this](float v) {
        impl_->choice = juce::roundToInt(v);
        impl_->type.setSelectedId(impl_->choice + 1, juce::dontSendNotification);
        impl_->applyLetter();
    });
    im.typeAtt->sendInitialUpdate();
    // Picking a letter here does what picking one in a cell does: it arrives
    // with the arguments a fresh command has.
    im.type.onChange = [this, typeParam = &type, xParam = &x, yParam = &y] {
        auto& in = *impl_;
        const int id = in.type.getSelectedId();
        if (id < 1 || id - 1 == in.choice) return;
        const auto cmd = plugin::cmdFromChoice(id - 1);
        auto* history = historyFor(*this);
        // The letter and the arguments it brings with it are one edit.
        if (history != nullptr) history->beginGesture(typeParam->getName(64));
        const auto set = [&](juce::RangedAudioParameter& p, juce::ParameterAttachment& att, float v) {
            if (history != nullptr) history->setParameter(p, v);
            else att.setValueAsCompleteGesture(v);
        };
        set(*typeParam, *in.typeAtt, float(id - 1));
        if (cmd != bank::Cmd::None) {
            // The letter arrives with what a fresh command of it holds,
            // clamped by the parameters' own ranges.
            const auto def = plugin::defaultCommand(cmd);
            set(*xParam, *in.argAtt[0], float(def.a));
            set(*yParam, *in.argAtt[1], float(def.b));
        }
        if (history != nullptr) history->endGesture();
    };
}

void CommandSlot::hexChanged()
{
    impl_->applyLetter();
    repaint();
}

void CommandSlot::setChannelKind(plugin::ChannelKind kind)
{
    if (kind == impl_->kind) return;
    impl_->kind = kind;
    impl_->buildMenu();
}

void CommandSlot::setInert(bool inert, const juce::String& why)
{
    auto& im = *impl_;
    if (inert == im.inert && why == im.inertWhy) return;
    im.inert = inert;
    im.inertWhy = why;
    // The children read isEnabled() through their parent, so one call greys
    // the letter and both arguments and stops them taking the mouse.
    setEnabled(!inert);
    if (inert) {
        im.type.setTooltip(why);
        for (auto& a : im.arg) a.setTooltip(why);
        im.byteArg.setTooltip(why);
    }
    else im.applyLetter();               // the letter's own tooltips come back
    repaint();
}

void CommandSlot::paint(juce::Graphics& g)
{
    auto& im = *impl_;
    const float alpha = im.inert ? 0.45f : 1.0f;
    const auto area = getLocalBounds().withHeight(kCaption);
    const juce::Font lf = Fonts::caption(10.0f);
    const juce::String up = im.label.toUpperCase();
    g.setFont(lf);
    g.setColour(colours::textDim.withMultipliedAlpha(alpha));
    g.drawText(up, area, juce::Justification::centredLeft, false);
    const int x = int(draw::textWidth(lf, up)) + 8;
    // An inert slot says so where the resolved command reads, so the greying
    // is explained on the strip and not only in the tooltip.
    if (im.inert && im.inertWhy.isNotEmpty()) {
        g.setFont(Fonts::sans(10.0f));
        g.setColour(colours::textDim);
        g.drawText(im.inertWhy, area.withTrimmedLeft(x), juce::Justification::centredLeft, true);
        return;
    }
    if (im.caption.isEmpty()) return;
    g.setFont(Fonts::mono(10.0f));
    g.setColour(colours::textMute.withMultipliedAlpha(alpha));
    g.drawText(im.caption, area.withTrimmedLeft(x), juce::Justification::centredLeft, true);
}

void CommandSlot::resized()
{
    auto& im = *impl_;
    auto row = getLocalBounds().withTrimmedTop(kCaption).withHeight(kControls);
    int used = 0;
    for (const auto& a : im.arg) if (a.isVisible()) ++used;
    if (im.byteArg.isVisible()) used = 1;
    // An empty slot has nothing to put beside the letter, so the letter takes
    // the row: a wide target to pick one with.
    im.type.setBounds(row.removeFromLeft(used == 0 ? row.getWidth() : juce::jmin(kTypeWidth, row.getWidth())));
    if (im.byteArg.isVisible()) { row.removeFromLeft(kGap); im.byteArg.setBounds(row); return; }
    for (int i = 0; i < used; ++i) {
        row.removeFromLeft(kGap);
        const int w = i == used - 1 ? row.getWidth() : juce::jmax(kMinArg, (row.getWidth() - kGap) / 2);
        im.arg[i].setBounds(row.removeFromLeft(w));
    }
}

} // namespace chipboy::ui
