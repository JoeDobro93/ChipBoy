// lsdjref-pc: the three things it needs from inside SameBoy's core that the
// trace tool's own shim does not expose -- the program counter and the ROM
// bank -- compiled the way the core is, as trace/shim.c is.
#define GB_INTERNAL
#include "gb.h"
#include <stdint.h>
uint64_t px_ticks(GB_gameboy_t *gb){ return gb->absolute_debugger_ticks; }
uint16_t px_pc(GB_gameboy_t *gb){ return gb->pc; }
uint8_t  px_bank(GB_gameboy_t *gb){ return (uint8_t)gb->mbc_rom_bank; }
uint8_t  px_io(GB_gameboy_t *gb, uint8_t low){ return gb->io_registers[low]; }
