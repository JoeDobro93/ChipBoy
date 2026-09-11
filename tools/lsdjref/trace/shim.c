// lsdjref -- the two things the trace tool needs from inside SameBoy's core.
//
// SameBoy hides GB_gameboy_t's fields unless GB_INTERNAL is defined, and that
// header is GNU C, not C++. So this one C file is compiled the way the core
// is and hands the tool a cycle stamp and an I/O register read; nothing else
// of the emulator's insides crosses into C++.
#define GB_INTERNAL
#include "gb.h"

#include <stdint.h>

/// The master clock since reset, in 8 MHz ticks -- twice the 4.194304 MHz CPU
/// cycle ChipBoy counts in. SameBoy keeps it for its debugger's `ticks`
/// command; it is the only monotonic cycle counter the core exposes.
uint64_t lsdjref_ticks_8mhz(GB_gameboy_t *gb)
{
    return gb->absolute_debugger_ticks;
}

/// One I/O register straight out of the shadow file, without the read side
/// effects GB_read_memory would have.
uint8_t lsdjref_io(GB_gameboy_t *gb, uint8_t low)
{
    return gb->io_registers[low];
}

/// The APU's own idea of a channel's volume, after whatever the last NRx2
/// write did to it. Channels 0 and 1 are the pulses, 3 is the noise; the wave
/// channel has a level, not a volume, so it reads back as its NR32 shift.
uint8_t lsdjref_volume(GB_gameboy_t *gb, int channel)
{
    switch (channel) {
        case 0: return gb->apu.square_channels[0].current_volume;
        case 1: return gb->apu.square_channels[1].current_volume;
        case 3: return gb->apu.noise_channel.current_volume;
        default: return (gb->io_registers[GB_IO_NR32] >> 5) & 3;
    }
}

/// The wave channel's own position: which of the thirty-two nibbles it is
/// sounding, and the byte the pair of them came from. This is the only way to
/// see the order the DAC reads wave RAM in without inferring it from audio.
void lsdjref_wave_state(GB_gameboy_t *gb, uint8_t *index, uint8_t *byte)
{
    *index = gb->apu.wave_channel.current_sample_index;
    *byte  = gb->apu.wave_channel.current_sample_byte;
}

/// A pulse channel's position in its duty cycle, 0-7, and the duty NRx1
/// selects. Together with the rendered sample these say which way that
/// channel's DAC runs, which is the only way to tell a global polarity
/// convention from the wave channel being inverted on its own.
void lsdjref_pulse_state(GB_gameboy_t *gb, int channel, uint8_t *pos, uint8_t *duty)
{
    *pos  = gb->apu.square_channels[channel].current_sample_index & 7;
    *duty = (gb->io_registers[channel ? GB_IO_NR21 : GB_IO_NR11] >> 6) & 3;
}
