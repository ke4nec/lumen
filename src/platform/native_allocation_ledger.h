#pragma once

#include "native_frame_allocator_api.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <limits>

namespace lumen::platform::detail {

// Allocation-free accounting; the platform hook serializes access, including
// the native allocator call. Owners identify native heaps/zones, not C++ ownership.
template <std::size_t Capacity = 65536>
class NativeAllocationLedger {
    static_assert(Capacity > 0 && (Capacity & (Capacity - 1)) == 0);

  public:
    std::uint64_t begin() noexcept {
        if (activeToken_ != 0 || nextToken_ == std::numeric_limits<std::uint64_t>::max()) return 0;
        entries_.fill({});
        sample_ = {};
        sample_.complete = true;
        activeToken_ = ++nextToken_;
        return activeToken_;
    }

    NativeAllocationSample finish(std::uint64_t token) noexcept {
        if (token == 0 || token != activeToken_) return {};
        activeToken_ = 0;
        return sample_;
    }

    void cancel(std::uint64_t token) noexcept { (void)finish(token); }
    void invalidate() noexcept { sample_.complete = false; }
    void abandon() noexcept { activeToken_ = 0; }

    void record(std::uintptr_t address, std::uint64_t bytes,
                std::uintptr_t owner = 0) noexcept {
        if (activeToken_ == 0 || !sample_.complete || address == 0) return;
        if (address == tombstone) { invalidate(); return; }
        auto slot = firstSlot(address);
        Entry* reusable = nullptr;
        for (std::size_t i = 0; i < Capacity; ++i) {
            auto& entry = entries_[slot];
            if (entry.address == address) { invalidate(); return; }
            if (entry.address == tombstone && reusable == nullptr) reusable = &entry;
            if (entry.address == 0) {
                insert(reusable ? *reusable : entry, address, bytes, owner);
                return;
            }
            slot = (slot + 1) & (Capacity - 1);
        }
        if (reusable != nullptr) insert(*reusable, address, bytes, owner);
        else invalidate();
    }

    void forget(std::uintptr_t address) noexcept {
        if (activeToken_ == 0 || !sample_.complete) return;
        if (auto* entry = find(address)) retire(*entry);
    }

    void forgetOwner(std::uintptr_t owner) noexcept {
        if (activeToken_ == 0 || !sample_.complete || owner == 0) return;
        for (auto& entry : entries_) {
            if (entry.address > tombstone && entry.owner == owner) retire(entry);
        }
    }

    std::uint64_t allocationId(std::uintptr_t address, std::uint64_t token) noexcept {
        if (token == 0 || token != activeToken_ || !sample_.complete) return 0;
        const auto* entry = find(address);
        return entry ? entry->allocationId : 0;
    }

    // Windows CRT debug/alignment headers precede the pointer returned to clients.
    std::uint64_t containingAllocationId(std::uintptr_t address, std::uint64_t token) noexcept {
        if (token == 0 || token != activeToken_ || !sample_.complete || address <= tombstone) return 0;
        if (const auto id = allocationId(address, token)) return id;
        for (const auto& entry : entries_) {
            if (entry.address > tombstone && address >= entry.address &&
                address - entry.address < entry.bytes) return entry.allocationId;
        }
        return 0;
    }

  private:
    static constexpr std::uintptr_t tombstone = 1;
    struct Entry {
        std::uintptr_t address{0};
        std::uint64_t bytes{0};
        std::uint64_t allocationId{0};
        std::uintptr_t owner{0};
    };

    static std::size_t firstSlot(std::uintptr_t address) noexcept {
        return ((address >> 4) * std::uintptr_t{0x9e3779b1}) & (Capacity - 1);
    }

    Entry* find(std::uintptr_t address) noexcept {
        if (address <= tombstone) return nullptr;
        auto slot = firstSlot(address);
        for (std::size_t i = 0; i < Capacity; ++i) {
            auto& entry = entries_[slot];
            if (entry.address == address) return &entry;
            if (entry.address == 0) return nullptr;
            slot = (slot + 1) & (Capacity - 1);
        }
        return nullptr;
    }

    bool add(std::uint64_t& target, std::uint64_t bytes) noexcept {
        if (bytes > std::numeric_limits<std::uint64_t>::max() - target) {
            invalidate();
            return false;
        }
        target += bytes;
        return true;
    }

    void insert(Entry& entry, std::uintptr_t address, std::uint64_t bytes,
                std::uintptr_t owner) noexcept {
        if (add(sample_.allocationCount, 1) && add(sample_.allocatedBytes, bytes) &&
            add(sample_.liveBytes, bytes)) {
            entry = {address, bytes, sample_.allocationCount, owner};
            sample_.peakBytes = std::max(sample_.peakBytes, sample_.liveBytes);
        }
    }

    void retire(Entry& entry) noexcept {
        sample_.liveBytes -= entry.bytes;
        entry.address = tombstone;
    }

    std::array<Entry, Capacity> entries_{};
    std::uint64_t nextToken_{0};
    std::uint64_t activeToken_{0};
    NativeAllocationSample sample_{};
};

}  // namespace lumen::platform::detail
