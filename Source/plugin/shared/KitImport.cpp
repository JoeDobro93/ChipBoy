#include "plugin/shared/KitImport.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace chipboy::plugin {

using namespace juce;

namespace {

constexpr int kNameChars = 15;         ///< what a link name field carries (kInstNameChars - 1)
constexpr int64 kMaxFrames = 1 << 26;  ///< a sanity cap on decoded frames, whatever maxSeconds says
constexpr double kCutoffRatio = 0.45;  ///< anti-alias corner as a fraction of the target rate

/// A 4th-order Butterworth low-pass as two biquads: the anti-alias filter in
/// front of the decimation. Lagrange interpolation on its own does not
/// band-limit, and 44.1 kHz material folded down to 11 kHz would hiss.
void antiAlias(std::vector<float>& x, double sampleRate, double cutoffHz)
{
    IIRFilter first, second;
    first.setCoefficients(IIRCoefficients::makeLowPass(sampleRate, cutoffHz, 0.5412));
    second.setCoefficients(IIRCoefficients::makeLowPass(sampleRate, cutoffHz, 1.3066));
    first.processSamples(x.data(), int(x.size()));
    second.processSamples(x.data(), int(x.size()));
}

/// The one chooser at a time; it lives until its callback has run. Opening
/// another while one is up dismisses the first.
std::unique_ptr<FileChooser>& chooser()
{
    static std::unique_ptr<FileChooser> c;
    return c;
}

File& lastDirectory()
{
    static File dir;
    return dir;
}

} // namespace

KitImportResult importKitSample(const File& file, uint16_t kitPeriod, double maxSeconds)
{
    KitImportResult r;
    r.sample.name = file.getFileNameWithoutExtension().substring(0, kNameChars).trim().toStdString();
    if (r.sample.name.empty()) r.sample.name = "sample";
    r.sample.note = 60;
    r.sample.loopPoint = 0;

    if (!file.existsAsFile()) { r.error = "file not found"; return r; }

    AudioFormatManager formats;
    formats.registerBasicFormats();
    std::unique_ptr<AudioFormatReader> reader(formats.createReaderFor(file));
    if (!reader) { r.error = "not an audio file this build can read"; return r; }

    r.sourceRate = reader->sampleRate;
    r.sourceChannels = int(reader->numChannels);
    if (r.sourceRate <= 0.0 || r.sourceChannels <= 0 || reader->lengthInSamples <= 0) { r.error = "the file has no audio"; return r; }

    // decode, truncated to maxSeconds at the source rate
    const int64 limit = std::max<int64>(1, int64(std::max(0.0, maxSeconds) * r.sourceRate));
    const int frames = int(std::min<int64>(std::min<int64>(reader->lengthInSamples, limit), kMaxFrames));
    AudioBuffer<float> decoded(r.sourceChannels, frames);
    if (!reader->read(decoded.getArrayOfWritePointers(), r.sourceChannels, 0, frames)) { r.error = "could not read the audio data"; return r; }

    // mono: the plain average of the channels
    std::vector<float> mono(size_t(frames), 0.0f);
    const float gain = 1.0f / float(r.sourceChannels);
    for (int c = 0; c < r.sourceChannels; ++c) {
        const float* src = decoded.getReadPointer(c);
        for (int i = 0; i < frames; ++i) mono[size_t(i)] += src[i] * gain;
    }

    // to the kit's rate: `ratio` input samples per output sample
    const double targetRate = bank::sampleRateForPeriod(kitPeriod);
    const double ratio = r.sourceRate / targetRate;
    if (ratio > 1.0) antiAlias(mono, r.sourceRate, kCutoffRatio * targetRate);
    const int outFrames = std::max(1, int(std::ceil(double(frames) / ratio)));
    std::vector<float> resampled(size_t(outFrames), 0.0f);
    LagrangeInterpolator interpolator;
    interpolator.reset();
    interpolator.process(ratio, mono.data(), resampled.data(), outFrames, frames, 0);

    // peak normalise
    float peak = 0.0f;
    for (const float v : resampled) peak = std::max(peak, std::abs(v));
    const float norm = peak > 0.0f ? 1.0f / peak : 0.0f;

    // 4 bits on the bank's own scale (7.5 + 7.5 x, as Bank.cpp's q4), plus
    // TPDF dither of one LSB: two uniform halves summed, so the rounding
    // error is decorrelated from the signal and quiet tails become a soft
    // hiss rather than a buzz. Seeded from the path, so an import repeats.
    Random rng(file.getFullPathName().hashCode64());
    r.sample.data.resize(size_t(outFrames));
    for (int i = 0; i < outFrames; ++i) {
        const double x = double(resampled[size_t(i)] * norm);
        const double dither = rng.nextDouble() - rng.nextDouble();
        const long q = std::lround(7.5 + 7.5 * x + dither);
        r.sample.data[size_t(i)] = uint8_t(std::clamp<long>(q, 0, 15));
    }
    r.ok = true;
    return r;
}

void chooseAndImportKitSamples(Component* parent, uint16_t kitPeriod, std::function<void(KitImportResult)> onEach)
{
    AudioFormatManager formats;
    formats.registerBasicFormats();
    const File start = lastDirectory().isDirectory() ? lastDirectory() : File::getSpecialLocation(File::userMusicDirectory);
    chooser() = std::make_unique<FileChooser>("Import samples", start, formats.getWildcardForAllFormats(), true, false, parent);
    const int flags = FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::canSelectMultipleItems;
    chooser()->launchAsync(flags, [kitPeriod, onEach = std::move(onEach)](const FileChooser& fc) {
        const Array<File> files = fc.getResults();   // copied first: onEach may open another chooser
        if (!files.isEmpty()) lastDirectory() = files[0].getParentDirectory();
        for (const auto& f : files)
            if (onEach) onEach(importKitSample(f, kitPeriod));
    });
}

} // namespace chipboy::plugin
