// ChipBoy -- the link region (spec section 11.3).
//
// One region per main instance, memory-mapped by the main plugin and by the
// Voice plugins that claim its channels. Everything here is plain data at
// fixed offsets: atomics for the handshakes, an SPSC ring per channel for
// the events, seqlocks for the snapshots. No pointers, no allocation, and
// nothing read from a region is trusted without a range check (section
// 11.8): the helpers below do the checking so callers cannot forget.
//
// Timing: a Voice stamps every event with the host's sample position at the
// start of the block it arrived in. The main renders one block behind
// (section 11.4), so an event for host block N is applied during the main's
// block N+1 whichever plugin the host ran first.
#pragma once

#include "core/Bank/Bank.h"
#include "core/Driver/Driver.h"
#include "core/Link/Spsc.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace chipboy::link {

constexpr uint32_t kMagic = 0x4B4C4243u;      ///< "CBLK"
constexpr uint32_t kVersion = 2;
constexpr uint32_t kUuidChars = 40;
constexpr uint32_t kNameChars = 64;
constexpr uint32_t kInstNameChars = 16;
constexpr uint32_t kEventRing = 512;
constexpr uint32_t kScopeRing = 4096;
constexpr uint64_t kNoHostFrame = ~uint64_t(0);
constexpr uint64_t kHeartbeatStaleMs = 3000;  ///< a claim or an instance older than this is dead

static_assert(std::is_trivially_copyable_v<driver::NoteEvent>);
static_assert(std::is_trivially_copyable_v<driver::ChannelParams>);
static_assert(std::is_trivially_copyable_v<bank::InstrumentCore>);

/// What a Voice sends: a note event or a parameter snapshot, stamped.
struct LinkEvent {
    enum Kind : uint8_t { Note = 0, Params = 1 };
    uint64_t hostFrame = kNoHostFrame; ///< host sample position of the block start
    uint32_t offset = 0;               ///< frames into that block
    uint32_t blockSize = 0;
    uint8_t  kind = Note;
    uint8_t  pad[7] = {};
    driver::NoteEvent note;
    driver::ChannelParams params;
};

/// A change of DAC level at an APU cycle: the digital trace (UI_DESIGN
/// section 3). Level -1 means the DAC is off.
struct ScopeSample {
    uint64_t cycle;
    int8_t   level;
    uint8_t  pad[7];
};

/// Single writer, any number of readers; readers copy the tail they need
/// and check the head again afterwards.
struct ScopeRing {
    std::atomic<uint32_t> head{ 0 };
    ScopeSample items[kScopeRing];

    void push(uint64_t cycle, int level)
    {
        const uint32_t h = head.load(std::memory_order_relaxed);
        auto& s = items[h & (kScopeRing - 1)];
        s.cycle = cycle; s.level = int8_t(level);
        head.store(h + 1, std::memory_order_release);
    }
    /// Copy the newest `count` samples (oldest first). Returns how many are valid.
    uint32_t snapshot(ScopeSample* out, uint32_t count) const
    {
        for (int attempt = 0; attempt < 4; ++attempt) {
            const uint32_t h1 = head.load(std::memory_order_acquire);
            const uint32_t n = count < kScopeRing - 16 ? count : kScopeRing - 16;
            const uint32_t avail = h1 < n ? h1 : n;
            for (uint32_t i = 0; i < avail; ++i) out[i] = items[(h1 - avail + i) & (kScopeRing - 1)];
            const uint32_t h2 = head.load(std::memory_order_acquire);
            if (h2 - h1 < kScopeRing - n) return avail;   // nothing we copied was overwritten
        }
        return 0;
    }
};

/// A seqlocked snapshot of a trivially copyable value.
template <typename T>
struct Snapshot {
    static_assert(std::is_trivially_copyable_v<T>);
    std::atomic<uint32_t> seq{ 0 };
    T value{};

    void write(const T& v)
    {
        const uint32_t s = seq.load(std::memory_order_relaxed);
        seq.store(s + 1, std::memory_order_release);          // odd: writing
        std::memcpy(static_cast<void*>(&value), &v, sizeof(T));
        seq.store(s + 2, std::memory_order_release);          // even: done
    }
    /// False if a write was in flight every time we looked; `out` untouched.
    bool read(T& out) const
    {
        for (int attempt = 0; attempt < 8; ++attempt) {
            const uint32_t s1 = seq.load(std::memory_order_acquire);
            if (s1 & 1u) continue;
            T tmp;
            std::memcpy(static_cast<void*>(&tmp), &value, sizeof(T));
            std::atomic_thread_fence(std::memory_order_acquire);
            if (seq.load(std::memory_order_acquire) == s1) { out = tmp; return true; }
        }
        return false;
    }
    uint32_t version() const { return seq.load(std::memory_order_acquire); }
};

/// Message-thread requests from a Voice to the main (spec section 12.5).
enum class Request : uint32_t { None = 0, PushToSlot = 1, PullFromSlot = 2 };

/// One hardware channel as seen through the link.
struct ChannelSlot {
    // --- claim (Voice writes, main reads) ---------------------------------
    std::atomic<uint32_t> claimed{ 0 };          ///< 1 while a Voice holds the channel
    std::atomic<uint64_t> claimHeartbeat{ 0 };   ///< ms since epoch, refreshed by the Voice
    char claimUuid[kUuidChars] = {};
    char claimName[kNameChars] = {};             ///< the Voice's track/instance name

    // --- Voice -> main, audio threads --------------------------------------
    Spsc<LinkEvent, kEventRing> events;
    std::atomic<uint32_t> useLocal{ 0 };         ///< 1: play `local` instead of the bank slot
    Snapshot<bank::InstrumentCore> local;

    // --- requests, message threads ----------------------------------------
    std::atomic<uint32_t> request{ 0 };          ///< Request
    std::atomic<uint32_t> requestSlot{ 0 };      ///< instrument slot 1-128
    std::atomic<uint32_t> requestSerial{ 0 };    ///< Voice bumps with each request
    std::atomic<uint32_t> ackSerial{ 0 };        ///< main sets to the serial it handled
    std::atomic<uint32_t> ackResult{ 0 };        ///< 0 ok, 1 refused
    Snapshot<bank::InstrumentCore> exchange;     ///< push: Voice fills; pull: main fills
    char exchangeName[kInstNameChars] = {};      ///< the instrument's name, same direction
    std::atomic<uint32_t> focusRequest{ 0 };     ///< Voice sets; main editor jumps here and clears

    // --- main -> Voice, display only (section 11.7) ------------------------
    std::atomic<uint64_t> state{ 0 };            ///< packed, see packState()
    std::atomic<uint64_t> state2{ 0 };           ///< the running state, see packState2()
    ScopeRing scope;
};

/// The region: header then four slots.
struct InstanceRegion {
    uint32_t magic = 0;
    uint32_t version = 0;
    uint32_t size = 0;                            ///< sizeof(InstanceRegion) of the writer
    uint32_t pid = 0;
    char uuid[kUuidChars] = {};
    char name[kNameChars] = {};
    std::atomic<uint64_t> heartbeat{ 0 };        ///< ms since epoch, main's message thread
    std::atomic<uint32_t> sampleRate{ 0 };
    std::atomic<uint32_t> blockSize{ 0 };
    std::atomic<uint32_t> linkEnabled{ 0 };      ///< link mode on the main (section 11.4)
    std::atomic<uint32_t> model{ 0 };            ///< 0 DMG, 1 CGB, 2 RAW
    std::atomic<uint32_t> bankSerial{ 0 };       ///< bumps when instrument names change
    char instrumentNames[128][kInstNameChars] = {};
    uint8_t instrumentTypes[128] = {};            ///< bank::InstrumentType, 255 = empty
    ChannelSlot slots[4];
};

// --- packing helpers --------------------------------------------------------

/// regs: NRx0..NRx4 in the low 40 bits; then active, dacOn, level, note.
inline uint64_t packState(const driver::VoiceView& v)
{
    uint64_t s = 0;
    for (int i = 0; i < 5; ++i) s |= uint64_t(v.regs[i]) << (8 * i);
    s |= uint64_t(v.active ? 1 : 0) << 40;
    s |= uint64_t(v.dacOn ? 1 : 0) << 41;
    s |= uint64_t(v.outOfRange ? 1 : 0) << 42;
    s |= uint64_t(v.volume & 15) << 44;
    s |= uint64_t(v.note) << 48;
    s |= uint64_t(v.instrument) << 56;
    return s;
}
inline void unpackState(uint64_t s, driver::VoiceView& v)
{
    for (int i = 0; i < 5; ++i) v.regs[i] = uint8_t(s >> (8 * i));
    v.active = (s >> 40) & 1; v.dacOn = (s >> 41) & 1; v.outOfRange = (s >> 42) & 1;
    v.volume = uint8_t((s >> 44) & 15);
    v.note = uint8_t(s >> 48);
    v.instrument = uint8_t(s >> 56);
    v.period = uint16_t(v.regs[3] | ((v.regs[4] & 7) << 8));
}

/// The running state (docs/COMMANDS_AND_TEMPO.md section 3): what the two
/// command slots and the instrument have actually done to the channel.
inline uint64_t packState2(const driver::VoiceView& v)
{
    uint64_t s = 0;
    s |= uint64_t(v.envVol & 15);
    s |= uint64_t(v.envRate & 7) << 4;
    s |= uint64_t(v.envDir ? 1 : 0) << 7;
    s |= uint64_t(v.vibSpeed & 15) << 8;
    s |= uint64_t(v.vibDepth & 15) << 12;
    s |= uint64_t(uint16_t(v.pitchOffset)) << 16;
    s |= uint64_t(v.pan & 3) << 32;
    s |= uint64_t(v.duty & 3) << 34;
    s |= uint64_t(v.tableSlot & 127) << 36;
    s |= uint64_t(v.tableStep & 15) << 43;
    s |= uint64_t(v.frame & 31) << 47;
    s |= uint64_t(v.groove) << 52;
    return s;
}
inline void unpackState2(uint64_t s, driver::VoiceView& v)
{
    v.envVol = uint8_t(s & 15);
    v.envRate = uint8_t((s >> 4) & 7);
    v.envDir = uint8_t((s >> 7) & 1);
    v.vibSpeed = uint8_t((s >> 8) & 15);
    v.vibDepth = uint8_t((s >> 12) & 15);
    v.pitchOffset = int16_t(uint16_t((s >> 16) & 0xFFFF));
    v.pan = uint8_t((s >> 32) & 3);
    v.duty = uint8_t((s >> 34) & 3);
    v.tableSlot = uint8_t((s >> 36) & 127);
    v.tableStep = uint8_t((s >> 43) & 15);
    v.frame = uint8_t((s >> 47) & 31);
    v.groove = uint8_t((s >> 52) & 255);
}

/// Copy a C string field out of a region without trusting its termination.
inline void safeString(const char* field, uint32_t capacity, char* out, uint32_t outCapacity)
{
    uint32_t n = 0;
    while (n < capacity && n + 1 < outCapacity && field[n] != 0) { out[n] = field[n]; ++n; }
    out[n] = 0;
}
inline void setString(char* field, uint32_t capacity, const char* s)
{
    uint32_t n = 0;
    while (n + 1 < capacity && s[n] != 0) { field[n] = s[n]; ++n; }
    for (; n < capacity; ++n) field[n] = 0;
}

/// Header sanity: magic, version and size must all match this build.
inline bool regionLooksValid(const InstanceRegion* r, uint64_t mappedBytes)
{
    if (!r || mappedBytes < sizeof(InstanceRegion)) return false;
    return r->magic == kMagic && r->version == kVersion && r->size == sizeof(InstanceRegion);
}
inline bool fresh(uint64_t heartbeatMs, uint64_t nowMs) { return heartbeatMs != 0 && nowMs >= heartbeatMs && nowMs - heartbeatMs < kHeartbeatStaleMs; }

} // namespace chipboy::link
