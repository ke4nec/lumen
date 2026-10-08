// F6 设计器预览堆峰值探针。
//
// 该探针是独立进程，避免把全局 new/delete 记账注入 lumen-tests。它只为
// headless 性能 fixture 提供“操作期间活跃堆字节”的证据，不把结果写入
// Renderer HUD，也不把 RSS 当作帧分配量。

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "lumen/core/virtual_list.h"
#include "lumen/dsl/design_preview_frame.h"
#include "lumen/dsl/design_schema.h"
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
    try {
        operation();
    } catch (...) {
        g_tracker.enabled = false;
        throw;
    }
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

lumen::dsl::DesignValue numberValue(double value) {
    return lumen::dsl::DesignValue{
        lumen::dsl::DesignValue::Variant{value}};
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

lumen::dsl::DesignDocument makeVirtualListDocument() {
    lumen::dsl::DesignDocument document;
    document.documentId = "designer-memory-virtual-list";
    document.pageName = "virtual-list";
    document.root = lumen::dsl::DesignNode{1, "VirtualList"};
    document.root.properties["key"] = stringValue("design-preview-list");
    document.root.properties["width"] = numberValue(800.0);
    document.root.properties["height"] = numberValue(600.0);
    document.root.properties["virtualCacheExtent"] = numberValue(200.0);
    document.root.references["virtualSource"] = "preview_rows";
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

int runMemoryProbe() {
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

    auto designDocument = makeVirtualListDocument();
    auto designSource = std::make_shared<lumen::core::VirtualListController>();
    designSource->setItemCount(1000);
    designSource->setEstimatedExtent(32.0F);
    designSource->setItemBuilder([](std::size_t index) {
        auto item = lumen::core::makeText(
            "Design preview item " + std::to_string(index));
        item.key = "design-preview-item-" + std::to_string(index);
        item.height = 32.0F;
        return item;
    });
    lumen::dsl::MapDesignRuntimeContext designContext;
    designContext.registerTypedReference(
        lumen::dsl::DesignReferenceKind::VirtualSource, "preview_rows",
        designSource.get(), designSource);
    lumen::dsl::DesignPreviewFrame designFrame;
    const auto designDocumentPreview = measure([&] {
        if (!designFrame.update(designDocument, designContext)) std::abort();
        const auto renderNode = lumen::layout::LayoutEngine::layout(
            designFrame.widget(), lumen::core::Constraints::tight(
                                      lumen::core::Size{800.0F, 600.0F}));
        lumen::render::CpuRenderer renderer;
        renderer.beginFrame(lumen::core::Size{800.0F, 600.0F});
        lumen::render::paintScene(renderer, renderNode);
        renderer.endFrame();
    });
    designFrame.clear();

    const auto peakBytes = std::max({open.peakBytes, edit.peakBytes,
                                     virtualList.peakBytes,
                                     designDocumentPreview.peakBytes});
    const auto totalAllocations = open.allocationCount + edit.allocationCount +
                                  virtualList.allocationCount +
                                  designDocumentPreview.allocationCount;
    const auto totalAllocatedBytes =
        open.allocatedBytes + edit.allocatedBytes +
        virtualList.allocatedBytes + designDocumentPreview.allocatedBytes;

    const bool valid = checkSample("preview_open", open) &&
                       checkSample("preview_edit", edit) &&
                       checkSample("virtual_list", virtualList) &&
                       checkSample("design_document_preview",
                                   designDocumentPreview);
    std::printf(
        "{\"schema\":1,\"scope\":\"designer_preview_operations\","
        "\"peak_bytes\":%zu,\"allocation_count\":%zu,"
        "\"allocated_bytes\":%zu,"
        "\"preview_open\":{\"allocations\":%zu,\"allocated_bytes\":%zu,"
        "\"peak_bytes\":%zu,\"live_bytes\":%zu},"
        "\"preview_edit\":{\"allocations\":%zu,\"allocated_bytes\":%zu,"
        "\"peak_bytes\":%zu,\"live_bytes\":%zu},"
        "\"virtual_list\":{\"allocations\":%zu,\"allocated_bytes\":%zu,"
        "\"peak_bytes\":%zu,\"live_bytes\":%zu},"
        "\"design_document_preview\":{\"allocations\":%zu,"
        "\"allocated_bytes\":%zu,\"peak_bytes\":%zu,"
        "\"live_bytes\":%zu}}\n",
        peakBytes, totalAllocations, totalAllocatedBytes, open.allocationCount,
        open.allocatedBytes, open.peakBytes,
        open.liveBytes, edit.allocationCount, edit.allocatedBytes,
        edit.peakBytes, edit.liveBytes, virtualList.allocationCount,
        virtualList.allocatedBytes, virtualList.peakBytes,
        virtualList.liveBytes, designDocumentPreview.allocationCount,
        designDocumentPreview.allocatedBytes, designDocumentPreview.peakBytes,
        designDocumentPreview.liveBytes);
    return valid ? 0 : 1;
}

namespace {

using Clock = std::chrono::steady_clock;
constexpr std::array<const char*, 9> kPhaseNames{
    "import", "serialize", "read", "schema", "compile", "layout", "paint",
    "edit", "outline"};

enum class FixtureKind { L0, Edit, Outline, VirtualList };

struct PhaseSample {
    double microseconds{0.0};
    std::size_t allocations{0};
    std::size_t allocatedBytes{0};
    bool measured{false};
};

struct FixtureSample {
    std::array<PhaseSample, kPhaseNames.size()> phases{};
    MemorySample heap{};
    std::uint64_t frameHash{0};
    std::uint64_t rebuilds{0};
    std::size_t documentNodes{0};
    std::size_t renderNodes{0};
    std::size_t materializedItems{0};
};

template <typename Operation>
void measurePhase(FixtureSample& sample, std::size_t phase,
                  Operation&& operation) {
    const auto allocations = g_tracker.allocationCount;
    const auto allocatedBytes = g_tracker.allocatedBytes;
    const auto begin = Clock::now();
    operation();
    sample.phases[phase] = PhaseSample{
        std::chrono::duration<double, std::micro>(Clock::now() - begin).count(),
        g_tracker.allocationCount - allocations,
        g_tracker.allocatedBytes - allocatedBytes, true};
}

std::size_t countNodes(const lumen::core::RenderNode& node) {
    std::size_t count = 1;
    for (const auto& child : node.children) count += countNodes(child);
    return count;
}

std::size_t countNodes(const lumen::dsl::DesignPreviewOutlineNode& node) {
    std::size_t count = 1;
    for (const auto& child : node.children) count += countNodes(child);
    return count;
}

void requireBenchmark(bool condition, const char* message) {
    if (!condition) throw std::runtime_error{message};
}

FixtureSample runFixture(FixtureKind kind) {
    const std::size_t nodes = kind == FixtureKind::L0 ? 12
                              : kind == FixtureKind::Edit ? 100 : 1000;
    const auto document = kind == FixtureKind::VirtualList
                              ? makeVirtualListDocument() : makeDocument(nodes);
    lumen::dsl::DesignPreviewWorkbench editor;
    if (kind == FixtureKind::Edit || kind == FixtureKind::Outline) {
        requireBenchmark(editor.openDocument(document),
                         "unable to prepare the editor fixture");
    }
    const auto editorRebuilds = editor.frame().rebuildCount();
    auto source = std::make_shared<lumen::core::VirtualListController>();
    source->setItemCount(1000);
    source->setEstimatedExtent(32.0F);
    source->setItemBuilder([](std::size_t index) {
        auto item = lumen::core::makeText("Design preview item " +
                                          std::to_string(index));
        item.key = "design-preview-item-" + std::to_string(index);
        item.height = 32.0F;
        return item;
    });
    lumen::dsl::MapDesignRuntimeContext context;
    context.registerTypedReference(
        lumen::dsl::DesignReferenceKind::VirtualSource, "preview_rows",
        source.get(), source);
    std::string importSource{"page baseline { Column(key: \"root\") {"};
    for (std::size_t index = 1; index < 12; ++index) {
        importSource += " Text(\"Designer baseline node " +
                        std::to_string(index) + "\", key: \"node-" +
                        std::to_string(index) + "\")";
    }
    importSource += " } }";

    FixtureSample sample;
    sample.heap = measure([&] {
        lumen::dsl::DesignParseResult imported;
        const auto* activeDocument = &document;
        if (kind == FixtureKind::L0) {
            measurePhase(sample, 0, [&] {
                imported = lumen::dsl::parseLumenSource(importSource,
                                                        "baseline.lumen");
                requireBenchmark(imported.ok(), "L0 import failed");
            });
            activeDocument = &imported.document;
        }
        std::string encoded;
        measurePhase(sample, 1, [&] {
            encoded = lumen::dsl::serializeDesignDocument(*activeDocument);
        });
        lumen::dsl::DesignReadResult read;
        measurePhase(sample, 2, [&] {
            read = lumen::dsl::readDesignDocument(encoded, "baseline.design");
            requireBenchmark(read.ok() && read.document == *activeDocument,
                             "document round trip failed");
        });
        measurePhase(sample, 3, [&] {
            requireBenchmark(
                lumen::dsl::validateDesignDocument(read.document).empty(),
                "schema validation failed");
        });
        lumen::dsl::DesignPreviewFrame frame;
        measurePhase(sample, 4, [&] {
            requireBenchmark(frame.update(read.document, context) &&
                                 frame.diagnostics().empty(),
                             "preview compilation failed");
        });
        sample.documentNodes = frame.trace().nodes.size();
        requireBenchmark(sample.documentNodes ==
                             (kind == FixtureKind::VirtualList ? 1 : nodes),
                         "document fixture shape changed");
        const auto* renderWidget = &frame.widget();
        if (kind == FixtureKind::Edit) {
            measurePhase(sample, 7, [&] {
                requireBenchmark(editor.setProperty(
                                     2, "text", stringValue("Edited node")) &&
                                     editor.dirty(),
                                 "property edit failed");
            });
            renderWidget = &editor.frame().widget();
        }
        lumen::core::RenderNode renderNode;
        measurePhase(sample, 5, [&] {
            renderNode = lumen::layout::LayoutEngine::layout(
                *renderWidget, lumen::core::Constraints::tight(
                                    lumen::core::Size{800.0F, 600.0F}));
        });
        lumen::render::CpuRenderer renderer;
        measurePhase(sample, 6, [&] {
            renderer.beginFrame(lumen::core::Size{800.0F, 600.0F});
            lumen::render::paintScene(renderer, renderNode);
            renderer.endFrame();
        });
        sample.frameHash = lumen::render::frameHash(renderer.pixels());
        sample.renderNodes = countNodes(renderNode);
        if (kind == FixtureKind::VirtualList) {
            sample.materializedItems = source->lastMaterializedItems();
            requireBenchmark(sample.materializedItems > 0 &&
                                 sample.materializedItems < 50,
                             "virtual list materialized the whole source");
        } else {
            requireBenchmark(sample.renderNodes == nodes,
                             "render fixture shape changed");
        }
        if (kind == FixtureKind::Outline) {
            measurePhase(sample, 8, [&] {
                const auto outline = editor.outline();
                requireBenchmark(outline && countNodes(*outline) == nodes,
                                 "outline fixture shape changed");
            });
        }
        sample.rebuilds = frame.rebuildCount() +
                          editor.frame().rebuildCount() - editorRebuilds;
    });
    requireBenchmark(checkSample("designer_pipeline", sample.heap),
                     "invalid scoped heap sample");
    return sample;
}

double percentile(std::vector<double> values, double fraction) {
    std::sort(values.begin(), values.end());
    const auto index = static_cast<std::size_t>(
        std::ceil(fraction * static_cast<double>(values.size()))) - 1;
    return values.at(index);
}

template <typename Projection>
double metric(const std::vector<FixtureSample>& samples,
              Projection&& projection, double fraction = 0.5) {
    std::vector<double> values;
    values.reserve(samples.size());
    for (const auto& sample : samples) {
        values.push_back(static_cast<double>(projection(sample)));
    }
    return percentile(std::move(values), fraction);
}

void printFixture(const char* name, FixtureKind kind, int warmup,
                  int iterations) {
    std::vector<FixtureSample> samples;
    samples.reserve(static_cast<std::size_t>(iterations));
    std::optional<FixtureSample> reference;
    for (int index = 0; index < warmup + iterations; ++index) {
        const auto sample = runFixture(kind);
        if (!reference) reference = sample;
        requireBenchmark(sample.frameHash == reference->frameHash &&
                             sample.documentNodes == reference->documentNodes &&
                             sample.renderNodes == reference->renderNodes &&
                             sample.materializedItems == reference->materializedItems &&
                             sample.rebuilds == reference->rebuilds,
                         "fixture output is not deterministic");
        if (index >= warmup) samples.push_back(sample);
    }
    const auto& first = *reference;
    std::printf("\"%s\":{\"document_nodes\":%zu,\"render_nodes\":%zu,"
                "\"materialized_items\":%zu,\"rebuilds\":%llu,"
                "\"frame_hash\":\"%016llx\",\"heap\":{"
                "\"allocs_p50\":%.0f,\"alloc_bytes_p50\":%.0f,"
                "\"peak_bytes_p50\":%.0f,\"live_bytes_p50\":%.0f},"
                "\"phases\":{",
                name, first.documentNodes, first.renderNodes,
                first.materializedItems,
                static_cast<unsigned long long>(first.rebuilds),
                static_cast<unsigned long long>(first.frameHash),
                metric(samples, [](const auto& s) { return s.heap.allocationCount; }),
                metric(samples, [](const auto& s) { return s.heap.allocatedBytes; }),
                metric(samples, [](const auto& s) { return s.heap.peakBytes; }),
                metric(samples, [](const auto& s) { return s.heap.liveBytes; }));
    bool separator = false;
    for (std::size_t phase = 0; phase < kPhaseNames.size(); ++phase) {
        if (!first.phases[phase].measured) continue;
        std::printf("%s\"%s\":{\"p50_us\":%.3f,\"p95_us\":%.3f,"
                    "\"allocs_p50\":%.0f,\"alloc_bytes_p50\":%.0f}",
                    separator ? "," : "", kPhaseNames[phase],
                    metric(samples, [phase](const auto& s) {
                        return s.phases[phase].microseconds;
                    }),
                    metric(samples, [phase](const auto& s) {
                        return s.phases[phase].microseconds;
                    }, 0.95),
                    metric(samples, [phase](const auto& s) {
                        return s.phases[phase].allocations;
                    }),
                    metric(samples, [phase](const auto& s) {
                        return s.phases[phase].allocatedBytes;
                    }));
        separator = true;
    }
    std::printf("}}");
}

int runBenchmark(int warmup, int iterations) {
    std::printf("{\"schema\":2,\"scope\":\"designer_pipeline_cpp_heap\","
                "\"toolchain\":\"%s\",\"build_type\":\"%s\","
                "\"viewport\":[800,600],\"warmup\":%d,\"iterations\":%d,"
                "\"fixtures\":{", LUMEN_BENCH_TOOLCHAIN,
                LUMEN_BENCH_BUILD_TYPE, warmup, iterations);
    printFixture("l0_12", FixtureKind::L0, warmup, iterations);
    std::printf(",");
    printFixture("edit_100", FixtureKind::Edit, warmup, iterations);
    std::printf(",");
    printFixture("outline_1000", FixtureKind::Outline, warmup, iterations);
    std::printf(",");
    printFixture("virtual_list_1000", FixtureKind::VirtualList, warmup, iterations);
    std::printf("}}\n");
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 1) return runMemoryProbe();
    if (std::string_view{argv[1]} != "--benchmark") {
        std::fprintf(stderr, "usage error: expected --benchmark\n");
        return 2;
    }
    int warmup = 3;
    int iterations = 20;
    for (int index = 2; index < argc; index += 2) {
        const std::string_view option{argv[index]};
        if (index + 1 >= argc ||
            (option != "--warmup" && option != "--iterations")) {
            std::fprintf(stderr, "usage error: invalid benchmark option\n");
            return 2;
        }
        const std::string_view text{argv[index + 1]};
        int value = 0;
        const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() ||
            value < (option == "--warmup" ? 0 : 1) || value > 1000) {
            std::fprintf(stderr, "usage error: sampling count is out of range\n");
            return 2;
        }
        (option == "--warmup" ? warmup : iterations) = value;
    }
    try {
        return runBenchmark(warmup, iterations);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "designer benchmark: %s\n", error.what());
        return 1;
    }
}
