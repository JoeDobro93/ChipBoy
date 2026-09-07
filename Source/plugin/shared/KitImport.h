// ChipBoy -- kit sample import (spec section 9.8): any audio file the host
// can read, to mono, to the kit's playback rate, to 4 bits.
#pragma once

#include "core/Bank/Bank.h"

#include <juce_core/juce_core.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdint>
#include <functional>

namespace chipboy::plugin {

struct KitImportResult {
    bool ok = false;
    juce::String error;
    bank::KitSample sample;
    double sourceRate = 0;
    int sourceChannels = 0;
};

/// Decodes one file (WAV, AIFF, FLAC, Ogg Vorbis, and MP3 where the platform
/// decodes it), mixes to mono, keeps the first `maxSeconds`, resamples to
/// bank::sampleRateForPeriod(kitPeriod) behind an anti-alias filter,
/// normalises to peak and quantises to 4 bits with TPDF dither. The name is
/// the file name without its extension (15 characters), the note is 60 and
/// the loop point 0. Message thread.
KitImportResult importKitSample(const juce::File& file, uint16_t kitPeriod, double maxSeconds = 4.0);

/// An asynchronous file chooser; `onEach` runs on the message thread once
/// per chosen file, in order.
void chooseAndImportKitSamples(juce::Component* parent, uint16_t kitPeriod, std::function<void(KitImportResult)> onEach);

} // namespace chipboy::plugin
