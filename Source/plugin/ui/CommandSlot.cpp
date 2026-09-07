#include "plugin/ui/CommandSlot.h"

#include <cmath>

namespace chipboy::ui {

namespace {

constexpr int kTypeWidth = 54, kGap = 4, kMinArg = 52;

/// What one argument reads as on its stepper. The letter decides: O is a
/// pan position, P is signed around 128, E's y carries the direction.
juce::String argText(bank::Cmd cmd, int arg, int v)
{
    if (cmd == bank::Cmd::O && arg == 0) { static const char* n[] = { "off", "L", "R", "LR" }; return n[v & 3]; }
    if (cmd == bank::Cmd::P && arg == 0) { const int s = v - 128; return (s > 0 ? "+" : "") + juce::String(s); }
    if (cmd == bank::Cmd::E && arg == 1) return juce::String::charToString((v & 8) ? 0x2191 : 0x2193) + juce::String(v & 7);
    return juce::String(v);
}

} // namespace

struct CommandSlot::Impl {
    CommandSlot& owner;
    juce::String label;
    plugin::ChannelKind kind;

    juce::ComboBox type;
    Stepper arg[2];
    std::unique_ptr<juce::ParameterAttachment> typeAtt, argAtt[2];
    int choice = 0, value[2] = { 0, 0 };
    juce::String caption;

    Impl(CommandSlot& o, const juce::String& l, plugin::ChannelKind k) : owner(o), label(l), kind(k) {}

    bank::Cmd cmd() const { return plugin::cmdFromChoice(choice); }

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

    /// The letter changed: the steppers take its ranges, its readouts and
    /// its tooltips, and the ones it does not use go away.
    void applyLetter()
    {
        const auto c = cmd();
        const auto* info = plugin::commandInfo(c);
        const int n = info != nullptr ? info->nargs : 0;
        for (int i = 0; i < 2; ++i) {
            const bool used = i < n;
            arg[i].setVisible(used);
            if (!used) continue;
            arg[i].setRange(info->lo[i], info->hi[i], info->def[i]);
            value[i] = arg[i].value();       // setRange may have clamped it into the letter's range
            arg[i].setTextFunction([c, i](int v) { return argText(c, i, v); });
            arg[i].setTooltip(juce::String(i == 0 ? "x" : "y") + juce::String(juce::CharPointer_UTF8(" \xe2\x80\x94 ")) + info->args
                              + "  (" + juce::String(info->lo[i]) + juce::String(juce::CharPointer_UTF8("\xe2\x80\x93")) + juce::String(info->hi[i]) + ")");
        }
        type.setTooltip(info != nullptr ? juce::String(info->name) + juce::String(juce::CharPointer_UTF8(" \xe2\x80\x94 ")) + info->args
                                        : juce::String("No command in this slot. A letter fires at the next tick and again at every note-on."));
        refreshCaption();
        owner.resized();
    }

    void refreshCaption()
    {
        bank::Command c;
        c.cmd = cmd();
        c.a = int16_t(value[0]);
        c.b = int16_t(value[1]);
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
    addAndMakeVisible(im.type);
    for (auto& a : im.arg) addAndMakeVisible(a);
    im.buildMenu();
    im.applyLetter();
}

CommandSlot::~CommandSlot() = default;

void CommandSlot::attach(juce::RangedAudioParameter& type, juce::RangedAudioParameter& x, juce::RangedAudioParameter& y)
{
    auto& im = *impl_;
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
    im.type.onChange = [this] {
        auto& in = *impl_;
        const int id = in.type.getSelectedId();
        if (id < 1 || id - 1 == in.choice) return;
        const auto cmd = plugin::cmdFromChoice(id - 1);
        in.typeAtt->setValueAsCompleteGesture(float(id - 1));
        if (cmd == bank::Cmd::None) return;
        const auto def = plugin::defaultCommand(cmd);
        in.argAtt[0]->setValueAsCompleteGesture(float(def.a));
        in.argAtt[1]->setValueAsCompleteGesture(float(def.b));
    };
}

void CommandSlot::setChannelKind(plugin::ChannelKind kind)
{
    if (kind == impl_->kind) return;
    impl_->kind = kind;
    impl_->buildMenu();
}

void CommandSlot::paint(juce::Graphics& g)
{
    auto& im = *impl_;
    const auto area = getLocalBounds().withHeight(kCaption);
    const juce::Font lf = Fonts::caption(10.0f);
    const juce::String up = im.label.toUpperCase();
    g.setFont(lf);
    g.setColour(colours::textDim);
    g.drawText(up, area, juce::Justification::centredLeft, false);
    if (im.caption.isEmpty()) return;
    const int x = int(draw::textWidth(lf, up)) + 8;
    g.setFont(Fonts::mono(10.0f));
    g.setColour(colours::textMute);
    g.drawText(im.caption, area.withTrimmedLeft(x), juce::Justification::centredLeft, true);
}

void CommandSlot::resized()
{
    auto& im = *impl_;
    auto row = getLocalBounds().withTrimmedTop(kCaption).withHeight(kControls);
    int used = 0;
    for (const auto& a : im.arg) if (a.isVisible()) ++used;
    // An empty slot has nothing to put beside the letter, so the letter takes
    // the row: a wide target to pick one with.
    im.type.setBounds(row.removeFromLeft(used == 0 ? row.getWidth() : juce::jmin(kTypeWidth, row.getWidth())));
    for (int i = 0; i < used; ++i) {
        row.removeFromLeft(kGap);
        const int w = i == used - 1 ? row.getWidth() : juce::jmax(kMinArg, (row.getWidth() - kGap) / 2);
        im.arg[i].setBounds(row.removeFromLeft(w));
    }
}

} // namespace chipboy::ui
