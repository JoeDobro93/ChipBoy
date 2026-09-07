// ChipBoy -- the driver (L2, spec section 8): a virtual Game Boy music engine.
//
// The only thing that writes APU registers. It runs on its own tick, exactly
// as a real driver's interrupt handler does: notes, parameters, tables and
// commands are sampled at tick boundaries and turned into register writes
// stamped with CPU cycles. Nothing here is per sample except the wave-RAM
// streaming scheduler, which a real driver also runs off a timer (section 8.2).
//
// Plain C++20. Knows the bank and the song; knows nothing of MIDI buffers,
// hosts or the plugin framework -- the plugin translates.
#pragma once

#include "core/Analog/AnalogModel.h"
#include "core/Bank/Bank.h"
#include "core/Driver/Clock.h"
#include "core/Tracker/Song.h"

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace chipboy::driver {

/// The channel, as the window shows it (docs/COMMANDS_AND_TEMPO.md section 3):
/// an instrument, a table, a few performance fields and two command slots.
/// Everything the old override lanes did arrives through a command now.
struct ChannelParams {
    uint8_t  instrument = 0;     ///< 0 none (type default), 1-128
    uint8_t  table = 0;          ///< 0 = instrument's table, else override
    uint8_t  level = 255;        ///< PU/NOI 0-15, WAV 0-3, 255 = instrument's
    uint8_t  pan = 255;          ///< bank::Pan, 255 = instrument's
    int8_t   transpose = 0;      ///< semitones
    bank::Command cmd[2];        ///< the two slots, in the order they apply
    bool     liveFollow = false;
    uint8_t  velocityMode = 0;   ///< 0 -> start volume, 1 -> instrument bank, 2 ignored
    bool     keyswitch = false;
    uint8_t  reserved[6] = {};   ///< the link region copies this whole struct: keep its size fixed
};
static_assert(sizeof(ChannelParams) == 32);

struct GlobalParams {
    uint8_t masterL = 7, masterR = 7;
    bool    volumeAtEdges = false;   ///< M8: NRx2 writes wait for the quiet half-cycle
};

struct NoteEvent {
    enum Kind : uint8_t { NoteOn, NoteOff, PitchBend, Control, AllNotesOff, Command };
    enum Source : uint8_t { Midi = 0, Tracker = 1 };
    uint32_t offset = 0;         ///< sample offset in the block
    uint8_t  channel = 0;        ///< hardware channel 0-3
    Kind     kind = NoteOn;
    Source   source = Midi;
    uint8_t  a = 0;              ///< note, or CC number
    uint8_t  b = 0;              ///< velocity, or CC value
    int16_t  value = 0;          ///< pitch bend -8192..8191
    bank::Command cmd1, cmd2;    ///< tracker cells carry their commands
    uint8_t  inst = 0, table = 0;///< tracker cells: 0 keep
};

struct RegWrite { uint64_t cycle; uint16_t addr; uint8_t value; };

/// What the UI shows per channel: the registers, and the running state the
/// strip prints under the two command slots (section 3).
struct VoiceView {
    bool     active = false, dacOn = false, outOfRange = false;
    uint8_t  note = 0, velocity = 0, instrument = 0;
    uint16_t period = 0;
    uint8_t  volume = 0, duty = 0, frame = 0;
    uint8_t  tableSlot = 0, tableStep = 0;
    uint8_t  regs[5] = { 0, 0, 0, 0, 0 };
    // running state
    uint8_t  envVol = 0, envRate = 0, envDir = 0;   ///< dir 0 down, 1 up
    uint8_t  vibSpeed = 0, vibDepth = 0;
    int16_t  pitchOffset = 0;
    uint8_t  pan = 0;                                ///< bank::Pan
    uint8_t  groove = kNoGroove;                     ///< the Player's, published here for the strip
    static constexpr uint8_t kNoGroove = 255;        ///< the phrase's own
};

class Driver {
public:
    Driver();

    void prepare(double sampleRate, const bank::Bank* bank, const tracker::Song* song, Console model);
    void setBank(const bank::Bank* bank) { bank_ = bank; }
    void setSong(const tracker::Song* song) { song_ = song; }
    void setModel(Console m) { model_ = m; }
    void reset();

    void setGlobal(const GlobalParams& g) { global_ = g; }
    /// An instrument used instead of the bank slot on one channel (a Voice's
    /// local instrument, spec section 12.5). Null restores the bank.
    void setLocalInstrument(int ch, const bank::Instrument* inst) { local_[size_t(ch & 3)] = inst; }
    /// While recording, incoming MIDI plays on tracker channels too (the
    /// lane is muted by the player meanwhile).
    void setRecording(bool on) { recording_ = on; }
    /// Mute / solo as NR51 gates (bit per channel, 1 = audible). Takes effect
    /// on the next tick, and pops like the hardware does.
    void setGateMask(uint32_t enabledMask) { const uint8_t m = uint8_t(enabledMask & 15); if (m != gateMask_) { gateMask_ = m; gateDirty_ = true; } }
    uint32_t gateMask() const { return gateMask_; }

    /// A write list marker (spec 12.3, "volume writes at edges"): the plugin
    /// delays the writes that follow it, on the same channel, to the next
    /// cycle where that pulse channel's output is low (Apu::cyclesUntilPulseLow).
    static constexpr uint16_t kAlignToQuietEdge = 0xFFFF;
    void setParams(int ch, const ChannelParams& p) { params_[size_t(ch & 3)] = p; }
    const ChannelParams& params(int ch) const { return params_[size_t(ch & 3)]; }
    /// The command in force in a slot: the parameter, or the last tracker cell
    /// that wrote it. This is what the recorder writes (section 5).
    const bank::Command& slot(int ch, int i) const { return v_[size_t(ch & 3)].slot[size_t(i & 1)]; }
    /// Quantise MIDI notes to ticks (section 4). Bends and controllers never
    /// wait; tracker cells are always on ticks anyway.
    void setNotesOnTick(bool on) { notesOnTick_ = on; }
    /// The groove the Player has in force, for the running-state line.
    void setViewGroove(int ch, uint8_t g) { view_[size_t(ch & 3)].groove = g; }

    /// One block. `events` sorted by offset; `ticks` are this block's tick
    /// boundaries from the Clock; `cycleAt(absoluteFrame)` maps a frame to the
    /// APU cycle exactly as the renderer does. Writes are appended to `out` in
    /// cycle order.
    void process(const NoteEvent* events, size_t n, uint32_t numSamples, uint64_t frameAbs,
                 const TickPoint* ticks, size_t nTicks,
                 const std::function<uint64_t(uint64_t)>& cycleAt,
                 std::vector<RegWrite>& out);

    const VoiceView& view(int ch) const { return view_[size_t(ch & 3)]; }
    uint8_t nr50() const { return shadow_[0x14]; }
    uint8_t nr51() const { return shadow_[0x15]; }
    uint64_t tickCount() const { return tickCount_; }

    /// Pitch helpers, public because the UI shows them.
    static int  periodForNote(double note, bool waveChannel);     ///< -1 below range
    static void noisePairForNote(int note, uint8_t& shift, uint8_t& divisor);
    static double noiseClockHz(uint8_t shift, uint8_t divisor);

private:
    struct Voice {
        bank::InstrumentCore inst;
        ChannelParams p;
        bool     haveInst = false;
        bool     active = false, dacOn = false, killed = false;
        uint8_t  note = 0, vel = 0;
        double   bend = 0.0;
        int16_t  basePeriod = 0;
        int16_t  lastPeriod = -1;
        int16_t  pOffset = 0;
        int16_t  slideTarget = 0; uint8_t slideRate = 0; bool sliding = false;
        uint32_t ticks = 0;
        int32_t  vibPos = 0; uint8_t vibSpeed = 0, vibDepth = 0; bank::VibShape vibShape = bank::VibShape::Triangle; uint8_t vibDelay = 0;
        uint8_t  tableSlot = 0, tableStep = 0; bool tableOn = false;
        uint8_t  tableOverride = 0, tableParam = 0;   ///< in force (parameter or cell), and the parameter it came from
        uint8_t  chord[3] = { 0, 0, 0 }; uint8_t chordN = 0, chordIdx = 0;
        uint8_t  dutyIdx = 0, duty = 2;
        uint8_t  envVol = 15, envRate = 0; bank::EnvDir envDir = bank::EnvDir::Down;
        uint8_t  waveLevel = 3;            ///< WAV/KIT running level, 0 mute .. 3 full
        uint8_t  volume = 15;              ///< last written level
        uint8_t  sweepRate = 0, sweepShift = 0; bool sweepDown = false;
        uint8_t  noiseShift = 5, noiseDiv = 1; bool lfsr7 = false; int8_t noiseSweep = 0;
        bank::Pan pan = bank::Pan::Both;
        uint16_t lengthCode = 0;
        // wave
        uint8_t  waveSlot = 1, frameIdx = 0, frameCount = 0; int8_t frameDir = 1;
        std::array<uint8_t, 16> ram{};
        bool     ramValid = false;
        // kit
        bool     kitOn = false; uint8_t kitIdx = 0; uint32_t kitPos = 0; uint32_t kitLen = 0; uint32_t kitLoopPoint = 0; bank::KitLoop kitLoop = bank::KitLoop::Once;
        uint32_t kitLoopsStreamed = 0;
        // counters
        int16_t  delay = -1, kill = -1;
        uint8_t  retrigEvery = 0, retrigCount = 0;
        bool     pendingOn = false; uint8_t pendingNote = 0, pendingVel = 0;
        // held notes for last-note priority
        std::array<uint8_t, 16> held{}; uint8_t heldCount = 0;
        uint8_t  ksInstrument = 0;
        bank::Command lastCmd;                        ///< for Z inside a table step
        bank::Command slot[2], slotParam[2];          ///< in force, and the parameter it came from
        int16_t  retrigStep = 0;                      ///< R: volume change per retrigger
        uint32_t rng = 1;
        // model of the wave channel timer, for streaming
        uint64_t nextFetch = 0; uint32_t fetchPeriod = 0; uint32_t fetchIndex = 0; bool timerValid = false;
        bool     streamActive = false; uint8_t streamByte = 0; std::array<uint8_t, 16> streamData{};
    };

    // block state
    std::vector<RegWrite>* out_ = nullptr;
    uint64_t cycle_ = 0;                 ///< the current tick's cycle
    uint32_t burst_ = 0;                 ///< writes emitted at this cycle so far

    void emit(uint16_t addr, uint8_t v, bool force = false);
    void emitAt(uint64_t cycle, uint16_t addr, uint8_t v);
    void tick(int ch);
    void tickAll();
    void handleEvent(const NoteEvent& e);
    void noteOn(int ch, uint8_t note, uint8_t vel, const NoteEvent* cell);
    void noteOff(int ch, uint8_t note);
    void startVoice(int ch, uint8_t note, uint8_t vel, bool legato);
    void stopVoice(int ch, bool kill);
    void latch(int ch);
    void writePeriod(int ch, bool trigger);
    void writeEnvelope(int ch, bool trigger);
    void writeNr51();
    void writeNr50(uint8_t l, uint8_t r);
    void applyCommand(int ch, const bank::Command& c, bool fromTable);
    void revertCommand(int ch, bank::Cmd cmd);
    void updateSlots(int ch);                 ///< a slot whose value changed fires at this tick
    void syncSlots(int ch);                   ///< adopt the parameters' slots without firing them
    void fireSlots(int ch);                   ///< and again at every note-on
    bank::Command slotForNoteOn(int ch, int i);        ///< with Z's randomised argument
    int16_t randomArg(int ch, int max);
    void applyLevelParam(int ch);
    void reloadInstrument(int ch);            ///< a cell's instrument column, or Live follow
    static bank::InstrumentType defaultType(int ch);
    static bool typeFits(int ch, bank::InstrumentType t);
    void stepTable(int ch);
    void loadFrame(int ch, const bank::Frame& f, bool trigger);
    void updateWaveTimer(int ch, uint16_t freq, bool trigger);
    void scheduleStreams(uint64_t cycleStart, uint64_t cycleEnd);
    void kitNextChunk(int ch, std::array<uint8_t, 16>& chunk, bool& ended);
    int  computePeriod(int ch);
    uint8_t levelFromVelocity(uint8_t vel) const;
    void refreshView(int ch);
    const bank::Instrument* resolveInstrument(int ch, uint8_t vel);

    const bank::Bank* bank_ = nullptr;
    const tracker::Song* song_ = nullptr;
    std::array<const bank::Instrument*, 4> local_{};
    bool recording_ = false;
    uint8_t gateMask_ = 15;
    bool gateDirty_ = false;
    Console model_ = Console::DMG;
    double sampleRate_ = 48000.0;
    GlobalParams global_;
    std::array<ChannelParams, 4> params_;
    std::array<Voice, 4> v_;
    std::array<VoiceView, 4> view_;
    std::array<uint8_t, 0x30> shadow_{};
    std::array<bool, 0x30>    known_{};
    uint8_t masterL_ = 255, masterR_ = 255;
    uint64_t tickCount_ = 0;
    bool     notesOnTick_ = false;
    bool     inNoteOn_ = false;   ///< commands set state; the note's own writes carry it
    /// Notes waiting for the next tick while notes-on-tick is on; they survive
    /// a block boundary, so the tick they wait for may be in the next block.
    std::array<NoteEvent, 256> pending_{}; size_t pendingCount_ = 0;
    std::array<int8_t, 128> noiseShiftMap_{}, noiseDivMap_{};
};

} // namespace chipboy::driver
