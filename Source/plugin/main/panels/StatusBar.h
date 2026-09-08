// ChipBoy -- the status line (UI_DESIGN section 2, item 4): model, sample
// rate, latency, tick source, who owns the transport, link state -- and, on
// the right, what the last file or preset did, until it goes stale.
#pragma once

#include "plugin/main/panels/PanelCommon.h"

namespace chipboy::plugin {

class StatusBar : public juce::Component {
public:
    explicit StatusBar(ChipBoyProcessor& p);
    void tick();
    /// A line from a panel -- a song loaded, a preset placed -- shown on the
    /// right in place of the tagline for a while.
    void setMessage(const juce::String& text);
    static constexpr int kHeight = 26;
    /// How long a message stays up before the tagline comes back.
    static constexpr int kMessageMs = 20000;
    void paint(juce::Graphics&) override;

private:
    ChipBoyProcessor& processor_;
    RichText machine_, tick_, transport_, link_;
    juce::String message_;
    juce::uint32 messageAt_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StatusBar)
};

} // namespace chipboy::plugin
