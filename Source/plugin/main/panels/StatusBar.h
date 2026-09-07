// ChipBoy -- the status line (UI_DESIGN section 2, item 4): model, sample
// rate, latency, tick source, link state.
#pragma once

#include "plugin/main/panels/PanelCommon.h"

namespace chipboy::plugin {

class StatusBar : public juce::Component {
public:
    explicit StatusBar(ChipBoyProcessor& p);
    void tick();
    static constexpr int kHeight = 26;
    void paint(juce::Graphics&) override;

private:
    ChipBoyProcessor& processor_;
    RichText machine_, tick_, link_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(StatusBar)
};

} // namespace chipboy::plugin
