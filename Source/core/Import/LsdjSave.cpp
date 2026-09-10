// ChipBoy -- reading an LSDj .sav (docs/plan-lsdj-import.md section 1).
#include "core/Import/LsdjSave.h"

#include <cstring>

#include <algorithm>
#include <array>

namespace chipboy::lsdj {

namespace {
constexpr size_t kNamesAt = 0x8000, kActiveAt = 0x8140, kAllocAt = 0x8141, kBlock0 = 0x8000, kBlockSize = 0x200;
constexpr size_t kInstAllocAt = 0x2040, kSongRowsAt = 0x1290;
// What the two default codes expand to (liblsdj's constants, confirmed on the
// user's save: every file decompresses to 32768 bytes with them).
constexpr std::array<uint8_t, 16> kDefaultWave = { 0x8E, 0xCD, 0xCC, 0xBB, 0xAA, 0xA9, 0x99, 0x88, 0x87, 0x76, 0x66, 0x55, 0x54, 0x43, 0x32, 0x31 };
constexpr std::array<uint8_t, 16> kDefaultInstrument = { 0xA8, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x03, 0x00, 0x00, 0xD0, 0x00, 0x00, 0x00, 0xF3, 0x00, 0x00 };

std::string nameAt(const uint8_t* data, int file)
{
    std::string s;
    for (int k = 0; k < 8; ++k) {
        const uint8_t c = data[kNamesAt + size_t(file) * 8 + size_t(k)];
        if (c == 0) break;
        s += (c >= 32 && c < 127) ? char(c) : '?';
    }
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}
int blockCountOf(const uint8_t* data, int file)
{
    int n = 0;
    for (int b = 0; b < kBlockCount; ++b) if (data[kAllocAt + size_t(b)] == file) ++n;
    return n;
}
} // namespace

bool indexSave(const uint8_t* data, size_t size, SaveIndex& out, std::string& error)
{
    out = SaveIndex{};
    if (data == nullptr || size < kSaveSize) { error = "not an LSDj save: it should be 131072 bytes"; return false; }
    const uint8_t active = data[kActiveAt];
    out.activeFile = active < kFileSlots ? int(active) : -1;
    out.workingFormat = formatVersionOf(data, kSongSize);
    out.workingUsed = songLooksUsed(data, kSongSize);
    for (int f = 0; f < kFileSlots; ++f) {
        const int blocks = blockCountOf(data, f);
        if (blocks == 0) continue;
        SaveEntry e;
        e.file = f; e.name = nameAt(data, f); e.blocks = blocks; e.active = f == out.activeFile;
        std::vector<uint8_t> song; std::string err;
        e.formatVersion = decompressFile(data, size, f, song, err) ? formatVersionOf(song.data(), song.size()) : -1;
        if (e.name.empty()) e.name = "FILE " + std::to_string(f);
        out.files.push_back(std::move(e));
    }
    return true;
}

/// The run-length stream shared by a save's file and a project file. `block`
/// is the first block's index; `blockAt(b)` its offset in `data`, or a size
/// beyond `size` when `b` is not a block; `sequential` reads a jump code as
/// "the next block" (a project file), else as the block it names (a save).
namespace {

template <typename BlockAt>
bool decodeStream(const uint8_t* data, size_t size, int block, int blockLimit, bool sequential, BlockAt blockAt,
                  std::vector<uint8_t>& song, std::string& error)
{
    song.clear();
    song.reserve(kSongSize);
    int visited = 0;
    size_t i = blockAt(block), end = i + kBlockSize;
    if (end > size) { error = "block " + std::to_string(block) + " is past the end of the file"; return false; }
    for (;;) {
        if (i >= end) { error = "the stream ran past block " + std::to_string(block); return false; }
        if (song.size() > kSongSize) { error = "the file is longer than a song"; return false; }
        const uint8_t c = data[i++];
        if (c == 0xC0) {
            if (i >= end) { error = "a run marker at the end of block " + std::to_string(block); return false; }
            const uint8_t v = data[i++];
            if (v == 0xC0) song.push_back(0xC0);
            else { if (i >= end) { error = "a run without a count"; return false; } const uint8_t n = data[i++]; song.insert(song.end(), size_t(n), v); }
        } else if (c == 0xE0) {
            if (i >= end) { error = "an escape at the end of block " + std::to_string(block); return false; }
            const uint8_t v = data[i++];
            if (v == 0xE0) song.push_back(0xE0);
            else if (v == 0xF0 || v == 0xF1) {
                if (i >= end) { error = "a default without a count"; return false; }
                const uint8_t n = data[i++];
                const auto& def = v == 0xF0 ? kDefaultWave : kDefaultInstrument;
                for (int k = 0; k < int(n); ++k) song.insert(song.end(), def.begin(), def.end());
            }
            else if (v == 0xFF) break;
            else if (v >= 0xF2) { error = "unknown code E0 " + std::to_string(int(v)) + " in block " + std::to_string(block); return false; }
            else {
                block = sequential ? block + 1 : int(v);
                if (block < 0 || block > blockLimit || ++visited > blockLimit + 1) { error = "a jump to block " + std::to_string(block) + " leaves the file"; return false; }
                i = blockAt(block); end = i + kBlockSize;
                if (end > size) { error = "block " + std::to_string(block) + " is past the end of the file"; return false; }
            }
        } else song.push_back(c);
    }
    if (song.size() != kSongSize) { error = "the file decompressed to " + std::to_string(song.size()) + " bytes, not 32768"; return false; }
    return true;
}

} // namespace

bool decompressFile(const uint8_t* data, size_t size, int file, std::vector<uint8_t>& song, std::string& error)
{
    song.clear();
    if (data == nullptr || size < kSaveSize) { error = "not an LSDj save"; return false; }
    if (file < 0 || file >= kFileSlots) { error = "no such file"; return false; }
    int first = -1;
    for (int b = 0; b < kBlockCount && first < 0; ++b) if (data[kAllocAt + size_t(b)] == file) first = b + 1;
    if (first < 0) { error = "the file has no blocks"; return false; }
    return decodeStream(data, size, first, kBlockCount, false,
                        [](int b) { return b <= 0 ? size_t(kSaveSize) : kBlock0 + size_t(b) * kBlockSize; }, song, error);
}

constexpr size_t kProjectHead = 9;   // 8 bytes of name, one version byte

bool looksLikeProject(const uint8_t* data, size_t size)
{
    if (data == nullptr || size < kProjectHead + kBlockSize || size == kSaveSize) return false;
    if ((size - kProjectHead) % kBlockSize != 0) return false;
    for (size_t k = 0; k < 8; ++k) { const uint8_t c = data[k]; if (c != 0 && (c < 32 || c > 126)) return false; }
    return true;
}

bool decompressProject(const uint8_t* data, size_t size, std::string& name, int& version, std::vector<uint8_t>& song, std::string& error)
{
    song.clear(); name.clear(); version = -1;
    if (!looksLikeProject(data, size)) { error = "not an LSDj project file (a name, a version byte, whole blocks)"; return false; }
    for (size_t k = 0; k < 8; ++k) { const uint8_t c = data[k]; if (c == 0) break; name += char(c); }
    while (!name.empty() && name.back() == ' ') name.pop_back();
    version = int(data[8]);
    const int blocks = int((size - kProjectHead) / kBlockSize);
    return decodeStream(data, size, 0, blocks - 1, true, [](int b) { return kProjectHead + size_t(b) * kBlockSize; }, song, error);
}

bool workingSong(const uint8_t* data, size_t size, std::vector<uint8_t>& song)
{
    if (data == nullptr || size < kSongSize) return false;
    song.assign(data, data + kSongSize);
    return true;
}

int formatVersionOf(const uint8_t* song, size_t size)
{
    return song != nullptr && size > kFormatVersionAt ? int(song[kFormatVersionAt]) : -1;
}

bool songLooksUsed(const uint8_t* song, size_t size)
{
    if (song == nullptr || size < kSongSize) return false;
    for (int i = 0; i < 64; ++i) if (song[kInstAllocAt + size_t(i)]) return true;
    for (int ch = 0; ch < 4; ++ch) if (song[kSongRowsAt + size_t(ch)] != 0xFF) return true;
    return false;
}

std::string romVersion(const uint8_t* rom, size_t size)
{
    // The cartridge title at 0x134: "LSDj-v9.3.9". Anything else is not LSDj.
    if (rom == nullptr || size < 0x150) return {};
    std::string title;
    for (size_t k = 0x134; k < 0x144; ++k) { if (rom[k] == 0) break; title += char(rom[k]); }
    const std::string tag = "LSDj-v";
    if (title.compare(0, tag.size(), tag) == 0) {
        std::string v = title.substr(tag.size());
        while (!v.empty() && (v.back() == ' ' || uint8_t(v.back()) > 127)) v.pop_back();
        return v;
    }
    // Before 4.3 the title is "LSDJ" alone and the version sits in the welcome
    // line, "WELCOME TO LITTLE SOUND DJ V3.5.1!", within the first bank.
    const char* key = "LITTLE SOUND DJ V";
    const size_t keyLen = std::strlen(key), limit = std::min<size_t>(size, 0x10000);
    for (size_t i = 0; i + keyLen < limit; ++i) {
        if (std::memcmp(rom + i, key, keyLen) != 0) continue;
        std::string v;
        for (size_t k = i + keyLen; k < limit && v.size() < 8; ++k) {
            const char c = char(rom[k]);
            if ((c >= '0' && c <= '9') || c == '.' || (c >= 'A' && c <= 'Z')) v += c; else break;
        }
        if (v.size() >= 5 && v[1] == '.') return v;
    }
    return {};
}

} // namespace chipboy::lsdj
