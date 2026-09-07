// ChipBoy -- the main plugin: the machine (spec section 11.1).
//
// Owns the APU, the bank, the driver, the analog stage and the stereo output.
// Everything on the audio thread is allocation-free; the bank and song are
// published from the message thread by pointer swap (section 3.5).
#pragma once

#include "core/Apu/Apu.h"
#include "core/Bank/Bank.h"
#include "core/Driver/Driver.h"
#include "core/Render/Renderer.h"
#include "core/Tracker/Song.h"
#include "plugin/shared/Parameters.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <vector>

namespace chipboy::plugin {

class ChipBoyProcessor : public juce::AudioProcessor
{
public:
    ChipBoyProcessor();
    ~ChipBoyProcessor() override;

    // --- AudioProcessor ------------------------------------------------
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "ChipBoy"; }
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

    // --- for the editor (message thread) --------------------------------
    juce::AudioProcessorValueTreeState apvts;
    std::shared_ptr<const bank::Bank> bank() const { return bankShared_; }
    std::shared_ptr<const tracker::Song> song() const { return songShared_; }
    void publishBank(std::shared_ptr<const bank::Bank> b);
    void publishSong(std::shared_ptr<const tracker::Song> s);
    const driver::Driver& driverView() const { return driver_; }
    juce::String instanceName() const { return instanceName_; }
    void setInstanceName(const juce::String& n) { instanceName_ = n; }
    juce::Uuid instanceId() const { return uuid_; }
    double currentSampleRate() const { return sampleRate_; }
    int latencyFrames() const { return renderer_.latencyFrames(); }
    /// 4-bit levels per channel as last rendered, for meters (audio thread writes).
    std::array<std::atomic<int>, 4> channelLevels;

private:
    static ChannelKind kindOf(int ch) { return ch == 0 ? ChannelKind::Pulse1 : ch == 1 ? ChannelKind::Pulse2 : ch == 2 ? ChannelKind::Wave : ChannelKind::Noise; }
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void routeMidi(const juce::MidiMessage& m, int offset);
    void applyModel();
    void applyHardwareOptions();

    // engine, audio thread
    Apu apu_;
    render::Renderer renderer_;
    driver::Driver driver_;
    std::vector<driver::NoteEvent> events_, delayed_;
    std::vector<driver::RegWrite> writes_;
    std::function<uint64_t(uint64_t)> cycleAt_;
    uint64_t frames_ = 0;
    double sampleRate_ = 48000.0;
    int blockSize_ = 512;
    int modelIndex_ = -1;
    bool linkActive_ = false;

    // bank & song
    std::shared_ptr<const bank::Bank> bankShared_;
    std::shared_ptr<const tracker::Song> songShared_;
    std::atomic<const bank::Bank*> bankPtr_{ nullptr };
    std::atomic<const tracker::Song*> songPtr_{ nullptr };
    std::deque<std::shared_ptr<const void>> retired_;   ///< kept alive a while after a swap

    // parameters
    std::array<ChannelParamCache, 4> chParams_;
    std::atomic<float>* pModel_ = nullptr; std::atomic<float>* pMasterL_ = nullptr; std::atomic<float>* pMasterR_ = nullptr;
    std::atomic<float>* pTrim_ = nullptr; std::atomic<float>* pNoise_ = nullptr; std::atomic<float>* pLcd_ = nullptr;
    std::atomic<float>* pBassMod_ = nullptr; std::atomic<float>* pEdges_ = nullptr; std::atomic<float>* pDeclick_ = nullptr;
    std::atomic<float>* pDeclickMs_ = nullptr; std::atomic<float>* pSoften_ = nullptr; std::atomic<float>* pTickSource_ = nullptr;
    std::atomic<float>* pTicksPerBeat_ = nullptr; std::atomic<float>* pTickHz_ = nullptr; std::atomic<float>* pLink_ = nullptr;

    juce::String instanceName_ { "ChipBoy 1" };
    juce::Uuid uuid_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChipBoyProcessor)
};

} // namespace chipboy::plugin
