// ChipBoy -- one of a channel's two command slots, as the strip and the
// Voice window draw it (docs/COMMANDS_AND_TEMPO.md section 3).
//
// A letter picker and the two arguments, with the letter's own ranges on the
// steppers and its meaning -- "vol 12 . down 3", never a packed byte -- in
// the caption. The three host parameters (type, x, y) are the source of
// truth; the widget only ever writes complete gestures.
#pragma once

#include "plugin/shared/Parameters.h"
#include "plugin/ui/Widgets.h"

#include <memory>

namespace chipboy::ui {

class CommandSlot : public juce::Component {
public:
    /// `label` is the caption ("CMD1"); `kind` greys the letters this
    /// channel cannot use.
    CommandSlot(const juce::String& label, plugin::ChannelKind kind);
    ~CommandSlot() override;

    void attach(juce::RangedAudioParameter& type, juce::RangedAudioParameter& x, juce::RangedAudioParameter& y);
    /// Re-grey the letters when the Voice plugin moves to another channel.
    void setChannelKind(plugin::ChannelKind kind);
    /// The slot is not read at all -- a Hybrid channel takes its commands
    /// from the song's cells (docs/COMMANDS_AND_TEMPO.md section 20). The
    /// letter and its arguments grey out and say why, and the parameters
    /// keep their values for when the channel plays MIDI again.
    void setInert(bool inert, const juce::String& why = {});

    static constexpr int kCaption = 12, kControls = 22;
    static constexpr int kHeight = kCaption + kControls;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CommandSlot)
};

} // namespace chipboy::ui
