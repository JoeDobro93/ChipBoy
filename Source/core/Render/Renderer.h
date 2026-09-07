// ChipBoy -- the renderer (L4) and the analog stage (L3) it drives.
//
// Consumes the APU's event streams and produces host-rate stereo in rail
// units: one channel at full scale is +-1, four channels at master volume 7
// are +-4. The signal path, in hardware order (spec section 6):
//
//   DAC (hold when off) -> NR51 gate -> sum -> NR50 -> [amp low-pass, folded
//   into the band-limited step] -> soft clip -> AC coupling -> noise floor
//   -> decimation to the host rate.
//
// Steps are placed at exact CPU-cycle positions as band-limited steps at
// twice the host rate (BLEP); everything after is sample-by-sample at that
// rate. Every position derives from absolute counters, so the output does
// not depend on how the host chops time into blocks (spec section 7.1).
#pragma once

#include "core/Analog/AnalogModel.h"
#include "core/Apu/Apu.h"
#include "core/Render/Kernel.h"

#include <cstdint>
#include <vector>

namespace chipboy::render {

class Renderer {
public:
    static constexpr int kOversample = 2;

    /// Build the kernel and filters for a host rate. `maxBlockFrames` sizes
    /// the buffers; larger render() calls are chunked internally.
    void prepare(double hostSampleRate, const AnalogModel& model, int maxBlockFrames = 4096);
    /// Back to time zero with everything at rest. Events at cycle 0 then
    /// describe the starting state and produce no step.
    void reset();

    /// The APU cycle at which absolute host frame `frame` begins. Run the APU
    /// to cycleForFrame(framesRendered() + n) before render(n).
    uint64_t cycleForFrame(uint64_t frame) const { return cycleForWork(frame * kOversample); }

    /// Consume every event the APU has emitted and write `nFrames` of stereo.
    /// The APU must have been run to at least cycleForFrame(framesRendered()
    /// + nFrames); its event vectors are cleared on return.
    void render(Apu& apu, float* outL, float* outR, int nFrames);

    /// Frames between a register write and its effect in the output.
    int latencyFrames() const { return (kernel_.head + (firTaps_ - 1) / 2) / kOversample; }
    uint64_t framesRendered() const { return frames_; }

    /// Headphone Noise (spec C8): the one switch. Default on.
    void setNoise(bool on) { noise_ = on; }
    bool noise() const { return noise_; }

    const AnalogModel& model() const { return model_; }
    double hostSampleRate() const { return fsHost_; }
    double workingSampleRate() const { return fsWork_; }
    const Kernel& kernel() const { return kernel_; }

    /// The analog stage's soft clip (section 6.3), exposed for the reference path.
    static double softClip(double x, const AnalogModel& m);

private:
    struct Side {
        std::vector<float> ring;      ///< band-limited deltas by working sample
        std::vector<float> firHist;
        double integrator = 0.0;      ///< running sum of the deltas
        double hpIn = 0.0, hpOut = 0.0;
        double current = 0.0;         ///< analog value the steps are computed from
        uint32_t firPos = 0;
    };

    uint64_t cycleForWork(uint64_t m) const;
    void applyChannel(const ApuEvent& e);
    void applyMix(const MixEvent& e);
    void updateSides(uint64_t cycle);
    void addStep(Side& s, double position, double height);
    double sideValue(int side) const;
    void renderWorking(uint64_t m1, float* outL, float* outR);

    AnalogModel model_;
    Kernel kernel_;
    std::vector<float> fir_;
    int firTaps_ = 1;
    uint32_t firMask_ = 0;
    int maxBlock_ = 0;
    uint32_t ringMask_ = 0;

    double fsHost_ = 0.0, fsWork_ = 0.0;
    double cyclesPerWork_ = 0.0, workPerCycle_ = 0.0;
    double hpCoef_ = 1.0;
    double hissPerSample_ = 0.0;
    uint64_t lineInc_ = 0, line2Inc_ = 0, frameInc_ = 0;
    uint64_t linePhase_ = 0, line2Phase_ = 0, framePhase_ = 0;

    uint64_t frames_ = 0, work_ = 0;
    Side side_[2];

    double  dacVal_[4]{};
    bool    dacOn_[4]{};
    uint8_t nr50_ = 0, nr51_ = 0;
    bool    powered_ = false;
    bool    noise_ = true;
    uint64_t seed_ = 0x243F6A8885A308D3ull;
};

} // namespace chipboy::render
