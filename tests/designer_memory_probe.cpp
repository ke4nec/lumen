// F6 设计器预览堆峰值探针。
//
// 该探针是独立进程，避免把全局 new/delete 记账注入 lumen-tests。它只为
// headless 性能 fixture 提供“操作期间活跃堆字节”的证据，不把结果写入
// Renderer HUD，也不把 RSS 当作帧分配量。

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <new>
#include <optional>
#include <string>
#include <utility>

#include "lumen/core/virtual_list.h"
#include "lumen/dsl/design_workbench.h"
#include "lumen/layout/layout.h"
#include "lumen/render/cpu_renderer.h"
#include "lumen/render/painter.h"

namespace {

constexpr std::uint64_t kHeaderMagic = 0x4c554d454e4d454dULL;

struct TrackerState {
    bool enabled{false};
    std::uint64_t generation{0};
    std::size_t liveBytes{0};
    std::size_t peakBytes{0};
    std::size_t allocationCount{0};
    std::size_t allocatedBytes{0};
};

thread_local TrackerState g_tracker;

struct AllocationHeader {
    std::uint64_t magic{0};
    void* raw{nullptr};
    std::size_t bytes{0};
    std::uint64_t generation{0};
    bool tracked{false};
};

static_assert(sizeof(AllocationHeader) % alignof(AllocationHeader) == 0);

std::size_t saturatingAdd(std::size_t left, std::size_t right) {
    if (std::numeric_limits<std::size_t>::max() - left < right) {
        return std::numeric_limits<std::size_t>::max();
    }
    return left + right;
}

std::uintptr_t alignUp(std::uintptr_t value, std::size_t alignment) {
    const auto remainder = value % alignment;
    return remainder == 0 ? value : value + alignment - remainder;
}

void* allocateBytes(std::size_t bytes, std::size_t alignment) {
    alignment = std::max(alignment, alignof(AllocationHeader));
    const std::size_t overhead = sizeof(AllocationHeader) + sizeof(void*) +
                                  alignment - 1;
    if (bytes > std::numeric_limits<std::size_t>::max() - overhead) {
        throw std::bad_alloc{};
    }
    void* raw = std::malloc(bytes + overhead);
    if (raw == nullptr) throw std::bad_alloc{};

    const auto headerAddress = alignUp(
        reinterpret_cast<std::uintptr_t>(raw), alignof(AllocationHeader));
    const auto candidate = headerAddress + sizeof(AllocationHeader) +
                           sizeof(void*);
    const auto payloadAddress = alignUp(candidate, alignment);
    auto* header = reinterpret_cast<AllocationHeader*>(headerAddress);
    auto* headerSlot = reinterpret_cast<AllocationHeader**>(
        payloadAddress - sizeof(void*));
    *headerSlot = header;
    *header = AllocationHeader{kHeaderMagic, raw, bytes, g_tracker.generation,
                               g_tracker.enabled};
    if (g_tracker.enabled) {
        g_tracker.liveBytes = saturatingAdd(g_tracker.liveBytes, bytes);
        g_tracker.peakBytes =
            std::max(g_tracker.peakBytes, g_tracker.liveBytes);
        ++g_tracker.allocationCount;
        g_tracker.allocatedBytes =
            saturatingAdd(g_tracker.allocatedBytes, bytes);
    }
    return reinterpret_cast<void*>(payloadAddress);
}

void releaseBytes(void* pointer) noexcept {
    if (pointer == nullptr) return;
    auto* headerSlot = reinterpret_cast<AllocationHeader**>(
        reinterpret_cast<std::uintptr_t>(pointer) - sizeof(void*));
    auto* header = *headerSlot;
    if (header->magic != kHeaderMagic) std::abort();
    if (header->tracked && g_tracker.enabled &&
        header->generation == g_tracker.generation) {
        g_tracker.liveBytes =
            header->bytes > g_tracker.liveBytes
                ? 0
                : g_tracker.liveBytes - header->bytes;
    }
    void* raw = header->raw;
    header->magic = 0;
    std::free(raw);
}

}  // namespace

void* operator new(std::size_t bytes) {
    return allocateBytes(bytes, alignof(std::max_align_t));
}
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete(void* pointer) noexcept { releaseBytes(pointer); }
void operator delete[](void* pointer) noexcept { releaseBytes(pointer); }
void operator delete(void* pointer, std::size_t) noexcept {
    releaseBytes(pointer);
}
void operator delete[](void* pointer, std::size_t) noexcept {
    releaseBytes(pointer);
}

void* operator new(std::size_t bytes, std::align_val_t alignment) {
    return allocateBytes(bytes, static_cast<std::size_t>(alignment));
}
void* operator new[](std::size_t bytes, std::align_val_t alignment) {
    return ::operator new(bytes, alignment);
}
void operator delete(void* pointer, std::align_val_t) noexcept {
    releaseBytes(pointer);
}
void operator delete[](void* pointer, std::align_val_t) noexcept {
    releaseBytes(pointer);
}
void operator delete(void* pointer, std::size_t, std::align_val_t) noexcept {
    releaseBytes(pointer);
}
void operator delete[](void* pointer, std::size_t,
                       std::align_val_t) noexcept {
    releaseBytes(pointer);
}

void* operator new(std::size_t bytes, const std::nothrow_t&) noexcept {
    try {
        return ::operator new(bytes);
    } catch (...) {
        return nullptr;
    }
}
void* operator new[](std::size_t bytes, const std::nothrow_t& tag) noexcept {
    return ::operator new(bytes, tag);
}
void operator delete(void* pointer, const std::nothrow_t&) noexcept {
    releaseBytes(pointer);
}
void operator delete[](void* pointer, const std::nothrow_t&) noexcept {
    releaseBytes(pointer);
}
void* operator new(std::size_t bytes, std::align_val_t alignment,
                   const std::nothrow_t&) noexcept {
    try {
        return ::operator new(bytes, alignment);
    } catch (...) {
        return nullptr;
    }
}
void* operator new[](std::size_t bytes, std::align_val_t alignment,
                     const std::nothrow_t& tag) noexcept {
    return ::operator new(bytes, alignment, tag);
}
void operator delete(void* pointer, std::align_val_t,
                     const std::nothrow_t&) noexcept {
    releaseBytes(pointer);
}
void operator delete[](void* pointer, std::align_val_t,
                       const std::nothrow_t&) noexcept {
    releaseBytes(pointer);
}

namespace {

struct MemorySample {
    std::size_t allocationCount{0};
    std::size_t allocatedBytes{0};
    std::size_t peakBytes{0};
    std::size_t liveBytes{0};
};

template <typename Operation>
MemorySample measure(Operation&& operation) {
    ++g_tracker.generation;
    if (g_tracker.generation == 0) ++g_tracker.generation;
    g_tracker.enabled = true;
    g_tracker.liveBytes = 0;
    g_tracker.peakBytes = 0;
    g_tracker.allocationCount = 0;
    g_tracker.allocatedBytes = 0;
    operation();
    const MemorySample sample{g_tracker.allocationCount,
                              g_tracker.allocatedBytes, g_tracker.peakBytes,
                              g_tracker.liveBytes};
    g_tracker.enabled = false;
    return sample;
}

lumen::dsl::DesignValue stringValue(std::string value) {
    return lumen::dsl::DesignValue{
        lumen::dsl::DesignValue::Variant{std::move(value)}};
}

lumen::dsl::DesignDocument makeDocument(std::size_t nodeCount) {
    lumen::dsl::DesignDocument document;
    document.documentId = "designer-memory-probe";
    document.pageName = "memory";
    document.root.id = 1;
    document.root.type = "Column";
    document.root.properties["key"] = stringValue("root");
    document.root.children.reserve(nodeCount - 1);
    for (std::size_t index = 1; index < nodeCount; ++index) {
        lumen::dsl::DesignNode child;
        child.id = static_cast<std::uint64_t>(index + 1);
        child.type = "Text";
        child.properties["key"] =
            stringValue("node-" + std::to_string(index));
        child.properties["text"] =
            stringValue("Memory probe node " + std::to_string(index));
        document.root.children.push_back(std::move(child));
    }
    return document;
}

bool checkSample(const char* name, const MemorySample& sample) {
    if (sample.allocationCount == 0 || sample.allocatedBytes == 0 ||
        sample.peakBytes == 0 || sample.liveBytes > sample.peakBytes) {
        std::fprintf(stderr,
                     "%s produced invalid allocation sample: count=%zu "
                     "allocated=%zu peak=%zu live=%zu\n",
                     name, sample.allocationCount, sample.allocatedBytes,
                     sample.peakBytes, sample.liveBytes);
        return false;
    }
    return true;
}

}  // namespace

int main() {
    auto document = makeDocument(1000);
    lumen::dsl::DesignPreviewWorkbench workbench;

    const auto open = measure([&] {
        if (!workbench.openDocument(document)) std::abort();
    });
    const auto edit = measure([&] {
        if (!workbench.setProperty(
                2, "text", stringValue("Memory probe edited node"))) {
            std::abort();
        }
    });

    lumen::core::VirtualListController source;
    source.setItemCount(1000);
    source.setEstimatedExtent(32.0F);
    source.setItemBuilder([](std::size_t index) {
        auto item = lumen::core::makeText("Memory probe item " +
                                          std::to_string(index));
        item.key = "memory-item-" + std::to_string(index);
        item.height = 32.0F;
        return item;
    });
    auto list = lumen::core::makeVirtualList(
        &source, "memory-list", std::nullopt, 600.0F, 200.0F);
    const auto virtualList = measure([&] {
        const auto renderNode = lumen::layout::LayoutEngine::layout(
            list, lumen::core::Constraints::tight(
                      lumen::core::Size{800.0F, 600.0F}));
        lumen::render::CpuRenderer renderer;
        renderer.beginFrame(lumen::core::Size{800.0F, 600.0F});
        lumen::render::paintScene(renderer, renderNode);
        renderer.endFrame();
    });

    const auto peakBytes = std::max({open.peakBytes, edit.peakBytes,
                                     virtualList.peakBytes});
    const auto totalAllocations = open.allocationCount + edit.allocationCount +
                                  virtualList.allocationCount;
    const auto totalAllocatedBytes =
        open.allocatedBytes + edit.allocatedBytes +
        virtualList.allocatedBytes;

    const bool valid = checkSample("preview_open", open) &&
                       checkSample("preview_edit", edit) &&
                       checkSample("virtual_list", virtualList);
    std::printf(
        "{\"schema\":1,\"scope\":\"designer_preview_operations\","
        "\"peak_bytes\":%zu,\"allocation_count\":%zu,"
        "\"allocated_bytes\":%zu,"
        "\"preview_open\":{\"allocations\":%zu,\"allocated_bytes\":%zu,"
        "\"peak_bytes\":%zu,\"live_bytes\":%zu},"
        "\"preview_edit\":{\"allocations\":%zu,\"allocated_bytes\":%zu,"
        "\"peak_bytes\":%zu,\"live_bytes\":%zu},"
        "\"virtual_list\":{\"allocations\":%zu,\"allocated_bytes\":%zu,"
        "\"peak_bytes\":%zu,\"live_bytes\":%zu}}\n",
        peakBytes, totalAllocations, totalAllocatedBytes, open.allocationCount,
        open.allocatedBytes, open.peakBytes,
        open.liveBytes, edit.allocationCount, edit.allocatedBytes,
        edit.peakBytes, edit.liveBytes, virtualList.allocationCount,
        virtualList.allocatedBytes, virtualList.peakBytes,
        virtualList.liveBytes);
    return valid ? 0 : 1;
}
