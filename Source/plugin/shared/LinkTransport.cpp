#include "plugin/shared/LinkTransport.h"

#include <mutex>
#include <set>

#if JUCE_WINDOWS
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#else
 #include <unistd.h>
#endif

namespace chipboy::plugin {

using namespace juce;
using namespace chipboy::link;

namespace {

uint32_t currentPid()
{
#if JUCE_WINDOWS
    return uint32_t(GetCurrentProcessId());
#else
    return uint32_t(getpid());
#endif
}

constexpr int64_t kRetireMs = 2500;         ///< how long a mapping outlives its last use
constexpr uint64_t kReapFileMs = 60000;     ///< files this stale are deleted

std::mutex& liveMutex() { static std::mutex m; return m; }
std::set<String>& liveInProcess() { static std::set<String> s; return s; }

template <typename Retired>
void reapList(Retired& list)
{
    const int64_t now = Time::currentTimeMillis();
    for (size_t i = 0; i < list.size();) {
        if (now - list[i].second > kRetireMs) list.erase(list.begin() + long(i));
        else ++i;
    }
}

} // namespace

/* ------------------------------------------------------ LinkRegionFile */

File LinkRegionFile::directory()
{
    return File::getSpecialLocation(File::tempDirectory).getChildFile("chipboy-link");
}

bool LinkRegionFile::create(const String& uuid)
{
    map_.reset(); region_ = nullptr;
    const File dir = directory();
    dir.createDirectory();
    file_ = dir.getChildFile(uuid + ".cbl");
    {
        // Size and zero the file before mapping it: mapping past EOF faults on POSIX.
        FileOutputStream out(file_);
        if (out.failedToOpen()) return false;
        out.setPosition(0); out.truncate();
        MemoryBlock zeros(sizeof(InstanceRegion), true);
        out.write(zeros.getData(), zeros.getSize());
        out.flush();
    }
    map_ = std::make_unique<MemoryMappedFile>(file_, MemoryMappedFile::readWrite, false);
    if (map_->getData() == nullptr || map_->getSize() < int64(sizeof(InstanceRegion))) { map_.reset(); return false; }
    region_ = static_cast<InstanceRegion*>(map_->getData());
    return true;
}

bool LinkRegionFile::open(const File& f)
{
    map_.reset(); region_ = nullptr;
    file_ = f;
    if (!f.existsAsFile() || f.getSize() < int64(sizeof(InstanceRegion))) return false;
    map_ = std::make_unique<MemoryMappedFile>(f, MemoryMappedFile::readWrite, false);
    if (map_->getData() == nullptr || map_->getSize() < int64(sizeof(InstanceRegion))) { map_.reset(); return false; }
    auto* r = static_cast<InstanceRegion*>(map_->getData());
    if (!regionLooksValid(r, uint64_t(map_->getSize()))) { map_.reset(); return false; }
    region_ = r;
    return true;
}

void LinkRegionFile::remove()
{
    region_ = nullptr;
    map_.reset();
    if (file_.existsAsFile()) file_.deleteFile();
}

/* ------------------------------------------------------------ LinkHost */

LinkHost::~LinkHost() { unpublish(); retired_.clear(); }

bool LinkHost::publish(String& uuid, const String& name)
{
    unpublish();
    for (int attempt = 0; attempt < 4; ++attempt) {
        if (uuid.isEmpty()) uuid = Uuid().toString();
        bool taken = false;
        {
            std::lock_guard<std::mutex> lock(liveMutex());
            taken = liveInProcess().count(uuid) > 0;     // a duplicated instance in this process
        }
        if (!taken) {
            // A live region of another process with the same UUID: same story.
            LinkRegionFile probe;
            if (probe.open(LinkRegionFile::directory().getChildFile(uuid + ".cbl"))) {
                const auto* r = probe.region();
                if (fresh(r->heartbeat.load(), linkNowMs()) && r->pid != currentPid()) taken = true;
            }
        }
        if (taken) { uuid = Uuid().toString(); continue; }

        auto f = std::make_unique<LinkRegionFile>();
        if (!f->create(uuid)) return false;
        InstanceRegion* r = f->region();
        r->version = kVersion;
        r->size = uint32_t(sizeof(InstanceRegion));
        r->pid = currentPid();
        setString(r->uuid, kUuidChars, uuid.toRawUTF8());
        setString(r->name, kNameChars, name.toRawUTF8());
        r->heartbeat.store(linkNowMs());
        for (auto& t : r->instrumentTypes) t = 255;
        std::atomic_thread_fence(std::memory_order_release);
        r->magic = kMagic;
        {
            std::lock_guard<std::mutex> lock(liveMutex());
            liveInProcess().insert(uuid);
        }
        file_ = std::move(f);
        region_.store(r, std::memory_order_release);
        return true;
    }
    return false;
}

void LinkHost::unpublish()
{
    if (!file_) return;
    if (auto* r = file_->region()) {
        r->magic = 0;
        r->heartbeat.store(0);
        std::lock_guard<std::mutex> lock(liveMutex());
        char u[kUuidChars]; safeString(r->uuid, kUuidChars, u, kUuidChars);
        liveInProcess().erase(String(u));
    }
    region_.store(nullptr, std::memory_order_release);
    ownedMask_.store(0);
    // The audio thread may still be inside a block that read the pointer.
    // Keep the mapping alive a little, then delete the file with it.
    retired_.emplace_back(std::move(file_), Time::currentTimeMillis());
}

void LinkHost::reapRetired()
{
    const int64_t now = Time::currentTimeMillis();
    for (size_t i = 0; i < retired_.size();) {
        if (now - retired_[i].second > kRetireMs) { retired_[i].first->remove(); retired_.erase(retired_.begin() + long(i)); }
        else ++i;
    }
}

void LinkHost::maintain(const String& name, double sampleRate, int blockSize, bool linkEnabled, int model)
{
    reapRetired();
    auto* r = region_.load();
    if (!r) return;
    const uint64_t now = linkNowMs();
    r->heartbeat.store(now, std::memory_order_release);
    r->sampleRate.store(uint32_t(sampleRate));
    r->blockSize.store(uint32_t(std::max(0, blockSize)));
    r->linkEnabled.store(linkEnabled ? 1u : 0u);
    r->model.store(uint32_t(std::clamp(model, 0, 2)));
    char cur[kNameChars]; safeString(r->name, kNameChars, cur, kNameChars);
    if (String(cur) != name) setString(r->name, kNameChars, name.toRawUTF8());

    uint32_t mask = 0;
    for (int ch = 0; ch < 4; ++ch) {
        auto& s = r->slots[ch];
        const bool claimed = s.claimed.load() == 1;
        const uint64_t hb = s.claimHeartbeat.load();
        if (claimed && fresh(hb, now)) mask |= 1u << ch;
        else if (claimed && (hb == 0 || now - hb > 4 * kHeartbeatStaleMs)) {
            s.claimed.store(0);                 // a Voice that died: free the channel
            s.useLocal.store(0);
            setString(s.claimUuid, kUuidChars, "");
            setString(s.claimName, kNameChars, "");
        }
    }
    ownedMask_.store(mask, std::memory_order_release);
}

void LinkHost::setInstrumentNames(const std::vector<std::pair<String, int>>& names)
{
    auto* r = region_.load();
    if (!r) return;
    for (int i = 0; i < 128; ++i) {
        if (size_t(i) < names.size()) {
            setString(r->instrumentNames[i], kInstNameChars, names[size_t(i)].first.toRawUTF8());
            r->instrumentTypes[i] = uint8_t(names[size_t(i)].second < 0 ? 255 : names[size_t(i)].second);
        } else {
            setString(r->instrumentNames[i], kInstNameChars, "");
            r->instrumentTypes[i] = 255;
        }
    }
    r->bankSerial.fetch_add(1, std::memory_order_release);
}

String LinkHost::claimName(int ch) const
{
    auto* r = region_.load();
    if (!r || !(ownedMask_.load() & (1u << (ch & 3)))) return {};
    char n[kNameChars]; safeString(r->slots[ch & 3].claimName, kNameChars, n, kNameChars);
    return String(n);
}

/* ---------------------------------------------------------- LinkClient */

LinkClient::LinkClient() = default;
LinkClient::~LinkClient() { release(); maps_.clear(); retired_.clear(); }

String LinkClient::statusText(Status s)
{
    switch (s) {
        case Status::Unlinked: return "not linked";
        case Status::Waiting: return "waiting";
        case Status::Linked: return "linked";
        case Status::Busy: return "channel busy";
        case Status::Disconnected: return "disconnected";
        case Status::LinkOff: return "link mode is off on the ChipBoy instance";
        case Status::Incompatible: return "incompatible ChipBoy version";
    }
    return {};
}

LinkClient::Mapping* LinkClient::findMapping(const String& uuid)
{
    for (auto& m : maps_) if (m.uuid == uuid) return &m;
    return nullptr;
}

void LinkClient::reapRetired() { reapList(retired_); }

void LinkClient::scan()
{
    reapRetired();
    const File dir = LinkRegionFile::directory();
    Array<File> files;
    dir.findChildFiles(files, File::findFiles, false, "*.cbl");
    const int64_t nowLocal = Time::currentTimeMillis();
    const uint64_t now = linkNowMs();

    for (const auto& f : files) {
        const String uuid = f.getFileNameWithoutExtension();
        if (auto* m = findMapping(uuid)) { m->lastSeen = nowLocal; continue; }
        auto rf = std::make_unique<LinkRegionFile>();
        if (rf->open(f)) {
            maps_.push_back({ std::move(rf), uuid, nowLocal });
        } else if (uuid != targetUuid_) {
            // Not a region of this version, or truncated. If nobody is
            // keeping it alive, clean it up.
            if (f.getLastModificationTime().toMilliseconds() + int64(kReapFileMs) < nowLocal) f.deleteFile();
        }
    }
    // Mappings whose file vanished, and dead ones we do not target, retire.
    for (size_t i = 0; i < maps_.size();) {
        auto& m = maps_[i];
        const auto* r = m.file->region();
        const bool gone = m.lastSeen != nowLocal;
        const bool dead = r == nullptr || !fresh(r->heartbeat.load(), now);
        if (m.uuid != targetUuid_ && (gone || (dead && r && now - r->heartbeat.load() > kReapFileMs))) {
            if (dead && !gone) m.file->file().deleteFile();
            retired_.emplace_back(std::move(m.file), nowLocal);
            maps_.erase(maps_.begin() + long(i));
        } else ++i;
    }

    instances_.clear();
    for (auto& m : maps_) {
        const auto* r = m.file->region();
        if (!r) continue;
        InstanceInfo info;
        info.uuid = m.uuid;
        char n[kNameChars]; safeString(r->name, kNameChars, n, kNameChars); info.name = String(n);
        info.alive = r->magic == kMagic && fresh(r->heartbeat.load(), now);
        info.compatible = r->version == kVersion && r->size == sizeof(InstanceRegion);
        info.linkEnabled = r->linkEnabled.load() != 0;
        info.model = int(r->model.load() & 3);
        for (int ch = 0; ch < 4; ++ch) {
            const auto& s = r->slots[ch];
            char cu[kUuidChars]; safeString(s.claimUuid, kUuidChars, cu, kUuidChars);
            info.claimed[ch] = s.claimed.load() == 1 && fresh(s.claimHeartbeat.load(), now) && String(cu) != myUuid_;
            char cn[kNameChars]; safeString(s.claimName, kNameChars, cn, kNameChars); info.claimNames[ch] = String(cn);
        }
        instances_.push_back(std::move(info));
    }
    std::sort(instances_.begin(), instances_.end(), [](const InstanceInfo& a, const InstanceInfo& b) {
        if (a.alive != b.alive) return a.alive;
        return a.name.compareNatural(b.name) < 0;
    });
}

void LinkClient::setTarget(const String& uuid, int channel)
{
    channel &= 3;
    if (uuid == targetUuid_ && channel == targetChannel_) return;
    release();
    targetUuid_ = uuid; targetChannel_ = channel; targetName_.clear();
    status_ = uuid.isEmpty() ? Status::Unlinked : Status::Waiting;
}

void LinkClient::dropCurrent()
{
    slot_.store(nullptr, std::memory_order_release);
    region_.store(nullptr, std::memory_order_release);
    current_ = nullptr;
}

void LinkClient::release()
{
    if (auto* s = slot_.load()) {
        char cu[kUuidChars]; safeString(s->claimUuid, kUuidChars, cu, kUuidChars);
        if (String(cu) == myUuid_) {
            s->claimed.store(0);
            s->claimHeartbeat.store(0);
            s->useLocal.store(0);
            setString(s->claimUuid, kUuidChars, "");
            setString(s->claimName, kNameChars, "");
        }
    }
    dropCurrent();
    if (status_ == Status::Linked || status_ == Status::Busy || status_ == Status::LinkOff) status_ = targetUuid_.isEmpty() ? Status::Unlinked : Status::Waiting;
}

void LinkClient::maintain(const String& myUuid, const String& myName)
{
    myUuid_ = myUuid;
    reapRetired();
    if (targetUuid_.isEmpty()) { dropCurrent(); status_ = Status::Unlinked; return; }

    Mapping* m = findMapping(targetUuid_);
    if (!m) {
        // Maybe it appeared since the last scan.
        LinkRegionFile probe;
        const File f = LinkRegionFile::directory().getChildFile(targetUuid_ + ".cbl");
        if (f.existsAsFile()) { auto rf = std::make_unique<LinkRegionFile>(); if (rf->open(f)) { maps_.push_back({ std::move(rf), targetUuid_, Time::currentTimeMillis() }); m = &maps_.back(); } }
        if (!m) { dropCurrent(); status_ = Status::Waiting; return; }
    }
    InstanceRegion* r = m->file->region();
    const uint64_t now = linkNowMs();
    if (!r || r->version != kVersion || r->size != sizeof(InstanceRegion)) { dropCurrent(); status_ = Status::Incompatible; return; }
    if (r->magic != kMagic || !fresh(r->heartbeat.load(), now)) {
        // The main is gone (or not loaded yet). Keep our settings, say so.
        dropCurrent();
        status_ = Status::Disconnected;
        return;
    }
    char n[kNameChars]; safeString(r->name, kNameChars, n, kNameChars); targetName_ = String(n);

    auto& s = r->slots[targetChannel_ & 3];
    char cu[kUuidChars]; safeString(s.claimUuid, kUuidChars, cu, kUuidChars);
    const bool mine = String(cu) == myUuid_;
    const bool heldByOther = s.claimed.load() == 1 && !mine && fresh(s.claimHeartbeat.load(), now);
    if (heldByOther) { dropCurrent(); status_ = Status::Busy; return; }

    if (!mine) {
        // Free or stale: take it. A simultaneous taker converges on the
        // next round, when one of us sees the other's fresh claim.
        uint32_t expected = 0;
        if (!s.claimed.compare_exchange_strong(expected, 1u)) {
            // stale claim by someone else: overwrite
            s.claimed.store(1);
        }
        setString(s.claimUuid, kUuidChars, myUuid_.toRawUTF8());
        s.events.clear();
    }
    char cn[kNameChars]; safeString(s.claimName, kNameChars, cn, kNameChars);
    if (String(cn) != myName) setString(s.claimName, kNameChars, myName.toRawUTF8());
    s.claimHeartbeat.store(now, std::memory_order_release);

    current_ = r;
    region_.store(r, std::memory_order_release);
    slot_.store(&s, std::memory_order_release);
    status_ = r->linkEnabled.load() ? Status::Linked : Status::LinkOff;
}

String LinkClient::instrumentName(int slot1) const
{
    auto* r = region_.load();
    if (!r || slot1 < 1 || slot1 > 128) return {};
    char n[kInstNameChars]; safeString(r->instrumentNames[slot1 - 1], kInstNameChars, n, kInstNameChars);
    return String(n);
}

int LinkClient::instrumentType(int slot1) const
{
    auto* r = region_.load();
    if (!r || slot1 < 1 || slot1 > 128) return -1;
    const uint8_t t = r->instrumentTypes[slot1 - 1];
    return t > 3 ? -1 : int(t);
}

uint32_t LinkClient::bankSerial() const
{
    auto* r = region_.load();
    return r ? r->bankSerial.load() : 0;
}

} // namespace chipboy::plugin
