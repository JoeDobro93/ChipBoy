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
enum class VibShape : uint8_t { Triangle = 0, Saw = 1, Square = 2 };
enum class VibDir : uint8_t { Down = 0, Up = 1 };
/// How fast P, L and V move (docs/COMMANDS_AND_TEMPO.md section 7). Fast is
/// 360 updates a second, tempo-independent; Tick is one per tracker tick, so
/// the effect follows the tempo; Step is Fast with P as an immediate offset
/// instead of a bend; Drum is Fast with P and L in semitones, for pitch kicks.
enum class PitchSpeed : uint8_t { Fast = 0, Tick = 1, Step = 2, Drum = 3 };
/// Tick: the table runs a row per tick (or per its own G). Step: it advances
/// one row each time the instrument is triggered.
enum class TableMode : uint8_t { Tick = 0, Step = 1 };
/// A note that arrives over a held one: Legato changes the pitch only (a bare
/// note), Retrig starts the instrument again (section 8).
enum class Overlap : uint8_t { Legato = 0, Retrig = 1 };
enum class FrameLoop : uint8_t { Loop = 0, Once = 1, PingPong = 2 };
/// How S and P move the noise channel (docs/COMMANDS_AND_TEMPO.md section 66):
/// Notes walks the map by note, Register subtracts from NR43 nibble by nibble.
enum class NoiseSweepDomain : uint8_t { Notes = 0, Register = 1 };
/// Section 66: each nibble less the matching nibble of `xy`, modulo sixteen,
/// with no borrow between them -- LSDj's arithmetic for S and P on noise.
inline uint8_t noiseNibbleSub(uint8_t nr43, uint8_t xy)
{
    return uint8_t(((((nr43 >> 4) - (xy >> 4)) & 15) << 4) | (((nr43 & 15) - (xy & 15)) & 15));
}
/// The same nibbles added: the deltas compose, so one running byte holds every
/// S and P a note has taken, and `noiseNibbleSub` applies the lot at once.
inline uint8_t noiseNibbleAdd(uint8_t a, uint8_t b)
{
    return uint8_t(((((a >> 4) + (b >> 4)) & 15) << 4) | (((a & 15) + (b & 15)) & 15));
}
/// How the instrument's level is made (docs/COMMANDS_AND_TEMPO.md section 27):
/// Chip is the chip's own NRx2 envelope -- an initial volume, a direction and
/// one of its seven rates -- and Shaped is an ADSR the driver renders one
/// level per tick and writes through zombie mode (section 26).
enum class EnvMode : uint8_t { Chip = 0, Shaped = 1 };
/// A shaped segment's shape: Linear moves evenly, Exponential fast at the
/// start, Logarithmic slowly at the start (section 27).
enum class EnvCurve : uint8_t { Linear = 0, Exponential = 1, Logarithmic = 2 };
enum class KitLoop : uint8_t { Once = 0, Loop = 1, FromPoint = 2 };
enum class TableEnd : uint8_t { Loop = 0, Hop = 1, Stop = 2 };

/// Commands, LSDj lettering, ChipBoy semantics (docs/COMMANDS_AND_TEMPO.md
/// section 2). Both arguments are 0-255; the letter says what they mean:
///   A table slot 1-64, 0 stops              C x, y semitones        D ticks
///   E vol 0-15, y 0-7 decay / 8-15 attack   F frame 1-16            G groove 1-16, 0 straight
///   H step 1-16 (0 stops), tables only      K ticks after note-on   L slide duration
///   M left/right 0-7, 8 keep, 9-15 relative O pan 0-3 (off L R both)
///   P bend speed x - 128                    R x volume step, y ticks
///   S rate 0-7, shift 0-7 (x >= 128 down)   T BPM 40-255            V speed 1-15, depth 0-15
///   W duty 0-3 (pulse) / wave slot (WAV)    Z random 0..x, 0..y added to the last command
/// `B` is appended rather than inserted after `A`: every other letter keeps the
/// value it has had, so a song file, a preset and the host's command parameter
/// all read unchanged (section 73).
enum class Cmd : uint8_t { None = 0, A, C, D, E, F, G, H, K, L, M, O, P, R, S, T, V, W, Z, B };
constexpr int kCmdCount = 19;                ///< letters, not counting None
/// `c` = kRevert makes the command the *revert form* of its letter: "put this
/// letter back where the instrument left it", which is exactly what a command
/// slot going to none does (docs/COMMANDS_AND_TEMPO.md section 3). `a` and `b`
/// are unused then. It is a value a cell or a table step can hold, so a
/// recorded slot change back to none plays back as itself instead of as a
/// concrete value that would then stay in force.
constexpr int16_t kRevert = 1;
struct Command {
    Cmd     cmd = Cmd::None;
    int16_t a = 0, b = 0, c = 0;             ///< a, b are the spec's x and y; c = kRevert is the revert form
};
inline bool sameCmd(const Command& a, const Command& b) { return a.cmd == b.cmd && a.a == b.a && a.b == b.b && a.c == b.c; }
inline bool isRevert(const Command& c) { return c.cmd != Cmd::None && c.c == kRevert; }
/// The letters that leave something behind, so there is something to revert:
/// the instrument's value (E F O S V W), zero (P), the parameters (M, T), the
/// phrase's groove (G) or a stopped table (A). C D H K L R Z are per-note and
/// leave nothing, so their revert form is nothing.
inline bool cmdPersists(Cmd c)
{
    switch (c) {
        case Cmd::A: case Cmd::E: case Cmd::F: case Cmd::G: case Cmd::M:
        case Cmd::O: case Cmd::P: case Cmd::S: case Cmd::T: case Cmd::V: case Cmd::W:
            return true;
        case Cmd::None: case Cmd::B: case Cmd::C: case Cmd::D: case Cmd::H: case Cmd::K:
        case Cmd::L: case Cmd::R: case Cmd::Z:
            return false;
    }
    return false;
}
/// The revert form of a letter, or Cmd::None when the letter leaves nothing.
inline Command revertOf(Cmd c) { return cmdPersists(c) ? Command{ c, 0, 0, kRevert } : Command{}; }

/// T's argument is the **byte LSDj stores** (docs/COMMANDS_AND_TEMPO.md 34):
/// `28`-`FF` are 40-255 BPM and `00`-`27` are 256-295. The tempo is derived
/// from the byte, never stored beside it, so a song file and a playback ROM
/// carry one encoding.
inline int tempoBpmOfByte(int b) { const int v = b & 0xFF; return v >= 0x28 ? v : v + 256; }
inline int tempoByteOfBpm(int bpm) { const int v = bpm < 40 ? 40 : bpm > 295 ? 295 : bpm; return v <= 255 ? v : v - 256; }
/// P is stored **two's complement** (section 34): the byte 0-255 read signed.
inline int bendOfByte(int b) { return int(int8_t(uint8_t(b & 0xFF))); }
inline int byteOfBend(int v) { return int(uint8_t(int8_t(v < -128 ? -128 : v > 127 ? 127 : v))); }
const char* cmdLetter(Cmd c);
Cmd cmdFromLetter(char c);

/// The instrument's own vibrato, in V's units (section 7): one cycle every
/// 720 / speed pitch updates (speed / 2 Hz in Fast), depth an index into
/// LSDj's semitone table (0 = 1/8 .. 15 = 8 semitones).
struct Vibrato {
    VibShape shape = VibShape::Triangle;
    VibDir   dir = VibDir::Down;   ///< Down swings to note - depth, Up to note + depth
    uint8_t  speed = 8;     ///< 1-15, as V's x (8 = 4 Hz)
    uint8_t  depth = 0;     ///< 0-15, as V's y: the semitone table, 0 = off here
    uint8_t  delay = 0;     ///< ticks before it starts
};

/// The shaped envelope (section 27): Attack from silence to Peak, Decay to
/// Sustain, which is held while the note is, and a Release from the level at
/// note-off to silence, each in ticks and each with its own curve. It is
/// rendered one level per tick (0-15; the wave channel takes the four NR32
/// levels), so a playback ROM can replay it from a list.
struct Envelope {
    EnvMode  mode = EnvMode::Chip;
    uint8_t  start = 0;            ///< 0-15, where the attack begins (section 51); 0 is silence
    uint8_t  attackTicks = 0;      ///< 0-255; 0 starts at the peak
    uint8_t  peak = 15;            ///< 0-15
    uint8_t  decayTicks = 0;       ///< 0-255; 0 drops to the sustain at once
    uint8_t  sustain = 15;         ///< 0-15, held while the note is held -- or faded from
    uint8_t  fadeTicks = 0;        ///< 0-255; 0 is no fade (section 51), else ticks from the sustain to fadeTo
    uint8_t  fadeTo = 0;           ///< 0-15, held once the fade reaches it
    uint8_t  releaseTicks = 0;     ///< 0-255; 0 is silent at once
    EnvCurve attackCurve = EnvCurve::Linear;
    EnvCurve decayCurve = EnvCurve::Linear;
    EnvCurve fadeCurve = EnvCurve::Linear;
    EnvCurve releaseCurve = EnvCurve::Linear;
};

/// The level a shaped segment has reached: `from` to `to` over `ticks`, at
/// tick `t` (0 at the segment's start, `ticks` at its end). Integer, so the
/// driver, a test and a playback ROM all agree on the list (section 27):
/// linear is t / n, exponential (fast start) 1 - (1 - t/n)^2 and logarithmic
/// (slow start) (t/n)^2, rounded half away from zero.
int envSegmentLevel(int from, int to, int ticks, int t, EnvCurve curve);

/// The part of an instrument the driver latches. Trivially copyable.
struct InstrumentCore {
    InstrumentType type = InstrumentType::Pulse;
    Pan      pan = Pan::Both;
    uint16_t length = 0;             ///< 0 off; 1-64 (PU/NOI), 1-256 (WAV/KIT)
    uint8_t  table = 0;              ///< 0 none, 1-64
    bool     transpose = true;       ///< whether table transpose applies
    bool     envRetrig = false;      ///< E re-attacks the note (section 59): LSDj's own rule before 8.8
    NoteOff  noteOff = NoteOff::Kill;
    Overlap  overlap = Overlap::Legato;
    PitchSpeed pitchSpeed = PitchSpeed::Fast;
    uint8_t  cmdRate = 0;            ///< 0-15: R (and P, V in Tick) step every cmdRate + 1 ticks
    uint8_t  chordRate = 0;          ///< 0-15: C steps every chordRate + 1 ticks (section 37)
    TableMode tableMode = TableMode::Tick;
    Vibrato  vib;
    // pulse
    uint8_t  duty = 2;               ///< 0 12.5%, 1 25%, 2 50%, 3 75%
    std::array<uint8_t, 16> dutySeq{};
    uint8_t  dutySeqLen = 0;         ///< 0 = no sequence
    uint8_t  envVol = 15;
    EnvDir   envDir = EnvDir::Down;
    uint8_t  envRate = 0;            ///< 0 = hold
    Envelope env;                    ///< Chip by default; Shaped renders its own level (section 27)
    uint8_t  sweepRate = 0;          ///< PU1 only
    int8_t   pu2Transpose = 0;       ///< semitones added on the second pulse only (section 49)
    bool     sweepDown = false;
    uint8_t  sweepShift = 0;
    // wave
    uint8_t  wave = 1;               ///< wave slot 1-64
    /// The run a note walks (section 65): `frameLength` frames spread across the
    /// wave's own, 0 meaning every one of them; Loop and PingPong turn at
    /// `frameLoopStep`, which is a step of that run and not a frame number.
    uint8_t  frameLength = 0;
    uint8_t  frameLoopStep = 0;
    uint8_t  frameAdvance = 0;       ///< ticks per frame, 0 holds
    FrameLoop frameLoop = FrameLoop::Loop;
    uint8_t  waveLevel = 3;          ///< 0 mute, 1 25%, 2 50%, 3 100%
    // kit
    uint8_t  kit = 1;                ///< kit slot 1-32
    KitLoop  kitLoop = KitLoop::Once;
    // noise
    /// Which domain the noise sweep commands work in (section 66): Notes moves
    /// the note through the map, Register the NR43 byte nibble-wise.
    NoiseSweepDomain noiseDomain = NoiseSweepDomain::Notes;
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

/// A table's row. Its three lanes step on their own pointers (section 64):
/// VOL and LEN are one, TSP and `cmd1` the second, `cmd2` the third.
struct TableStep {
    int8_t  vol = -1;                ///< -1 blank, else 0-15
    uint8_t volTicks = 0;            ///< 0 = as long as the table's row, else 1-15 ticks (section 64)
    int8_t  volHop = -1;             ///< -1 none, else 0-15: the row the volume lane hops to
    bool    hasTranspose = false;
    int8_t  transpose = 0;           ///< -128..127, the byte LSDj shows (section 52)
    Command cmd1, cmd2;
};
struct Table {
    bool        used = false;
    std::string name;
    std::array<TableStep, kTableSteps> steps{};
    TableEnd    end = TableEnd::Loop;
    uint8_t     hopStep = 1;         ///< 1-16, for End::Hop
};

/* --------------------------------------------------------- the synth */
// docs/COMMANDS_AND_TEMPO.md section 33. A wave's frames can be generated
// from parameters the bank keeps, so a run can be regenerated after an edit
// and the exporter still only ships frames. Rendering is `synthesize()` in
// WaveSynth.h -- deterministic, integer in, integer out.

/// Where a frame starts before the shapers touch it.
enum class SynthSource : uint8_t {
    Sine = 0, Triangle, Saw, Square, Additive, Noise, Drawn
};
constexpr int kSynthSourceCount = 7;

/// One link of the shaper chain. None is a no-op, and so is any stage whose
/// amount is 0, so an empty chain and a chain of zeros are the same wave.
enum class SynthShaper : uint8_t {
    None = 0, LowPass, HighPass, BandPass, AllPass,
    Clip, Fold, Wrap, Rotate, Shift, Invert, Reverse, Smooth, Crush, Quantise, Normalise
};
constexpr int kSynthShaperCount = 16;
constexpr int kSynthStages = 4;        ///< shapers in the chain, applied in order
constexpr int kSynthPartials = 8;      ///< the additive source's harmonics

/// One end of the morph: the shape it starts as and every number the shape
/// is made from. The chain's shapers are the Synth's; the shape and the
/// values are the state's, so a run can go from a sine to a saw (section 36).
struct SynthState {
    /// Drawn by default, so a fresh synth is the wave that is already there
    /// and every shaper starts from it.
    SynthSource source = SynthSource::Drawn;
    uint8_t width = 16;                                  ///< Square: samples high, 1-31
    std::array<uint8_t, kSynthPartials> partials{ { 15, 0, 0, 0, 0, 0, 0, 0 } };   ///< Additive: 0-15 each
    std::array<int8_t, kSynthStages> amount{};           ///< -15..15 per stage; 0 is a no-op
    std::array<uint8_t, kSynthStages> resonance{};       ///< 0-15, the filters' Q
};

/// The synth behind one wave slot's frame run: a chain of shapers, a start
/// and an end state, and where in the slot the frames that morph between
/// them go. The frames themselves stay the wave's -- this is what made them.
struct Synth {
    bool used = false;                 ///< false = the frames were drawn, not generated
    std::array<SynthShaper, kSynthStages> chain{};
    SynthState start, end;
    uint8_t first = 0;                 ///< 0-15, the slot frame the run starts at (section 36)
    uint8_t frames = 1;                ///< 1-16, the run Generate writes; first + frames <= 16
    uint8_t seed = 1;                  ///< the Noise source, so a run is repeatable
};

struct Frame { std::array<uint8_t, 32> s{}; };   ///< 32 samples, 0-15
struct Wave {
    bool        used = false;
    std::string name;
    std::vector<Frame> frames;       ///< 1-16
    Synth       synth;               ///< what generated the run, when it was generated (section 33)
};

/// The run a wave instrument walks (section 65): `frameLength` frames spread
/// evenly across the wave's own, 0 (or a length past them) meaning every one.
/// `n` is how many frames the wave has. Writes the frame indices into `out` and
/// returns how many steps the run has, at least one.
inline int waveRun(int n, int frameLength, uint8_t* out)
{
    const int frames = n < 1 ? 1 : (n > 16 ? 16 : n);
    int len = frameLength <= 0 || frameLength > frames ? frames : frameLength;
    if (len < 1) len = 1;
    for (int i = 0; i < len; ++i) {
        const int f = len == 1 ? 0 : (i * frames) / (len - 1);
        out[i] = uint8_t(f < frames - 1 ? f : frames - 1);
    }
    return len;
}

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
/// One cycle of audio, `n` samples in -1..1, as a frame (section 40): the
/// mean removed, box-filtered onto the 32 samples (a shorter input is held),
/// peak-normalised and rounded to the sixteen levels -- no dither, a wave is
/// a shape. Silence, or nothing, is the middle level.
Frame frameFromCycle(const float* x, size_t n);

/// Wave-channel period register for a sample rate, and back (section 9.8).
uint16_t periodForSampleRate(double hz);
double   sampleRateForPeriod(uint16_t period);

} // namespace chipboy::bank
