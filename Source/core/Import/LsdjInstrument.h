// ChipBoy -- LSDj's sixteen instrument bytes read as a channel reads them
// (docs/COMMANDS_AND_TEMPO.md section 197, docs/plan-any-channel.md).
// Core: <std> and core/ only.
#pragma once

#include "core/Bank/Bank.h"
#include "core/Import/LsdjModel.h"

#include <cstdint>
#include <string>

namespace chipboy::lsdj {

struct ImportNotes;

constexpr double kPitchClockMs = 11712.0 * 1000.0 / 4194304.0;   // 2.7924 ms

inline int signedByte(int b) { return b >= 128 ? b - 256 : b; }
inline std::string hex2(int v) { static const char* d = "0123456789ABCDEF"; std::string s; s += d[(v >> 4) & 15]; s += d[v & 15]; return s; }
inline bank::PitchSpeed pitchSpeedOf(uint8_t b5)
{
    return (b5 & 0x80) ? bank::PitchSpeed::Step : (b5 & 0x40) ? bank::PitchSpeed::Drum : (b5 & 0x10) ? bank::PitchSpeed::Tick : bank::PitchSpeed::Fast;
}
/// Section 103: LSDj synth k is ChipBoy wave slot k + 1.
inline int waveSlotFor(int synth) { return (synth & (bank::kWaveSlots - 1)) + 1; }

/// The sixteen bytes of LSDj instrument read as `kind` -- 0 pulse, 1 wave, 2 kit
/// (the shared fields only: a kit's samples are the importer's, they need the
/// ROM), 3 noise -- under the model's laws, into ChipBoy's fields. `bank`
/// supplies the noise map (section 81), `tickMs` the tempo the shaped picture
/// of a software-stage envelope is drawn at, `notes` may be null. The
/// importer reads every instrument through this; the driver reads an
/// instrument a channel of another kind names (section 197).
bool decodeInstrumentBytes(const uint8_t* b, int kind, const LsdjModel& m, const bank::Bank& bank, double tickMs,
                           bank::InstrumentCore& o, ImportNotes* notes, const std::string& name);

/// The inverse for a ChipBoy instrument, in the 9.4.2 layout: what LSDj would
/// hold for it, lossy where ChipBoy has more than LSDj.
void encodeInstrumentBytes(const bank::InstrumentCore& i, uint8_t* out);

} // namespace chipboy::lsdj
