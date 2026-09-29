// ChipBoy Solo -- one channel of the machine as a plugin of its own
// (docs/plan-solo.md, docs/COMMANDS_AND_TEMPO.md section 227). The same
// APU, analog stage, driver and bank as ChipBoy, driving one channel from
// every MIDI channel of its track; no song, no link, no noise floor. The
// row -- instrument, table, level, pan, transpose, two commands -- is the
// host's parameters; sixty-four sounds recall a row; keys outside the
// channel's range select sounds. Message-thread state is published by
// pointer swap, as the main plugin's bank is.
#pragma once

#include "core/Apu/Apu.h"
#include "core/Bank/Bank.h"
#include "core/Driver/Clock.h"
#include "core/Driver/Driver.h"
#include "core/Render/Renderer.h"
#include "plugin/shared/EditorHost.h"
#include "plugin/shared/Parameters.h"
#include "plugin/shared/ScopeBuffers.h"
#include "plugin/solo/SoloState.h"
#include "plugin/ui/EditHistory.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <vector>

namespace chipboy::plugin {

namespace solo::ids {
constexpr const char* channel = "channel";          ///< PU1 / PU2 / WAV / NOI
constexpr const char* model = "model";
constexpr const char* tempoSource = "tempo_source"; ///< Host / Own
constexpr const char* tempo = "tempo";              ///< the own tempo, BPM
constexpr const char* notesOnTick = "notes_on_tick";
constexpr const char* sound = "sound";              ///< 0 none, 1-64
constexpr const char* volume = "volume";            ///< NR50, both sides
constexpr const char* trim = "trim";
constexpr const char* hexDisplay = "hex";
constexpr const char* keyMap = "keymap";            ///< keys outside the range select sounds
constexpr const char* prefix = "s_";                ///< the channel set (Parameters.h ids::)
} // namespace solo::ids

class SoloProcessor : public juce::AudioProcessor, public EditorHost, private juce::Timer
{
public:
    SoloProcessor();
    ~SoloProcessor() override;

    // --- AudioProcessor ------------------------------------------------
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    using juce::AudioProcessor::processBlock;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "ChipBoy Solo"; }
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

    // --- EditorHost (message thread) -------------------------------------
    juce::AudioProcessorValueTreeState apvts;
    ChannelParamCache params;
    juce::AudioProcessorValueTreeState& state() override { return apvts; }
    const juce::AudioProcessorValueTreeState& state() const override { return apvts; }
    std::shared_ptr<const bank::Bank> bank() const override { return bankShared_; }
    void editBank(const juce::String& name, const std::function<void(bank::Bank&)>& fn) override;
    ui::UndoHistory& history() override { return history_; }
    ScopeBuffers& scopes() override { return scopes_; }
    double effectiveTempo() const override { return tempo_.load(); }
    void previewKitSample(int slot, int sample) override;
    int channelCount() const override { return 1; }
    int hardwareChannel(int) const override { return channel(); }
    juce::String channelParamId(int, const char* id) const override { return juce::String(solo::ids::prefix) + id; }

    /// The hardware channel the plugin plays, 0-3, and its kind for the palettes.
    int channel() const { return std::clamp(paramInt(pChannel_), 0, 3); }
    ChannelKind kind() const { const int c = channel(); return c == 0 ? ChannelKind::Pulse1 : c == 1 ? ChannelKind::Pulse2 : c == 2 ? ChannelKind::Wave : ChannelKind::Noise; }
    void noteRange(int& lo, int& hi) const { soloNoteRange(channel(), lo, hi); }

    // --- the sounds, the library, the key map ----------------------------
    std::shared_ptr<const SoloState> solo() const { return soloShared_; }
    /// A hand edit of the sounds, the library or the key map: copy-on-write,
    /// one undo step under `name`.
    void editSolo(const juce::String& name, const std::function<void(SoloState&)>& fn);
    /// The row as it stands -- the parameters -- into a sound slot.
    void storeSound(int slot, const juce::String& name = {});
    /// A sound's row into the parameters. By hand it is one undo step and the
    /// Sound parameter follows; from a key or the parameter it is not an edit.
    void recallSound(int slot, bool byHand);
    /// What the last recall was, for the status line, and a count that moves.
    juce::String lastRecall() const { return lastRecall_; }
    int recallSerial() const { return recallSerial_; }
    /// The library's entry into a command slot (0 or 1), and a slot into the library.
    void useLibraryCommand(int entry, int slot);
    void storeLibraryCommand(int entry, int slot, const juce::String& name = {});

    /// A whole bank arriving (a file): one undo step.
    void loadBankEdit(const juce::String& name, const bank::Bank& b);
    void restoreBank(std::shared_ptr<const bank::Bank> b);
    void restoreSolo(std::shared_ptr<const SoloState> s);

    /// The instance as a file (docs/plan-solo.md section 3): the bank, the
    /// sounds, the library, the key map, the channel and the row.
    bool saveSoloFile(const juce::File& file) const;
    bool loadSoloFile(const juce::File& file, juce::String& report);
    static juce::File soloFolder();

    /// What the driver last loaded and the last note, for the strip.
    int lastNote() const { return lastNote_.load(); }
    int lastKey() const { return lastKey_.load(); }
    bool transportPlaying() const { return playing_.load(); }
    bool hostTempo() const { return hostTempo_.load(); }
    /// A recall still overriding the parameters on the audio thread (a harness reads it).
    bool recallPending() const { return overrideOn_; }
    const driver::Driver& driverView() const { return driver_; }
    int latencyFrames() const { return renderer_.latencyFrames(); }
    /// Every register write the driver emits, for a harness (section 9.5).
    void setWriteLog(std::vector<driver::RegWrite>* log) { driver_.setWriteLog(log); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void timerCallback() override;
    void publishBank(std::shared_ptr<const bank::Bank> b);
    void publishSolo(std::shared_ptr<const SoloState> s);
    void applyModel();
    /// The audio thread's recall (docs/plan-solo.md section 2): the row the
    /// driver reads is the sound's from this block on, and the parameters are
    /// brought up to it by the timer.
    void recallOnAudioThread(int slot, const SoloState* s, int key);
    void mixPreview(float* left, float* right, int n);
    void applyWrites();
    void tapScopes(int n);
    std::atomic<float>* raw(const char* id) { return apvts.getRawParameterValue(id); }

    // engine, audio thread
    Apu apu_;
    render::Renderer renderer_;
    driver::Driver driver_;
    driver::Clock clock_;
    std::vector<driver::NoteEvent> events_;
    std::vector<driver::RegWrite> writes_;
    std::function<uint64_t(uint64_t)> cycleAt_;
    uint64_t frames_ = 0;
    double sampleRate_ = 48000.0;
    int blockSize_ = 512;
    int modelIndex_ = -1;
    int prevChannel_ = -1;
    std::atomic<int> lastSoundParam_{ -1 };   ///< a state restore resets it from the message thread
    /// The recall in force on the audio thread until the parameters agree.
    bool overrideOn_ = false;
    int  overrideBlocks_ = 0;
    uint8_t overrideInst_ = 0, overrideTable_ = 0;   ///< the sound's row, without its name (no allocation here)
    bank::Command overrideCmd_[2];
    std::atomic<uint32_t> recallReq_{ 0 };       ///< slot | key << 8 | serial << 16, for the timer
    std::atomic<uint32_t> recallApplied_{ 0 };   ///< the serial the timer last wrote into the parameters
    uint32_t recallSeen_ = 0, recallSerialAudio_ = 0;
    std::atomic<int> lastNote_{ -1 }, lastKey_{ -1 };
    std::atomic<bool> playing_{ false }, hostTempo_{ false };
    std::atomic<double> tempo_{ 120.0 };

    // the kit auditioner (D-UI-29), as the main plugin's
    std::atomic<uint32_t> previewReq_{ 0 };
    uint32_t previewSeq_ = 0, previewSeen_ = 0;
    int previewSlot_ = 0, previewIdx_ = 0;
    double previewPos_ = 0.0, previewStep_ = 0.0;
    bool previewOn_ = false;

    ScopeBuffers scopes_;

    // bank and state, published by pointer swap
    std::shared_ptr<const bank::Bank> bankShared_;
    std::shared_ptr<const SoloState> soloShared_;
    std::atomic<const bank::Bank*> bankPtr_{ nullptr };
    std::atomic<const SoloState*> soloPtr_{ nullptr };
    std::deque<std::shared_ptr<const void>> retired_;
    ui::UndoHistory history_;
    juce::String lastRecall_;
    int recallSerial_ = 0;

    // parameters
    std::atomic<float>* pChannel_ = nullptr; std::atomic<float>* pModel_ = nullptr; std::atomic<float>* pTempoSource_ = nullptr;
    std::atomic<float>* pTempo_ = nullptr; std::atomic<float>* pNotesOnTick_ = nullptr; std::atomic<float>* pSound_ = nullptr;
    std::atomic<float>* pVolume_ = nullptr; std::atomic<float>* pTrim_ = nullptr; std::atomic<float>* pKeyMap_ = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SoloProcessor)
};

} // namespace chipboy::plugin
