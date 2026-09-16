// ChipBoy -- LSDj's instrument bytes as a channel reads them (section 197).
#include "core/Import/LsdjInstrument.h"
#include "core/Import/LsdjSong.h"

#include <algorithm>
#include <cmath>

namespace chipboy::lsdj {

namespace {
/// The envelope laws (sections 51, 58, 164 and 189), as the interpreter had them.
struct EnvLaw {
    const LsdjModel& m;
    double tickMs;
    // --- envelope (sections 51 and 58) ------------------------------------
    /// The ticks a stage costs to walk `delta` levels at `speed`: the measured
    /// period table on the software stages, the chip's own (period / 64) of a
    /// second a level on the hardware ones (section 58).
    int envTicks(int delta, int speed) const
    {
        if (speed == 0 || delta == 0) return 0;
        const double perLevelMs = m.envPeriods != nullptr ? double(m.envPeriods[size_t(speed & 15)]) * kPitchClockMs
                                                          : double(speed & 7) * 1000.0 / 64.0;
        const double ms = std::abs(delta) * perLevelMs;
        return std::clamp(int(std::lround(ms / tickMs)), 1, 255);
    }
    /// Section 132: a stage's length carried to 1/256 of a tick. §121 gave the
    /// bank and the driver the fine byte; the importer was still rounding each
    /// stage to a whole tick, which stretched `READROOM`'s instrument 09 from
    /// 75.4 ms a level to 76.7 and slid every level after the first.
    void envStage(uint8_t& ticks, uint8_t& fine, int delta, int speed) const
    {
        ticks = 0; fine = 0;
        if (speed == 0 || delta == 0) return;
        const double perLevelMs = m.envPeriods != nullptr ? double(m.envPeriods[size_t(speed & 15)]) * kPitchClockMs
                                                          : double(speed & 7) * 1000.0 / 64.0;
        const double t = std::abs(delta) * perLevelMs / tickMs;
        const int whole = std::clamp(int(t), 0, 255);
        int frac = int(std::lround((t - double(whole)) * 256.0));
        ticks = uint8_t(whole); fine = uint8_t(std::clamp(frac, 0, 255));
    }
    void envelope(const uint8_t* b, bank::InstrumentCore& o)
    {
        if (m.envelopeLaw == EnvelopeLaw::Chip) {
            // The byte is NRx2: the chip's own envelope, exactly.
            o.env.mode = bank::EnvMode::Chip;
            o.envVol = uint8_t(b[1] >> 4); o.envDir = (b[1] & 8) ? bank::EnvDir::Up : bank::EnvDir::Down; o.envRate = uint8_t(b[1] & 7);
            return;
        }
        const bool hw = m.envelopeLaw == EnvelopeLaw::HardwareStages;
        if (hw) {
            // Section 189: byte 1 is NRx2 and the chip runs it; bytes 9 and 10
            // are the stages the ROM writes with a retrigger, byte for byte.
            o.env.mode = bank::EnvMode::Chip;
            o.envVol = uint8_t(b[1] >> 4); o.envDir = (b[1] & 8) ? bank::EnvDir::Up : bank::EnvDir::Down; o.envRate = uint8_t(b[1] & 7);
            o.envStage2 = b[9]; o.envStage3 = b[9] ? b[10] : uint8_t(0);
            return;
        }
        const int mask = hw ? 7 : 15;                                     // NRx2 keeps the period in three bits
        const int a1 = b[1] >> 4, s1 = b[1] & mask, a2 = b[9] >> 4, s2 = b[9] & mask, a3 = b[10] >> 4, s3 = b[10] & mask;
        // On the chip a stage hands over only when its ramp can reach the next
        // amplitude; a direction that points away from it never arrives, and
        // the note holds where it is (section 58).
        const auto reaches = [hw](uint8_t from, int to, int speed) {
            if (speed == 0) return false;
            if (!hw) return true;
            return (from & 8) ? to > (from >> 4) : to < (from >> 4);
        };
        o.envVol = uint8_t(a1);
        if (!reaches(b[1], a2, s1)) {   // a held level: the chip's own envelope says it best
            o.env.mode = bank::EnvMode::Chip;
            o.envDir = (b[1] & 8) ? bank::EnvDir::Up : bank::EnvDir::Down;
            o.envRate = uint8_t(hw ? s1 : 0);
            return;
        }
        o.env.mode = bank::EnvMode::Shaped;
        // Section 164: the software machine runs the ROM's bytes as they are;
        // the shaped stages below are its picture for the Instrument tab.
        if (!hw) { o.env.lsdj = true; o.env.lsdjByte1 = b[1]; o.env.lsdjByte9 = b[9]; o.env.lsdjByte10 = b[10]; }
        o.env.start = uint8_t(a1); envStage(o.env.attackTicks, o.env.attackFine, a1 - a2, s1); o.env.peak = uint8_t(a2); o.env.releaseTicks = 0;
        if (!reaches(b[9], a3, s2)) { o.env.decayTicks = 0; o.env.decayFine = 0; o.env.sustain = uint8_t(a2); o.env.fadeTicks = 0; o.env.fadeFine = 0; }
        else {
            envStage(o.env.decayTicks, o.env.decayFine, a2 - a3, s2); o.env.sustain = uint8_t(a3);
            if (s3 && !hw) { envStage(o.env.fadeTicks, o.env.fadeFine, a3, s3); o.env.fadeTo = 0; }
        }
        // Sections 116 and 121: the levels are walked on the pitch clock and the
        // stages carry their fraction of a tick, so nothing is rounded away and
        // there is nothing left to warn about.
    }

};
} // namespace

bool decodeInstrumentBytes(const uint8_t* b, int t, const LsdjModel& m, const bank::Bank& bank, double tickMs,
                           bank::InstrumentCore& o, ImportNotes* notes, const std::string& name)
{
    if (t < 0 || t > 3) return false;
    const auto type = t == 0 ? bank::InstrumentType::Pulse : t == 1 ? bank::InstrumentType::Wave : t == 2 ? bank::InstrumentType::Kit : bank::InstrumentType::Noise;
    o = static_cast<const bank::InstrumentCore&>(bank::Instrument::defaults(type));
    o.pan = bank::Pan(b[7] & 3); o.length = 0; o.noteOff = bank::NoteOff::Kill;
    o.cmdRate = uint8_t(b[8] & 15); o.chordRate = o.cmdRate;
    o.tableMode = (b[5] & 0x08) ? bank::TableMode::Step : bank::TableMode::Tick;
    o.vib.shape = bank::VibShape((b[5] >> 1) & 3);   // section 114: 3 is off
    o.vib.dir = (b[5] & 1) ? bank::VibDir::Up : bank::VibDir::Down;
    o.vib.speed = 8; o.vib.depth = 0; o.vib.delay = 0;
    o.table = (b[6] & 0x20) ? uint8_t((b[6] & 0x1F) + 1) : uint8_t(0);
    o.transpose = !(b[5] & 0x20);
    // Before 8.8 an E writes NRx2 and triggers (section 59). The
    // levels come at the same interval either way -- 9.3.9 steps them
    // itself at the chip's own rate (section 70).
    o.envRetrig = m.envelopeLaw != EnvelopeLaw::SoftwareStages;
    // Section 195: before 9.4.0 a roll leaves a DRUM pitch running.
    o.retrigKeepsPitch = !m.retrigResetsDrumPitch;
    o.vibScale = m.vibScale;                                              // section 210
    // Section 81: a noise instrument reads LSDj's own table straight off
    // the bank, so the cell's note is LSDj's note and the byte the driver
    // writes is the byte the ROM writes.
    if (type == bank::InstrumentType::Noise && m.noiseRule == NoiseRule::Map && bank.noiseMapSet) {
        o.noiseLsdjMap = true;
        // The table's entries are whole NR43 bytes, so nothing may be
        // added to the shift afterwards: Shift stays at its neutral 5.
        o.noiseShift = 5;
    }
    if (t == 0 || t == 3) EnvLaw{ m, tickMs }.envelope(b, o);
    if (t == 0) {
        o.duty = uint8_t(b[7] >> 6); o.dutySeqLen = 0; o.pitchSpeed = m.pitchLaw == PitchLaw::Register ? bank::PitchSpeed::Drum : pitchSpeedOf(b[5]);
        // Section 134: a pulse instrument's LENGTH, which was never read
        // at all. Byte 3's low six bits are NR11's length code and bit 6
        // enables the counter -- a short length with the bit set is what
        // makes `READROOM`'s `R` rolls stutter instead of ringing on.
        if (b[3]) { o.length = uint16_t(64 - int(b[3] & 63)); o.lengthLatent = (b[3] & 0x40) == 0; }
        o.pitchRegisterUnits = m.pitchLaw == PitchLaw::Register;      // section 88
        const int nr10 = (~b[4]) & 0xFF;
        o.sweepRate = uint8_t((nr10 >> 4) & 7); o.sweepDown = (nr10 & 8) != 0; o.sweepShift = uint8_t(nr10 & 7);
        // Section 49: a whole signed byte of semitones, which ChipBoy's
        // own pu2Transpose is too -- carried exactly, so no note unless
        // it takes a note off the keyboard (pu2TransposeRange()).
        if (m.pu2Transpose && b[2]) o.pu2Transpose = int8_t(signedByte(b[2]));
        // Section 191: before 9.x the nibble in byte 7 bits 2-5, v/32 of a
        // semitone down -- 9.4.2's `F 0v`, so eight times it in byte 11's units.
        // Section 196: 3.6.8 - 5.0.3 read the nibble as period units -- about
        // 42/256 of a semitone each at the middle of the keyboard, capped at a
        // semitone (the closest the 9.x byte comes; a whole nibble is far past it).
        if (m.fineTuneNibble) { const int v = (b[7] >> 2) & 15; o.fineTune = uint8_t(m.fineTuneUnits ? std::min(255, int(std::lround(v * 42.2))) : v * 8); }
        else if (m.formatVersion >= 15) o.fineTune = b[11];   // section 112; section 208: unread before 8.8.6 (3.1.5 - 3.5.1 have none)
    } else if (t == 1) {
        static const uint8_t kLevel[4] = { 0, 3, 2, 1 };       // the stored bits are the NR32 code, 1 = 100 %
        o.waveLevel = kLevel[(b[1] >> 5) & 3];
        // The synth byte is 2 before 9.x and 3 after (section 60); its low
        // nibble is LSDj's LOOP POS, not a start frame (section 65).
        const uint8_t wb = b[size_t(m.waveByte == 3 ? 3 : 2)];
        const int synth = wb >> 4;
        // Section 93: REPEAT is a byte of its own. Both bytes carry the
        // synth in their high nibble, so reading the run's loop point
        // off the synth byte was right only for formats 9 to 15.
        const int loopPos = b[size_t(m.waveRepeatByte == 3 ? 3 : 2)] & 15;
        o.wave = uint8_t(waveSlotFor(synth));
        // Section 171: on the 9.x layout byte 3 is the frame index whole,
        // synth above and the start frame below; the ROM adds the run's
        // steps to it unwrapped, and in MANUAL it is the frame that plays.
        o.frameStart = uint8_t(m.waveByte == 3 ? (wb & 15) : 0);
        // Section 170: the wave FINETUNE, a signed byte of 1/256 semitones.
        if (m.waveFineTuneByte >= 0) o.fineTune = b[size_t(m.waveFineTuneByte)];
        o.pitchSpeed = m.pitchLaw == PitchLaw::Register ? bank::PitchSpeed::Drum : pitchSpeedOf(b[5]);
        o.pitchRegisterUnits = m.pitchLaw == PitchLaw::Register;      // section 88
        // docs/LSDJ_VERSIONS.md: a wave instrument walks a run of frames
        // only from format 7. Before that it loads frame 0 and holds it,
        // and bytes 9, 10 and 11 mean something else -- reading them as
        // the run gives an old song a frame run it never had, which is
        // heard as the wave channel retriggering two or three times a
        // step.
        if (!m.waveFrameRun) {
            // Sections 200 and 201: no LENGTH or SPEED, but byte 9's low two bits
            // are PLAY (ONCE 0, LOOP 1, PINGPONG 2, MANUAL 3) and the REPEAT
            // nibble the loop, counted from the run's end. The run is the one
            // frame at a tick a step until a W lengthens it; ONCE plays that
            // frame a tick and the channel goes quiet.
            o.frameLength = 1; o.frameAdvance = 1;
            // Section 211: the loop is counted from the run's end, the nibble
            // the steps before the last (section 205: read from 6.0.1).
            o.frameLoopFromEnd = true; o.frameLoopStep = uint8_t(m.waveRepeatNibble ? loopPos : 0); o.frameLoopEnd = 0;
            switch (b[9] & 3) {
                case 0: o.frameLoop = bank::FrameLoop::Once; break;
                case 1: o.frameLoop = bank::FrameLoop::Loop; break;
                case 2: o.frameLoop = bank::FrameLoop::PingPong; break;
                default: o.frameLoop = bank::FrameLoop::Loop; o.frameAdvance = 0; break;   // MANUAL: only an F moves it
            }
        } else {
            // The run: LENGTH is 16 - the low nibble of byte 10, SPEED is
            // byte 11 and costs four ticks on top, PLAY is byte 9's low two
            // bits, and the loop covers the last 16 - LOOP POS steps.
            const int len = 16 - int(b[10] & 15);
            o.frameLength = uint8_t(len);
            // Section 198: before 7.7.6 the nibble counts the loop's steps less
            // one from the run's end; from 7.7.6 it is the steps before the loop.
            // Section 211: before 7.7.6 the loop is counted from the run's end
            // (the nibble is the steps before the last), so a W that changes the
            // length keeps it at the new end.
            if (m.waveRepeatCount) { o.frameLoopFromEnd = true; o.frameLoopStep = uint8_t(loopPos); }
            else o.frameLoopStep = uint8_t(std::max(0, len - (16 - loopPos)));
            o.frameLoopEnd = 0;
            // Section 171: PLAY is the whole byte -- 4 is 9.2.E's RESYNC,
            // ping-pong with every frame written at its tick.
            // Section 198: before 7.7.6 the low two bits are ONCE 0, LOOP 1,
            // PINGPONG 2, MANUAL 3 (probed on 6.8.2 and 7.0.2).
            const int play = m.wavePlayOld ? (b[9] & 3) + 1 == 4 ? 0 : (b[9] & 3) + 1 : int(b[9]);
            switch (play) {
                case 0: o.frameAdvance = 0; o.frameLoop = bank::FrameLoop::Loop; break;      // MANUAL: only an F moves it
                case 1: o.frameLoop = bank::FrameLoop::Once; break;
                case 3: o.frameLoop = bank::FrameLoop::PingPong; break;
                case 4: o.frameLoop = bank::FrameLoop::Resync; break;
                default: o.frameLoop = bank::FrameLoop::Loop; break;
            }
            // Section 91: SPEED is a **signed** byte and the run advances
            // every `speed + 4` ticks, so FD is one tick and not 255.
            // Every wave instrument in the user's SAMESONG stores a
            // negative speed, which read unsigned froze the run.
            if (play) o.frameAdvance = uint8_t(std::clamp(signedByte(b[11]) + 4, 1, 255));
        }
    } else if (t == 2) {
        return true;                                          // the kit is the importer's: it needs the ROM
    } else {
        o.lfsr7 = false; o.noiseManual = false; o.noiseShift = 5; o.noiseDivisor = 1; o.noiseSweep = 0;
        // Section 66: before 9 the noise commands work on the NR43 byte.
        o.noiseDomain = m.noiseS == NoiseS::Semitones ? bank::NoiseSweepDomain::Notes : bank::NoiseSweepDomain::Register;
        // Section 86 and docs/LSDJ_VERSIONS.md: PITCH. Only 9.2 and
        // later restart the channel on a pitch change at all -- zero is
        // FREE (a restart when the 7-bit LFSR comes on) and anything
        // else SAFE (a restart on every change). Before that the byte
        // is the S CMD / S MODE setting, which clamps the LFSR width
        // during an S command and has no ChipBoy equivalent.
        if (m.noisePitchByte < 0) {
            o.noisePitch = bank::NoisePitch::Never;
            // Section 188: the note picks NR43 from SHAPE (byte 4), S MODE
            // (byte 2, nonzero = STABLE) keeps the width bit through S, P and C.
            if (m.noiseRule == NoiseRule::Shape) {
                o.noiseShapeMode = true; o.noiseShape = b[4]; o.noiseLsdjMap = false;
                // Section 207: the width bit through an S -- never before 4.1.0, byte 2 (S MODE) from 4.1.0.
                o.noiseStable = m.noiseStableRule == NoiseStable::Byte2 && b[2] != 0;
                o.noiseTspNibbles = m.noiseTspNibbles;
            }
            else if (b[2] && notes) notes->add("noise instrument " + name + " has S MODE = STABLE (byte 2 = " + hex2(b[2]) + "), which holds the LFSR width through an S command; ChipBoy has no equivalent and lets S cross it");
        } else {
            o.noisePitch = b[size_t(m.noisePitchByte)] ? bank::NoisePitch::Safe : bank::NoisePitch::Free;
        }
        // Section 134, correcting §87: byte 3's low six bits are the
        // length code and **bit 6 enables the counter** at the note-on.
        // §87 saw only instruments with the bit clear, which is the
        // latent case -- the code sits in NR41 and nothing arms it.
        if (b[3]) { o.length = uint16_t(64 - int(b[3] & 63)); o.lengthLatent = (b[3] & 0x40) == 0; }
    }
    return true;
}

void encodeInstrumentBytes(const bank::InstrumentCore& i, uint8_t* b)
{
    std::fill(b, b + 16, uint8_t(0));
    const bool pulse = i.type == bank::InstrumentType::Pulse, wave = i.type == bank::InstrumentType::Wave, kit = i.type == bank::InstrumentType::Kit, noise = i.type == bank::InstrumentType::Noise;
    b[0] = uint8_t(int(i.type));
    if (pulse || noise) b[1] = uint8_t((i.envVol << 4) | (i.envDir == bank::EnvDir::Up ? 8 : 0) | (i.envRate & 7));
    else { static const uint8_t kCode[4] = { 0, 3, 2, 1 }; b[1] = uint8_t(kCode[i.waveLevel & 3] << 5); }
    if (pulse) b[2] = uint8_t(i.pu2Transpose);
    else if (kit) b[2] = uint8_t((i.kit - 1) & 0x3F);
    else if (noise) b[2] = uint8_t(i.noisePitch == bank::NoisePitch::Free ? 0 : 4);
    if (pulse || noise) b[3] = i.length ? uint8_t(((64 - int(i.length)) & 63) | (i.lengthLatent ? 0 : 0x40)) : uint8_t(0);
    else if (wave) b[3] = uint8_t((((i.wave - 1) & 15) << 4) | (i.frameStart & 15));
    b[4] = 0xFF;
    if (pulse) b[4] = uint8_t(~((i.sweepRate << 4) | (i.sweepDown ? 8 : 0) | (i.sweepShift & 7)) & 0xFF);
    else if (noise && i.noiseShapeMode) b[4] = i.noiseShape;
    b[5] = uint8_t((i.pitchSpeed == bank::PitchSpeed::Step ? 0x80 : 0) | (i.pitchSpeed == bank::PitchSpeed::Drum ? 0x40 : 0) | (i.transpose ? 0 : 0x20)
                   | (i.pitchSpeed == bank::PitchSpeed::Tick ? 0x10 : 0) | (i.tableMode == bank::TableMode::Step ? 0x08 : 0) | ((int(i.vib.shape) & 3) << 1) | (i.vib.dir == bank::VibDir::Up ? 1 : 0));
    b[6] = i.table ? uint8_t(0x20 | ((i.table - 1) & 0x1F)) : uint8_t(0);
    b[7] = uint8_t((int(i.pan) & 3) | (pulse ? (i.duty & 3) << 6 : 0));
    b[8] = uint8_t(i.cmdRate & 15);
    if (pulse) { b[9] = i.envStage2; b[10] = i.envStage3; b[11] = i.fineTune; }
    else if (wave) {
        b[9] = uint8_t(i.frameAdvance == 0 && i.frameLoop == bank::FrameLoop::Loop ? 0 : i.frameLoop == bank::FrameLoop::Once ? 1 : i.frameLoop == bank::FrameLoop::PingPong ? 3 : i.frameLoop == bank::FrameLoop::Resync ? 4 : 2);
        b[10] = uint8_t((16 - (i.frameLength ? i.frameLength : 16)) & 15);
        b[11] = uint8_t(int(i.frameAdvance) - 4);
        b[12] = i.fineTune;
    } else if (kit) { b[9] = 0; b[10] = 0xD0; }
}

} // namespace chipboy::lsdj
