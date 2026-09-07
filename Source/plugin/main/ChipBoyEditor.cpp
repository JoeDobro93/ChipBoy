#include "plugin/main/ChipBoyEditor.h"

#include "plugin/main/panels/HardwarePanel.h"
#include "plugin/main/panels/InstrumentPanel.h"
#include "plugin/main/panels/KitsPanel.h"
#include "plugin/main/panels/LinkPanel.h"
#include "plugin/main/panels/PhrasesPanel.h"
#include "plugin/main/panels/TablesPanel.h"
#include "plugin/main/panels/WavesPanel.h"

#include <cmath>

namespace chipboy::plugin {

using namespace juce;
using namespace chipboy::ui;

namespace {
constexpr int kTabsHeight = 34, kEditorPad = 12, kTabBarWidth = 640;
const Identifier kScaleProp("ui_scale");
const char* kTabNames[] = { "Instrument", "Tables", "Waves", "Kits", "Phrases", "Link", "Hardware" };
}

/// The tab strip with the context line at its right ("Editing PU2 · instrument 05 Bass 07").
class ChipBoyEditor::TabBar : public Component {
public:
    TabBar()
    {
        for (const char* n : kTabNames) bar_.addTab(n, Colours::transparentBlack, -1);
        bar_.onChange = [this](int i) { if (onChange) onChange(i); };
        addAndMakeVisible(bar_);
    }
    std::function<void(int)> onChange;
    void setCurrent(int i) { if (bar_.getCurrentTabIndex() != i) bar_.setCurrentTabIndex(i, false); }
    void setContext(const RichText& c) { context_ = c; repaint(); }
    void paint(Graphics& g) override
    {
        g.fillAll(colours::panel);
        g.setColour(colours::lineSoft);
        g.fillRect(0, getHeight() - 1, getWidth(), 1);
        const Rectangle<float> area(float(kTabBarWidth + 24), 0.0f, float(getWidth() - kTabBarWidth - 24 - 12), float(getHeight() - 6));
        context_.attributed(12.0f, colours::textMute, colours::text, Justification::centredRight).draw(g, area);
    }
    void resized() override { bar_.setBounds(12, 6, kTabBarWidth, getHeight() - 6); }
private:
    struct Bar : TabbedButtonBar {
        Bar() : TabbedButtonBar(TabbedButtonBar::TabsAtTop) {}
        std::function<void(int)> onChange;
        void currentTabChanged(int i, const String&) override { if (onChange) onChange(i); }
    } bar_;
    RichText context_;
};

/* ----------------------------------------------------------- editor */

ChipBoyEditor::ChipBoyEditor(ChipBoyProcessor& p)
    : AudioProcessorEditor(p), processor_(p), header_(p), mixer_(p), status_(p), tooltips_(this, 600)
{
    setLookAndFeel(&lookAndFeel_);
    addAndMakeVisible(content_);
    content_.setWantsKeyboardFocus(true);
    content_.addAndMakeVisible(header_);
    content_.addAndMakeVisible(mixer_);
    tabs_ = std::make_unique<TabBar>();
    content_.addAndMakeVisible(*tabs_);
    content_.addAndMakeVisible(status_);

    panels_[Instrument] = std::make_unique<InstrumentPanel>(processor_);
    panels_[Tables] = std::make_unique<TablesPanel>(processor_);
    panels_[Waves] = std::make_unique<WavesPanel>(processor_);
    panels_[Kits] = std::make_unique<KitsPanel>(processor_);
    panels_[Phrases] = std::make_unique<PhrasesPanel>(processor_);
    panels_[Link] = std::make_unique<LinkPanel>(processor_);
    auto hardware = std::make_unique<HardwarePanel>(processor_);
    hardware->onDisplaySettings = [this](ScopeView::Trace t, int periods) { mixer_.setScopeSettings(t, periods); };
    panels_[Hardware] = std::move(hardware);
    for (auto& panel : panels_) {
        panel->onContextChanged = [this] { refreshContext(); };
        panel->onSelectChannel = [this](int ch) { selectChannel(ch); };
        content_.addChildComponent(*panel);
    }

    header_.onScale = [this](float f) { setScale(f); };
    header_.onToggleVisualizer = [this] { toggleVisualizer(); };
    mixer_.onSelect = [this](int ch) { selectChannel(ch); };
    tabs_->onChange = [this](int i) { showTab(i); };

    // the display preference: hex or decimal, applied wherever a value is drawn
    hexWatch_ = std::make_unique<ParamWatch>(param(processor_, ids::hexDisplay), [this](float v) {
        ValueFormat::setHex(v > 0.5f);
        mixer_.hexChanged();
        for (auto& panel : panels_) if (panel) panel->hexChanged();
        refreshContext();
        content_.repaint();
    });
    mixer_.setScopeSettings(HardwarePanel::storedTrace(processor_), HardwarePanel::storedPeriods(processor_));
    lastCorner_ = analogCornerHz(processor_);
    mixer_.setAnalogCornerHz(lastCorner_);

    const double stored = double(processor_.apvts.state.getProperty(kScaleProp, 1.0));
    const float f = std::abs(stored - 1.25) < 0.01 ? 1.25f : std::abs(stored - 1.5) < 0.01 ? 1.5f : 1.0f;
    setResizable(false, false);
    setWantsKeyboardFocus(true);
    setScale(f);

    lastBank_ = processor_.bank().get();
    lastSong_ = processor_.song().get();
    selectChannel(0);
    showTab(Instrument);
    startTimerHz(30);
}

ChipBoyEditor::~ChipBoyEditor()
{
    stopTimer();
    visualizer_.reset();
    setLookAndFeel(nullptr);
}

void ChipBoyEditor::selectChannel(int ch)
{
    selected_ = std::clamp(ch, 0, 3);
    mixer_.setSelected(selected_);
    for (auto& panel : panels_) if (panel) panel->setChannel(selected_);
    refreshContext();
}

void ChipBoyEditor::showTab(int tab)
{
    const int t = std::clamp(tab, 0, int(kTabs) - 1);
    for (int i = 0; i < int(kTabs); ++i) {
        if (!panels_[size_t(i)]) continue;
        const bool show = i == t;
        if (panels_[size_t(i)]->isVisible() != show) { panels_[size_t(i)]->setVisible(show); panels_[size_t(i)]->shown(show); }
    }
    tab_ = t;
    tabs_->setCurrent(t);
    refreshContext();
}

void ChipBoyEditor::setScale(float factor)
{
    scale_ = std::abs(factor - 1.25f) < 0.01f ? 1.25f : std::abs(factor - 1.5f) < 0.01f ? 1.5f : 1.0f;
    header_.setScale(scale_);
    processor_.apvts.state.setProperty(kScaleProp, double(scale_), nullptr);
    content_.setTransform(AffineTransform::scale(scale_));
    setSize(roundToInt(kMainWidth * scale_), roundToInt(kMainHeight * scale_));
}

void ChipBoyEditor::refreshContext()
{
    if (!panels_[size_t(tab_)]) return;
    const RichText c = panels_[size_t(tab_)]->contextLine();
    if (c != lastContext_) { lastContext_ = c; tabs_->setContext(c); }
}

void ChipBoyEditor::toggleVisualizer()
{
    if (visualizer_) {
        visualizer_.reset();
        header_.setVisualizerOpen(false);
        return;
    }
    visualizer_ = std::make_unique<VisualizerWindow>(processor_.scopes(), [this] { return analogCornerHz(processor_); });
    visualizer_->setLookAndFeel(&lookAndFeel_);
    Component::SafePointer<ChipBoyEditor> safe(this);
    visualizer_->onClose = [safe] {
        MessageManager::callAsync([safe] {
            if (safe == nullptr) return;
            safe->visualizer_.reset();
            safe->header_.setVisualizerOpen(false);
        });
    };
    visualizer_->setVisible(true);
    visualizer_->toFront(true);
    header_.setVisualizerOpen(true);
}

/* ------------------------------------------------------------ timer */

void ChipBoyEditor::timerCallback()
{
    if (const int r = processor_.takeFocusRequest(); r >= 0) { selectChannel(r & 3); showTab(Instrument); }

    const auto b = processor_.bank();
    const auto s = processor_.song();
    if (b.get() != lastBank_) {
        lastBank_ = b.get();
        mixer_.bankChanged();
        for (auto& panel : panels_) if (panel) panel->bankChanged();
    }
    if (s.get() != lastSong_) {
        lastSong_ = s.get();
        for (auto& panel : panels_) if (panel) panel->songChanged();
    }
    const double corner = analogCornerHz(processor_);
    if (std::abs(corner - lastCorner_) > 1.0e-9) { lastCorner_ = corner; mixer_.setAnalogCornerHz(corner); }

    header_.tick();
    mixer_.tick();
    status_.tick();
    if (panels_[size_t(tab_)]) panels_[size_t(tab_)]->tick();
    refreshContext();
}

/* ----------------------------------------------------------- layout */

void ChipBoyEditor::paint(Graphics& g)
{
    g.fillAll(colours::bg);
}

void ChipBoyEditor::resized()
{
    content_.setBounds(0, 0, kMainWidth, kMainHeight);
    layoutContent();
}

void ChipBoyEditor::layoutContent()
{
    auto area = Rectangle<int>(0, 0, kMainWidth, kMainHeight);
    header_.setBounds(area.removeFromTop(HeaderBar::kHeight));
    mixer_.setBounds(area.removeFromTop(MixerRow::kHeight));
    tabs_->setBounds(area.removeFromTop(kTabsHeight));
    status_.setBounds(area.removeFromBottom(StatusBar::kHeight));
    const auto editor = area.reduced(kEditorPad);
    for (auto& panel : panels_) if (panel) panel->setBounds(editor);
}

bool ChipBoyEditor::keyPressed(const KeyPress& key)
{
    if (key == KeyPress::escapeKey) { content_.grabKeyboardFocus(); return true; }
    return false;
}

} // namespace chipboy::plugin
