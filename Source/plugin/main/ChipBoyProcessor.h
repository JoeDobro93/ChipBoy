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
#include "core/Driver/Clock.h"
#include "core/Driver/Driver.h"
#include "core/Link/LinkLayout.h"
#include "core/Render/Renderer.h"
#include "core/Tracker/Player.h"
#include "core/Tracker/Song.h"
#include "plugin/shared/LinkTransport.h"
#include "plugin/shared/Parameters.h"
#include "plugin/shared/ScopeBuffers.h"
#include "plugin/shared/SongFiles.h"
#include "plugin/ui/EditHistory.h"

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
    using juce::AudioProcessor::processBlock;
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
    /// Publishes a song after building its tempo map (section 4), so the
    /// audio thread never scans the chains. The Song tempo parameter is the
    /// base that map is built on and is stamped into the song, so a song
    /// saved from here carries it; `fromFile` reverses that for a song that
    /// arrives with its own tempo, which becomes the parameter's value.
    void publishSong(std::shared_ptr<tracker::Song> s, bool fromFile = false);
    /// Copy-on-write edits: the editor mutates a copy, the audio thread swaps
    /// to it. These are the plain path -- nothing about them is undoable, so
    /// the timer and the link can use them.
    void mutateBank(const std::function<void(bank::Bank&)>& fn);
    void mutateSong(const std::function<void(tracker::Song&)>& fn);
    /// Replace the bank wholesale (file import, factory reset).
    void loadBank(const bank::Bank& b) { publishBank(std::make_shared<const bank::Bank>(b)); }
    juce::String bankName() const { return bankName_; }

    // --- undo (message thread only; UI_DESIGN section 2.1) --------------
    //
    // Everything a hand does here goes on this history. Host automation, a
    // state restore, the Voice's own edits and anything the audio thread
    // does never open a transaction, so a lane the host is drawing cannot
    // bury what the musician typed.

    /// The history the window's controls, its buttons and its keys use.
    ui::UndoHistory& history() { return history_; }

    /// A hand edit of the bank or the song: the copy-on-write path above,
    /// with the snapshots it already makes put on the history under `name`.
    void editBank(const juce::String& name, const std::function<void(bank::Bank&)>& fn);
    void editSong(const juce::String& name, const std::function<void(tracker::Song&)>& fn);
    /// A whole bank arriving (a file, the factory reset): the bank and the
    /// name it goes under are one step.
    void loadBankEdit(const juce::String& name, const bank::Bank& b, const juce::String& bankName);
    /// The bank's name, typed in the header.
    void setBankNameEdit(const juce::String& n);
    /// Puts a snapshot back. The history's own actions call these; nothing
    /// else should.
    void restoreBank(std::shared_ptr<const bank::Bank> b, const juce::String& bankName);
    void restoreSong(std::shared_ptr<const tracker::Song> s);

    const driver::Driver& driverView() const { return driver_; }   ///< read-only, may be a block stale
    /// Tap every register write the driver emits, for the record test
    /// (docs/COMMANDS_AND_TEMPO.md section 9.5). Null to stop.
    void setWriteLog(std::vector<driver::RegWrite>* log) { driver_.setWriteLog(log); }
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

    /// Mute and solo are NR51 gates (UI_DESIGN section 2) and pop like the hardware.
    void setChannelMute(int ch, bool on) { uint32_t m = muteMask_.load(); m = on ? (m | (1u << (ch & 3))) : (m & ~(1u << (ch & 3))); muteMask_.store(m); }
    bool channelMute(int ch) const { return (muteMask_.load() >> (ch & 3)) & 1; }
    void setChannelSolo(int ch, bool on) { uint32_t m = soloMask_.load(); m = on ? (m | (1u << (ch & 3))) : (m & ~(1u << (ch & 3))); soloMask_.store(m); }
    bool channelSolo(int ch) const { return (soloMask_.load() >> (ch & 3)) & 1; }

    // tracker
    void setRecordArm(bool on) { recordArm_.store(on); }
    bool recordArm() const { return recordArm_.load(); }
    /// The per-channel arm lives in the song (section 14), so it travels with
    /// the song file and the plugin state -- and is an edit like any other.
    void setChannelArm(int ch, bool on);
    bool channelArm(int ch) const { const auto s = songShared_; return s ? s->recordArm[size_t(ch & 3)] : true; }
    const tracker::Player& player() const { return player_; }

    // --- the tracker's own transport (section 16) -----------------------
    //
    // With no host play head -- the Standalone, or a host that offers no
    // position -- the plugin runs the song itself, at the Song tempo, from
    // the song start. In a host the host's transport rules and these do
    // nothing but report it.
    void transportPlay() { transportRequest_.store(1); }
    void transportStop() { transportRequest_.store(2); }
    void setLoop(bool on) { loopOn_.store(on); }
    bool loopEnabled() const { return loopOn_.load(); }
    /// The loop, in bars: from `first` up to but not including `last`. A
    /// negative `last` loops to the end of the song.
    void setLoopBars(int first, int last) { loopFrom_.store(std::max(0, first)); loopTo_.store(last); }
    int  loopFirstBar() const { return loopFrom_.load(); }
    int  loopLastBar() const { return loopTo_.load(); }
    /// Whether the plugin is running the transport rather than a host.
    bool ownsTransport() const { return ownsTransport_.load(); }
    bool transportPlaying() const { return playing_.load(); }

    /// Load a song file and publish it, reporting where this bank differs
    /// (section 15). Message thread.
    bool loadSongFile(const juce::File& file, SongReport& report);
    /// Write the song playing now, with the bank it plays through.
    bool saveSongFile(const juce::File& file) const;
    double transportPpq() const { return ppq_.load(); }
    double transportBpm() const { return bpm_.load(); }
    double beatsPerBar() const { return beatsPerBar_.load(); }
    /// Which tempo the ticks come from, and the tempo in force (section 4).
    bool songTempoSource() const { return songTempo_.load(); }
    double tempoInForce() const { return tempo_.load(); }
    /// The tracker's position, in ticks, and how many ticks a bar holds.
    int64_t trackerTick() const { return trackerTick_.load(); }
    int barTicks() const { return barTicks_.load(); }

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
    void recordNote(const driver::NoteEvent& e, double tickAtEvent, const bank::Bank* bank);
    void recordSlots(int ch, double tick);
    void applyRecordMessages();
    /// Notes in a channel's keyswitch octave select an instrument and never
    /// sound, so the recorder never writes them as cells (section 9.4).
    bool keyswitchNote(int ch, const bank::Bank* bank, uint8_t note) const;
    /// The Song tempo parameter: the song's base tempo (section 4).
    double songTempoParam() const { return double(std::clamp(paramInt(pSongTempo_, 120), 40, 255)); }
    /// All notes off on a channel: unconditional, never filtered by the
    /// Trk/MIDI gate, so a lane changing hands leaves nothing ringing (9.1).
    void flushChannel(int ch, std::vector<driver::NoteEvent>& dst, uint32_t offset = 0);
    void publishInstrumentNames();
    void handleVoiceRequests();
    void tapScopes(int n, const float* L, const float* R);

    // engine, audio thread
    Apu apu_;
    render::Renderer renderer_;
    driver::Driver driver_;
    driver::Clock clock_;
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

    std::atomic<uint32_t> muteMask_{ 0 }, soloMask_{ 0 };
    void applyWrites();

    // tracker / record
    link::Spsc<tracker::RecordMessage, 1024> recordFifo_;
    std::atomic<bool> recordArm_{ false };
    std::atomic<bool> playing_{ false };
    // The plugin's own transport (section 16): the buttons ask on the message
    // thread, the audio thread does it, so the clock has one owner.
    std::atomic<int>  transportRequest_{ 0 };     ///< 1 play, 2 stop
    std::atomic<bool> loopOn_{ true }, ownsTransport_{ false };
    std::atomic<int>  loopFrom_{ 0 }, loopTo_{ -1 };
    uint32_t prevRecMask_ = 0;
    std::atomic<bool> songTempo_{ false };
    std::atomic<double> ppq_{ 0.0 }, bpm_{ 120.0 }, beatsPerBar_{ 4.0 }, tempo_{ 120.0 };
    std::atomic<int64_t> trackerTick_{ 0 };
    std::atomic<int> barTicks_{ driver::kTicksPerBeat * 4 };
    double tempoBase_ = 120.0;      ///< the Song tempo the published song's map was built on
    bool recWasArmed_ = false;
    // What each lane looked like last block, so a change of hands can be
    // flushed (section 9.1). -1 means "nothing seen yet".
    std::array<int, 4> prevSource_{ -1, -1, -1, -1 };
    std::array<int, 4> prevNoteSource_{ -1, -1, -1, -1 };
    uint32_t prevOwned_ = 0;
    bool     prevLink_ = false;

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

    /// Undo, message thread only. It outlives every window, so its actions
    /// can hold parameters and bank / song snapshots safely.
    ui::UndoHistory history_;

    // parameters
    std::atomic<float>* pModel_ = nullptr; std::atomic<float>* pMasterL_ = nullptr; std::atomic<float>* pMasterR_ = nullptr;
    std::atomic<float>* pTrim_ = nullptr; std::atomic<float>* pNoise_ = nullptr; std::atomic<float>* pLcd_ = nullptr;
    std::atomic<float>* pBassMod_ = nullptr; std::atomic<float>* pEdges_ = nullptr; std::atomic<float>* pDeclick_ = nullptr;
    std::atomic<float>* pDeclickMs_ = nullptr; std::atomic<float>* pSoften_ = nullptr; std::atomic<float>* pTempoSource_ = nullptr;
    std::atomic<float>* pSongTempo_ = nullptr; std::atomic<float>* pNotesOnTick_ = nullptr; std::atomic<float>* pLink_ = nullptr;

    juce::String instanceName_ { "ChipBoy 1" };
    juce::String uuid_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChipBoyProcessor)
};

} // namespace chipboy::plugin
