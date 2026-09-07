// ChipBoy -- the main plugin: the machine (spec section 11.1).
//
// Owns the APU, the bank, the song, the driver, the analog stage and the
// stereo output. Everything on the audio thread is allocation-free; the
// bank and song are published from the message thread by pointer swap
// (section 3.5). A timer on the message thread keeps the link region alive,
// answers Voice requests and applies recorded cells.
#pragma once

#include "core/Apu/Apu.h"
#include "core/Bank/Bank.h"
#include "core/Driver/Driver.h"
#include "core/Link/LinkLayout.h"
#include "core/Render/Renderer.h"
#include "core/Tracker/Player.h"
#include "core/Tracker/Song.h"
#include "plugin/shared/LinkTransport.h"
#include "plugin/shared/Parameters.h"
#include "plugin/shared/ScopeBuffers.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <array>
#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <vector>

namespace chipboy::plugin {

class ChipBoyProcessor : public juce::AudioProcessor, private juce::Timer
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
    std::array<ChannelParamCache, 4> channelParams;
    static ChannelKind kindOf(int ch) { return ch == 0 ? ChannelKind::Pulse1 : ch == 1 ? ChannelKind::Pulse2 : ch == 2 ? ChannelKind::Wave : ChannelKind::Noise; }

    std::shared_ptr<const bank::Bank> bank() const { return bankShared_; }
    std::shared_ptr<const tracker::Song> song() const { return songShared_; }
    void publishBank(std::shared_ptr<const bank::Bank> b);
    void publishSong(std::shared_ptr<const tracker::Song> s);
    /// Copy-on-write edits: the editor mutates a copy, the audio thread swaps to it.
    void mutateBank(const std::function<void(bank::Bank&)>& fn);
    void mutateSong(const std::function<void(tracker::Song&)>& fn);
    /// Replace the bank wholesale (file import, factory reset).
    void loadBank(const bank::Bank& b) { publishBank(std::make_shared<const bank::Bank>(b)); }
    juce::String bankName() const { return bankName_; }
    void setBankName(const juce::String& n) { bankName_ = n; }

    const driver::Driver& driverView() const { return driver_; }   ///< read-only, may be a block stale
    ScopeBuffers& scopes() { return scopes_; }
    juce::String instanceName() const { return instanceName_; }
    void setInstanceName(const juce::String& n) { instanceName_ = n; }
    juce::String instanceUuid() const { return uuid_; }
    double currentSampleRate() const { return sampleRate_; }
    int latencyFrames() const { return renderer_.latencyFrames(); }
    bool linkActive() const { return linkActive_; }
    /// Which channels a Voice plugin owns right now, and its name.
    uint32_t voiceOwnedMask() const { return linkHost_.ownedMask(); }
    juce::String voiceName(int ch) const { return linkHost_.claimName(ch); }
    /// A Voice asked the window to show this channel (-1 none); reading clears it.
    int takeFocusRequest() { return focusRequest_.exchange(-1); }

    // tracker
    void setRecordArm(bool on) { recordArm_.store(on); }
    bool recordArm() const { return recordArm_.load(); }
    const tracker::Player& player() const { return player_; }
    bool transportPlaying() const { return playing_.load(); }
    double transportPpq() const { return ppq_.load(); }
    double transportBpm() const { return bpm_.load(); }
    double beatsPerBar() const { return beatsPerBar_.load(); }

    /// 4-bit levels per channel as last rendered (-1 = DAC off), for meters.
    std::array<std::atomic<int>, 4> channelLevels;
    /// The last note played per channel from any source, for the tracker's piano-roll view.
    std::array<std::atomic<int>, 4> lastNotes;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void timerCallback() override;
    void routeMidi(const juce::MidiMessage& m, int offset, std::vector<driver::NoteEvent>& dst);
    void applyModel();
    void applyHardwareOptions();
    void consumeLink(int n, uint64_t hostFrame, bool hostTimeKnown);
    void placeLinkEvent(const link::LinkEvent& e, int n, uint64_t hostFrame, bool hostTimeKnown);
    void recordNote(const driver::NoteEvent& e, const driver::Transport& t, int n);
    void applyRecordMessages();
    void publishInstrumentNames();
    void handleVoiceRequests();
    void tapScopes(int n, const float* L, const float* R);

    // engine, audio thread
    Apu apu_;
    render::Renderer renderer_;
    driver::Driver driver_;
    tracker::Player player_;
    std::vector<driver::NoteEvent> events_, delayed_;
    std::vector<driver::RegWrite> writes_;
    std::function<uint64_t(uint64_t)> cycleAt_;
    uint64_t frames_ = 0;
    double sampleRate_ = 48000.0;
    int blockSize_ = 512;
    int prevBlock_ = 0;
    int modelIndex_ = -1;
    bool linkActive_ = false;

    // link consumption
    LinkHost linkHost_;
    std::vector<link::LinkEvent> pendingLink_, pendingScratch_;
    uint64_t lastHostFrame_ = link::kNoHostFrame;
    std::array<driver::ChannelParams, 4> linkParams_{};
    std::array<bool, 4> haveLinkParams_{};
    std::array<bank::Instrument, 4> localInst_{};
    std::array<uint32_t, 4> localSeq_{};
    std::array<bool, 4> localOn_{};
    std::atomic<int> focusRequest_{ -1 };

    // tracker / record
    link::Spsc<tracker::RecordMessage, 1024> recordFifo_;
    std::atomic<bool> recordArm_{ false };
    std::atomic<bool> playing_{ false };
    std::atomic<double> ppq_{ 0.0 }, bpm_{ 120.0 }, beatsPerBar_{ 4.0 };

    // scopes
    ScopeBuffers scopes_;

    // bank & song
    std::shared_ptr<const bank::Bank> bankShared_;
    std::shared_ptr<const tracker::Song> songShared_;
    std::atomic<const bank::Bank*> bankPtr_{ nullptr };
    std::atomic<const tracker::Song*> songPtr_{ nullptr };
    std::deque<std::shared_ptr<const void>> retired_;   ///< kept alive a while after a swap
    const bank::Bank* namesPublishedFor_ = nullptr;
    juce::String bankName_ { "Factory" };

    // parameters
    std::atomic<float>* pModel_ = nullptr; std::atomic<float>* pMasterL_ = nullptr; std::atomic<float>* pMasterR_ = nullptr;
    std::atomic<float>* pTrim_ = nullptr; std::atomic<float>* pNoise_ = nullptr; std::atomic<float>* pLcd_ = nullptr;
    std::atomic<float>* pBassMod_ = nullptr; std::atomic<float>* pEdges_ = nullptr; std::atomic<float>* pDeclick_ = nullptr;
    std::atomic<float>* pDeclickMs_ = nullptr; std::atomic<float>* pSoften_ = nullptr; std::atomic<float>* pTickSource_ = nullptr;
    std::atomic<float>* pTicksPerBeat_ = nullptr; std::atomic<float>* pTickHz_ = nullptr; std::atomic<float>* pLink_ = nullptr;

    juce::String instanceName_ { "ChipBoy 1" };
    juce::String uuid_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChipBoyProcessor)
};

} // namespace chipboy::plugin
