// ChipBoy -- the Link tab (spec section 11, UI_DESIGN section 8): this
// instance, its four channels and who owns them, the Voice plugins in the
// session, link mode, and the diagram of how the link works.
#pragma once

#include "plugin/main/panels/PanelCommon.h"

namespace chipboy::plugin {

class LinkPanel : public EditorPanel {
public:
    explicit LinkPanel(ChipBoyProcessor& p);
    ~LinkPanel() override;

    RichText contextLine() const override;
    void tick() override;
    void resized() override;

private:
    class SlotRow;
    class InstanceBlock;
    class VoicesBlock;
    class Diagram;

    ScrollBlock scroll_;
    InstanceBlock* instance_ = nullptr;
    VoicesBlock* voices_ = nullptr;
    juce::String lastKey_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LinkPanel)
};

} // namespace chipboy::plugin
