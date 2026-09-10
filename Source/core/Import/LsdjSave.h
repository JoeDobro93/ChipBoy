// ChipBoy -- reading an LSDj .sav: the file table, the compressed files and
// the working song (docs/plan-lsdj-import.md section 1). Core: <std> only.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace chipboy::lsdj {

constexpr size_t kSaveSize   = 0x20000;   ///< a whole save
constexpr size_t kSongSize   = 0x8000;    ///< one song, the working song included
constexpr int    kFileSlots  = 32;
constexpr int    kBlockCount = 191;       ///< blocks 1..191 after the file table, block 0
constexpr size_t kFormatVersionAt = 0x7FFF;

/// One saved song in the file table.
struct SaveEntry {
    int         file = -1;           ///< 0-31
    std::string name;                ///< up to 8 characters
    int         formatVersion = -1;  ///< byte 0x7FFF of the decompressed song, -1 unreadable
    bool        active = false;      ///< the file the working song was loaded from
    int         blocks = 0;
};

struct SaveIndex {
    std::vector<SaveEntry> files;    ///< the files that exist, in slot order
    int  activeFile = -1;            ///< -1 when the working song was never saved
    int  workingFormat = -1;         ///< the working song's format version
    bool workingUsed = false;        ///< it holds an instrument or a song row
};

/// Reads the file table and each file's format version. False, with a
/// message, when the buffer is not a save.
bool indexSave(const uint8_t* data, size_t size, SaveIndex& out, std::string& error);
/// Decompresses one file into 32768 bytes. False, with a message, when the
/// file does not exist or its stream is damaged.
bool decompressFile(const uint8_t* data, size_t size, int file, std::vector<uint8_t>& song, std::string& error);
/// The working song: the save's first 32 KB, copied.
bool workingSong(const uint8_t* data, size_t size, std::vector<uint8_t>& song);
/// Byte 0x7FFF of a song, -1 when the buffer is short.
int  formatVersionOf(const uint8_t* song, size_t size);
/// Whether a song holds anything: an allocated instrument or a song row.
bool songLooksUsed(const uint8_t* song, size_t size);
/// The version an LSDj ROM's cartridge title names ("LSDj-v9.3.9" -> "9.3.9");
/// empty when the buffer is not an LSDj ROM. Reads the header only.
std::string romVersion(const uint8_t* rom, size_t size);

} // namespace chipboy::lsdj
