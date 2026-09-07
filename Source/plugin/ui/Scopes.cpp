// ChipBoy -- the scopes (UI_DESIGN section 3): a period-locked view of one
// channel's digital trace with an approximation of the analog trace after
// the coupling capacitor, the master output in LCD green, and the register
// line under a channel strip.
//
// The digital trace is a list of (cycle, level) steps from the audio thread
// (link::ScopeRing); cycles are 4194304 Hz. The window is a whole number of
// periods of the channel's own frequency register and starts on a rising
// edge, so a sustained note is a still picture. The analog trace is the
// exact response of a one-pole high-pass to that staircase: between steps
// the capacitor's voltage decays exponentially, at a step it jumps by the
// step, so it costs one exp() per sample and one per sub-pixel, no
// sub-stepping through the staircase.
#include "plugin/ui/Widgets.h"

#include "core/Apu/Apu.h"

#include <algorithm>
#include <cmath>

namespace chipboy::ui {

namespace {

constexpr int kFps = 30;
constexpr double kNoiseWindowSeconds = 0.020;

double cyclesPerPeriod(int ch, int period)
{
    const double base = double(2048 - juce::jlimit(0, 2047, period));
    return ch == 2 ? 64.0 * base : 32.0 * base;
}

/// Index of the last sample with cycle <= c, or -1.
int lastAtOrBefore(const link::ScopeSample* s, uint32_t n, double c)
{
    const auto* end = s + n;
    const auto* it = std::upper_bound(s, end, c, [](double value, const link::ScopeSample& x) { return value < double(x.cycle); });
    return int(it - s) - 1;
}

void strokeWithGlow(juce::Graphics& g, const juce::Path& p, juce::Colour c, float width)
{
    if (p.isEmpty()) return;
    g.setColour(c.withAlpha(0.18f));
    g.strokePath(p, juce::PathStrokeType(width + 3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(c);
    g.strokePath(p, juce::PathStrokeType(width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void paintGround(juce::Graphics& g, juce::Rectangle<int> bounds, juce::Colour ground, juce::Colour border)
{
    g.setColour(ground);
    g.fillRoundedRectangle(bounds.toFloat(), 3.0f);
    g.setColour(border);
    g.drawRoundedRectangle(bounds.toFloat().reduced(0.5f), 3.0f, 1.0f);
}

} // namespace

// ===========================================================================
// ScopeView
// ===========================================================================
struct ScopeView::Impl : juce::Timer {
    ScopeView& owner;
    Source src;
    int ch = 0;
    Trace trace = Trace::Digital;
    int periods = 2;
    double cornerHz = 25.0;
    bool chrome = true, frozen = false, idleDim = true, chromeShown = false;
    juce::Colour ground = colours::lcd, grid = colours::lcdGrid, border = colours::scopeBorder;
    float lineWidth = 1.0f;

    // what the timer read last; paint only draws it
    std::vector<link::ScopeSample> buf;
    std::vector<double> capState;     ///< the capacitor's voltage right after each sample, level units
    std::vector<std::pair<int, int>> offSpans;
    uint32_t n = 0;
    uint64_t packed = 0, latest = 0;
    bool active = false;
    int period = 0;
    juce::Path digital, analog;

    Segmented traceSeg, zoomSeg;

    explicit Impl(ScopeView& o) : owner(o)
    {
        buf.resize(link::kScopeRing);
        capState.resize(link::kScopeRing);
        offSpans.reserve(512);
        digital.preallocateSpace(1400 * 4 * 3);
        analog.preallocateSpace(1400 * 4 * 3);

        traceSeg.setOptions({ "Dig", "Ana", "Both" });
        traceSeg.setMini(true);
        traceSeg.setOptionTooltip(0, "The 4-bit staircase going into the DAC");
        traceSeg.setOptionTooltip(1, "What leaves the machine after the coupling capacitor");
        traceSeg.setOptionTooltip(2, "Both traces");
        traceSeg.onChange = [this](int i) { owner.setTrace(Trace(i)); };
        zoomSeg.setOptions({ juce::String::charToString(0x00d7) + "1", juce::String::charToString(0x00d7) + "2",
                             juce::String::charToString(0x00d7) + "4", juce::String::charToString(0x00d7) + "8" });
        zoomSeg.setMini(true);
        zoomSeg.setTooltip("Periods shown, from the chip's own frequency register");
        zoomSeg.onChange = [this](int i) { owner.setPeriods(1 << i); };
        owner.addChildComponent(traceSeg);
        owner.addChildComponent(zoomSeg);
    }

    ~Impl() override { stopTimer(); }

    void syncChrome()
    {
        traceSeg.setSelected(int(trace), juce::dontSendNotification);
        zoomSeg.setSelected(periods >= 8 ? 3 : periods >= 4 ? 2 : periods >= 2 ? 1 : 0, juce::dontSendNotification);
        const bool raw = cornerHz <= 0.0;
        traceSeg.setOptionEnabled(1, !raw);
        traceSeg.setOptionEnabled(2, !raw);
        zoomSeg.setEnabled(ch != 3);
    }
    void showChrome(bool on)
    {
        if (on == chromeShown) return;
        chromeShown = on;
        traceSeg.setVisible(on);
        zoomSeg.setVisible(on);
    }
    void layoutChrome()
    {
        const auto b = owner.getLocalBounds();
        zoomSeg.setBounds(b.getRight() - 4 - zoomSeg.preferredWidth(), b.getBottom() - 4 - zoomSeg.preferredHeight(), zoomSeg.preferredWidth(), zoomSeg.preferredHeight());
        traceSeg.setBounds(zoomSeg.getX() - 4 - traceSeg.preferredWidth(), zoomSeg.getY(), traceSeg.preferredWidth(), traceSeg.preferredHeight());
    }
    void restartTimer()
    {
        if (src.ring != nullptr && !frozen) startTimerHz(kFps); else stopTimer();
    }

    void timerCallback() override
    {
        if (frozen || !owner.isShowing()) return;
        showChrome(chrome && owner.isMouseOver(true));
        if (src.ring == nullptr) return;
        const uint32_t count = src.ring->snapshot(buf.data(), uint32_t(buf.size()));
        const uint64_t newPacked = src.state != nullptr ? src.state->load(std::memory_order_relaxed) : 0;
        const uint64_t newLatest = src.latestCycle != nullptr ? src.latestCycle->load(std::memory_order_relaxed) : 0;
        const uint64_t newestCycle = count > 0 ? buf[count - 1].cycle : 0;
        const bool changed = count != n || newPacked != packed || (count > 0 && newestCycle != latest) || (ch == 3 && newLatest != latest);
        n = count;
        packed = newPacked;
        latest = ch == 3 ? newLatest : newestCycle;
        driver::VoiceView v;
        link::unpackState(packed, v);
        active = v.active;
        period = v.period;
        if (changed) owner.repaint();
    }

    // --- the picture ----------------------------------------------------
    void paint(juce::Graphics& g)
    {
        const auto bounds = owner.getLocalBounds();
        paintGround(g, bounds, ground, border);
        const auto area = bounds.reduced(1);
        const int W = area.getWidth(), H = area.getHeight();
        if (W < 8 || H < 8) return;
        const float top = float(area.getY()) + 4.0f, span = float(H) - 8.0f;
        auto toY = [top, span](double level) { return top + span * float(1.0 - level / 16.0); };

        // the DAC's own scale, not dB: a line every four levels, the rest faint on tall scopes
        for (int lv = 0; lv <= 16; ++lv) {
            if (lv % 4 != 0 && H < 140) continue;
            g.setColour(lv % 4 == 0 ? grid : grid.withAlpha(0.5f));
            g.fillRect(float(area.getX()), std::round(toY(lv)) - 0.5f, float(W), 1.0f);
        }

        if (n > 0) buildAndDraw(g, area, toY);

        if (idleDim && !active) { g.setColour(ground.withAlpha(0.4f)); g.fillRoundedRectangle(bounds.toFloat(), 3.0f); }
    }

    template <typename ToY>
    void buildAndDraw(juce::Graphics& g, juce::Rectangle<int> area, ToY toY)
    {
        const int W = area.getWidth();
        const float x0 = float(area.getX());
        const link::ScopeSample* s = buf.data();
        const bool noise = ch == 3;
        const double cpp = cyclesPerPeriod(ch, period);
        const double newest = double(s[n - 1].cycle);
        double end = src.latestCycle != nullptr ? double(src.latestCycle->load(std::memory_order_relaxed)) : newest;
        if (end < newest) end = newest;
        const double len = noise ? kNoiseWindowSeconds * double(kCpuHz) : double(periods) * cpp;
        double start = end - len;

        if (!noise && n >= 2) {   // a still picture: start on the last rising edge before end - len, up to one period back
            for (int i = lastAtOrBefore(s, n, start); i >= 1 && double(s[i].cycle) >= start - cpp; --i)
                if (s[i].level > s[i - 1].level && s[i - 1].level >= 0) { start = double(s[i].cycle); end = start + len; break; }
        }

        // --- digital: per pixel column, the level at its start and the span of levels inside it
        digital.clear();
        offSpans.clear();
        int k = lastAtOrBefore(s, n, start);
        bool open = false;
        int offStart = -1;
        for (int x = 0; x < W; ++x) {
            const double c1 = start + len * double(x + 1) / double(W);
            const int level0 = k >= 0 ? s[k].level : -1;
            int lo = level0, hi = level0, last = level0;
            bool sawOff = level0 < 0;
            while (k + 1 < int(n) && double(s[k + 1].cycle) <= c1) {
                ++k;
                const int lv = s[k].level;
                if (lv < 0) { sawOff = true; last = lv; continue; }
                if (lo < 0 || lv < lo) lo = lv;
                if (hi < 0 || lv > hi) hi = lv;
                last = lv;
            }
            const float px = x0 + float(x);
            if (level0 < 0 && k < 0) { if (open) open = false; continue; }   // before the oldest sample: nothing known
            if (level0 >= 0) {
                if (!open) { digital.startNewSubPath(px, toY(level0)); open = true; }
                else digital.lineTo(px, toY(level0));
                if (hi > lo) { digital.lineTo(px, toY(hi)); digital.lineTo(px, toY(lo)); }
                if (last >= 0 && last != lo) digital.lineTo(px, toY(last));
            }
            if (last < 0) open = false;
            if (sawOff) { if (offStart < 0) offStart = x; }
            else if (offStart >= 0) { offSpans.emplace_back(offStart, x); offStart = -1; }
        }
        if (offStart >= 0) offSpans.emplace_back(offStart, W);

        // --- analog: the capacitor's exact response to the staircase
        const bool wantAnalog = cornerHz > 0.0 && trace != Trace::Digital;
        analog.clear();
        if (wantAnalog) {
            const double tau = double(kCpuHz) / (2.0 * juce::MathConstants<double>::pi * cornerHz);
            const auto* first = std::lower_bound(s, s + n, start - 8.0 * tau, [](const link::ScopeSample& x, double value) { return double(x.cycle) < value; });
            const int i0 = int(first - s);
            auto xOf = [](int level) { return level >= 0 ? double(level) - 7.5 : 0.0; };
            double y = 0.0, xPrev = i0 < int(n) ? xOf(s[i0].level) : 0.0;
            for (int i = i0; i < int(n); ++i) {
                if (i > i0) y *= std::exp(-double(s[i].cycle - s[i - 1].cycle) / tau);
                const double xi = xOf(s[i].level);
                y += xi - xPrev;
                xPrev = xi;
                capState[size_t(i)] = y;
            }
            const int subs = 4 * W;
            int j = lastAtOrBefore(s, n, start);
            bool openA = false;
            for (int sIdx = 0; sIdx < subs; ++sIdx) {
                const double t = start + len * double(sIdx) / double(subs);
                while (j + 1 < int(n) && double(s[j + 1].cycle) <= t) ++j;
                if (j < i0) { openA = false; continue; }
                const double v = 7.5 + capState[size_t(j)] * std::exp(-(t - double(s[j].cycle)) / tau);
                const float px = x0 + float(sIdx) * 0.25f, py = toY(v);
                if (!openA) { analog.startNewSubPath(px, py); openA = true; }
                else analog.lineTo(px, py);
            }
        }

        // --- draw
        const auto colour = colours::channel(ch);
        g.saveState();
        g.reduceClipRegion(area);
        if (!offSpans.empty()) {
            const float dashes[2] = { 3.0f, 3.0f };
            const float yBase = std::round(toY(0.0)) - 0.5f;
            g.setColour(colour.withAlpha(0.4f));
            for (const auto& sp : offSpans)
                g.drawDashedLine({ x0 + float(sp.first), yBase, x0 + float(sp.second), yBase }, dashes, 2, 1.0f);
        }
        const bool showDigital = trace != Trace::Analog || !wantAnalog;
        if (showDigital) strokeWithGlow(g, digital, colour, 1.5f * lineWidth);
        if (wantAnalog) strokeWithGlow(g, analog, trace == Trace::Both ? colour.interpolatedWith(juce::Colours::white, 0.7f) : colour, 1.2f * lineWidth);
        g.restoreState();
    }
};

ScopeView::ScopeView() : impl_(std::make_unique<Impl>(*this))
{
    setOpaque(false);
    setSize(220, 92);
    impl_->syncChrome();
}
ScopeView::~ScopeView() = default;

void ScopeView::setSource(Source s) { impl_->src = s; impl_->n = 0; impl_->restartTimer(); repaint(); }
void ScopeView::setChannel(int ch) { impl_->ch = juce::jlimit(0, 3, ch); impl_->syncChrome(); repaint(); }
void ScopeView::setTrace(Trace t) { impl_->trace = t; impl_->syncChrome(); repaint(); }
void ScopeView::setPeriods(int p) { impl_->periods = p >= 8 ? 8 : p >= 4 ? 4 : p >= 2 ? 2 : 1; impl_->syncChrome(); repaint(); }
void ScopeView::setAnalogCornerHz(double hz)
{
    if (std::abs(hz - impl_->cornerHz) < 1e-9) return;
    impl_->cornerHz = hz;
    impl_->syncChrome();
    repaint();
}
void ScopeView::setLcdGround(bool lcd)
{
    if (lcd) setGround(colours::lcd, colours::lcdGrid, colours::scopeBorder);
    else setGround(colours::well, colours::lineSoft, colours::lineSoft);
}
void ScopeView::setGround(juce::Colour ground, juce::Colour grid, juce::Colour border)
{
    impl_->ground = ground; impl_->grid = grid; impl_->border = border;
    repaint();
}
void ScopeView::setLineWidth(float px) { impl_->lineWidth = juce::jmax(0.5f, px); repaint(); }
void ScopeView::setChrome(bool on) { impl_->chrome = on; if (!on) impl_->showChrome(false); }
void ScopeView::setFrozen(bool frozen) { impl_->frozen = frozen; impl_->restartTimer(); }
void ScopeView::setIdleDim(bool on) { impl_->idleDim = on; repaint(); }
ScopeView::Trace ScopeView::trace() const { return impl_->trace; }
int ScopeView::periods() const { return impl_->periods; }
void ScopeView::resized() { impl_->layoutChrome(); }
void ScopeView::paint(juce::Graphics& g) { impl_->paint(g); }
void ScopeView::mouseEnter(const juce::MouseEvent&) { impl_->showChrome(impl_->chrome); }
void ScopeView::mouseExit(const juce::MouseEvent&) { if (!isMouseOver(true)) impl_->showChrome(false); }

// ===========================================================================
// MasterScope
// ===========================================================================
struct MasterScope::Impl : juce::Timer {
    MasterScope& owner;
    const plugin::AudioRing* ring = nullptr;
    double windowMs = 20.0;
    bool frozen = false, clipped = false;
    juce::Colour ground = colours::lcd, grid = colours::lcdGrid, border = colours::scopeBorder;
    float lineWidth = 1.0f;
    std::vector<float> left, right;
    uint32_t n = 0;
    uint32_t lastHead = 0;
    juce::Path pathL, pathR;

    explicit Impl(MasterScope& o) : owner(o)
    {
        left.resize(plugin::kAudioRing);
        right.resize(plugin::kAudioRing);
        pathL.preallocateSpace(1400 * 2 * 3);
        pathR.preallocateSpace(1400 * 2 * 3);
    }
    ~Impl() override { stopTimer(); }

    uint32_t wantedFrames() const
    {
        const double sr = ring != nullptr && ring->sampleRate > 0.0 ? ring->sampleRate : 48000.0;
        return uint32_t(juce::jlimit(16, int(plugin::kAudioRing) - 1024, juce::roundToInt(sr * windowMs / 1000.0)));
    }
    void restartTimer() { if (ring != nullptr && !frozen) startTimerHz(kFps); else stopTimer(); }

    void timerCallback() override
    {
        if (frozen || !owner.isShowing() || ring == nullptr) return;
        n = ring->snapshot(left.data(), right.data(), wantedFrames());
        clipped = false;
        for (uint32_t i = 0; i < n; ++i) if (std::abs(left[i]) > 4.0f || std::abs(right[i]) > 4.0f) { clipped = true; break; }
        owner.repaint();
    }

    void build(juce::Path& p, const float* v, juce::Rectangle<int> area) const
    {
        p.clear();
        if (n < 2) return;
        const float W = float(area.getWidth()), cy = float(area.getCentreY()), scale = float(area.getHeight()) * 0.5f / 4.5f;
        const float x0 = float(area.getX());
        if (n <= uint32_t(2 * area.getWidth())) {
            for (uint32_t i = 0; i < n; ++i) {
                const float x = x0 + W * float(i) / float(n - 1), y = cy - v[i] * scale;
                if (i == 0) p.startNewSubPath(x, y); else p.lineTo(x, y);
            }
            return;
        }
        const int cols = area.getWidth();
        for (int x = 0; x < cols; ++x) {
            const uint32_t a = uint32_t(uint64_t(n) * uint64_t(x) / uint64_t(cols)), b = juce::jmax(a + 1, uint32_t(uint64_t(n) * uint64_t(x + 1) / uint64_t(cols)));
            float lo = v[a], hi = v[a];
            for (uint32_t i = a + 1; i < b && i < n; ++i) { lo = juce::jmin(lo, v[i]); hi = juce::jmax(hi, v[i]); }
            const float px = x0 + float(x);
            if (x == 0) p.startNewSubPath(px, cy - hi * scale); else p.lineTo(px, cy - hi * scale);
            p.lineTo(px, cy - lo * scale);
        }
    }

    void paint(juce::Graphics& g)
    {
        const auto bounds = owner.getLocalBounds();
        paintGround(g, bounds, ground, border);
        const auto area = bounds.reduced(1);
        if (area.getWidth() < 8 || area.getHeight() < 8) return;
        for (int i = 0; i <= 4; ++i) {
            g.setColour(i == 2 ? grid.brighter(0.4f) : grid);
            g.fillRect(float(area.getX()), std::round(float(area.getY()) + float(area.getHeight()) * float(i) / 4.0f) - 0.5f, float(area.getWidth()), 1.0f);
        }
        build(pathL, left.data(), area);
        build(pathR, right.data(), area);
        g.saveState();
        g.reduceClipRegion(area);
        strokeWithGlow(g, pathR, colours::lcdTrace.withAlpha(0.55f), 1.2f * lineWidth);
        strokeWithGlow(g, pathL, colours::lcdTrace, 1.4f * lineWidth);
        g.restoreState();
        if (clipped) {
            g.setColour(colours::bad.withAlpha(0.12f));
            g.fillRoundedRectangle(bounds.toFloat(), 3.0f);
            g.setColour(colours::bad);
            g.setFont(Fonts::mono(9.0f));
            g.drawText("CLIP", area.reduced(5, 3), juce::Justification::topRight, false);
        }
    }
};

MasterScope::MasterScope() : impl_(std::make_unique<Impl>(*this))
{
    setOpaque(false);
    setSize(220, 92);
}
MasterScope::~MasterScope() = default;

void MasterScope::setSource(const plugin::AudioRing* ring) { impl_->ring = ring; impl_->n = 0; impl_->restartTimer(); repaint(); }
void MasterScope::setWindowMs(double ms) { impl_->windowMs = juce::jlimit(1.0, 300.0, ms); }
void MasterScope::setLcdGround(bool lcd)
{
    if (lcd) setGround(colours::lcd, colours::lcdGrid, colours::scopeBorder);
    else setGround(colours::well, colours::lineSoft, colours::lineSoft);
}
void MasterScope::setGround(juce::Colour ground, juce::Colour grid, juce::Colour border)
{
    impl_->ground = ground; impl_->grid = grid; impl_->border = border;
    repaint();
}
void MasterScope::setLineWidth(float px) { impl_->lineWidth = juce::jmax(0.5f, px); repaint(); }
void MasterScope::setFrozen(bool frozen) { impl_->frozen = frozen; impl_->restartTimer(); }
void MasterScope::resized() {}
void MasterScope::paint(juce::Graphics& g) { impl_->paint(g); }

// ===========================================================================
// RegisterLine
// ===========================================================================
void RegisterLine::setChannel(int ch) { ch_ = juce::jlimit(0, 3, ch); repaint(); }
void RegisterLine::setState(uint64_t packed)
{
    if ((packed & 0xFFFFFFFFFFull) == (state_ & 0xFFFFFFFFFFull)) { state_ = packed; return; }   // only the registers are drawn
    state_ = packed;
    repaint();
}

void RegisterLine::paint(juce::Graphics& g)
{
    driver::VoiceView v;
    link::unpackState(state_, v);
    const auto font = Fonts::mono(10.5f).withExtraKerningFactor(0.02f);
    g.setFont(font);
    const int first = ch_ == 0 || ch_ == 2 ? 0 : 1;   // PU2 and NOI have no NRx0
    const juce::String dot = " " + juce::String::charToString(0x00b7) + " ";
    float x = 0.0f;
    const auto b = getLocalBounds();
    auto put = [&](const juce::String& text, juce::Colour c) {
        const float w = draw::textWidth(font, text);
        g.setColour(c);
        g.drawText(text, juce::Rectangle<float>(x, 0.0f, w + 2.0f, float(b.getHeight())), juce::Justification::centredLeft, false);
        x += w;
    };
    for (int i = first; i < 5; ++i) {
        if (i > first) put(dot, colours::textDim);
        put("NR" + juce::String(ch_ + 1) + juce::String(i), colours::textMute);
        put(" " + ValueFormat::byte(v.regs[i]), colours::textDim);
    }
}

} // namespace chipboy::ui
