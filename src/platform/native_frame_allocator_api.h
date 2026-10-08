#pragma once

#include <cstdint>

namespace lumen::platform::detail {

// Private, versioned ABI between the optional preload library and SDK.
struct NativeAllocationSample {
    bool complete{false};
    std::uint64_t allocationCount{0};
    std::uint64_t allocatedBytes{0};
    std::uint64_t peakBytes{0};
    std::uint64_t liveBytes{0};
};

struct NativeFrameAllocatorApi {
    std::uint32_t version;
    std::uint32_t size;
    bool (*bindingsValid)() noexcept;
    std::uint64_t (*begin)() noexcept;
    NativeAllocationSample (*finish)(std::uint64_t token) noexcept;
    void (*cancel)(std::uint64_t token) noexcept;
    std::uint64_t (*allocationId)(std::uintptr_t address, std::uint64_t token) noexcept;
};

}  // namespace lumen::platform::detail
