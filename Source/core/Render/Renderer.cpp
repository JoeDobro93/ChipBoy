#include "core/Render/Renderer.h"

#include <algorithm>
#include <cmath>

namespace chipboy::render {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr int    kDecimatorTaps = 153;      ///< 96 dB, 0.21 -> 0.25 of the working rate
constexpr double kSqrt3 = 1.7320508075688772;

/// Stateless hash of a sample index: the same index always gives the same
/// noise, whatever the block size (spec section 7.1).
uint64_t hash64(uint64_t x)
{
    x ^= x >> 30; x *= 0xBF58476D1CE4E5B9ull;
    x ^= x >> 27; x *= 0x94D049BB133111EBull;
    x ^= x >> 31;
    return x;
}

double dacValue(uint8_t level)
{
    // Linear, and inverting like the hardware (reference section 12.2):
    // digital 0 is the positive rail, 15 the negative one.
    return -(double(level) - 7.5) / 7.5;
}

} // namespace

double Renderer::softClip(double x, const AnalogModel& m)
{
    const double a = std::fabs(x);
    if (a <= m.clipKnee) return x;
    const double room = m.clipLimit - m.clipKnee;
    const double y = m.clipKnee + room * std::tanh((a - m.clipKnee) / room);
    return x < 0 ? -y : y;
}

void Renderer::prepare(double hostSampleRate, const AnalogModel& model, int maxBlockFrames)
{
    model_ = model;
    fsHost_ = hostSampleRate;
    fsWork_ = hostSampleRate * kOversample;
    cyclesPerWork_ = double(kCpuHz) / fsWork_;
    workPerCycle_ = fsWork_ / double(kCpuHz);
    kernel_ = Kernel::design(fsWork_, model.ampCutoffHz);
    hpCoef_ = std::pow(model.couplingPerCycle, cyclesPerWork_);

    fir_ = designLowpass(kDecimatorTaps, 0.23, 96.0);
    firTaps_ = int(fir_.size());
    uint32_t fn = 1;
    while (fn < uint32_t(firTaps_)) fn <<= 1;
    firMask_ = fn - 1;

    maxBlock_ = std::max(maxBlockFrames, 16);
    const size_t need = size_t(maxBlock_) * kOversample + size_t(kernel_.span) + 512;
    size_t rn = 1;
    while (rn < need) rn <<= 1;
    ringMask_ = uint32_t(rn - 1);
    for (auto& s : side_) {
        s.ring.assign(rn, 0.0f);
        s.firHist.assign(fn, 0.0f);
    }

    // The measured floor is an RMS over 0-96 kHz; keep the spectral density.
    hissPerSample_ = model.hissRms * std::sqrt(fsWork_ / 192000.0) * kSqrt3;
    auto inc = [&](double hz) { return uint64_t(std::ldexp(hz / fsWork_, 64)); };
    lineInc_  = inc(model.lcdLineHz);
    line2Inc_ = inc(2.0 * model.lcdLineHz);
    frameInc_ = inc(model.frameHz);
    reset();
}

void Renderer::reset()
{
    frames_ = work_ = 0;
    for (auto& s : side_) {
        std::fill(s.ring.begin(), s.ring.end(), 0.0f);
        std::fill(s.firHist.begin(), s.firHist.end(), 0.0f);
        s.integrator = s.hpIn = s.hpOut = s.current = 0.0;
        s.firPos = 0;
    }
    for (int c = 0; c < 4; ++c) { dacVal_[c] = 0.0; dacOn_[c] = false; }
    nr50_ = nr51_ = 0;
    powered_ = false;
    linePhase_ = line2Phase_ = framePhase_ = 0;
}

uint64_t Renderer::cycleForWork(uint64_t m) const
{
    return uint64_t(std::ceil(double(m) * cyclesPerWork_));
}

double Renderer::sideValue(int side) const
{
    if (!powered_) return 0.0;
    const int shift = side == 0 ? 4 : 0;
    const double gain = double(((nr50_ >> shift) & 7) + 1) / 8.0;
    double sum = 0.0;
    for (int c = 0; c < 4; ++c)
        if (nr51_ & (1u << (c + shift))) sum += dacVal_[c];
    return gain * sum;
}

void Renderer::addStep(Side& s, double position, double height)
{
    const uint64_t mf = uint64_t(position);
    const double frac = position - double(mf);
    const double fi = frac * kernel_.phases;
    const int fiI = int(fi);
    const float fu = float(fi - fiI);
    const float* T = kernel_.delta.data();
    const float h = float(height);
    uint64_t m = mf + 1;
    for (int j = 0; j < kernel_.span; ++j, ++m) {
        const int k = (j + 1) * kernel_.phases - fiI;
        const float t = T[k - 1] * fu + T[k] * (1.0f - fu);
        s.ring[m & ringMask_] += h * t;
    }
}

void Renderer::updateSides(uint64_t cycle)
{
    for (int i = 0; i < 2; ++i) {
        const double v = sideValue(i);
        const double h = v - side_[i].current;
        if (h == 0.0) continue;
        if (cycle == 0 && work_ == 0) {
            // The starting state, not a transition: the stage was already there.
            side_[i].integrator = v;
            side_[i].hpIn = v;
        } else {
            addStep(side_[i], double(cycle) * workPerCycle_, h);
        }
        side_[i].current = v;
    }
}

void Renderer::applyChannel(const ApuEvent& e)
{
    const int c = e.channel & 3;
    // A disabled DAC holds its last output; it does not return to zero
    // (reference section 9, measured 2026-09-07).
    if (e.dacOn) dacVal_[c] = dacValue(e.level);
    dacOn_[c] = e.dacOn;
    updateSides(e.cycle);
}

void Renderer::applyMix(const MixEvent& e)
{
    if (powered_ && !e.powered)
        for (int c = 0; c < 4; ++c) { dacVal_[c] = 0.0; dacOn_[c] = false; }
    nr50_ = e.nr50;
    nr51_ = e.nr51;
    powered_ = e.powered;
    updateSides(e.cycle);
}

void Renderer::render(Apu& apu, float* outL, float* outR, int nFrames)
{
    auto& ev = apu.events();
    auto& mx = apu.mixEvents();
    size_t i = 0, j = 0;
    int done = 0;
    while (done < nFrames) {
        const int n = std::min(nFrames - done, maxBlock_);
        const bool last = done + n == nFrames;
        const uint64_t m1 = work_ + uint64_t(n) * kOversample;
        const uint64_t cEnd = cycleForWork(m1);
        while (i < ev.size() || j < mx.size()) {
            const uint64_t ce = i < ev.size() ? ev[i].cycle : ~0ull;
            const uint64_t cm = j < mx.size() ? mx[j].cycle : ~0ull;
            if (!last && std::min(ce, cm) >= cEnd) break;
            if (cm <= ce) applyMix(mx[j++]);
            else          applyChannel(ev[i++]);
        }
        renderWorking(m1, outL + done, outR + done);
        done += n;
    }
    apu.clearEvents();
    frames_ += uint64_t(nFrames);
}

void Renderer::renderWorking(uint64_t m1, float* outL, float* outR)
{
    float* out[2] = { outL, outR };
    int k = 0;
    for (uint64_t m = work_; m < m1; ++m) {
        // Noise floor, shared components (the LCD and frame lines are common
        // to both sides; the hiss is not).
        double lines = 0.0;
        if (noise_) {
            linePhase_ += lineInc_; line2Phase_ += line2Inc_; framePhase_ += frameInc_;
            const double s = std::ldexp(1.0, -64) * 2.0 * kPi;
            lines = model_.lcdLineAmp  * std::sin(double(linePhase_)  * s)
                  + model_.lcdLine2Amp * std::sin(double(line2Phase_) * s)
                  + model_.frameAmp    * std::sin(double(framePhase_) * s);
        }
        for (int side = 0; side < 2; ++side) {
            Side& s = side_[side];
            float& slot = s.ring[m & ringMask_];
            s.integrator += double(slot);
            slot = 0.0f;
            const double x = softClip(s.integrator, model_);
            const double y = hpCoef_ * s.hpOut + x - s.hpIn;
            s.hpIn = x;
            s.hpOut = y;
            double v = y;
            if (noise_) {
                const uint64_t z = hash64((m * 2 + uint64_t(side)) ^ seed_);
                const double u = (double(z & 0xFFFF) + double((z >> 16) & 0xFFFF)
                                + double((z >> 32) & 0xFFFF) + double(z >> 48)) / 65536.0 - 2.0;
                v += u * hissPerSample_ + lines;
            }
            s.firHist[s.firPos] = float(v);
            s.firPos = (s.firPos + 1) & firMask_;
            if ((m % kOversample) == kOversample - 1) {
                double acc = 0.0;
                uint32_t idx = s.firPos;
                for (int t = 0; t < firTaps_; ++t) {
                    idx = (idx - 1) & firMask_;
                    acc += double(fir_[size_t(t)]) * double(s.firHist[idx]);
                }
                out[side][k] = float(acc);
            }
        }
        if ((m % kOversample) == kOversample - 1) ++k;
    }
    work_ = m1;
}

} // namespace chipboy::render
