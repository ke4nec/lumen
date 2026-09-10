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
#include <cmath>
#include <cstdio>
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
    static Widget card(int index, int frame) {
        char key[32];
        std::snprintf(key, sizeof(key), "card-%d", index);
        char label[48];
        std::snprintf(label, sizeof(label), "Card %d v%d", index, frame);
        char value[48];
        std::snprintf(value, sizeof(value), "%d items", index * 7 + frame % 13);

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

// --- Benchmark app ----------------------------------------------------------------

// Minimal frame pipeline mirroring CounterApp: reconcile a changed template,
// layout, collect damage, repaint the damaged region on the preserved frame.
class BenchApp {
  public:
    BenchApp() : element_(BenchScene::root(0)) {
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
            element_.update(BenchScene::root(frame));
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
            if (partial) {
                renderer_.beginFrame(Size{kViewportWidth, kViewportHeight},
                                     lumen::render::CpuRenderer::FrameMode::Preserve,
                                     *bounds);
                renderer_.save();
                renderer_.clipRect(*bounds);
                lumen::render::paintScene(renderer_, fresh, {});
                renderer_.restore();
                result.partial = true;
            } else {
                renderer_.beginFrame(Size{kViewportWidth, kViewportHeight});
                lumen::render::paintScene(renderer_, fresh, {});
            }
            renderer_.endFrame();
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

  private:
    void paintFull(const RenderNode& root) {
        renderer_.beginFrame(Size{kViewportWidth, kViewportHeight});
        lumen::render::paintScene(renderer_, root, {});
        renderer_.endFrame();
    }

    static std::size_t countNodes(const RenderNode& node) {
        std::size_t count = 1;
        for (const auto& child : node.children) {
            count += countNodes(child);
        }
        return count;
    }

    Element element_;
    lumen::render::CpuRenderer renderer_{1.0F};
    RenderNode previousRoot_{};
    bool hasPrevious_{false};
    PhaseSample reconcile_{};
    PhaseSample layout_{};
    PhaseSample paint_{};
};

// --- Report -----------------------------------------------------------------------

// Counts paint operations flowing into the renderer so the report shows the
// per-frame draw-call load (stage 7B replaces this with the recorded command
// count).
class CountingRenderer final : public lumen::render::Renderer {
  public:
    explicit CountingRenderer(lumen::render::Renderer& target) : target_(target) {}
    void beginFrame(Size viewport) override { target_.beginFrame(viewport); }
    void save() override {
        ++saves_;
        target_.save();
    }
    void restore() override { target_.restore(); }
    void clipRect(lumen::core::Rect rect) override {
        ++clips_;
        target_.clipRect(rect);
    }
    void drawRect(lumen::core::Rect rect, Color color,
                  CornerRadius radius) override {
        ++drawCalls_;
        target_.drawRect(rect, color, radius);
    }
    void drawText(lumen::render::TextRun run, lumen::core::TextStyle style) override {
        ++drawCalls_;
        target_.drawText(std::move(run), style);
    }
    void drawImage(lumen::render::ImageId id, lumen::core::Rect destination) override {
        ++drawCalls_;
        target_.drawImage(id, destination);
    }
    void endFrame() override { target_.endFrame(); }
    [[nodiscard]] std::size_t drawCalls() const { return drawCalls_; }

  private:
    lumen::render::Renderer& target_;
    std::size_t saves_{0};
    std::size_t clips_{0};
    std::size_t drawCalls_{0};
};

struct Options {
    int warmupFrames{30};
    int measuredFrames{300};
    bool json{false};
};

Options parseOptions(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string flag = argv[i];
        if (flag == "--frames" && i + 1 < argc) {
            options.measuredFrames = std::max(1, std::atoi(argv[++i]));
        } else if (flag == "--warmup" && i + 1 < argc) {
            options.warmupFrames = std::max(0, std::atoi(argv[++i]));
        } else if (flag == "--json") {
            options.json = true;
        } else {
            std::fprintf(stderr,
                         "usage: lumen-scene-bench [--frames N] [--warmup N] [--json]\n");
            std::exit(2);
        }
    }
    return options;
}

const char* kPhaseNames[] = {"reconcile", "layout", "paint"};

void reportText(const Options& options, const std::map<std::string, PhaseStats>& phases,
                std::uint64_t finalHash, std::size_t nodeCount, std::size_t drawCalls,
                int partialFrames) {
    std::printf("lumen-scene-bench (v0.2 stage 7A CPU baseline)\n");
    std::printf("viewport: %.0fx%.0f  cards: %dx%d  nodes: %zu  draw-calls/frame: %zu\n",
                kViewportWidth, kViewportHeight, kGridColumns, kGridRows, nodeCount,
                drawCalls);
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
                std::uint64_t finalHash, std::size_t nodeCount, std::size_t drawCalls,
                int partialFrames) {
    std::printf("{\n");
    std::printf("  \"benchmark\": \"lumen-scene-bench\",\n");
    std::printf("  \"backend\": \"cpu\",\n");
    std::printf("  \"viewport\": [%.0f, %.0f],\n", kViewportWidth, kViewportHeight);
    std::printf("  \"cards\": [%d, %d],\n", kGridColumns, kGridRows);
    std::printf("  \"nodes\": %zu,\n", nodeCount);
    std::printf("  \"draw_calls\": %zu,\n", drawCalls);
    std::printf("  \"warmup_frames\": %d,\n", options.warmupFrames);
    std::printf("  \"measured_frames\": %d,\n", options.measuredFrames);
    std::printf("  \"partial_repaint_frames\": %d,\n", partialFrames);
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
    const Options options = parseOptions(argc, argv);

    BenchApp app;
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

    // Draw calls per frame: replay one full frame through the counting
    // decorator (same scene as frame 0; outside the timed region above).
    std::size_t drawCalls = 0;
    {
        Element probe(BenchScene::root(0));
        const RenderNode fresh = lumen::layout::LayoutEngine::layout(
            probe.widget(), Constraints::tight(Size{kViewportWidth, kViewportHeight}));
        lumen::render::CpuRenderer renderer{1.0F};
        CountingRenderer counter{renderer};
        renderer.beginFrame(Size{kViewportWidth, kViewportHeight});
        lumen::render::paintScene(counter, fresh, {});
        renderer.endFrame();
        drawCalls = counter.drawCalls();
    }

    if (options.json) {
        reportJson(options, phases, finalHash, nodeCount, drawCalls, partialFrames);
    } else {
        reportText(options, phases, finalHash, nodeCount, drawCalls, partialFrames);
    }
    return 0;
}
