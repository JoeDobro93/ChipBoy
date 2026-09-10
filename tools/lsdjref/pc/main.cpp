// lsdjref-pc: the trace tool plus **the PC and ROM bank of every write**, and
// a --watch range so a work-RAM address can be followed as well as the APU.
//
// It answers "which code wrote that", which is the question that turns a sweep
// of register values into a law. Rule L3 allows deriving behaviour from the ROM
// (docs/CHIPBOY_SPEC.md 3.3); nothing of the ROM is committed, and this tool
// reads one the user owns from outside the tree.
//
//   lsdjref_pc --rom ROM --bootrom-dir DIR --sav SAV --frames N [--keys FRAME]
//              [--watch ADDR[-ADDR]] --out FILE.csv
//
// `--keys 180` presses START at frame 180, which is what run.py does. Columns
// are cycle,addr,value,pc,bank. Feed an address it names to lsdjref_dis.py.
//
// Worked example -- how S on PU1 was settled (COMMANDS_AND_TEMPO.md 72):
//   1. trace a probe with an S23 in a phrase and grep the NR10 write: it comes
//      from bank 02:$6051, which is the note refresh, not the handler;
//   2. read that with lsdjref_dis.py and see `ld a,[$C2E4]; cpl; ldh [NR10],a`;
//   3. re-trace with `--watch C2E4`: the writes come from bank 02:$4832 and
//      $483C;
//   4. disassemble there and the accumulate is twelve instructions of plain
//      arithmetic.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <fstream>
extern "C" {
#include "gb.h"
uint64_t px_ticks(GB_gameboy_t*); uint16_t px_pc(GB_gameboy_t*);
uint8_t px_bank(GB_gameboy_t*);   uint8_t px_io(GB_gameboy_t*, uint8_t);
}
struct W { uint64_t cyc; uint16_t addr; uint8_t val; uint16_t pc; uint8_t bank; };
static std::vector<W> g_w; static GB_gameboy_t* g_gb;
static int g_watchLo = -1, g_watchHi = -1;
static bool onWrite(GB_gameboy_t*, uint16_t a, uint8_t v)
{
    if ((a >= 0xFF04 && a <= 0xFF07) || (a >= 0xFF10 && a <= 0xFF3F)
        || (g_watchLo >= 0 && a >= g_watchLo && a <= g_watchHi))
        g_w.push_back({ px_ticks(g_gb) / 2, a, v, px_pc(g_gb), px_bank(g_gb) });
    return true;
}
static bool readFile(const std::string& p, std::vector<uint8_t>& out)
{
    std::ifstream f(p, std::ios::binary); if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()); return true;
}
static uint32_t encodeGrey(GB_gameboy_t*, uint8_t r, uint8_t g, uint8_t b){ return uint32_t((r*77+g*151+b*28)>>8); }
int main(int argc, char** argv)
{
    std::string rom, sav, out, boot, keys; long frames = 420;
    for (int i = 1; i < argc; ++i) { std::string a = argv[i];
        auto nx = [&]{ return std::string(argv[++i]); };
        if (a=="--rom") rom=nx(); else if (a=="--sav") sav=nx(); else if (a=="--out") out=nx();
        else if (a=="--bootrom-dir") boot=nx(); else if (a=="--keys") keys=nx();
        else if (a=="--frames") frames=atol(nx().c_str());
        else if (a=="--watch") { std::string w = nx(); size_t d = w.find('-');
            g_watchLo = (int) strtol(w.substr(0, d).c_str(), 0, 16);
            g_watchHi = d == std::string::npos ? g_watchLo : (int) strtol(w.substr(d+1).c_str(), 0, 16); } }
    std::vector<uint8_t> rb, bb;
    if (!readFile(rom, rb) || !readFile(boot + "/dmg_boot.bin", bb)) { std::fprintf(stderr, "missing rom/boot\n"); return 2; }
    GB_gameboy_t gb; GB_init(&gb, GB_MODEL_DMG_B); g_gb = &gb;
    GB_load_boot_rom_from_buffer(&gb, bb.data(), bb.size());
    GB_load_rom_from_buffer(&gb, rb.data(), rb.size());
    if (!sav.empty()) GB_load_battery(&gb, sav.c_str());
    GB_set_rendering_disabled(&gb, true); GB_set_sample_rate(&gb, 0);
    GB_set_turbo_mode(&gb, true, true); GB_set_write_memory_callback(&gb, onWrite);
    (void) encodeGrey;
    long keyFrame = keys.empty() ? -1 : atol(keys.c_str());
    for (long f = 0; f < frames; ++f) {
        if (f == keyFrame) GB_set_key_state(&gb, GB_KEY_START, true);
        if (f == keyFrame + 6) GB_set_key_state(&gb, GB_KEY_START, false);
        GB_run_frame(&gb);
    }
    FILE* o = out.empty() ? stdout : fopen(out.c_str(), "wb");
    fprintf(o, "cycle,addr,value,pc,bank\n");
    for (auto& w : g_w) fprintf(o, "%llu,%04X,%02X,%04X,%02X\n",
        (unsigned long long) w.cyc, w.addr, w.val, w.pc, w.bank);
    if (o != stdout) fclose(o);
    GB_free(&gb); return 0;
}
