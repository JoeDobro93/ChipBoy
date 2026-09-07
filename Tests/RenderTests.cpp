// ChipBoy -- analog stage and renderer tests (spec sections 6, 7, 16.2-16.4).
#include "core/Render/Kernel.h"
#include "core/Render/Reference.h"
#include "core/Render/Renderer.h"
#include "tools/harness/Script.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

using namespace chipboy;
using namespace chipboy::harness;
using chipboy::render::Renderer;

namespace {

double rms(const std::vector<float>& v, size_t from, size_t to)
{
    double s = 0.0;
    for (size_t i = from; i < to; ++i) s += double(v[i]) * double(v[i]);
    return std::sqrt(s / double(to - from));
}

} // namespace

TEST_CASE("band-limited step kernel", "[render]")
{
    const auto k = render::Kernel::design(96000.0, 80000.0);
    CHECK(k.stepAt(-1000.0) == 0.0);
    CHECK(k.stepAt(1000.0) == 1.0);
    CHECK(std::fabs(k.step.front()) < 1e-6);
    CHECK(std::fabs(k.step.back() - 1.0f) < 1e-6);
    // Placed at any fractional position, the per-sample differences sum to
    // exactly one step, so the integrator lands on the step height.
    for (double frac : { 0.0, 0.1, 0.25, 0.5, 0.75, 0.999 }) {
        const double fi = frac * k.phases;
        const int fiI = int(fi);
        const float fu = float(fi - fiI);
        double sum = 0.0;
        for (int j = 0; j < k.span; ++j) {
            const int i = (j + 1) * k.phases - fiI;
            sum += k.delta[size_t(i - 1)] * fu + k.delta[size_t(i)] * (1.0f - fu);
        }
        INFO("fraction " << frac);
        CHECK(std::fabs(sum - 1.0) < 1e-5);
    }
}

TEST_CASE("output does not depend on the block size", "[render]")
{
    const auto script = testScript();
    const auto model = AnalogModel::dmg();
    const uint64_t frames = 48000 * 3 / 10;
    const auto ref = renderScript(script, model, 48000.0, frames, [](uint64_t) { return 512; }, true);
    REQUIRE(ref.size() == frames * 2);
    REQUIRE(rms(ref, 0, ref.size()) > 0.01);

    auto same = [&](const std::vector<float>& out, const char* what) {
        REQUIRE(out.size() == ref.size());
        size_t first = out.size();
        for (size_t i = 0; i < out.size(); ++i)
            if (std::memcmp(&out[i], &ref[i], sizeof(float)) != 0) { first = i; break; }
        INFO(what << ": first difference at interleaved index " << first);
        CHECK(first == out.size());
    };
    for (int bs : { 32, 64, 128, 2048 })
        same(renderScript(script, model, 48000.0, frames, [bs](uint64_t) { return bs; }, true),
             bs == 32 ? "32" : bs == 64 ? "64" : bs == 128 ? "128" : "2048");
    same(renderScript(script, model, 48000.0, frames, [](uint64_t b) { return int(1 + (b * 37) % 211); }, true),
         "varying");
    // Larger than the renderer's own buffer: it chunks internally.
    same(renderScript(script, model, 48000.0, frames, [](uint64_t) { return 12000; }, true), "12000");
}

TEST_CASE("fast path nulls against the brute-force reference", "[render]")
{
    const auto script = testScript();
    const auto model = AnalogModel::dmg();
    const uint64_t frames = 48000 * 15 / 100;
    std::vector<ApuEvent> ev;
    std::vector<MixEvent> mx;
    const auto fast = renderScript(script, model, 48000.0, frames, [](uint64_t) { return 256; }, false, &ev, &mx);

    Renderer probe;
    probe.prepare(48000.0, model);
    const uint64_t cycles = probe.cycleForFrame(frames);
    std::vector<float> L, R;
    render::Reference::render(ev, mx, cycles, model, 48000.0, L, R);
    REQUIRE(L.size() >= frames);

    std::vector<float> fl(frames), fr(frames), dl(frames), dr(frames);
    for (uint64_t f = 0; f < frames; ++f) {
        fl[f] = fast[f * 2];
        fr[f] = fast[f * 2 + 1];
        dl[f] = fl[f] - L[f];
        dr[f] = fr[f] - R[f];
    }
    const size_t from = size_t(probe.latencyFrames()) + 64;
    const double sig = rms(fl, from, frames);
    const double resL = rms(dl, from, frames), resR = rms(dr, from, frames);
    REQUIRE(sig > 0.01);
    const double dbL = 20.0 * std::log10(resL / sig + 1e-30), dbR = 20.0 * std::log10(resR / sig + 1e-30);
    INFO("signal rms " << sig << ", residual L " << dbL << " dB, R " << dbR << " dB");
    CHECK(dbL < -90.0);
    CHECK(dbR < -90.0);
}

TEST_CASE("a disabled DAC holds its level", "[render]")
{
    // Reference section 9 as measured: DAC-off makes no step, DAC-on at the
    // held level makes none either, DAC-on at another level steps the
    // difference. NR51 = $F3 puts CH3 on the left only.
    std::vector<Write> s;
    auto at = [&](double t, uint16_t a, uint8_t v) { s.push_back({uint64_t(t * kCpuHz), a, v}); };
    for (int i = 0; i < 16; ++i) at(0.0, uint16_t(0xFF30 + i), 0xFF);   // constant 15
    at(0.0, 0xFF1A, 0x80);
    at(0.0, 0xFF1C, 0x20);
    at(0.0, 0xFF1D, 0xFF);
    at(0.0, 0xFF1E, 0x87);
    // The trigger plays the stale sample buffer (0) for one period before the
    // first fetch, so there is one real -2 step at cycle 8; its tail through
    // the 6.4 ms coupling is below 5e-3 after 40 ms.
    at(0.10, 0xFF1A, 0x00);                        // DAC off: hold
    at(0.15, 0xFF1A, 0x80);                        // DAC on, same level: nothing
    at(0.15, 0xFF1E, 0x87);
    at(0.20, 0xFF1A, 0x00);
    for (int i = 0; i < 16; ++i) at(0.2001, uint16_t(0xFF30 + i), 0x00);
    at(0.2002, 0xFF1A, 0x80);                      // DAC on at level 0: a two-rail step
    at(0.2002, 0xFF1E, 0x87);
    const uint64_t frames = 48000 / 4;
    const auto out = renderScript(s, AnalogModel::dmg(), 48000.0, frames, [](uint64_t) { return 256; }, false);
    auto peakL = [&](double t0, double t1) {
        float p = 0.0f;
        for (uint64_t f = uint64_t(t0 * 48000); f < uint64_t(t1 * 48000); ++f) p = std::max(p, std::fabs(out[f * 2]));
        return p;
    };
    CHECK(peakL(0.0, 0.01) > 1.5f);               // the stale-buffer step
    CHECK(peakL(0.06, 0.099) < 5e-3f);            // settled
    CHECK(peakL(0.12, 0.149) < 5e-3f);            // DAC off: nothing (a step would read ~0.05 here)
    CHECK(peakL(0.17, 0.199) < 5e-3f);            // DAC on at the held level: nothing
    CHECK(peakL(0.20, 0.21) > 1.5f);              // -1 to +1 rail
}

TEST_CASE("golden render", "[render]")
{
    // Spec section 16.2 asks for a hash; a float comparison with a tolerance
    // is used instead because the kernel design goes through libm, whose
    // last bits differ between platforms. Regenerate with
    // CHIPBOY_REGEN_GOLDEN=1 after a deliberate change, and say so in CHANGES.md.
    const std::filesystem::path path = std::filesystem::path(CHIPBOY_GOLDEN_DIR) / "render_dmg_48k.f32";
    const uint64_t frames = 12000;
    const auto out = renderScript(testScript(), AnalogModel::dmg(), 48000.0, frames, [](uint64_t) { return 512; }, true);
    if (std::getenv("CHIPBOY_REGEN_GOLDEN")) {
        std::ofstream f(path, std::ios::binary);
        f.write(reinterpret_cast<const char*>(out.data()), std::streamsize(out.size() * sizeof(float)));
        SUCCEED("golden regenerated");
        return;
    }
    std::ifstream f(path, std::ios::binary);
    if (!f) SKIP("missing golden file " << path.string());
    std::vector<float> ref(out.size());
    f.read(reinterpret_cast<char*>(ref.data()), std::streamsize(ref.size() * sizeof(float)));
    REQUIRE(size_t(f.gcount()) == ref.size() * sizeof(float));
    double maxDiff = 0.0;
    size_t where = 0;
    for (size_t i = 0; i < out.size(); ++i) {
        const double d = std::fabs(double(out[i]) - double(ref[i]));
        if (d > maxDiff) { maxDiff = d; where = i; }
    }
    INFO("largest difference " << maxDiff << " at interleaved index " << where);
    CHECK(maxDiff < 2e-5);
}
