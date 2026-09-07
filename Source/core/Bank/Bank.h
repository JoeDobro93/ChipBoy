// ChipBoy -- the bank: instruments, tables, waves, kits (spec section 9).
//
// Plain data, fixed slot counts (section 9.1), no dependencies. Everything the
// driver latches at note-on is in InstrumentCore, which is trivially copyable
// so a note-on never allocates on the audio thread. Names live outside it.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace chipboy::bank {

constexpr int kInstrumentSlots = 128;
constexpr int kTableSlots = 64;
constexpr int kWaveSlots = 64;
constexpr int kKitSlots = 32;
constexpr int kTableSteps = 16;
constexpr int kMaxFrames = 16;
constexpr int kMaxKitSamples = 32;

enum class InstrumentType : uint8_t { Pulse = 0, Wave = 1, Kit = 2, Noise = 3 };
enum class Pan : uint8_t { Off = 0, Left = 1, Right = 2, Both = 3 };
enum class EnvDir : uint8_t { Down = 0, Up = 1 };
enum class NoteOff : uint8_t { Kill = 0, Release = 1, Ignore = 2 };
enum class VibShape : uint8_t { Triangle = 0, Square = 1, SawUp = 2, SawDown = 3 };
enum class FrameLoop : uint8_t { Loop = 0, Once = 1, PingPong = 2 };
enum class KitLoop : uint8_t { Once = 0, Loop = 1, FromPoint = 2 };
enum class TableEnd : uint8_t { Loop = 0, Hop = 1, Stop = 2 };

/// Commands, LSDj lettering, ChipBoy semantics (docs/COMMANDS_AND_TEMPO.md
/// section 2). Both arguments are 0-255; the letter says what they mean:
///   A table slot 1-64, 0 stops              C x, y semitones        D ticks
///   E vol 0-15, y 0-7 decay / 8-15 attack   F frame 1-16            G groove 1-16, 0 straight
///   H step 1-16 (0 stops), tables only      K ticks after note-on   L rate 0-15
///   M left 0-7, right 0-7                   O pan 0-3 (off L R both)
///   P offset x - 128 period units           R x volume step, y ticks
///   S rate 0-7, shift 0-7 (x >= 128 down)   T BPM 40-255            V speed 1-15, depth 0-15
///   W duty 0-3 (pulse) / wave slot (WAV)    Z max, randomises the other slot
enum class Cmd : uint8_t { None = 0, A, C, D, E, F, G, H, K, L, M, O, P, R, S, T, V, W, Z };
constexpr int kCmdCount = 18;                ///< letters, not counting None
struct Command {
    Cmd     cmd = Cmd::None;
    int16_t a = 0, b = 0, c = 0;             ///< a, b are the spec's x and y; c is internal
};
inline bool sameCmd(const Command& a, const Command& b) { return a.cmd == b.cmd && a.a == b.a && a.b == b.b && a.c == b.c; }
const char* cmdLetter(Cmd c);
Cmd cmdFromLetter(char c);

struct Vibrato {
    VibShape shape = VibShape::Triangle;
    uint8_t  speed = 4;     ///< ticks per step
    uint8_t  depth = 0;     ///< raw period units
    uint8_t  delay = 0;     ///< ticks before it starts
};

/// The part of an instrument the driver latches. Trivially copyable.
struct InstrumentCore {
    InstrumentType type = InstrumentType::Pulse;
    Pan      pan = Pan::Both;
    uint16_t length = 0;             ///< 0 off; 1-64 (PU/NOI), 1-256 (WAV/KIT)
    uint8_t  table = 0;              ///< 0 none, 1-64
    bool     transpose = true;       ///< whether table transpose applies
    NoteOff  noteOff = NoteOff::Kill;
    bool     legato = false;
    Vibrato  vib;
    // pulse
    uint8_t  duty = 2;               ///< 0 12.5%, 1 25%, 2 50%, 3 75%
    std::array<uint8_t, 16> dutySeq{};
    uint8_t  dutySeqLen = 0;         ///< 0 = no sequence
    uint8_t  envVol = 15;
    EnvDir   envDir = EnvDir::Down;
    uint8_t  envRate = 0;            ///< 0 = hold
    uint8_t  sweepRate = 0;          ///< PU1 only
    bool     sweepDown = false;
    uint8_t  sweepShift = 0;
    // wave
    uint8_t  wave = 1;               ///< wave slot 1-64
    uint8_t  frameAdvance = 0;       ///< ticks per frame, 0 holds
    FrameLoop frameLoop = FrameLoop::Loop;
    uint8_t  waveLevel = 3;          ///< 0 mute, 1 25%, 2 50%, 3 100%
    // kit
    uint8_t  kit = 1;                ///< kit slot 1-32
    KitLoop  kitLoop = KitLoop::Once;
    // noise
    bool     lfsr7 = false;
    bool     noiseManual = false;
    uint8_t  noiseShift = 5;
    uint8_t  noiseDivisor = 1;
    int8_t   noiseSweep = 0;         ///< shift steps per tick
};

struct Instrument : InstrumentCore {
    bool        used = false;
    std::string name;
    static Instrument defaults(InstrumentType t, const char* name = "");
};

struct TableStep {
    int8_t  vol = -1;                ///< -1 blank, else 0-15
    bool    hasTranspose = false;
    int8_t  transpose = 0;           ///< -60..60
    Command cmd1, cmd2;
};
struct Table {
    bool        used = false;
    std::string name;
    std::array<TableStep, kTableSteps> steps{};
    TableEnd    end = TableEnd::Loop;
    uint8_t     hopStep = 1;         ///< 1-16, for End::Hop
};

struct Frame { std::array<uint8_t, 32> s{}; };   ///< 32 samples, 0-15
struct Wave {
    bool        used = false;
    std::string name;
    std::vector<Frame> frames;       ///< 1-16
};

struct KitSample {
    std::string name;
    uint8_t  note = 60;              ///< the MIDI note that plays it
    std::vector<uint8_t> data;       ///< 4-bit samples, one per byte, 0-15
    uint32_t loopPoint = 0;
};
struct Kit {
    bool        used = false;
    std::string name;
    std::vector<KitSample> samples;  ///< up to 32
    uint16_t    period = 1865;       ///< NR33/34 value: 2097152 / (2048 - period) samples per second
    KitLoop     loop = KitLoop::Once;
};

struct Bank {
    std::array<Instrument, kInstrumentSlots> instruments;
    std::array<Table, kTableSlots>           tables;
    std::array<Wave, kWaveSlots>             waves;
    std::array<Kit, kKitSlots>               kits;

    /// Slot access, 1-based; nullptr for 0 or an unused slot.
    const Instrument* instrument(int slot) const;
    const Table*      table(int slot) const;
    const Wave*       wave(int slot) const;
    const Kit*        kit(int slot) const;

    static Bank factory();
    static Bank empty();
};

/// Generated shapes, quantised to 4 bits on creation (section 9.7).
Frame frameSine();
Frame frameTriangle();
Frame frameSaw();
Frame framePulse(int widthSamples);
Frame frameInterpolate(const Frame& a, const Frame& b, double t);

/// Wave-channel period register for a sample rate, and back (section 9.8).
uint16_t periodForSampleRate(double hz);
double   sampleRateForPeriod(uint16_t period);

} // namespace chipboy::bank
