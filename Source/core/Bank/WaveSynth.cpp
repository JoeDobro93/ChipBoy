#include "core/Bank/WaveSynth.h"

#include <algorithm>
#include <cmath>

namespace chipboy::bank {

namespace {

constexpr int kN = 32;                 ///< samples in a frame
constexpr int kHarmonics = kN / 2;     ///< 1..16; 0 is the DC term
constexpr double kPi = 3.14159265358979323846;

/// The chip's sixteen levels, the way every other generator in the bank
/// writes them (Bank.cpp): -1 .. +1 mapped onto 0 .. 15, rounded half away
/// from zero, so a synthesised sine is the same bytes as frameSine().
uint8_t quantise(double v) { return uint8_t(std::clamp(int(std::lround(7.5 + 7.5 * v)), 0, 15)); }
double  level(uint8_t s) { return (double(s) - 7.5) / 7.5; }

/// The numbers of one morph position: SynthState with the integers turned
/// into the doubles the render works in, so the frames between the ends move
/// smoothly instead of stepping.
struct Params {
    double width = 16.0;
    double partials[kSynthPartials] = { 15.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };
    double amount[kSynthStages] = { 0.0, 0.0, 0.0, 0.0 };
    double resonance[kSynthStages] = { 0.0, 0.0, 0.0, 0.0 };
};

double lerp(double a, double b, double t) { return a + (b - a) * t; }

Params morph(const SynthState& a, const SynthState& b, double t)
{
    Params p;
    p.width = lerp(double(a.width), double(b.width), t);
    for (int i = 0; i < kSynthPartials; ++i) p.partials[i] = lerp(double(a.partials[size_t(i)]), double(b.partials[size_t(i)]), t);
    for (int i = 0; i < kSynthStages; ++i) {
        p.amount[i] = lerp(double(a.amount[size_t(i)]), double(b.amount[size_t(i)]), t);
        p.resonance[i] = lerp(double(a.resonance[size_t(i)]), double(b.resonance[size_t(i)]), t);
    }
    return p;
}

/* ----------------------------------------------------------- sources */

/// A 32-bit xorshift, so the Noise source is the same run everywhere. The
/// seed alone decides it: every frame of a run starts from the same noise,
/// and what moves across the run is what the shapers do to it.
uint32_t nextRandom(uint32_t& state)
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

void makeSource(SynthSource src, const Params& p, uint8_t seed, const Frame& drawn, double* out)
{
    switch (src) {
        case SynthSource::Sine:
            for (int i = 0; i < kN; ++i) out[i] = std::sin(2.0 * kPi * (double(i) + 0.5) / double(kN));
            break;
        case SynthSource::Triangle:
            // The bank's own triangle: up over the first half, down over the
            // second, so a synthesised one is frameTriangle() exactly.
            for (int i = 0; i < kN; ++i) out[i] = level(uint8_t(i < 16 ? i : 31 - i));
            break;
        case SynthSource::Saw:
            for (int i = 0; i < kN; ++i) out[i] = level(uint8_t(i / 2));
            break;
        case SynthSource::Square: {
            const double w = std::clamp(p.width, 1.0, 31.0);
            for (int i = 0; i < kN; ++i) out[i] = double(i) < w ? 1.0 : -1.0;
            break;
        }
        case SynthSource::Additive: {
            double peak = 0.0;
            for (int i = 0; i < kN; ++i) {
                double v = 0.0;
                for (int k = 0; k < kSynthPartials; ++k) {
                    const double a = std::clamp(p.partials[k], 0.0, 15.0) / 15.0;
                    if (a <= 0.0) continue;
                    v += a * std::sin(2.0 * kPi * double(k + 1) * (double(i) + 0.5) / double(kN));
                }
                out[i] = v;
                peak = std::max(peak, std::abs(v));
            }
            // One partial at full level is the sine exactly; more than one is
            // scaled to fit, so the sum never clips before the shapers see it.
            if (peak > 1.0e-9) for (int i = 0; i < kN; ++i) out[i] /= peak;
            break;
        }
        case SynthSource::Noise: {
            uint32_t state = uint32_t(seed) * 2654435761u + 1u;
            for (int i = 0; i < kN; ++i) out[i] = double(int32_t(nextRandom(state) >> 8)) / double(1 << 23) - 1.0;
            break;
        }
        case SynthSource::Drawn:
            for (int i = 0; i < kN; ++i) out[i] = level(drawn.s[size_t(i)]);
            break;
    }
}

/* ----------------------------------------------------------- shapers */

/// The 32-point transform, as amplitude and phase per harmonic. A cycle of
/// 32 points has sixteen of them and a DC term, and that is the whole of the
/// wave -- so a filter here is exact and perfectly cyclic, where a running
/// filter would depend on where its state started.
struct Spectrum {
    double re[kHarmonics + 1] = {};
    double im[kHarmonics + 1] = {};
};

Spectrum transform(const double* x)
{
    Spectrum s;
    for (int h = 0; h <= kHarmonics; ++h) {
        double re = 0.0, im = 0.0;
        for (int i = 0; i < kN; ++i) {
            const double a = 2.0 * kPi * double(h) * double(i) / double(kN);
            re += x[i] * std::cos(a);
            im -= x[i] * std::sin(a);
        }
        s.re[h] = re / double(kN);
        s.im[h] = im / double(kN);
    }
    return s;
}

void inverse(const Spectrum& s, double* x)
{
    for (int i = 0; i < kN; ++i) {
        double v = s.re[0];
        for (int h = 1; h < kHarmonics; ++h) {
            const double a = 2.0 * kPi * double(h) * double(i) / double(kN);
            v += 2.0 * (s.re[h] * std::cos(a) - s.im[h] * std::sin(a));
        }
        // The Nyquist term has no partner to pair with.
        v += s.re[kHarmonics] * std::cos(kPi * double(i)) - s.im[kHarmonics] * std::sin(kPi * double(i));
        x[i] = v;
    }
}

/// The magnitude and phase of a two-pole filter at harmonic `h`, cornered at
/// `fc` with quality `q`: the textbook response, so resonance is a peak at
/// the corner and the all-pass is the same filter's phase alone.
void twoPole(SynthShaper kind, double h, double fc, double q, double& gain, double& phase)
{
    const double w = h / std::max(0.25, fc);
    const double re = 1.0 - w * w;
    const double im = w / std::max(0.25, q);
    const double mag = std::sqrt(re * re + im * im);
    gain = 1.0;
    phase = 0.0;
    switch (kind) {
        case SynthShaper::LowPass:  gain = mag > 1.0e-9 ? 1.0 / mag : 1.0; break;
        case SynthShaper::HighPass: gain = mag > 1.0e-9 ? (w * w) / mag : 1.0; break;
        case SynthShaper::BandPass: gain = mag > 1.0e-9 ? im / mag : 1.0; break;
        case SynthShaper::AllPass:  phase = -2.0 * std::atan2(im, re); break;
        default: break;
    }
}

void filterStage(SynthShaper kind, double amount, double resonance, double* x)
{
    const double a = std::clamp(std::abs(amount), 0.0, 15.0);
    // The corner: a low-pass closes as the amount rises, a high-pass opens,
    // and the band-pass and all-pass put their corner on harmonic `a`.
    const double fc = kind == SynthShaper::LowPass ? 16.0 - a : a;
    const double q = 0.7071 + std::clamp(resonance, 0.0, 15.0) * 0.3;
    Spectrum s = transform(x);
    for (int h = 0; h <= kHarmonics; ++h) {
        double gain = 1.0, phase = 0.0;
        if (h == 0) {
            // DC: a low-pass and an all-pass keep it, the other two do not.
            gain = kind == SynthShaper::LowPass || kind == SynthShaper::AllPass ? 1.0 : 0.0;
        } else {
            twoPole(kind, double(h), fc, q, gain, phase);
        }
        const double re = s.re[h], im = s.im[h];
        const double c = std::cos(phase), sn = std::sin(phase);
        s.re[h] = gain * (re * c - im * sn);
        s.im[h] = gain * (re * sn + im * c);
    }
    inverse(s, x);
}

double foldInto(double v)
{
    // Reflect off +-1 until it is inside; a bounded loop, so a huge drive
    // cannot hang.
    for (int guard = 0; guard < 64 && (v > 1.0 || v < -1.0); ++guard)
        v = v > 1.0 ? 2.0 - v : -2.0 - v;
    return std::clamp(v, -1.0, 1.0);
}

double wrapInto(double v)
{
    const double t = std::fmod(v + 1.0, 2.0);
    return (t < 0.0 ? t + 2.0 : t) - 1.0;
}

void applyShaper(SynthShaper kind, double amount, double resonance, double* x)
{
    const double a = std::abs(amount);
    if (kind == SynthShaper::None || a < 1.0e-9) return;      // 0 is always a no-op
    const double mix = std::clamp(a / 15.0, 0.0, 1.0);
    switch (kind) {
        case SynthShaper::LowPass: case SynthShaper::HighPass:
        case SynthShaper::BandPass: case SynthShaper::AllPass:
            filterStage(kind, amount, resonance, x);
            break;
        case SynthShaper::Clip: {
            const double g = 1.0 + a * 0.5;
            for (int i = 0; i < kN; ++i) x[i] = std::clamp(x[i] * g, -1.0, 1.0);
            break;
        }
        case SynthShaper::Fold: {
            const double g = 1.0 + a * 0.5;
            for (int i = 0; i < kN; ++i) x[i] = foldInto(x[i] * g);
            break;
        }
        case SynthShaper::Wrap: {
            const double g = 1.0 + a * 0.5;
            for (int i = 0; i < kN; ++i) x[i] = wrapInto(x[i] * g);
            break;
        }
        case SynthShaper::Rotate: {
            // Two samples a step, so 15 turns the cycle almost the whole way
            // round; the sign is the direction.
            const int shift = int(std::lround(amount)) * 2;
            double t[kN];
            for (int i = 0; i < kN; ++i) t[i] = x[((i - shift) % kN + kN) % kN];
            for (int i = 0; i < kN; ++i) x[i] = t[i];
            break;
        }
        case SynthShaper::Shift: {
            // The one shaper whose sign is the whole point: up or down.
            const double d = std::clamp(amount, -15.0, 15.0) / 15.0;
            for (int i = 0; i < kN; ++i) x[i] = std::clamp(x[i] + d, -1.0, 1.0);
            break;
        }
        case SynthShaper::Invert:
            for (int i = 0; i < kN; ++i) x[i] = x[i] * (1.0 - 2.0 * mix);
            break;
        case SynthShaper::Reverse: {
            double t[kN];
            for (int i = 0; i < kN; ++i) t[i] = x[(kN - i) % kN];
            for (int i = 0; i < kN; ++i) x[i] = x[i] + mix * (t[i] - x[i]);
            break;
        }
        case SynthShaper::Smooth: {
            // One pass of [1 2 1] / 4 per unit of amount, around the cycle.
            const int passes = std::clamp(int(std::lround(a)), 1, 15);
            for (int n = 0; n < passes; ++n) {
                double t[kN];
                for (int i = 0; i < kN; ++i) t[i] = 0.25 * x[(i + kN - 1) % kN] + 0.5 * x[i] + 0.25 * x[(i + 1) % kN];
                for (int i = 0; i < kN; ++i) x[i] = t[i];
            }
            break;
        }
        case SynthShaper::Crush: {
            // Fewer levels than the sixteen the chip has: 15 down to 2.
            const int levels = std::clamp(16 - int(std::lround(a)), 2, 15);
            const double step = 2.0 / double(levels - 1);
            for (int i = 0; i < kN; ++i) x[i] = std::clamp(std::round((x[i] + 1.0) / step) * step - 1.0, -1.0, 1.0);
            break;
        }
        case SynthShaper::Quantise: {
            // Hold the wave in steps of k samples: the time axis, where
            // Crush is the level axis.
            const int k = std::clamp(1 + int(std::lround(a)), 2, kN);
            for (int i = 0; i < kN; ++i) {
                const int base = (i / k) * k;
                if (i != base) x[i] = x[base];
            }
            break;
        }
        case SynthShaper::Normalise: {
            double peak = 0.0;
            for (int i = 0; i < kN; ++i) peak = std::max(peak, std::abs(x[i]));
            if (peak <= 1.0e-9) break;
            const double g = 1.0 + mix * (1.0 / peak - 1.0);
            for (int i = 0; i < kN; ++i) x[i] = std::clamp(x[i] * g, -1.0, 1.0);
            break;
        }
        case SynthShaper::None: break;
    }
}

} // namespace

int synthFirstFrame(const Synth& s) { return std::clamp<int>(s.first, 0, kMaxFrames - 1); }
int synthFrameCount(const Synth& s) { return std::clamp<int>(s.frames, 1, kMaxFrames - synthFirstFrame(s)); }

Frame synthesizeFrame(const Synth& s, const Frame& drawn, int index)
{
    const int n = synthFrameCount(s);
    const int i = std::clamp(index, 0, n - 1);
    const double t = n <= 1 ? 0.0 : double(i) / double(n - 1);
    const Params p = morph(s.start, s.end, t);

    // Each end has its own shape (section 36): where they differ the two are
    // made and crossfaded by the morph position before the chain sees them,
    // and where they agree the bytes are exactly the one shape's.
    double x[kN] = {};
    makeSource(s.start.source, p, s.seed, drawn, x);
    if (s.end.source != s.start.source && t > 0.0) {
        double y[kN] = {};
        makeSource(s.end.source, p, s.seed, drawn, y);
        for (int k = 0; k < kN; ++k) x[k] = lerp(x[k], y[k], t);
    }
    for (int stage = 0; stage < kSynthStages; ++stage)
        applyShaper(s.chain[size_t(stage)], p.amount[stage], p.resonance[stage], x);

    Frame f;
    for (int k = 0; k < kN; ++k) f.s[size_t(k)] = quantise(x[k]);
    return f;
}

void synthesize(const Synth& s, const Frame& drawn, std::vector<Frame>& out)
{
    const int n = synthFrameCount(s);
    out.clear();
    out.reserve(size_t(n));
    for (int i = 0; i < n; ++i) out.push_back(synthesizeFrame(s, drawn, i));
}

void synthWriteRun(const Synth& s, const std::vector<Frame>& run, Wave& w)
{
    if (run.empty()) return;
    const int first = synthFirstFrame(s);
    const int count = std::min<int>(int(run.size()), kMaxFrames - first);
    // Section 103: a wave is kMaxFrames. One that arrives short -- a bank from
    // before that section, or a test's -- is filled out with the run's last
    // frame first, so nothing blank sits in it and the write lands where From
    // says it does.
    while (int(w.frames.size()) < kMaxFrames) w.frames.push_back(run.back());
    if (w.frames.size() > size_t(kMaxFrames)) w.frames.resize(size_t(kMaxFrames));
    for (int k = 0; k < count; ++k) w.frames[size_t(first + k)] = run[size_t(k)];
}

const char* synthSourceName(SynthSource s)
{
    switch (s) {
        case SynthSource::Sine: return "Sine";
        case SynthSource::Triangle: return "Triangle";
        case SynthSource::Saw: return "Saw";
        case SynthSource::Square: return "Square";
        case SynthSource::Additive: return "Additive";
        case SynthSource::Noise: return "Noise";
        case SynthSource::Drawn: return "Drawn";
    }
    return "Sine";
}

const char* synthSourceHelp(SynthSource s)
{
    switch (s) {
        case SynthSource::Sine: return "One cycle of a sine: the fundamental alone.";
        case SynthSource::Triangle: return "Up over the first half, down over the second: odd harmonics falling fast.";
        case SynthSource::Saw: return "A ramp with one hard edge: every harmonic, falling slowly.";
        case SynthSource::Square: return "High for Width samples of the 32, low for the rest: a pulse.";
        case SynthSource::Additive: return "Eight harmonics at the levels of the Partials bar, scaled to the rails.";
        case SynthSource::Noise: return "Random samples from the Seed, the same every time.";
        case SynthSource::Drawn: return "The frame on show, as drawn -- so the chain shapes what is already there.";
    }
    return "";
}

const char* synthShaperName(SynthShaper s)
{
    switch (s) {
        case SynthShaper::None: return "none";
        case SynthShaper::LowPass: return "Low-pass";
        case SynthShaper::HighPass: return "High-pass";
        case SynthShaper::BandPass: return "Band-pass";
        case SynthShaper::AllPass: return "All-pass";
        case SynthShaper::Clip: return "Clip";
        case SynthShaper::Fold: return "Fold";
        case SynthShaper::Wrap: return "Wrap";
        case SynthShaper::Rotate: return "Rotate";
        case SynthShaper::Shift: return "Shift";
        case SynthShaper::Invert: return "Invert";
        case SynthShaper::Reverse: return "Reverse";
        case SynthShaper::Smooth: return "Smooth";
        case SynthShaper::Crush: return "Bit-crush";
        case SynthShaper::Quantise: return "Quantise";
        case SynthShaper::Normalise: return "Normalise";
    }
    return "none";
}

const char* synthShaperHelp(SynthShaper s)
{
    switch (s) {
        case SynthShaper::None: return "Nothing. An amount of 0 is the same on any shaper.";
        case SynthShaper::LowPass: return "Two poles; the corner closes as the amount rises. Resonance is the peak at the corner.";
        case SynthShaper::HighPass: return "Two poles; the corner opens as the amount rises. Resonance is the peak at the corner.";
        case SynthShaper::BandPass: return "Keeps the harmonics around the amount. Resonance narrows the band.";
        case SynthShaper::AllPass: return "The same filter's phase without its gain: the shape changes, the spectrum does not.";
        case SynthShaper::Clip: return "Drives the wave and cuts what leaves the rails.";
        case SynthShaper::Fold: return "Drives the wave and reflects what leaves the rails back inside.";
        case SynthShaper::Wrap: return "Drives the wave and wraps what leaves the rails round to the other rail.";
        case SynthShaper::Rotate: return "Turns the cycle two samples a step. The sign is the direction.";
        case SynthShaper::Shift: return "Moves the whole wave up or down. The sign is the direction.";
        case SynthShaper::Invert: return "Turns the wave upside down, all the way at 15.";
        case SynthShaper::Reverse: return "Plays the cycle backwards, all the way at 15.";
        case SynthShaper::Smooth: return "One [1 2 1] pass around the cycle per unit of amount.";
        case SynthShaper::Crush: return "Fewer levels than the sixteen the chip has: 15 at an amount of 1, 2 at 15.";
        case SynthShaper::Quantise: return "Holds the wave in steps of amount + 1 samples.";
        case SynthShaper::Normalise: return "Scales the peak up to full, all the way at 15.";
    }
    return "";
}

bool synthShaperHasResonance(SynthShaper s)
{
    return s == SynthShaper::LowPass || s == SynthShaper::HighPass || s == SynthShaper::BandPass || s == SynthShaper::AllPass;
}

bool synthSourceHasWidth(SynthSource s) { return s == SynthSource::Square; }
bool synthSourceHasPartials(SynthSource s) { return s == SynthSource::Additive; }

} // namespace chipboy::bank
