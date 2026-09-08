// ChipBoy Voice -- a remote control for one channel of one ChipBoy instance
// (spec sections 11.1, 11.6, 12.4, 12.5). An instrument that outputs
// silence: its notes and parameters travel through the link region to the
// main instance, which is where the audio comes out.
#pragma once

#include "core/Bank/Bank.h"
#include "plugin/shared/LinkTransport.h"
#include "plugin/shared/Parameters.h"
#include "plugin/ui/EditHistory.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <atomic>
#include <mutex>

namespace chipboy::plugin {

class VoiceProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    VoiceProcessor();
    ~VoiceProcessor() override;

    // --- AudioProcessor ------------------------------------------------
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    using juce::AudioProcessor::processBlock;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "ChipBoy Voice"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;
    void updateTrackProperties(const TrackProperties& p) override;

    // --- for the editor (message thread) --------------------------------
    juce::AudioProcessorValueTreeState apvts;
    LinkClient& link() { return link_; }
    juce::String instanceUuid() const { return uuid_; }
    juce::String displayName() const { return trackName_.isNotEmpty() ? trackName_ : "Voice"; }
    void setDisplayName(const juce::String& n) { trackName_ = n; }

    /// Link target: an instance UUID and a hardware channel 0-3.
    void setTarget(const juce::String& uuid, int channel);
    /// The channel this Voice drives (0-3), used for parameter display.
    int channel() const { return link_.targetChannel(); }

    /// The local instrument (spec 12.5), edited in place then `localChanged()`.
    bank::Instrument& localInstrument() { return local_; }
    void localChanged();

    // --- undo (message thread; UI_DESIGN section 2.1) -------------------
    //
    // The Voice keeps its own history for its own parameters and its local
    // instrument. What the linked ChipBoy does with them is that instance's
    // edit and lives on its history, not this one.
    ui::UndoHistory& history() { return history_; }
    /// Puts a local instrument back: the history's own action calls this.
    void restoreLocalInstrument(const bank::Instrument& i);
    bool usingLocal() const { return paramInt(pSource_) == 1; }

    /// Explicit bank exchange with the main instance. Results arrive on the
    /// timer; `lastRequestMessage()` says what happened.
    void pushToSlot(int slot1);
    void pullFromSlot(int slot1);
    juce::String lastRequestMessage() const { return requestMessage_; }
    /// Ask the main window to show this channel's instrument.
    void requestFocus();

    /// What the main instance reports for this channel (display only).
    driver::VoiceView view() const;
    /// Newest scope samples for this channel; returns how many were copied.
    uint32_t scopeSnapshot(link::ScopeSample* out, uint32_t count) const;
    double linkedSampleRate() const;

    ChannelParamCache params;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void timerCallback() override;
    void publishLocal();

    LinkClient link_;
    juce::String uuid_, trackName_;
    bank::Instrument local_;
    std::atomic<bool> localDirty_{ true };
    std::mutex localMutex_;
    ui::UndoHistory history_;

    std::atomic<float>* pSource_ = nullptr;
    driver::ChannelParams lastParams_;
    bool haveLastParams_ = false;
    int blocksSinceParams_ = 0;
    int ticks_ = 0;
    uint32_t pendingSerial_ = 0;
    link::Request pendingRequest_ = link::Request::None;
    int pendingSlot_ = 0;
    juce::String requestMessage_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VoiceProcessor)
};

} // namespace chipboy::plugin
