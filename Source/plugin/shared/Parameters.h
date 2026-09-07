// ChipBoy -- the host-visible parameter layout (spec section 12).
//
// Every parameter is discrete with the hardware's range, except the output
// trim (C3). Per-channel parameters carry an extra "inst" position meaning
// "use the instrument's value", so automation overrides only when drawn.
#pragma once

#include "core/Driver/Driver.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <string>

namespace chipboy::plugin {

namespace ids {
constexpr const char* model = "model";
constexpr const char* masterL = "master_l";
constexpr const char* masterR = "master_r";
constexpr const char* trim = "trim";
constexpr const char* noise = "noise";
constexpr const char* lcd = "lcd";
constexpr const char* bassMod = "bass_mod";
constexpr const char* volEdges = "vol_edges";
constexpr const char* declick = "declick";
constexpr const char* declickMs = "declick_ms";
constexpr const char* soften = "soften";
constexpr const char* tickSource = "tick_source";
constexpr const char* ticksPerBeat = "ticks_per_beat";
constexpr const char* tickHz = "tick_hz";
constexpr const char* linkMode = "link";
constexpr const char* hexDisplay = "hex";
// per channel, prefixed "chN_" (N = 1..4) in the main plugin, "v_" in the Voice plugin
constexpr const char* source = "source";
constexpr const char* instrument = "instrument";
constexpr const char* table = "table";
constexpr const char* level = "level";
constexpr const char* pan = "pan";
constexpr const char* wave = "wave";
constexpr const char* frame = "frame";
constexpr const char* transpose = "transpose";
constexpr const char* detune = "detune";
constexpr const char* vibSpeed = "vib_speed";
constexpr const char* vibDepth = "vib_depth";
constexpr const char* arp = "arp";
constexpr const char* envVol = "env_vol";
constexpr const char* envDir = "env_dir";
constexpr const char* envRate = "env_rate";
constexpr const char* duty = "duty";
constexpr const char* sweepRate = "sweep_rate";
constexpr const char* sweepDir = "sweep_dir";
constexpr const char* sweepShift = "sweep_shift";
constexpr const char* lfsr = "lfsr";
constexpr const char* liveFollow = "live_follow";
constexpr const char* velocityMode = "velocity";
constexpr const char* keyswitch = "keyswitch";
} // namespace ids

enum class ChannelKind { Pulse1, Pulse2, Wave, Noise, Any };

juce::String channelPrefix(int channel);   ///< "ch1_" .. "ch4_"
juce::String channelParamId(int channel, const char* id);

/// Adds one channel's parameter set to a layout. `kind` decides the ranges;
/// Any (the Voice plugin) includes everything.
void addChannelParameters(juce::AudioProcessorValueTreeState::ParameterLayout& layout,
                          const juce::String& prefix, ChannelKind kind, bool withSource);
/// Adds the main plugin's global parameters.
void addGlobalParameters(juce::AudioProcessorValueTreeState::ParameterLayout& layout);

/// Raw-value pointers for one channel, read on the audio thread.
struct ChannelParamCache {
    std::atomic<float>* source = nullptr;
    std::atomic<float>* instrument = nullptr;
    std::atomic<float>* table = nullptr;
    std::atomic<float>* level = nullptr;
    std::atomic<float>* pan = nullptr;
    std::atomic<float>* wave = nullptr;
    std::atomic<float>* frame = nullptr;
    std::atomic<float>* transpose = nullptr;
    std::atomic<float>* detune = nullptr;
    std::atomic<float>* vibSpeed = nullptr;
    std::atomic<float>* vibDepth = nullptr;
    std::atomic<float>* arp = nullptr;
    std::atomic<float>* envVol = nullptr;
    std::atomic<float>* envDir = nullptr;
    std::atomic<float>* envRate = nullptr;
    std::atomic<float>* duty = nullptr;
    std::atomic<float>* sweepRate = nullptr;
    std::atomic<float>* sweepDir = nullptr;
    std::atomic<float>* sweepShift = nullptr;
    std::atomic<float>* lfsr = nullptr;
    std::atomic<float>* liveFollow = nullptr;
    std::atomic<float>* velocityMode = nullptr;
    std::atomic<float>* keyswitch = nullptr;

    void bind(juce::AudioProcessorValueTreeState& s, const juce::String& prefix);
    /// Translate to the driver's view. `kind` says which channel this is.
    driver::ChannelParams read(ChannelKind kind) const;
};

/// Value of a raw parameter as an int (choice index or int value).
inline int paramInt(const std::atomic<float>* p, int fallback = 0) { return p ? int(std::lround(p->load())) : fallback; }

/// Source choice index -> (omni, midi channel 1-16, off).
struct SourceChoice { bool omni = false; bool off = false; int midiChannel = 0; };
SourceChoice decodeSource(int choiceIndex);

} // namespace chipboy::plugin
