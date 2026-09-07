// ChipBoy -- the visualizer (UI_DESIGN section 3): a separate, resizable,
// clean window with the four channel scopes and the master, made for screen
// capture. The mockup's .video section: a small tool row, a 2 x 2 grid of
// scopes (or a stack) and the master strip below, on black.
#include "plugin/ui/Widgets.h"

#include <cmath>

namespace chipboy::ui {

namespace {
constexpr int kMinWidth = 600, kMinHeight = 400, kDefaultWidth = 1148, kDefaultHeight = 640;
constexpr int kGap = 8;
} // namespace

struct VisualizerWindow::Impl {
    struct Content : juce::Component, juce::Timer {
        plugin::ScopeBuffers& buffers;
        std::function<double()> cornerHz;
        Segmented ground, line, layout;
        ScopeView scopes[4];
        MasterScope master;
        juce::Rectangle<int> titleRect, groundRect, lineRect, layoutRect;
        double lastCorner = -1.0;

        Content(plugin::ScopeBuffers& b, std::function<double()> fn) : buffers(b), cornerHz(std::move(fn))
        {
            setOpaque(true);
            ground.setOptions({ "Black", "LCD" });
            line.setOptions({ "Thin", "Medium", "Thick" });
            layout.setOptions({ "Tiled 2" + juce::String::charToString(0x00d7) + "2", "Stacked" });
            for (auto* s : { &ground, &line, &layout }) { s->setMini(true); addAndMakeVisible(*s); }
            line.setSelected(1, juce::dontSendNotification);
            ground.setTooltip("Black for capture, or the LCD green of the mixer's scopes");
            line.setTooltip("Line weight");
            layout.setTooltip("The four channels tiled two by two, or stacked");
            ground.onChange = [this](int) { applyGround(); };
            line.onChange = [this](int) { applyLine(); };
            layout.onChange = [this](int) { resized(); };
            for (int ch = 0; ch < 4; ++ch) {
                auto& s = scopes[ch];
                s.setChannel(ch);
                s.setChrome(false);
                s.setPeriods(8);
                s.setTrace(ScopeView::Trace::Both);
                s.setIdleDim(false);
                s.setSource({ &buffers.channels[size_t(ch)], &buffers.state[size_t(ch)], &buffers.latestCycle });
                addAndMakeVisible(s);
            }
            master.setSource(&buffers.master);
            addAndMakeVisible(master);
            applyGround();
            applyLine();
            pollCorner();
            startTimerHz(4);
        }
        ~Content() override { stopTimer(); }

        void timerCallback() override { pollCorner(); }
        void pollCorner()
        {
            const double hz = cornerHz ? cornerHz() : 0.0;
            if (std::abs(hz - lastCorner) < 1.0e-9) return;
            lastCorner = hz;
            for (auto& s : scopes) s.setAnalogCornerHz(hz);
        }
        void applyGround()
        {
            const bool lcd = ground.selected() == 1;
            for (auto& s : scopes) { if (lcd) s.setLcdGround(true); else s.setGround(colours::black, colours::videoGrid, colours::videoBorder); }
            if (lcd) master.setLcdGround(true); else master.setGround(colours::black, colours::videoGrid, colours::videoBorder);
        }
        void applyLine()
        {
            const float w = float(line.selected() + 1);
            for (auto& s : scopes) s.setLineWidth(w);
            master.setLineWidth(w);
        }

        void paint(juce::Graphics& g) override
        {
            g.fillAll(colours::black);
            draw::caption(g, "Visualizer", titleRect);
            draw::caption(g, "Ground", groundRect);
            draw::caption(g, "Line", lineRect);
            draw::caption(g, "Layout", layoutRect);
        }

        void resized() override
        {
            auto b = getLocalBounds().withTrimmedTop(10).withTrimmedLeft(12).withTrimmedRight(12).withTrimmedBottom(12);
            auto tools = b.removeFromTop(22);
            auto captionWidth = [](const juce::String& t) { return juce::roundToInt(draw::textWidth(Fonts::caption(10.0f), t.toUpperCase())) + 2; };
            titleRect = tools.removeFromLeft(captionWidth("Visualizer")); tools.removeFromLeft(10);
            groundRect = tools.removeFromLeft(captionWidth("Ground")); tools.removeFromLeft(10);
            ground.setBounds(tools.removeFromLeft(ground.preferredWidth())); tools.removeFromLeft(10);
            lineRect = tools.removeFromLeft(captionWidth("Line")); tools.removeFromLeft(10);
            line.setBounds(tools.removeFromLeft(line.preferredWidth())); tools.removeFromLeft(10);
            layoutRect = tools.removeFromLeft(captionWidth("Layout")); tools.removeFromLeft(10);
            layout.setBounds(tools.removeFromLeft(layout.preferredWidth()));
            b.removeFromTop(8);

            const int masterH = juce::jmax(60, juce::roundToInt(float(b.getHeight()) * 0.22f));
            master.setBounds(b.removeFromBottom(masterH));
            b.removeFromBottom(kGap);
            if (layout.selected() == 0) {
                const int cw = (b.getWidth() - kGap) / 2, rh = (b.getHeight() - kGap) / 2;
                for (int ch = 0; ch < 4; ++ch)
                    scopes[ch].setBounds(b.getX() + (ch % 2) * (cw + kGap), b.getY() + (ch / 2) * (rh + kGap), cw, rh);
            }
            else {
                const int rh = (b.getHeight() - 3 * kGap) / 4;
                for (int ch = 0; ch < 4; ++ch)
                    scopes[ch].setBounds(b.getX(), b.getY() + ch * (rh + kGap), b.getWidth(), rh);
            }
        }
    };

    Content* content = nullptr;   ///< owned by the DocumentWindow
};

VisualizerWindow::VisualizerWindow(plugin::ScopeBuffers& buffers, std::function<double()> analogCornerHz)
    : juce::DocumentWindow("ChipBoy Visualizer", colours::black, juce::DocumentWindow::allButtons), impl_(std::make_unique<Impl>())
{
    setUsingNativeTitleBar(true);
    setResizable(true, false);
    setResizeLimits(kMinWidth, kMinHeight, 8192, 8192);
    impl_->content = new Impl::Content(buffers, std::move(analogCornerHz));
    setContentOwned(impl_->content, false);
    centreWithSize(kDefaultWidth, kDefaultHeight);
    setVisible(true);
}

VisualizerWindow::~VisualizerWindow() = default;

void VisualizerWindow::closeButtonPressed()
{
    if (onClose) onClose();   // the owner deletes the window: nothing after this
}

} // namespace chipboy::ui
