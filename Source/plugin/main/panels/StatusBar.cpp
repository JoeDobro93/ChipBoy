#include "plugin/main/panels/StatusBar.h"

#include <bit>
#include <cmath>

namespace chipboy::plugin {

using namespace juce;
using namespace chipboy::ui;

StatusBar::StatusBar(ChipBoyProcessor& p) : processor_(p)
{
    setInterceptsMouseClicks(false, false);
    tick();
}

void StatusBar::tick()
{
    const double sr = processor_.currentSampleRate();
    const int latency = processor_.getLatencySamples();
    const double ms = sr > 0.0 ? latency * 1000.0 / sr : 0.0;
    String khz = String(sr / 1000.0, 1);
    if (khz.endsWith(".0")) khz = khz.dropLastCharacters(2);
    const String dot = String(CharPointer_UTF8(" \xc2\xb7 "));
    RichText machine;
    machine.bold(modelName(modelIndex(processor_))).plain(dot + khz + " kHz" + dot + "latency " + String(latency) + " samples (" + String(ms, 1) + " ms)");

    RichText tk;
    tk.plain("tempo ");
    tk.bold(String(processor_.songTempoSource() ? "song " : "host ") + String(int(std::lround(processor_.tempoInForce()))));

    RichText lk;
    lk.plain("link ");
    if (paramValue(processor_, ids::linkMode) == 0) lk.bold("off");
    else {
        const int n = std::popcount(processor_.voiceOwnedMask() & 15u);
        lk.bold(String(n) + (n == 1 ? " voice" : " voices"));
    }

    if (machine != machine_ || tk != tick_ || lk != link_) {
        machine_ = machine; tick_ = tk; link_ = lk;
        repaint();
    }
}

void StatusBar::paint(Graphics& g)
{
    g.fillAll(colours::panel2);
    g.setColour(colours::lineSoft);
    g.fillRect(0, 0, getWidth(), 1);
    const Font f = Fonts::mono(11.0f);
    g.setFont(f);
    float x = 14.0f;
    auto drawRuns = [&](const RichText& t) {
        for (const auto& r : t.runs) {
            g.setColour(r.bold ? colours::textMute : colours::textDim);
            const float w = draw::textWidth(f, r.text);
            g.drawText(r.text, Rectangle<float>(x, 0.0f, w + 2.0f, float(getHeight())), Justification::centredLeft, false);
            x += w;
        }
        x += 16.0f;
    };
    drawRuns(machine_);
    drawRuns(tick_);
    drawRuns(link_);
    g.setColour(colours::textDim);
    g.drawText("every value is a register", getLocalBounds().withTrimmedRight(14), Justification::centredRight, false);
}

} // namespace chipboy::plugin
