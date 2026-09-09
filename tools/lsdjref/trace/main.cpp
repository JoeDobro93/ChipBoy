// lsdjref-trace -- what the real LSDj writes to the APU.
//
// Boots an LSDj ROM the user owns in SameBoy's core with a save file this
// harness authored, scripts the joypad to start playback, and logs every
// write to FF04-FF07 (the timer, which is LSDj's tick) and FF10-FF3F (the
// APU and wave RAM) with a cycle stamp, as CSV.
//
// The ROM never enters the repository and nothing of its contents leaves this
// tool: the output is a list of register writes, which is a measurement of
// behaviour, not a copy of code or data (docs/COMMANDS_AND_TEMPO.md 31).
//
//   lsdjref-trace --rom ROM [--sav SAV] [--model dmg|cgb] [--frames N]
//                 [--boot-frames N] [--keys SCRIPT] [--bootrom-dir DIR]
//                 [--out FILE] [--init-sav FILE] [--probe]
//
// The ROM path also comes from CHIPBOY_LSDJ_ROM. With no ROM the tool prints
// why and exits 77, which CTest reads as a skip.
//
// --init-sav boots with no save at all, lets LSDj format its own SRAM, and
// writes the battery out: that file is the canonical empty save for this exact
// ROM build, and the save authoring tool patches songs into a copy of it
// rather than guessing at defaults.
//
// The key script is `frame:key[:hold]`, comma separated, e.g.
// "300:start" presses START at frame 300 for the default six frames.
extern "C" {
#include "gb.h"
uint64_t lsdjref_ticks_8mhz(GB_gameboy_t* gb);
uint8_t  lsdjref_io(GB_gameboy_t* gb, uint8_t low);
uint8_t  lsdjref_volume(GB_gameboy_t* gb, int channel);
}

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr int kSkipExit = 77;          // CTest's SKIP_RETURN_CODE
constexpr uint32_t kCpuHz = 4194304u;  // the same clock ChipBoy counts in

struct Write {
    uint64_t cycle;   ///< CPU cycles at 4.194304 MHz since reset
    uint16_t addr;
    uint8_t  value;
    uint8_t  vol;     ///< the APU's volume on the channel the write names, after it
};

struct KeyPress {
    int64_t frame = 0;
    GB_key_t key = GB_KEY_START;
    int hold = 6;
};

/// Everything the write callback needs; SameBoy's callback carries only the
/// GB_gameboy_t, so the tool keeps one of these and finds it from the gb
/// pointer it was set up with.
struct Trace {
    GB_gameboy_t* gb = nullptr;
    std::vector<Write> writes;
    bool probe = false;
    int64_t frame = 0;
    int pending = -1;   ///< a write whose channel volume is still to be read
} g_trace;

bool logged(uint16_t addr)
{
    // The timer -- LSDj's tick comes from it -- and the whole sound block
    // including wave RAM.
    return (addr >= 0xFF04 && addr <= 0xFF07) || (addr >= 0xFF10 && addr <= 0xFF3F);
}

/// Which hardware channel a register belongs to, or -1.
int channelOf(uint16_t addr)
{
    if (addr >= 0xFF10 && addr <= 0xFF14) return 0;
    if (addr >= 0xFF16 && addr <= 0xFF19) return 1;
    if (addr >= 0xFF1A && addr <= 0xFF1E) return 2;
    if (addr >= 0xFF20 && addr <= 0xFF23) return 3;
    return -1;
}

/// The callback runs before SameBoy applies the write, so the volume a write
/// leaves behind is read at the next one -- or at the frame boundary, for the
/// last of a burst. Sixteen cycles apart, nothing else moves a volume in
/// between, so the reading belongs to the write it is filed under.
void settle()
{
    if (g_trace.pending < 0 || g_trace.writes.empty()) return;
    g_trace.writes.back().vol = lsdjref_volume(g_trace.gb, g_trace.pending);
    g_trace.pending = -1;
}

bool onWrite(GB_gameboy_t* gb, uint16_t addr, uint8_t value)
{
    if (logged(addr)) {
        settle();
        g_trace.writes.push_back({ lsdjref_ticks_8mhz(gb) / 2, addr, value, 0 });
        g_trace.pending = channelOf(addr);
    }
    return true;   // never block a write
}

GB_key_t keyFromName(const std::string& n, bool& ok)
{
    ok = true;
    if (n == "right")  return GB_KEY_RIGHT;
    if (n == "left")   return GB_KEY_LEFT;
    if (n == "up")     return GB_KEY_UP;
    if (n == "down")   return GB_KEY_DOWN;
    if (n == "a")      return GB_KEY_A;
    if (n == "b")      return GB_KEY_B;
    if (n == "select") return GB_KEY_SELECT;
    if (n == "start")  return GB_KEY_START;
    ok = false;
    return GB_KEY_START;
}

/// "300:start,320:a:2" -> presses. Returns false and complains on nonsense.
bool parseKeys(const std::string& s, std::vector<KeyPress>& out)
{
    size_t i = 0;
    while (i <= s.size()) {
        const size_t comma = std::min(s.find(',', i), s.size());
        const std::string item = s.substr(i, comma - i);
        i = comma + 1;
        if (item.empty()) { if (comma >= s.size()) break; continue; }

        const size_t c1 = item.find(':');
        if (c1 == std::string::npos) { std::fprintf(stderr, "lsdjref-trace: bad key step '%s'\n", item.c_str()); return false; }
        const size_t c2 = item.find(':', c1 + 1);

        KeyPress p;
        p.frame = std::strtoll(item.substr(0, c1).c_str(), nullptr, 10);
        const std::string name = item.substr(c1 + 1, (c2 == std::string::npos ? item.size() : c2) - c1 - 1);
        bool ok = false;
        p.key = keyFromName(name, ok);
        if (!ok) { std::fprintf(stderr, "lsdjref-trace: unknown key '%s'\n", name.c_str()); return false; }
        if (c2 != std::string::npos) p.hold = std::max(1, atoi(item.c_str() + c2 + 1));
        out.push_back(p);
        if (comma >= s.size()) break;
    }
    std::stable_sort(out.begin(), out.end(), [](const KeyPress& a, const KeyPress& b) { return a.frame < b.frame; });
    return true;
}

bool readFile(const std::string& path, std::vector<uint8_t>& out)
{
    FILE* f = std::fopen(path.c_str(), "rb");
    if (f == nullptr) return false;
    std::fseek(f, 0, SEEK_END);
    const long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n <= 0) { std::fclose(f); return false; }
    out.resize(size_t(n));
    const size_t got = std::fread(out.data(), 1, out.size(), f);
    std::fclose(f);
    return got == out.size();
}

const char* regName(uint16_t a)
{
    switch (a) {
        case 0xFF04: return "DIV";  case 0xFF05: return "TIMA"; case 0xFF06: return "TMA";  case 0xFF07: return "TAC";
        case 0xFF10: return "NR10"; case 0xFF11: return "NR11"; case 0xFF12: return "NR12"; case 0xFF13: return "NR13"; case 0xFF14: return "NR14";
        case 0xFF16: return "NR21"; case 0xFF17: return "NR22"; case 0xFF18: return "NR23"; case 0xFF19: return "NR24";
        case 0xFF1A: return "NR30"; case 0xFF1B: return "NR31"; case 0xFF1C: return "NR32"; case 0xFF1D: return "NR33"; case 0xFF1E: return "NR34";
        case 0xFF20: return "NR41"; case 0xFF21: return "NR42"; case 0xFF22: return "NR43"; case 0xFF23: return "NR44";
        case 0xFF24: return "NR50"; case 0xFF25: return "NR51"; case 0xFF26: return "NR52";
        default: return (a >= 0xFF30 && a <= 0xFF3F) ? "WAVE" : "?";
    }
}

/// The screen dump is greyscale: what matters is which LSDj screen is up.
uint32_t encodeGrey(GB_gameboy_t*, uint8_t r, uint8_t g, uint8_t b)
{
    return uint32_t((r * 77 + g * 151 + b * 28) >> 8) & 0xFFu;
}

/// A binary PGM, so the tool needs no image library; PIL or ImageMagick turns
/// it into something viewable.
bool writePgm(const std::string& path, const std::vector<uint32_t>& px)
{
    FILE* f = std::fopen(path.c_str(), "wb");
    if (f == nullptr) return false;
    std::fprintf(f, "P5\n160 144\n255\n");
    for (uint32_t v : px) { const uint8_t b = uint8_t(v & 0xFFu); std::fwrite(&b, 1, 1, f); }
    std::fclose(f);
    return true;
}

void usage()
{
    std::fprintf(stderr,
        "lsdjref-trace --rom ROM [--sav SAV] [--model dmg|cgb] [--frames N]\n"
        "              [--boot-frames N] [--keys F:KEY[:HOLD],...] [--bootrom-dir DIR]\n"
        "              [--out FILE] [--init-sav FILE] [--screen FILE.pgm] [--probe]\n");
}

} // namespace

int main(int argc, char** argv)
{
    std::string rom, sav, out, initSav, bootDir, keys, screen;
    std::string model = "dmg";
    int64_t frames = 600;
    int64_t bootFrames = -1;      // -1: press nothing extra, use --keys
    bool probe = false;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](const char* what) -> std::string {
            if (i + 1 >= argc) { std::fprintf(stderr, "lsdjref-trace: %s needs a value\n", what); std::exit(2); }
            return argv[++i];
        };
        if      (a == "--rom")         rom = next("--rom");
        else if (a == "--sav")         sav = next("--sav");
        else if (a == "--out")         out = next("--out");
        else if (a == "--init-sav")    initSav = next("--init-sav");
        else if (a == "--bootrom-dir") bootDir = next("--bootrom-dir");
        else if (a == "--keys")        keys = next("--keys");
        else if (a == "--model")       model = next("--model");
        else if (a == "--frames")      frames = std::strtoll(next("--frames").c_str(), nullptr, 10);
        else if (a == "--boot-frames") bootFrames = std::strtoll(next("--boot-frames").c_str(), nullptr, 10);
        else if (a == "--screen")      screen = next("--screen");
        else if (a == "--probe")       probe = true;
        else if (a == "-h" || a == "--help") { usage(); return 0; }
        else { std::fprintf(stderr, "lsdjref-trace: unknown option %s\n", a.c_str()); usage(); return 2; }
    }

    if (rom.empty()) {
        if (const char* env = std::getenv("CHIPBOY_LSDJ_ROM")) rom = env;
    }
    if (bootDir.empty()) {
        if (const char* env = std::getenv("CHIPBOY_LSDJREF_BOOTROM_DIR")) bootDir = env;
#ifdef CHIPBOY_LSDJREF_BOOTROM_DIR
        if (bootDir.empty()) bootDir = CHIPBOY_LSDJREF_BOOTROM_DIR;
#endif
    }

    std::vector<uint8_t> romBytes;
    if (rom.empty() || !readFile(rom, romBytes)) {
        std::fprintf(stderr, "lsdjref-trace: no LSDj ROM (--rom, or CHIPBOY_LSDJ_ROM); skipping\n");
        return kSkipExit;
    }
    const bool cgb = (model == "cgb");
    const std::string bootPath = bootDir + (cgb ? "/cgb_boot.bin" : "/dmg_boot.bin");
    std::vector<uint8_t> bootBytes;
    if (bootDir.empty() || !readFile(bootPath, bootBytes)) {
        std::fprintf(stderr, "lsdjref-trace: no boot ROM at '%s' (build SameBoy's with RGBDS); skipping\n", bootPath.c_str());
        return kSkipExit;
    }

    std::vector<KeyPress> presses;
    if (!keys.empty() && !parseKeys(keys, presses)) return 2;
    if (bootFrames >= 0) presses.push_back({ bootFrames, GB_KEY_START, 6 });
    std::stable_sort(presses.begin(), presses.end(), [](const KeyPress& a, const KeyPress& b) { return a.frame < b.frame; });

    GB_gameboy_t gb;
    GB_init(&gb, cgb ? GB_MODEL_CGB_E : GB_MODEL_DMG_B);
    GB_load_boot_rom_from_buffer(&gb, bootBytes.data(), bootBytes.size());
    GB_load_rom_from_buffer(&gb, romBytes.data(), romBytes.size());
    if (!sav.empty() && GB_load_battery(&gb, sav.c_str()) != 0) {
        std::fprintf(stderr, "lsdjref-trace: cannot read save '%s'\n", sav.c_str());
        GB_free(&gb);
        return 2;
    }
    // The pixels are not the measurement; they are only how a human works out
    // which screen LSDj is on when a key script needs writing.
    std::vector<uint32_t> pixels;
    if (screen.empty()) {
        GB_set_rendering_disabled(&gb, true);
    }
    else {
        pixels.assign(160 * 144, 0);
        GB_set_rgb_encode_callback(&gb, encodeGrey);
        GB_set_pixels_output(&gb, pixels.data());
    }
    GB_set_sample_rate(&gb, 0);             // nor is the audio: SameBoy's APU state is
    GB_set_turbo_mode(&gb, true, true);

    g_trace.gb = &gb;
    g_trace.probe = probe;
    g_trace.writes.reserve(1u << 18);
    GB_set_write_memory_callback(&gb, onWrite);

    // --- run ------------------------------------------------------------
    size_t nextPress = 0;
    std::vector<std::pair<int64_t, GB_key_t>> releases;   // (frame, key)
    for (int64_t f = 0; f < frames; ++f) {
        g_trace.frame = f;
        while (nextPress < presses.size() && presses[nextPress].frame == f) {
            GB_set_key_state(&gb, presses[nextPress].key, true);
            releases.push_back({ f + presses[nextPress].hold, presses[nextPress].key });
            ++nextPress;
        }
        for (size_t i = 0; i < releases.size();) {
            if (releases[i].first == f) { GB_set_key_state(&gb, releases[i].second, false); releases.erase(releases.begin() + long(i)); }
            else ++i;
        }
        if (probe) {
            const size_t before = g_trace.writes.size();
            GB_run_frame(&gb);
            settle();
            std::printf("frame %5lld  writes %6zu (+%zu)  NR52=%02X NR51=%02X TAC=%02X TMA=%02X\n",
                        (long long) f, g_trace.writes.size(), g_trace.writes.size() - before,
                        lsdjref_io(&gb, 0x26), lsdjref_io(&gb, 0x25), lsdjref_io(&gb, 0x07), lsdjref_io(&gb, 0x06));
        }
        else {
            GB_run_frame(&gb);
        }
        settle();
    }

    if (!screen.empty() && !writePgm(screen, pixels))
        std::fprintf(stderr, "lsdjref-trace: cannot write '%s'\n", screen.c_str());

    if (!initSav.empty()) {
        if (GB_save_battery(&gb, initSav.c_str()) != 0) {
            std::fprintf(stderr, "lsdjref-trace: cannot write '%s'\n", initSav.c_str());
            GB_free(&gb);
            return 2;
        }
        std::fprintf(stderr, "lsdjref-trace: wrote the initialised save to %s\n", initSav.c_str());
    }

    // --- the log --------------------------------------------------------
    FILE* o = out.empty() ? stdout : std::fopen(out.c_str(), "wb");
    if (o == nullptr) { std::fprintf(stderr, "lsdjref-trace: cannot write '%s'\n", out.c_str()); GB_free(&gb); return 2; }
    std::fprintf(o, "# lsdjref-trace 1\n# model=%s frames=%lld cpu_hz=%u\n# sav=%s\n",
                 cgb ? "cgb" : "dmg", (long long) frames, kCpuHz, sav.empty() ? "(none)" : sav.c_str());
    std::fprintf(o, "cycle,addr,name,value,vol\n");
    for (const auto& w : g_trace.writes)
        std::fprintf(o, "%llu,%04X,%s,%02X,%d\n", (unsigned long long) w.cycle, w.addr, regName(w.addr), w.value, int(w.vol));
    if (o != stdout) std::fclose(o);
    std::fprintf(stderr, "lsdjref-trace: %zu writes over %lld frames\n", g_trace.writes.size(), (long long) frames);

    GB_free(&gb);
    return g_trace.writes.empty() ? 1 : 0;
}
