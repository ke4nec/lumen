// Lumen fixed-scene benchmark (v0.2 stage 7A, M7 performance gate).
//
// Builds a deterministic 1080p scene (1920x1080 card grid), then runs the
// full frame pipeline — reconcile, layout, damage, paint — for a fixed number
// of frames while measuring per-phase wall time and heap allocation counts.
// The optional GPU mode uses the same scenes with a hidden SDL OpenGL surface
// and records submit/GPU-wait timings without a readback.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <functional>
#include <fstream>
#include <memory>
#include <cstdlib>
#include <cstring>
#include <map>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef LUMEN_BENCH_HAS_GPU
#include <SDL3/SDL.h>
#include "lumen/render/skia_gpu_renderer.h"
#endif

#include "lumen/core/damage.h"
#include "lumen/core/element.h"
#include "lumen/core/render_node.h"
#include "lumen/core/virtual_list.h"
#include "lumen/core/widget.h"
#include "lumen/layout/layout.h"
#include "lumen/accessibility/semantics.h"
#include "lumen/render/cpu_renderer.h"
#ifdef LUMEN_BENCH_HAS_SKIA
#include "lumen/render/skia_renderer.h"
#endif
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

// --- M7 基准场景 -------------------------------------------------------------------

// 文本密集：6x8 网格的段落卡片（CJK+拉丁+混合方向），每帧一张卡的文本
// 变化（真实 shaping 负载）。
class TextHeavyScene {
  public:
    static Widget paragraph(int index, int frame) {
        const bool active = index == frame % 48;
        Widget text;
        text.type = WidgetType::Text;
        text.multiline = true;
        text.text =
            (active ? std::to_string(frame) : std::to_string(index)) +
            " 号段落：混合方向 abc\u05e9\u05dc\u05d5\u05dd 与中文文本，"
            "grapheme 边界与 shaping 度量共同参与布局与绘制。Entry text "
            "sample with mixed scripts \u4f60\u597d\u4e16\u754c。";
        text.key = "para-" + std::to_string(index);
        return text;
    }

    [[nodiscard]] Widget root(int frame) const {
        std::vector<Widget> cards;
        for (int i = 0; i < 48; ++i) {
            Widget card;
            card.type = WidgetType::Container;
            card.color = lumen::core::Color::fromRGBA(32, 32, 38);
            card.children.push_back(paragraph(i, frame));
            card.key = "pcard-" + std::to_string(i);
            card.flex = 1.0F;
            cards.push_back(std::move(card));
        }
        Widget column;
        column.type = WidgetType::Column;
        column.key = "text-root";
        column.children = std::move(cards);
        return column;
    }
};

// Grid：8 列自适应网格，每帧一格内容变化（M3 场景）。
class GridScene {
  public:
    [[nodiscard]] Widget root(int frame) const {
        std::vector<Widget> cells;
        for (int i = 0; i < 96; ++i) {
            Widget cell;
            cell.type = WidgetType::Text;
            cell.text = (i == frame % 96)
                            ? "cell " + std::to_string(frame)
                            : "cell " + std::to_string(i);
            cell.key = "gcell-" + std::to_string(i);
            cells.push_back(std::move(cell));
        }
        Widget grid = lumen::core::makeGrid(std::move(cells), 0, 180.0F,
                                            8.0F, 8.0F, "bench-grid");
        Widget column;
        column.type = WidgetType::Column;
        column.key = "grid-root";
        column.children.push_back(std::move(grid));
        return column;
    }
};

// 语义 diff：固定树 + 每帧一处语义/文本微变（SemanticsTree 构建 +
// identity diff 负载；对应 Recording bridge 帧成本）。
class SemanticsDiffScene {
  public:
    [[nodiscard]] Widget root(int frame) const {
        std::vector<Widget> rows;
        for (int i = 0; i < 120; ++i) {
            Widget row;
            row.type = WidgetType::Text;
            row.text = (i == frame % 120)
                           ? "row " + std::to_string(frame)
                           : "row " + std::to_string(i);
            row.key = "srow-" + std::to_string(i);
            rows.push_back(std::move(row));
        }
        Widget column;
        column.type = WidgetType::Column;
        column.key = "semantics-root";
        column.children = std::move(rows);
        return column;
    }
};

// 资源上传：每帧 register/unregister 一张小位图并 drawImage（UploadImage/
// UnloadImage 命令路径）。
class ResourceUploadScene {
  public:
    [[nodiscard]] Widget root(int frame) const {
        std::vector<Widget> items;
        for (int i = 0; i < 8; ++i) {
            Widget image;
            image.type = WidgetType::Image;
            // 每 4 帧换一批 id：上传/卸载循环。
            image.imageId =
                static_cast<std::uint64_t>((frame / 4) * 8 + i + 1);
            image.width = 120.0F;
            image.height = 80.0F;
            image.key = "img-" + std::to_string(i);
            items.push_back(std::move(image));
        }
        Widget row;
        row.type = WidgetType::Row;
        row.key = "upload-root";
        row.children = std::move(items);
        return row;
    }
};

// --- Benchmark app ----------------------------------------------------------------

// Minimal frame pipeline mirroring CounterApp: reconcile a changed template,
// layout, collect damage, repaint the damaged region on the preserved frame.
class BenchApp {
  public:
    using RootBuilder = std::function<Widget(int)>;

    explicit BenchApp(RootBuilder rootAt, const std::string& backend,
                      bool semanticsPerFrame = false,
                      bool uploadPerFrame = false)
        : rootAt_(std::move(rootAt)),
          semanticsPerFrame_(semanticsPerFrame),
          uploadPerFrame_(uploadPerFrame) {
        // M7：Element 快照去子化——布局输入为本地完整树。
        Widget root0 = rootAt_(0);
        previousRoot_ = lumen::layout::LayoutEngine::layout(
            root0, Constraints::tight(Size{kViewportWidth, kViewportHeight}));
        element_.emplace(std::move(root0));
#ifdef LUMEN_BENCH_HAS_SKIA
        if (backend == "skia") {
            skia_ = std::make_unique<lumen::render::SkiaRenderer>(1.0F);
        }
#else
        (void)backend;
#endif
#ifdef LUMEN_BENCH_HAS_GPU
        if (backend == "gpu") {
            if (!SDL_Init(SDL_INIT_VIDEO)) {
                throw std::runtime_error(std::string("SDL_Init: ") + SDL_GetError());
            }
            sdlVideoInitialized_ = true;
            sdlWindow_ = SDL_CreateWindow("lumen-scene-bench", 1920, 1080,
                                          SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
            if (sdlWindow_ == nullptr) {
                SDL_QuitSubSystem(SDL_INIT_VIDEO);
                sdlVideoInitialized_ = false;
                throw std::runtime_error(std::string("SDL_CreateWindow: ") + SDL_GetError());
            }
            lumen::render::SkiaGpuRendererDesc desc;
            desc.sdlWindow = sdlWindow_;
            desc.widthPixels = static_cast<int>(kViewportWidth);
            desc.heightPixels = static_cast<int>(kViewportHeight);
            desc.vsync = false;
            desc.allowSwap = false;
            std::string diagnostics;
            gpu_ = lumen::render::createSkiaGpuRenderer(desc, &diagnostics);
            if (gpu_ == nullptr) {
                SDL_DestroyWindow(sdlWindow_);
                sdlWindow_ = nullptr;
                SDL_QuitSubSystem(SDL_INIT_VIDEO);
                sdlVideoInitialized_ = false;
                throw std::runtime_error("GPU renderer unavailable: " + diagnostics);
            }
        }
#else
        if (backend == "gpu") {
            throw std::runtime_error("GPU benchmark was not compiled");
        }
#endif
        paintFull(previousRoot_);
        hasPrevious_ = true;
    }

    ~BenchApp() {
#ifdef LUMEN_BENCH_HAS_GPU
        gpu_.reset();
        if (sdlWindow_ != nullptr) {
            SDL_DestroyWindow(sdlWindow_);
        }
        if (sdlVideoInitialized_) {
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
        }
#endif
    }

    // One measured frame: card (frame % total) gets new content.
    struct FrameResult {
        std::uint64_t frameHash{0};
        bool partial{false};
        std::size_t nodeCount{0};
    };

    FrameResult runFrame(int frame) {
        FrameResult result;
        // M7：对齐 AppShell 真实管线——build → layout（独立计时）→
        // update(move)。reconcile 相 = build 段 + update 段（与 M0 基线
        // 同口径；Element 快照去子化，update 零拷贝）。
        const AllocSnapshot buildStart = AllocSnapshot{};
        Widget frameTree = rootAt_(frame);
        const PhaseSample buildSample =
            AllocSnapshot{}.elapsedSince(buildStart);
        RenderNode fresh;
        std::vector<lumen::core::Rect> damage;
        bool damageValid = false;
        {
            const AllocSnapshot start = AllocSnapshot{};
            fresh = lumen::layout::LayoutEngine::layout(
                frameTree,
                Constraints::tight(Size{kViewportWidth, kViewportHeight}));
            damageValid = hasPrevious_ &&
                lumen::core::collectDamage(previousRoot_, fresh, damage);
            layout_ = AllocSnapshot{}.elapsedSince(start);
        }
        {
            const AllocSnapshot start = AllocSnapshot{};
            element_->update(std::move(frameTree));
            const PhaseSample updateSample =
                AllocSnapshot{}.elapsedSince(start);
            reconcile_.microseconds =
                buildSample.microseconds + updateSample.microseconds;
            reconcile_.allocations =
                buildSample.allocations + updateSample.allocations;
            reconcile_.allocBytes =
                buildSample.allocBytes + updateSample.allocBytes;
        }
        if (uploadPerFrame_) {
            // M7：资源上传/卸载循环（UploadImage/UnloadImage 命令路径；
            // 每帧一批 8 张 120x80 RGBA，旧批注销）。
            const AllocSnapshot start = AllocSnapshot{};
            for (const auto id : uploadedIds_) {
#ifdef LUMEN_BENCH_HAS_SKIA
                if (skia_ != nullptr) {
                    skia_->unregisterImage(id);
                } else {
                    renderer_.unregisterImage(id);
                }
#else
                renderer_.unregisterImage(id);
#endif
            }
            uploadedIds_.clear();
            for (int i = 0; i < 8; ++i) {
                lumen::render::PixelBuffer buffer;
                buffer.width = 120;
                buffer.height = 80;
                buffer.rgba.assign(
                    static_cast<std::size_t>(120) * 80 * 4, 0x80);
#ifdef LUMEN_BENCH_HAS_SKIA
                const auto id = skia_ != nullptr
                                    ? skia_->registerImage(std::move(buffer))
                                    : renderer_.registerImage(std::move(buffer));
#else
                const auto id = renderer_.registerImage(std::move(buffer));
#endif
                uploadedIds_.push_back(id);
            }
            uploadPhase_ = AllocSnapshot{}.elapsedSince(start);
        }
        if (semanticsPerFrame_) {
            // M7：语义 diff 负载计入 paint 相（Recording bridge 帧成本）。
            lumen::accessibility::SemanticsBuildOptions options2;
            auto tree = lumen::accessibility::buildSemanticsTree(fresh,
                                                                 options2);
            (void)tree;
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
            activeRenderer().submit(commands, info);
            commandCount_ = activeRenderer().stats().commandCount;
            culledCount_ = activeRenderer().stats().culledCommands;
            const auto stats = activeRenderer().stats();
            submitSample_.microseconds = stats.submitMs * 1000.0;
            gpuWaitSample_.microseconds = stats.gpuWaitMs * 1000.0;
            paint_ = AllocSnapshot{}.elapsedSince(start);
        }
        element_->clearDirtyTree();
        previousRoot_ = fresh;
        hasPrevious_ = true;
        result.frameHash = lumen::render::frameHash(pixels());
        result.nodeCount = countNodes(fresh);
        return result;
    }

    [[nodiscard]] lumen::render::Renderer& activeRenderer() {
#ifdef LUMEN_BENCH_HAS_SKIA
        if (skia_ != nullptr) {
            return *skia_;
        }
#endif
#ifdef LUMEN_BENCH_HAS_GPU
        if (gpu_ != nullptr) {
            return *gpu_;
        }
#endif
        return renderer_;
    }
    [[nodiscard]] const PhaseSample& reconcileSample() const { return reconcile_; }
    [[nodiscard]] const PhaseSample& layoutSample() const { return layout_; }
    [[nodiscard]] const PhaseSample& paintSample() const { return paint_; }
    [[nodiscard]] const PhaseSample& submitSample() const { return submitSample_; }
    [[nodiscard]] const PhaseSample& gpuWaitSample() const { return gpuWaitSample_; }
    [[nodiscard]] const lumen::render::PixelBuffer& pixels() const {
#ifdef LUMEN_BENCH_HAS_SKIA
        if (skia_) return skia_->pixels();
#endif
#ifdef LUMEN_BENCH_HAS_GPU
        if (gpu_ != nullptr) {
            // GPU presentation intentionally has no readback; the benchmark
            // still exposes a stable zero-sized hash marker for diagnostics.
            static const lumen::render::PixelBuffer empty{};
            return empty;
        }
#endif
        return renderer_.pixels();
    }
    [[nodiscard]] std::uint64_t commandCount() const { return commandCount_; }
    [[nodiscard]] std::uint64_t culledCount() const { return culledCount_; }

  private:
    void paintFull(const RenderNode& root) {
        lumen::render::FrameInfo info;
        info.viewport = Size{kViewportWidth, kViewportHeight};
        activeRenderer().submit(lumen::render::recordScene(root, {}), info);
    }

    static std::size_t countNodes(const RenderNode& node) {
        std::size_t count = 1;
        for (const auto& child : node.children) {
            count += countNodes(child);
        }
        return count;
    }

    RootBuilder rootAt_;
    bool semanticsPerFrame_{false};
    bool uploadPerFrame_{false};
    std::vector<lumen::render::ImageId> uploadedIds_{};
    PhaseSample uploadPhase_{};
    std::optional<Element> element_{};
    lumen::render::CpuRenderer renderer_{1.0F};
#ifdef LUMEN_BENCH_HAS_SKIA
    std::unique_ptr<lumen::render::SkiaRenderer> skia_{};
#endif
    RenderNode previousRoot_{};
    bool hasPrevious_{false};
    PhaseSample reconcile_{};
    PhaseSample layout_{};
    PhaseSample paint_{};
    PhaseSample submitSample_{};
    PhaseSample gpuWaitSample_{};
    std::uint64_t commandCount_{0};
    std::uint64_t culledCount_{0};
#ifdef LUMEN_BENCH_HAS_GPU
    SDL_Window* sdlWindow_{nullptr};
    bool sdlVideoInitialized_{false};
    std::unique_ptr<lumen::render::Renderer> gpu_{};
#endif
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
    // M7：后端选择（cpu = CpuRenderer；skia = 离屏光栅 SkiaRenderer；
    // gpu = Skia Ganesh on a hidden SDL OpenGL surface）。
    std::string backend{"cpu"};
    std::string dumpFrame{};
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
        } else if (flag == "--dump-frame" && i + 1 < argc) {
            options.dumpFrame = argv[++i];
        } else if (flag == "--backend" && i + 1 < argc) {
            options.backend = argv[++i];
            if (options.backend != "cpu" && options.backend != "skia" &&
                options.backend != "gpu") {
                std::fprintf(stderr, "--backend expects cpu|skia|gpu\n");
                std::exit(2);
            }
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
            } else if (value == "text-heavy" || value == "grid" ||
                       value == "semantics-diff" ||
                       value == "resource-upload") {
                // M7 新场景：名称规范为 <name>-1080p。
                options.scenario = value + "-1080p";
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
                         "[--scenario card-grid-6x8-1080p|virtual-list[-N]|text-heavy|"
                         "grid|semantics-diff|resource-upload] [--items N] "
                         "[--backend cpu|skia|gpu] [--dump-frame PATH]\n");
            std::exit(2);
        }
    }
    return options;
}

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
    std::printf("scenario: %s  backend: %s  toolchain: %s  build_type: %s\n",
                options.scenario.c_str(), options.backend.c_str(),
                benchToolchain().c_str(), benchBuildType().c_str());
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
                int partialFrames, lumen::render::AlphaMode alphaMode) {
    std::printf("{\n");
    std::printf("  \"benchmark\": \"lumen-scene-bench\",\n");
    std::printf("  \"backend\": \"%s\",\n", options.backend.c_str());
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
    std::printf("  \"alpha_mode\": \"%s\",\n", lumen::render::alphaModeName(alphaMode));
    std::printf("  \"clear_alpha\": 255,\n  \"measurement_scope\": \"headless; paint includes submit; no present\",\n");
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
    std::unique_ptr<TextHeavyScene> textScene;
    std::unique_ptr<GridScene> gridScene;
    std::unique_ptr<SemanticsDiffScene> semanticsScene;
    std::unique_ptr<ResourceUploadScene> uploadScene;
    BenchApp::RootBuilder rootAt;
    if (options.scenario.rfind("virtual-list", 0) == 0) {
        listScene = std::make_unique<VirtualListScene>(options.items);
        rootAt = [&listScene](int frame) { return listScene->root(frame); };
    } else if (options.scenario.rfind("text-heavy", 0) == 0) {
        textScene = std::make_unique<TextHeavyScene>();
        rootAt = [textScene = textScene.get()](int frame) {
            return textScene->root(frame);
        };
    } else if (options.scenario.rfind("grid-", 0) == 0) {
        gridScene = std::make_unique<GridScene>();
        rootAt = [gridScene = gridScene.get()](int frame) {
            return gridScene->root(frame);
        };
    } else if (options.scenario.rfind("semantics-diff", 0) == 0) {
        semanticsScene = std::make_unique<SemanticsDiffScene>();
        rootAt = [semanticsScene = semanticsScene.get()](int frame) {
            return semanticsScene->root(frame);
        };
    } else if (options.scenario.rfind("resource-upload", 0) == 0) {
        uploadScene = std::make_unique<ResourceUploadScene>();
        rootAt = [uploadScene = uploadScene.get()](int frame) {
            return uploadScene->root(frame);
        };
    } else {
        rootAt = [](int frame) { return BenchScene::root(frame); };
    }

    if (std::getenv("LUMEN_BENCH_CACHE_TREE") != nullptr) {
        // 诊断开关（M7）：构建一次后每帧深拷贝——分离“场景构建”与
        // “Element diff”成本（见 M7 完成记录的 reconcile 分解）。
        static const Widget cached = BenchScene::root(0);
        rootAt = [](int) { return cached; };
    }
    BenchApp app{std::move(rootAt), options.backend,
                 options.scenario.rfind("semantics-diff", 0) == 0,
                 options.scenario.rfind("resource-upload", 0) == 0};
    std::map<std::string, PhaseStats> phases;
    std::vector<PhaseSample> reconcileSamples;
    std::vector<PhaseSample> layoutSamples;
    std::vector<PhaseSample> paintSamples;
    std::vector<PhaseSample> submitSamples;
    std::vector<PhaseSample> gpuWaitSamples;
    std::vector<PhaseSample> frameSamples;
    reconcileSamples.reserve(static_cast<std::size_t>(options.measuredFrames));
    layoutSamples.reserve(static_cast<std::size_t>(options.measuredFrames));
    paintSamples.reserve(static_cast<std::size_t>(options.measuredFrames));
    submitSamples.reserve(static_cast<std::size_t>(options.measuredFrames));
    gpuWaitSamples.reserve(static_cast<std::size_t>(options.measuredFrames));
    frameSamples.reserve(static_cast<std::size_t>(options.measuredFrames));

    for (int frame = 0; frame < options.warmupFrames; ++frame) {
        (void)app.runFrame(frame);
    }

    std::uint64_t finalHash = 0;
    std::size_t nodeCount = 0;
    int partialFrames = 0;
    for (int frame = 0; frame < options.measuredFrames; ++frame) {
        const auto frameStart = std::chrono::steady_clock::now();
        const auto result = app.runFrame(frame);
        PhaseSample frameSample;
        frameSample.microseconds = std::chrono::duration<double, std::micro>(
            std::chrono::steady_clock::now() - frameStart).count();
        finalHash = result.frameHash;
        nodeCount = result.nodeCount;
        partialFrames += result.partial ? 1 : 0;
        reconcileSamples.push_back(app.reconcileSample());
        layoutSamples.push_back(app.layoutSample());
        paintSamples.push_back(app.paintSample());
        submitSamples.push_back(app.submitSample());
        gpuWaitSamples.push_back(app.gpuWaitSample());
        frameSamples.push_back(frameSample);
    }

    phases["reconcile"] = summarize(reconcileSamples);
    phases["layout"] = summarize(layoutSamples);
    phases["paint"] = summarize(paintSamples);
    phases["submit"] = summarize(submitSamples);
    phases["gpu_wait"] = summarize(gpuWaitSamples);
    phases["frame"] = summarize(frameSamples);

    if (!options.dumpFrame.empty()) {
        const auto& pixels = app.pixels();
        std::ofstream output(options.dumpFrame, std::ios::binary);
        output.write(reinterpret_cast<const char*>(pixels.rgba.data()), pixels.rgba.size());
        std::ofstream metadata(options.dumpFrame + ".txt");
        metadata << "width=" << pixels.width << "\nheight=" << pixels.height
                 << "\nalpha_mode=" << lumen::render::alphaModeName(pixels.alphaMode) << "\n";
        if (!output || !metadata) return 3;
    }

    if (options.json) {
        reportJson(options, phases, finalHash, nodeCount, app.commandCount(),
                   app.culledCount(), partialFrames, app.pixels().alphaMode);
    } else {
        reportText(options, phases, finalHash, nodeCount, app.commandCount(),
                   app.culledCount(), partialFrames);
    }
    return 0;
}
