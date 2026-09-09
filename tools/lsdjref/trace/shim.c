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
