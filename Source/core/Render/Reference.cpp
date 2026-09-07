#include "core/Render/Reference.h"

#include "core/Render/Kernel.h"
#include "core/Render/Renderer.h"

#include <algorithm>
#include <cmath>

namespace chipboy::render {

void Reference::render(const std::vector<ApuEvent>& events, const std::vector<MixEvent>& mix,
                       uint64_t cycles, const AnalogModel& model, double hostSampleRate,
                       std::vector<float>& outL, std::vector<float>& outR)
{
    constexpr int OS = Renderer::kOversample;
    const double fsWork = hostSampleRate * OS;
    const double cpw = double(kCpuHz) / fsWork;   // cycles per working sample
    const double wpc = 1.0 / cpw;
    const Kernel kernel = Kernel::design(fsWork, model.ampCutoffHz);

    // --- per-cycle side values --------------------------------------------
    double dac[4] = {0, 0, 0, 0};
    uint8_t nr50 = 0, nr51 = 0;
    bool powered = false;
    auto sideValue = [&](int side) {
        if (!powered) return 0.0;
        const int shift = side == 0 ? 4 : 0;
        double sum = 0.0;
        for (int c = 0; c < 4; ++c)
            if (nr51 & (1u << (c + shift))) sum += dac[c];
        return double(((nr50 >> shift) & 7) + 1) / 8.0 * sum;
    };
    std::vector<float> s[2];
    s[0].assign(size_t(cycles), 0.0f);
    s[1].assign(size_t(cycles), 0.0f);
    size_t i = 0, j = 0;
    uint64_t filled = 0;
    double cur[2] = {0.0, 0.0};
    auto fillTo = [&](uint64_t c) {
        c = std::min(c, cycles);
        for (uint64_t k = filled; k < c; ++k) { s[0][size_t(k)] = float(cur[0]); s[1][size_t(k)] = float(cur[1]); }
        filled = std::max(filled, c);
    };
    while (i < events.size() || j < mix.size()) {
        const uint64_t ce = i < events.size() ? events[i].cycle : ~0ull;
        const uint64_t cm = j < mix.size() ? mix[j].cycle : ~0ull;
        const uint64_t c = std::min(ce, cm);
        fillTo(c);
        if (cm <= ce) {
            const auto& e = mix[j++];
            if (powered && !e.powered) for (auto& d : dac) d = 0.0;
            nr50 = e.nr50; nr51 = e.nr51; powered = e.powered;
        } else {
            const auto& e = events[i++];
            if (e.dacOn) dac[e.channel & 3] = -(double(e.level) - 7.5) / 7.5;   // off: hold
        }
        cur[0] = sideValue(0);
        cur[1] = sideValue(1);
    }
    fillTo(cycles);
    // Before cycle 0 the stage sat at its starting value.
    const double initial[2] = { cycles ? double(s[0][0]) : 0.0, cycles ? double(s[1][0]) : 0.0 };

    // --- band-limit: integrate the staircase against the kernel -----------
    const uint64_t M = uint64_t(std::floor(double(cycles) * wpc));
    std::vector<double> y[2];
    y[0].assign(size_t(M), 0.0);
    y[1].assign(size_t(M), 0.0);
    for (uint64_t m = 0; m < M; ++m) {
        const int64_t cLo = int64_t(std::floor((double(m) - kernel.span) * cpw)) - 1;
        const int64_t cHi = std::min<int64_t>(int64_t(cycles), int64_t(std::ceil(double(m) * cpw)) + 1);
        double acc[2] = {0.0, 0.0};
        for (int64_t c = cLo; c < cHi; ++c) {
            const double d = double(m) - kernel.head - double(c) * wpc;
            const double w = kernel.stepAt(d) - kernel.stepAt(d - wpc);
            if (w == 0.0) continue;
            const double v0 = c < 0 ? initial[0] : double(s[0][size_t(c)]);
            const double v1 = c < 0 ? initial[1] : double(s[1][size_t(c)]);
            acc[0] += v0 * w;
            acc[1] += v1 * w;
        }
        y[0][size_t(m)] = acc[0];
        y[1][size_t(m)] = acc[1];
    }

    // --- clip, coupling, decimate -----------------------------------------
    const double hp = std::pow(model.couplingPerCycle, cpw);
    const std::vector<float> fir = designLowpass(153, 0.23, 96.0);
    const int taps = int(fir.size());
    for (int side = 0; side < 2; ++side) {
        auto& w = y[side];
        double xPrev = initial[side], yPrev = 0.0;
        for (auto& v : w) {
            const double x = Renderer::softClip(v, model);
            const double o = hp * yPrev + x - xPrev;
            xPrev = x; yPrev = o;
            v = o;
        }
        auto& out = side == 0 ? outL : outR;
        out.assign(size_t(M / OS), 0.0f);
        for (uint64_t f = 0; f < M / OS; ++f) {
            const int64_t m = int64_t(f) * OS + (OS - 1);
            double acc = 0.0;
            for (int t = 0; t < taps; ++t) {
                const int64_t k = m - t;
                if (k < 0) break;
                acc += double(fir[size_t(t)]) * w[size_t(k)];
            }
            out[size_t(f)] = float(acc);
        }
    }
}

} // namespace chipboy::render
