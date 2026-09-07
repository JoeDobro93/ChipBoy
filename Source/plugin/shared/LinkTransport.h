// ChipBoy -- the link transport (spec sections 11.3-11.8).
//
// A main instance publishes one memory-mapped region file; Voice plugins
// find it by UUID, claim a channel and stream events into it. Files live
// in a per-user directory under the temp folder, which every host process
// running as the same user can map. Mappings are never torn down while an
// audio thread might still be using them: they retire to a list and are
// freed a couple of seconds later by the message thread.
#pragma once

#include "core/Link/LinkLayout.h"

#include <juce_core/juce_core.h>

#include <atomic>
#include <memory>
#include <vector>

namespace chipboy::plugin {

/// One mapped region file.
class LinkRegionFile {
public:
    static juce::File directory();
    /// Create (or take over) the file for `uuid`, sized and zeroed.
    bool create(const juce::String& uuid);
    /// Map an existing file; false if it is not a valid region of this version.
    bool open(const juce::File& f);
    link::InstanceRegion* region() const { return region_; }
    const juce::File& file() const { return file_; }
    void remove();   ///< unmap and delete the file

private:
    juce::File file_;
    std::unique_ptr<juce::MemoryMappedFile> map_;
    link::InstanceRegion* region_ = nullptr;
};

/// The main plugin's side: publish, heartbeat, and say which channels a
/// Voice owns right now. Message thread except where noted.
class LinkHost {
public:
    ~LinkHost();
    /// Publish under `uuid`. If that UUID is already live (a duplicated
    /// instance), the caller gets a fresh one back through `uuid`.
    bool publish(juce::String& uuid, const juce::String& name);
    void unpublish();
    bool published() const { return region_.load() != nullptr; }

    /// Refresh heartbeat and header fields; recompute which channels are
    /// owned by a live Voice claim. Call every 100-250 ms.
    void maintain(const juce::String& name, double sampleRate, int blockSize, bool linkEnabled, int model);
    /// Publish instrument names for the Voice's selector.
    void setInstrumentNames(const std::vector<std::pair<juce::String, int>>& namesAndTypes);

    /// Audio thread: the region (or null) and the mask of Voice-owned channels.
    link::InstanceRegion* region() const { return region_.load(std::memory_order_acquire); }
    uint32_t ownedMask() const { return ownedMask_.load(std::memory_order_acquire); }
    /// Message thread: name of the Voice holding channel `ch`, or empty.
    juce::String claimName(int ch) const;

private:
    std::unique_ptr<LinkRegionFile> file_;
    std::atomic<link::InstanceRegion*> region_{ nullptr };
    std::atomic<uint32_t> ownedMask_{ 0 };
    std::vector<std::pair<std::unique_ptr<LinkRegionFile>, int64_t>> retired_;
    void reapRetired();
};

/// The Voice plugin's side: discover instances, claim a channel, keep it.
class LinkClient {
public:
    enum class Status { Unlinked, Waiting, Linked, Busy, Disconnected, LinkOff, Incompatible };

    struct InstanceInfo {
        juce::String uuid, name;
        bool alive = false, linkEnabled = false, compatible = true;
        bool claimed[4] = { false, false, false, false };
        juce::String claimNames[4];
        int model = 0;
    };

    LinkClient();
    ~LinkClient();

    /// Enumerate the link directory. Message thread, every second or so.
    void scan();
    std::vector<InstanceInfo> instances() const { return instances_; }

    /// Choose what to link to; the claim happens in maintain().
    void setTarget(const juce::String& uuid, int channel);
    juce::String targetUuid() const { return targetUuid_; }
    int targetChannel() const { return targetChannel_; }
    juce::String targetName() const { return targetName_; }
    Status status() const { return status_; }
    static juce::String statusText(Status s);

    /// Claim / refresh / release, and update status. Message thread, 100-250 ms.
    void maintain(const juce::String& myUuid, const juce::String& myName);
    void release();

    /// Audio thread: the claimed slot, or null.
    link::ChannelSlot* slot() const { return slot_.load(std::memory_order_acquire); }
    link::InstanceRegion* region() const { return region_.load(std::memory_order_acquire); }
    /// Instrument names of the linked instance (message thread; empty when unlinked).
    juce::String instrumentName(int slot1) const;
    int instrumentType(int slot1) const;   ///< -1 empty
    uint32_t bankSerial() const;

private:
    struct Mapping { std::unique_ptr<LinkRegionFile> file; juce::String uuid; int64_t lastSeen = 0; };
    std::vector<Mapping> maps_;
    std::vector<std::pair<std::unique_ptr<LinkRegionFile>, int64_t>> retired_;
    std::vector<InstanceInfo> instances_;
    juce::String targetUuid_, targetName_, myUuid_;
    int targetChannel_ = 0;
    Status status_ = Status::Unlinked;
    std::atomic<link::ChannelSlot*> slot_{ nullptr };
    std::atomic<link::InstanceRegion*> region_{ nullptr };
    link::InstanceRegion* current_ = nullptr;

    Mapping* findMapping(const juce::String& uuid);
    void reapRetired();
    void dropCurrent();
};

/// Wall-clock milliseconds for heartbeats (both processes share the clock).
inline uint64_t linkNowMs() { return uint64_t(juce::Time::currentTimeMillis()); }

} // namespace chipboy::plugin
