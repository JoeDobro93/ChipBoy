// ChipBoy -- a lock-free single-producer single-consumer ring.
//
// Fixed capacity, trivially copyable items, atomics only: usable within a
// process and inside a memory-mapped file shared between processes (the
// link, spec section 11.3). Every index read from the ring is range-checked
// before use, because a shared region may have been written by anyone
// (section 11.8).
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace chipboy::link {

template <typename T, uint32_t Capacity>
struct Spsc {
    static_assert(std::is_trivially_copyable_v<T>);
    static_assert((Capacity & (Capacity - 1)) == 0, "capacity must be a power of two");

    std::atomic<uint32_t> head{ 0 };   ///< written by the producer
    std::atomic<uint32_t> tail{ 0 };   ///< written by the consumer
    T items[Capacity];

    bool push(const T& v)
    {
        const uint32_t h = head.load(std::memory_order_relaxed);
        const uint32_t t = tail.load(std::memory_order_acquire);
        if (h - t >= Capacity) return false;
        items[h & (Capacity - 1)] = v;
        head.store(h + 1, std::memory_order_release);
        return true;
    }
    bool pop(T& v)
    {
        const uint32_t t = tail.load(std::memory_order_relaxed);
        const uint32_t h = head.load(std::memory_order_acquire);
        if (h == t) return false;
        if (h - t > Capacity) { tail.store(h, std::memory_order_release); return false; }   // corrupt: resync
        v = items[t & (Capacity - 1)];
        tail.store(t + 1, std::memory_order_release);
        return true;
    }
    uint32_t size() const
    {
        const uint32_t h = head.load(std::memory_order_acquire), t = tail.load(std::memory_order_acquire);
        return h - t > Capacity ? 0 : h - t;
    }
    void clear() { tail.store(head.load(std::memory_order_acquire), std::memory_order_release); }
};

} // namespace chipboy::link
