// Lumen fixed-scene benchmark (v0.2 阶段7A).
//
// Builds a deterministic 1080p scene (1920x1080 card grid), then runs the
// full CPU frame pipeline — reconcile, layout, damage, paint — for a fixed
// number of frames while measuring per-phase wall time and heap allocation
// counts. Frame hashes prove the run painted the expected pixels; the report
// (text or --json) is saved by CI as the CPU baseline for the 10% regression
// gate in the v0.2 plan §5.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <cstdlib>
#include <cstring>
#include <map>
#include <new>
#include <optional>
#include <string>
#include <vector>

#include "lumen/core/damage.h"
#include "lumen/core/element.h"
#include "lumen/core/render_node.h"
#include "lumen/core/virtual_list.h"
#include "lumen/core/widget.h"
#include "lumen/layout/layout.h"
#include "lumen/render/cpu_renderer.h"
#include "lumen/render/painter.h"

namespace {

// --- Global allocation accounting -------------------------------------------------
//
// The benchmark binary replaces the global allocation functions so per-phase
// heap traffic is visible without an external profiler. Counters are lock-free
// atomics; hooks never allocate, so they are safe to hit from anywhere.

std::atomic<std::size_t> g_allocCount{0};
std::atomic<std::size_t> g_allocBytes{0};

void accountAlloc(std::size_t bytes) noexcept {
    g_allocCount.fetch_add(1, std::memory_order_relaxed);
    g_allocBytes.fetch_add(bytes, std::memory_order_relaxed);
}

void accountFree(std::size_t bytes) noexcept {
    g_allocBytes.fetch_sub(bytes, std::memory_order_relaxed);
}

}  // namespace

void* operator new(std::size_t bytes) {
    accountAlloc(bytes);
    void* ptr = std::malloc(bytes ? bytes : 1);
    if (ptr == nullptr) {
        throw std::bad_alloc();
    }
    return ptr;
}

void* operator new[](std::size_t bytes) { return ::operator new(bytes); }

void operator delete(void* ptr) noexcept {
    accountFree(0);
    std::free(ptr);
}

void operator delete[](void* ptr) noexcept { ::operator delete(ptr); }

void operator delete(void* ptr, std::size_t bytes) noexcept {
    accountFree(bytes);
    std::free(ptr);
}

void operator delete[](void* ptr, std::size_t bytes) noexcept {
    ::operator delete(ptr, bytes);
}

namespace {

using lumen::core::Color;
using lumen::core::Constraints;
using lumen::core::CornerRadius;
using lumen::core::Element;
using lumen::core::EdgeInsets;
using lumen::core::RenderNode;
using lumen::core::Size;
using lumen::core::Widget;
using lumen::core::WidgetType;

// --- Scene ------------------------------------------------------------------------

// 6 x 8 card grid at 1080p: ~250 RenderNodes, mixed containers, texts,
// buttons and text fields. Content is a pure function of (card, frame) so
// every run paints identical pixels for a given frame index.
constexpr int kGridColumns = 6;
constexpr int kGridRows = 8;
constexpr float kViewportWidth = 1920.0F;
constexpr float kViewportHeight = 1080.0F;

struct BenchScene {
    // One card mutates per frame (frame % total): realistic partial damage.
    static Widget card(int index, int frame) {
        const bool active = index == frame % (kGridColumns * kGridRows);
        char key[32];
        std::snprintf(key, sizeof(key), "card-%d", index);
        char label[48];
        std::snprintf(label, sizeof(label), "Card %d v%d", index, active ? frame : 0);
        char value[48];
        std::snprintf(value, sizeof(value), "%d items", index * 7 + (active ? frame % 13 : 0));

        Widget text;
        text.type = WidgetType::Text;
        text.text = label;
        text.key = std::string(key) + "-title";
        Widget button;
        button.type = WidgetType::Button;
        button.text = "Open";
        button.key = std::string(key) + "-open";
        Widget field;
        field.type = WidgetType::TextField;
        field.text = value;
        field.placeholder = "Filter";
        field.key = std::string(key) + "-filter";

        Widget column;
        column.type = WidgetType::Column;
        column.key = std::string(key) + "-body";
        column.spacing = 10.0F;
        column.padding = EdgeInsets::all(14.0F);
        column.children = {text, button, field};

        Widget card;
        card.type = WidgetType::Container;
        card.key = key;
        card.radius = CornerRadius::all(8.0F);
        // Deterministic surface color per card.
        card.color = Color::fromRGBA(static_cast<std::uint8_t>(36 + (index * 5) % 24),
                                     static_cast<std::uint8_t>(36 + (index * 3) % 20),
                                     static_cast<std::uint8_t>(44 + (index * 7) % 26), 255);
        card.children = {column};
        return card;
    }

    static Widget root(int frame) {
        std::vector<Widget> cards;
        cards.reserve(static_cast<std::size_t>(kGridColumns * kGridRows));
        for (int row = 0; row < kGridRows; ++row) {
            std::vector<Widget> rowChildren;
            rowChildren.reserve(kGridColumns);
            for (int column = 0; column < kGridColumns; ++column) {
                rowChildren.push_back(card(row * kGridColumns + column, frame));
            }
            Widget rowWidget;
            rowWidget.type = WidgetType::Row;
            rowWidget.key = "row-" + std::to_string(row);
            rowWidget.spacing = 12.0F;
            rowWidget.children = std::move(rowChildren);
            cards.push_back(std::move(rowWidget));
        }
        Widget grid;
        grid.type = WidgetType::Column;
        grid.key = "grid";
        grid.spacing = 12.0F;
        grid.padding = EdgeInsets::all(16.0F);
        grid.children = std::move(cards);

        Widget page;
        page.type = WidgetType::Container;
        page.key = "root";
        page.color = Color::fromRGBA(24, 24, 27, 255);
        page.children = {grid};
        return page;
    }
};

// --- Measurement helpers ----------------------------------------------------------

struct PhaseSample {
    double microseconds{0.0};
    std::size_t allocations{0};
    std::size_t allocBytes{0};
};

// Snapshot of the allocation counters; pairing begin/end around a phase
// attributes heap traffic to that phase. The benchmark is single-threaded,
// so relaxed loads are exact within a phase.
class AllocSnapshot {
  public:
    AllocSnapshot()
        : clock_(std::chrono::steady_clock::now()),
          count_(g_allocCount.load(std::memory_order_relaxed)),
          bytes_(g_allocBytes.load(std::memory_order_relaxed)) {}

    [[nodiscard]] PhaseSample elapsedSince(const AllocSnapshot& start) const {
        PhaseSample sample;
        sample.microseconds =
            std::chrono::duration<double, std::micro>(clock_ - start.clock_).count();
        sample.allocations = count_ - start.count_;
        sample.allocBytes = bytes_ - start.bytes_;
        return sample;
    }

  private:
    std::chrono::steady_clock::time_point clock_;
    std::size_t count_;
    std::size_t bytes_;
};

double percentile(std::vector<double> values, double fraction) {
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const std::size_t index = std::min(
        values.size() - 1,
        static_cast<std::size_t>(std::floor(
            static_cast<double>(values.size() - 1) * fraction)));
    return values[static_cast<std::size_t>(index)];
}

struct PhaseStats {
    double p50Us{0.0};
    double p95Us{0.0};
    double meanUs{0.0};
    std::size_t allocsP50{0};
    std::size_t allocBytesP50{0};
};

PhaseStats summarize(const std::vector<PhaseSample>& samples) {
    PhaseStats stats;
    if (samples.empty()) {
        return stats;
    }
    std::vector<double> times;
    times.reserve(samples.size());
    double total = 0.0;
    for (const auto& sample : samples) {
        times.push_back(sample.microseconds);
        total += sample.microseconds;
    }
    stats.p50Us = percentile(times, 0.50);
    stats.p95Us = percentile(times, 0.95);
    stats.meanUs = total / static_cast<double>(samples.size());
    // Allocation medians: sort samples by time and take the mid sample's counts.
    std::vector<PhaseSample> sorted = samples;
    std::sort(sorted.begin(), sorted.end(),
              [](const PhaseSample& a, const PhaseSample& b) {
                  return a.microseconds < b.microseconds;
              });
    stats.allocsP50 = sorted[sorted.size() / 2].allocations;
    stats.allocBytesP50 = sorted[sorted.size() / 2].allocBytes;
    return stats;
}

// --- M3 virtual-list 场景 ----------------------------------------------------------
//
// 千/万项 VirtualList：每帧滚动 32px（可见窗口移动 → 物化/回收）。
// 输出节点数/命令数/分配量验证“不全量构建子树”（出口条件）。

class VirtualListScene {
  public:
    static constexpr char kName[] = "virtual-list";

    explicit VirtualListScene(int items) {
        controller_.setItemCount(static_cast<std::size_t>(items));
        controller_.setEstimatedExtent(44.0F);
        controller_.setItemBuilder([](std::size_t index) {
            Widget item;
            item.type = WidgetType::Text;
            item.text = "Entry " + std::to_string(index);
            item.key = "item-" + std::to_string(index);
            return item;
        });
        controller_.scroll().updateExtents(kViewportHeight,
                                           controller_.totalExtent());
    }

    [[nodiscard]] Widget root(int frame) {
        (void)frame;
        // 逐帧滚动（触底回绕，保证确定性且覆盖回收窗口）。
        controller_.scroll().scrollBy(32.0F);
        if (controller_.scroll().offset() >=
            controller_.scroll().maxScrollOffset()) {
            controller_.scroll().scrollTo(0.0F);
        }
        return lumen::core::makeVirtualList(&controller_, "bench-list",
                                            std::nullopt, kViewportHeight,
                                            200.0F);
    }

  private:
    mutable lumen::core::VirtualListController controller_{};
};

// --- Benchmark app ----------------------------------------------------------------

// Minimal frame pipeline mirroring CounterApp: reconcile a changed template,
// layout, collect damage, repaint the damaged region on the preserved frame.
class BenchApp {
  public:
    using RootBuilder = std::function<Widget(int)>;

    explicit BenchApp(RootBuilder rootAt)
        : rootAt_(std::move(rootAt)), element_(rootAt_(0)) {
        previousRoot_ = lumen::layout::LayoutEngine::layout(
            element_.widget(), Constraints::tight(Size{kViewportWidth, kViewportHeight}));
        paintFull(previousRoot_);
        hasPrevious_ = true;
    }

    // One measured frame: card (frame % total) gets new content.
    struct FrameResult {
        std::uint64_t frameHash{0};
        bool partial{false};
        std::size_t nodeCount{0};
    };

    FrameResult runFrame(int frame) {
        FrameResult result;
        {
            const AllocSnapshot start = AllocSnapshot{};
            element_.update(rootAt_(frame));
            reconcile_ = AllocSnapshot{}.elapsedSince(start);
        }
        RenderNode fresh;
        std::vector<lumen::core::Rect> damage;
        bool damageValid = false;
        {
            const AllocSnapshot start = AllocSnapshot{};
            fresh = lumen::layout::LayoutEngine::layout(
                element_.widget(),
                Constraints::tight(Size{kViewportWidth, kViewportHeight}));
            damageValid = hasPrevious_ &&
                lumen::core::collectDamage(previousRoot_, fresh, damage);
            layout_ = AllocSnapshot{}.elapsedSince(start);
        }
        {
            const AllocSnapshot start = AllocSnapshot{};
            const auto bounds = lumen::core::damageBounds(damage,
                                                          Size{kViewportWidth, kViewportHeight});
            const bool partial = damageValid && bounds.has_value();
            // v0.2 阶段7B 命令路径：录制一帧命令并按 damage 提交。
            lumen::render::RenderCommandList commands =
                lumen::render::recordScene(fresh, {});
            lumen::render::FrameInfo info;
            info.viewport = Size{kViewportWidth, kViewportHeight};
            info.frameIndex = static_cast<std::uint64_t>(frame);
            if (partial) {
                info.damage = bounds;
                info.preservePrevious = true;
                result.partial = true;
            }
            renderer_.submit(commands, info);
            commandCount_ = renderer_.stats().commandCount;
            culledCount_ = renderer_.stats().culledCommands;
            paint_ = AllocSnapshot{}.elapsedSince(start);
        }
        element_.clearDirtyTree();
        previousRoot_ = fresh;
        hasPrevious_ = true;
        result.frameHash = lumen::render::frameHash(renderer_.pixels());
        result.nodeCount = countNodes(fresh);
        return result;
    }

    [[nodiscard]] const PhaseSample& reconcileSample() const { return reconcile_; }
    [[nodiscard]] const PhaseSample& layoutSample() const { return layout_; }
    [[nodiscard]] const PhaseSample& paintSample() const { return paint_; }
    [[nodiscard]] std::uint64_t commandCount() const { return commandCount_; }
    [[nodiscard]] std::uint64_t culledCount() const { return culledCount_; }

  private:
    void paintFull(const RenderNode& root) {
        lumen::render::FrameInfo info;
        info.viewport = Size{kViewportWidth, kViewportHeight};
        renderer_.submit(lumen::render::recordScene(root, {}), info);
    }

    static std::size_t countNodes(const RenderNode& node) {
        std::size_t count = 1;
        for (const auto& child : node.children) {
            count += countNodes(child);
        }
        return count;
    }

    RootBuilder rootAt_;
    Element element_;
    lumen::render::CpuRenderer renderer_{1.0F};
    RenderNode previousRoot_{};
    bool hasPrevious_{false};
    PhaseSample reconcile_{};
    PhaseSample layout_{};
    PhaseSample paint_{};
    std::uint64_t commandCount_{0};
    std::uint64_t culledCount_{0};
};

// --- Report -----------------------------------------------------------------------

struct Options {
    int warmupFrames{30};
    int measuredFrames{300};
    bool json{false};
    // M3：场景选择。card-grid（M0 基线，禁止改动）或 virtual-list
    //（千/万项可见区物化；items 可配）。
    std::string scenario{"card-grid-6x8-1080p"};
    int items{1000};
    // 非 0 = 当前为 virtual-list 场景（--items 更新场景名）。
    int scenarioItems{0};
};

Options parseOptions(int argc, char** argv) {
    Options options;
    const auto itemCount = [](const std::string& value) {
        int count = 0;
        const auto result = std::from_chars(
            value.data(), value.data() + value.size(), count);
        if (result.ec != std::errc{} ||
            result.ptr != value.data() + value.size() || count <= 0) {
            std::fprintf(stderr, "item count must be a positive integer\n");
            std::exit(2);
        }
        return count;
    };
    for (int i = 1; i < argc; ++i) {
        const std::string flag = argv[i];
        if (flag == "--frames" && i + 1 < argc) {
            options.measuredFrames = std::max(1, std::atoi(argv[++i]));
        } else if (flag == "--warmup" && i + 1 < argc) {
            options.warmupFrames = std::max(0, std::atoi(argv[++i]));
        } else if (flag == "--json") {
            options.json = true;
        } else if (flag == "--scenario" && i + 1 < argc) {
            const std::string value = argv[++i];
            if (value == "virtual-list") {
                options.scenario = "virtual-list-" + std::to_string(options.items) +
                                   "-1080p";
                options.scenarioItems = 1;
            } else if (value.rfind("virtual-list-", 0) == 0) {
                // virtual-list-<items>：场景名携带项数。
                options.items = itemCount(value.substr(13));
                options.scenario = "virtual-list-" + std::to_string(options.items) +
                                   "-1080p";
                options.scenarioItems = 1;
            } else if (value == "card-grid-6x8-1080p") {
                options.scenario = value;
                options.scenarioItems = 0;
            } else {
                std::fprintf(stderr, "unknown scenario: %s\n", value.c_str());
                std::exit(2);
            }
        } else if (flag == "--items" && i + 1 < argc) {
            options.items = itemCount(argv[++i]);
            if (options.scenarioItems != 0) {
                options.scenario =
                    "virtual-list-" + std::to_string(options.items) + "-1080p";
            }
        } else {
            std::fprintf(stderr,
                         "usage: lumen-scene-bench [--frames N] [--warmup N] [--json] "
                         "[--scenario card-grid-6x8-1080p|virtual-list[-N]] [--items N]\n");
            std::exit(2);
        }
    }
    return options;
}

const char* kPhaseNames[] = {"reconcile", "layout", "paint"};

// M0 基线冻结：基线 JSON 必须携带 backend/scenario/viewport/warmup/
// measured/toolchain/build_type/frame_hash/各阶段 p50/p95/分配统计，
// 禁止用不同场景或不同后端直接比较（比较规则见
// docs/perf-baselines/README.md）。toolchain/commit/platform 允许经环境
// 变量覆盖，便于 CI 归档带元数据的 artifact；本地默认值为编译期信息。
constexpr char kBenchScenario[] = "card-grid-6x8-1080p";

std::string benchEnv(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr ? std::string(value) : std::string();
}

std::string jsonEscape(const std::string& input) {
    std::string output;
    output.reserve(input.size());
    for (char c : input) {
        switch (c) {
            case '"': output += "\\\""; break;
            case '\\': output += "\\\\"; break;
            case '\n': output += "\\n"; break;
            case '\r': output += "\\r"; break;
            case '\t': output += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    output += buf;
                } else {
                    output += c;
                }
                break;
        }
    }
    return output;
}

std::string benchToolchain() {
    std::string override = benchEnv("LUMEN_BENCH_TOOLCHAIN");
    if (!override.empty()) {
        return override;
    }
#ifdef __VERSION__
    return __VERSION__;
#elif defined(_MSC_VER)
    return "MSVC " + std::to_string(_MSC_VER);
#else
    return "unknown-toolchain";
#endif
}

std::string benchBuildType() {
    std::string override = benchEnv("LUMEN_BENCH_BUILD_TYPE");
    if (!override.empty()) {
        return override;
    }
#ifdef LUMEN_BENCH_BUILD_TYPE
    return LUMEN_BENCH_BUILD_TYPE;
#else
    return "unknown";
#endif
}

std::string benchCommit() {
    for (const char* name : {"LUMEN_BENCH_COMMIT", "GITHUB_SHA"}) {
        std::string value = benchEnv(name);
        if (!value.empty()) {
            return value;
        }
    }
    return "working-tree";
}

std::string benchPlatform() {
    for (const char* name : {"LUMEN_BENCH_PLATFORM", "RUNNER_OS"}) {
        std::string value = benchEnv(name);
        if (!value.empty()) {
            return value;
        }
    }
#if defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#elif defined(__linux__)
    return "linux";
#else
    return "unknown";
#endif
}

void reportText(const Options& options, const std::map<std::string, PhaseStats>& phases,
                std::uint64_t finalHash, std::size_t nodeCount,
                std::uint64_t commandCount, std::uint64_t culledCount,
                int partialFrames) {
    std::printf("lumen-scene-bench (v0.2 stage 7A CPU baseline, 7B command path)\n");
    std::printf("scenario: %s  backend: cpu  toolchain: %s  build_type: %s\n",
                options.scenario.c_str(), benchToolchain().c_str(),
                benchBuildType().c_str());
    std::printf("commit: %s  platform: %s\n", benchCommit().c_str(),
                benchPlatform().c_str());
    std::printf("viewport: %.0fx%.0f  cards: %dx%d  nodes: %zu  commands/frame: %llu  culled/partial-frame: %llu\n",
                kViewportWidth, kViewportHeight, kGridColumns, kGridRows, nodeCount,
                static_cast<unsigned long long>(commandCount),
                static_cast<unsigned long long>(culledCount));
    std::printf("warmup: %d  frames: %d  partial-repaint frames: %d\n",
                options.warmupFrames, options.measuredFrames, partialFrames);
    for (const auto& [name, stats] : phases) {
        std::printf("%-10s p50 %8.1f us  p95 %8.1f us  mean %8.1f us  "
                    "allocs p50 %5zu  bytes p50 %7zu\n",
                    name.c_str(), stats.p50Us, stats.p95Us, stats.meanUs,
                    stats.allocsP50, stats.allocBytesP50);
    }
    std::printf("frame-hash %016llx\n", static_cast<unsigned long long>(finalHash));
}

void reportJson(const Options& options, const std::map<std::string, PhaseStats>& phases,
                std::uint64_t finalHash, std::size_t nodeCount,
                std::uint64_t commandCount, std::uint64_t culledCount,
                int partialFrames) {
    std::printf("{\n");
    std::printf("  \"benchmark\": \"lumen-scene-bench\",\n");
    std::printf("  \"backend\": \"cpu\",\n");
    std::printf("  \"scenario\": \"%s\",\n", options.scenario.c_str());
    std::printf("  \"viewport\": [%.0f, %.0f],\n", kViewportWidth, kViewportHeight);
    std::printf("  \"cards\": [%d, %d],\n", kGridColumns, kGridRows);
    std::printf("  \"nodes\": %zu,\n", nodeCount);
    std::printf("  \"commands_per_frame\": %llu,\n",
                static_cast<unsigned long long>(commandCount));
    std::printf("  \"culled_commands\": %llu,\n",
                static_cast<unsigned long long>(culledCount));
    std::printf("  \"warmup_frames\": %d,\n", options.warmupFrames);
    std::printf("  \"measured_frames\": %d,\n", options.measuredFrames);
    std::printf("  \"partial_repaint_frames\": %d,\n", partialFrames);
    std::printf("  \"toolchain\": \"%s\",\n", jsonEscape(benchToolchain()).c_str());
    std::printf("  \"build_type\": \"%s\",\n", jsonEscape(benchBuildType()).c_str());
    std::printf("  \"commit\": \"%s\",\n", jsonEscape(benchCommit()).c_str());
    std::printf("  \"platform\": \"%s\",\n", jsonEscape(benchPlatform()).c_str());
    std::printf("  \"phases\": {\n");
    bool first = true;
    for (const auto& [name, stats] : phases) {
        std::printf("%s    \"%s\": {\"p50_us\": %.1f, \"p95_us\": %.1f, "
                    "\"mean_us\": %.1f, \"allocs_p50\": %zu, \"alloc_bytes_p50\": %zu}",
                    first ? "" : ",\n", name.c_str(), stats.p50Us, stats.p95Us,
                    stats.meanUs, stats.allocsP50, stats.allocBytesP50);
        first = false;
    }
    std::printf("\n  },\n");
    std::printf("  \"frame_hash\": \"%016llx\"\n",
                static_cast<unsigned long long>(finalHash));
    std::printf("}\n");
}

}  // namespace

int main(int argc, char** argv) {
    Options options = parseOptions(argc, argv);
    std::unique_ptr<VirtualListScene> listScene;
    BenchApp::RootBuilder rootAt;
    if (options.scenario.rfind("virtual-list", 0) == 0) {
        listScene = std::make_unique<VirtualListScene>(options.items);
        rootAt = [&listScene](int frame) { return listScene->root(frame); };
    } else {
        rootAt = [](int frame) { return BenchScene::root(frame); };
    }

    BenchApp app{std::move(rootAt)};
    std::map<std::string, PhaseStats> phases;
    std::vector<PhaseSample> reconcileSamples;
    std::vector<PhaseSample> layoutSamples;
    std::vector<PhaseSample> paintSamples;
    reconcileSamples.reserve(static_cast<std::size_t>(options.measuredFrames));
    layoutSamples.reserve(static_cast<std::size_t>(options.measuredFrames));
    paintSamples.reserve(static_cast<std::size_t>(options.measuredFrames));

    for (int frame = 0; frame < options.warmupFrames; ++frame) {
        (void)app.runFrame(frame);
    }

    std::uint64_t finalHash = 0;
    std::size_t nodeCount = 0;
    int partialFrames = 0;
    for (int frame = 0; frame < options.measuredFrames; ++frame) {
        const auto result = app.runFrame(frame);
        finalHash = result.frameHash;
        nodeCount = result.nodeCount;
        partialFrames += result.partial ? 1 : 0;
        reconcileSamples.push_back(app.reconcileSample());
        layoutSamples.push_back(app.layoutSample());
        paintSamples.push_back(app.paintSample());
    }

    phases["reconcile"] = summarize(reconcileSamples);
    phases["layout"] = summarize(layoutSamples);
    phases["paint"] = summarize(paintSamples);

    if (options.json) {
        reportJson(options, phases, finalHash, nodeCount, app.commandCount(),
                   app.culledCount(), partialFrames);
    } else {
        reportText(options, phases, finalHash, nodeCount, app.commandCount(),
                   app.culledCount(), partialFrames);
    }
    return 0;
}
