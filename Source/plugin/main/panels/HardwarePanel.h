// ChipBoy -- the Hardware tab (UI_DESIGN section 5): the three models,
// hardware states (legal on a real unit), departures (the header reads
// MODIFIED while one is on) and the display options.
#pragma once

#include "plugin/main/panels/PanelCommon.h"

namespace chipboy::plugin {

class HardwarePanel : public EditorPanel {
public:
    explicit HardwarePanel(ChipBoyProcessor& p);
    ~HardwarePanel() override;

    RichText contextLine() const override;
    void tick() override;
    void resized() override;
    /// The scope trace mode and periods, stored in apvts.state as ui_trace / ui_periods.
    std::function<void(ui::ScopeView::Trace, int)> onDisplaySettings;
    static ui::ScopeView::Trace storedTrace(const ChipBoyProcessor& p);
    static int storedPeriods(const ChipBoyProcessor& p);

private:
    class ModelCard;
    class ModelRow;
    class ToggleRow;
    class ChoiceRow;
    void refreshFacts();
    void announceDisplay();

    ScrollBlock scroll_;
    ModelRow* models_ = nullptr;
    ToggleRow* noise_ = nullptr;
    ToggleRow* lcd_ = nullptr;
    ToggleRow* edges_ = nullptr;
    ToggleRow* proSound_ = nullptr;
    ToggleRow* tame_ = nullptr;
    ToggleRow* soften_ = nullptr;
    ChoiceRow* bassMod_ = nullptr;
    ChoiceRow* trace_ = nullptr;
    ChoiceRow* periods_ = nullptr;
    ChoiceRow* values_ = nullptr;
    std::unique_ptr<StepperParam> tameMs_;
    std::unique_ptr<ParamWatch> modelWatch_;
    int lastModel_ = -1, lastNoise_ = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HardwarePanel)
};

} // namespace chipboy::plugin
