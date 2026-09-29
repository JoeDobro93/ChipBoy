// ChipBoy -- what an editor panel needs of the plugin it lives in
// (docs/plan-solo.md section 7). The Instrument, Tables, Waves and Kits
// tabs edit a bank through this and nothing else, so the main plugin and
// ChipBoy Solo share them file for file. Message thread.
#pragma once

#include "core/Bank/Bank.h"
#include "core/Tracker/Song.h"
#include "plugin/shared/ScopeBuffers.h"
#include "plugin/ui/EditHistory.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>
#include <memory>

namespace chipboy::plugin {

class EditorHost {
public:
    virtual ~EditorHost() = default;

    /// The host parameters.
    virtual juce::AudioProcessorValueTreeState& state() = 0;
    virtual const juce::AudioProcessorValueTreeState& state() const = 0;
    /// The bank that plays and that the window edits, and the song, which a
    /// plugin without one leaves null (the Instrument tab's uses count).
    virtual std::shared_ptr<const bank::Bank> bank() const = 0;
    virtual std::shared_ptr<const tracker::Song> song() const { return {}; }
    /// A hand edit of the bank: copy-on-write, on the undo history under `name`.
    virtual void editBank(const juce::String& name, const std::function<void(bank::Bank&)>& fn) = 0;
    virtual ui::UndoHistory& history() = 0;
    /// What the audio thread leaves for the scopes and the running-state lights.
    virtual ScopeBuffers& scopes() = 0;
    /// The tempo the ticks run at, for the Tables tab's step-rate line.
    virtual double effectiveTempo() const = 0;
    /// Audition one kit sample (D-UI-29); slot 0 stops it.
    virtual void previewKitSample(int slot, int sample) = 0;
    /// The lanes the window has -- four strips, or Solo's one -- and the
    /// hardware channel each plays, for the "used on" line.
    virtual int channelCount() const = 0;
    virtual int hardwareChannel(int lane) const = 0;
    /// The parameter id of a lane's field: "ch2_instrument", or Solo's one set.
    virtual juce::String channelParamId(int lane, const char* id) const = 0;
};

} // namespace chipboy::plugin
