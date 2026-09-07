#include "core/Render/Kernel.h"

#include <algorithm>
#include <cmath>

namespace chipboy::render {

namespace {

constexpr double kPi = 3.14159265358979323846;

double besselI0(double x)
{
    // Series expansion; converges quickly for the beta values used here.
    double sum = 1.0, term = 1.0;
    const double q = x * x / 4.0;
    for (int k = 1; k < 60; ++k) {
        term *= q / double(k * k);
        sum += term;
        if (term < sum * 1e-17) break;
    }
    return sum;
}

double kaiserBeta(double stopDb)
{
    return stopDb > 50.0 ? 0.1102 * (stopDb - 8.7)
         : stopDb > 21.0 ? 0.5842 * std::pow(stopDb - 21.0, 0.4) + 0.07886 * (stopDb - 21.0)
         : 0.0;
}

} // namespace

std::vector<float> designLowpass(int taps, double cutoff, double stopDb)
{
    if (taps % 2 == 0) ++taps;
    const int half = taps / 2;
    const double beta = kaiserBeta(stopDb);
    const double i0beta = besselI0(beta);
    std::vector<double> h(static_cast<size_t>(taps));
    double sum = 0.0;
    for (int i = 0; i < taps; ++i) {
        const double d = i - half;
        const double x = d / (half + 1.0);
        const double w = besselI0(beta * std::sqrt(std::max(0.0, 1.0 - x * x))) / i0beta;
        const double s = d == 0.0 ? 1.0 : std::sin(2.0 * kPi * cutoff * d) / (2.0 * kPi * cutoff * d);
        h[size_t(i)] = 2.0 * cutoff * s * w;
        sum += h[size_t(i)];
    }
    std::vector<float> out(static_cast<size_t>(taps));
    for (int i = 0; i < taps; ++i) out[size_t(i)] = float(h[size_t(i)] / sum);
    return out;
}

Kernel Kernel::design(double fsWorking, double ampCutoffHz, int halfLength, int phases, double stopDb)
{
    Kernel k;
    k.phases = phases;
    k.head = halfLength;
    k.sampleRate = fsWorking;

    // Windowed sinc at the working Nyquist: flat to a quarter of the working
    // rate (the host Nyquist at 2x oversampling) and -stopDb by three quarters.
    const double beta = kaiserBeta(stopDb);
    const double i0beta = besselI0(beta);
    const double fc = 0.5;            // cycles per working sample

    // The amplifier's one-pole low-pass lengthens the tail: allow ten time
    // constants, at least two samples, after the sinc ends.
    const double tauSamples = fsWorking / (2.0 * kPi * ampCutoffHz);
    const int tail = int(std::ceil(10.0 * tauSamples)) + 2;
    const int before = halfLength;                  // sinc support: [-H, H)
    const int after = halfLength + tail;
    const int n = (before + after) * phases + 1;    // grid over d in [-H, H + tail]
    const double dd = 1.0 / phases;

    // Windowed sinc on the fine grid.
    std::vector<double> h(size_t(n), 0.0);
    for (int i = 0; i < n; ++i) {
        const double d = -before + i * dd;
        if (d <= -halfLength || d >= halfLength) continue;
        const double x = d / halfLength;
        const double w = besselI0(beta * std::sqrt(1.0 - x * x)) / i0beta;
        const double s = d == 0.0 ? 1.0 : std::sin(kPi * 2.0 * fc * d) / (kPi * 2.0 * fc * d);
        h[size_t(i)] = 2.0 * fc * s * w;
    }
    // One-pole low-pass, exact for the piecewise-constant fine-grid signal.
    {
        const double b = std::exp(-dd / tauSamples);
        double y = 0.0;
        for (int i = 0; i < n; ++i) {
            y = b * y + (1.0 - b) * h[size_t(i)];
            h[size_t(i)] = y;
        }
    }
    // Integrate to the step response and normalise its final value to 1.
    std::vector<double> S(size_t(n), 0.0);
    double acc = 0.0;
    for (int i = 0; i < n; ++i) {
        acc += h[size_t(i)] * dd;
        S[size_t(i)] = acc;
    }
    const double norm = 1.0 / S.back();
    for (auto& v : S) v *= norm;

    // T(d) = S(d) - S(d - 1); one more sample so the difference reaches zero.
    k.span = before + after + 1;
    const int m = k.span * phases + 1;
    k.step.assign(size_t(m), 1.0f);
    k.delta.assign(size_t(m), 0.0f);
    auto sAt = [&](int i) { return i < 0 ? 0.0 : i >= n ? 1.0 : S[size_t(i)]; };
    for (int i = 0; i < m; ++i) {
        k.step[size_t(i)]  = float(sAt(i));
        k.delta[size_t(i)] = float(sAt(i) - sAt(i - phases));
    }
    return k;
}

double Kernel::stepAt(double d) const
{
    const double x = (d + head) * phases;
    if (x <= 0.0) return 0.0;
    const int last = int(step.size()) - 1;
    if (x >= double(last)) return 1.0;
    const int i = int(x);
    const double u = x - i;
    return double(step[size_t(i)]) * (1.0 - u) + double(step[size_t(i) + 1]) * u;
}

} // namespace chipboy::render
