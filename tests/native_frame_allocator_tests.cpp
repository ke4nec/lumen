// Native backend contracts: docs/lumen-frame-allocator-design.md, sections 2-5.
// Assertions run after finish: Catch2's own allocations must not enter samples.
#include <catch2/catch_test_macros.hpp>

#include <SDL3/SDL_stdinc.h>

#include <array>
#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <dlfcn.h>
#include <limits>
#include <malloc.h>
#include <new>
#include <stdexcept>
#include <thread>
#include <unistd.h>
#include <sys/wait.h>

#include "lumen/app/app_shell.h"
#include "lumen/platform/fake_host.h"
#include "lumen/platform/frame_allocator.h"
#include "native_frame_allocator_api.h"

extern "C" {
void* __libc_malloc(std::size_t) noexcept;
void* __libc_calloc(std::size_t, std::size_t) noexcept;
void* __libc_realloc(void*, std::size_t) noexcept;
void __libc_free(void*) noexcept;
}

namespace {
using lumen::platform::detail::NativeFrameAllocatorApi;

const NativeFrameAllocatorApi* nativeApi() {
    using Getter = const NativeFrameAllocatorApi* (*)() noexcept;
    auto getter = reinterpret_cast<Getter>(
        dlsym(RTLD_DEFAULT, "lumen_native_frame_allocator_v1"));
    return getter != nullptr ? getter() : nullptr;
}

std::atomic<int>* callbackPhase{nullptr};
void* SDLCALL mallocWithBackground(std::size_t bytes) {
    int expected = 1;
    if (callbackPhase && callbackPhase->compare_exchange_strong(expected, 2)) {
        while (callbackPhase->load(std::memory_order_acquire) != 3) {}
    }
    return std::malloc(bytes);
}
}  // namespace

TEST_CASE("custom SDL allocator bypassing native hooks is unavailable",
          "[native_allocator][sdl_untracked]") {
    REQUIRE(SDL_SetMemoryFunctions(__libc_malloc, __libc_calloc,
                                   __libc_realloc, __libc_free));
    CHECK(lumen::platform::makeNativeFrameAllocationSource() == nullptr);
}

TEST_CASE("factory permits background allocations retained during its probe",
          "[native_allocator][background_probe]") {
    std::atomic<int> phase{0};
    callbackPhase = &phase;
    REQUIRE(SDL_SetMemoryFunctions(mallocWithBackground, std::calloc,
                                   std::realloc, std::free));
    std::thread worker([&] {
        phase.store(1, std::memory_order_release);
        while (phase.load(std::memory_order_acquire) == 1) {}
        if (phase.load(std::memory_order_acquire) == 4) return;
        // Retain a native allocation until the factory's probe has finished.
        void* retained = std::malloc(512);
        phase.store(3, std::memory_order_release);
        while (phase.load(std::memory_order_acquire) != 4) {}
        std::free(retained);
    });
    while (phase.load(std::memory_order_acquire) != 1) {}
    auto source = lumen::platform::makeNativeFrameAllocationSource();
    phase.store(4, std::memory_order_release);
    worker.join();
    callbackPhase = nullptr;
    REQUIRE(source != nullptr);
    source->beginFrame(0);
    const auto empty = source->finishFrame();
    CHECK(empty.available);
    CHECK(empty.allocationCount == 0);
}

TEST_CASE("native frame allocator requires complete installed bindings",
          "[native_allocator][absent]") {
    CHECK(lumen::platform::makeNativeFrameAllocationSource() == nullptr);
}

TEST_CASE("native malloc calloc realloc report requested bytes and scoped peak",
          "[native_allocator][present]") {
    auto source = lumen::platform::makeNativeFrameAllocationSource();
    REQUIRE(source != nullptr);
    source->beginFrame(0);
    void* first = std::malloc(16);
    void* second = std::calloc(3, 4);
    void* grown = std::realloc(first, 24);
    std::free(second);
    auto stats = source->finishFrame();
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    REQUIRE(grown != nullptr);
    CHECK(stats.available);
    CHECK(stats.source == "glibc/malloc");
    CHECK(stats.allocationCount == 3);
    CHECK(stats.allocatedBytes == 52);
    CHECK(stats.peakBytes == 36);
    CHECK(stats.liveBytes == 24);

    // A previous scope's retained allocation is excluded from the new epoch.
    source->beginFrame(0);
    void* next = std::malloc(7);
    std::free(grown);
    std::free(next);
    stats = source->finishFrame();
    CHECK(stats.available);
    CHECK(stats.allocationCount == 1);
    CHECK(stats.allocatedBytes == 7);
    CHECK(stats.peakBytes == 7);
    CHECK(stats.liveBytes == 0);
}

TEST_CASE("failed native allocations preserve live bytes and errno semantics",
          "[native_allocator][present]") {
    auto source = lumen::platform::makeNativeFrameAllocationSource();
    REQUIRE(source != nullptr);
    volatile std::size_t huge = std::numeric_limits<std::size_t>::max();
    source->beginFrame(0);
    void* original = std::malloc(24);
    void* failed = std::realloc(original, huge);
    const int failureErrno = errno;
    void* overflow = std::calloc(huge, 2);
    void* aligned = original;
    errno = EDOM;
    const int invalidAlignment = posix_memalign(&aligned, 3, 32);
    const int alignmentErrno = errno;
    std::free(nullptr);
    const int freeErrno = errno;
    auto stats = source->finishFrame();
    REQUIRE(original != nullptr);
    CHECK(failed == nullptr);
    CHECK(failureErrno == ENOMEM);
    CHECK(overflow == nullptr);
    CHECK(invalidAlignment == EINVAL);
    CHECK(aligned == original);
    CHECK(alignmentErrno == EDOM);
    CHECK(freeErrno == EDOM);
    CHECK(stats.available);
    CHECK(stats.allocationCount == 1);
    CHECK(stats.allocatedBytes == 24);
    CHECK(stats.peakBytes == 24);
    CHECK(stats.liveBytes == 24);
    std::free(original);

    source->beginFrame(0);
    original = std::malloc(29);
    void* zero = std::realloc(original, 0);
    stats = source->finishFrame();
    CHECK(zero == nullptr);
    CHECK(stats.available);
    CHECK(stats.allocationCount == 1);
    CHECK(stats.allocatedBytes == 29);
    CHECK(stats.liveBytes == 0);
}

TEST_CASE("native aligned families and sized frees share the frame ledger",
          "[native_allocator][present]") {
    using SizedFree = void (*)(void*, std::size_t) noexcept;
    using AlignedFree = void (*)(void*, std::size_t, std::size_t) noexcept;
    auto sizedFree = reinterpret_cast<SizedFree>(dlsym(RTLD_DEFAULT, "free_sized"));
    auto alignedFree = reinterpret_cast<AlignedFree>(
        dlsym(RTLD_DEFAULT, "free_aligned_sized"));
    REQUIRE(sizedFree != nullptr);
    REQUIRE(alignedFree != nullptr);
    auto source = lumen::platform::makeNativeFrameAllocationSource();
    REQUIRE(source != nullptr);
    source->beginFrame(0);
    void* first = memalign(64, 17);
    void* second = std::aligned_alloc(64, 64);
    void* third = nullptr;
    const int status = posix_memalign(&third, 64, 37);
    void* fourth = valloc(11);
    void* fifth = pvalloc(1);
    const auto usable = malloc_usable_size(first);
    sizedFree(first, 17);
    alignedFree(second, 64, 64);
    std::free(third);
    std::free(fourth);
    std::free(fifth);
    auto stats = source->finishFrame();
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    REQUIRE(status == 0);
    REQUIRE(fourth != nullptr);
    REQUIRE(fifth != nullptr);
    CHECK(usable >= 17);
    CHECK(stats.available);
    CHECK(stats.allocationCount == 5);
    CHECK(stats.allocatedBytes == 129 + static_cast<std::uint64_t>(getpagesize()));
    CHECK(stats.peakBytes == stats.allocatedBytes);
    CHECK(stats.liveBytes == 0);
}

TEST_CASE("C++ and SDL allocations flow through the native allocator",
          "[native_allocator][present]") {
    auto source = lumen::platform::makeNativeFrameAllocationSource();
    REQUIRE(source != nullptr);
    source->beginFrame(0);
    void* plain = ::operator new(19);
    ::operator delete(plain);
    void* array = ::operator new[](37);
    ::operator delete[](array);
    void* aligned = ::operator new(64, std::align_val_t{64});
    ::operator delete(aligned, std::align_val_t{64});
    aligned = ::operator new[](128, std::align_val_t{64});
    ::operator delete[](aligned, std::align_val_t{64});
    void* sdl = SDL_malloc(29);
    SDL_free(sdl);
    sdl = SDL_calloc(3, 17);
    void* grown = SDL_realloc(sdl, 71);
    SDL_free(grown != nullptr ? grown : sdl);
    const auto stats = source->finishFrame();
    REQUIRE(grown != nullptr);
    CHECK(stats.available);
    CHECK(stats.allocationCount == 7);
    CHECK(stats.allocatedBytes == 399);
    CHECK(stats.peakBytes == 128);
    CHECK(stats.liveBytes == 0);
}

TEST_CASE("native scopes count other threads and cross thread frees",
          "[native_allocator][present]") {
    auto source = lumen::platform::makeNativeFrameAllocationSource();
    REQUIRE(source != nullptr);
    std::atomic<int> phase{0};
    void* shared = nullptr;
    std::thread worker([&] {
        phase.store(1, std::memory_order_release);
        while (phase.load(std::memory_order_acquire) != 2) {}
        std::free(shared);
        void* own = std::malloc(31);
        std::free(own);
        phase.store(3, std::memory_order_release);
        while (phase.load(std::memory_order_acquire) != 4) {}
    });
    while (phase.load(std::memory_order_acquire) != 1) {}
    source->beginFrame(0);
    shared = std::malloc(44);
    phase.store(2, std::memory_order_release);
    while (phase.load(std::memory_order_acquire) != 3) {}
    const auto stats = source->finishFrame();
    phase.store(4, std::memory_order_release);
    worker.join();
    CHECK(stats.available);
    CHECK(stats.allocationCount == 2);
    CHECK(stats.allocatedBytes == 75);
    CHECK(stats.peakBytes == 44);
    CHECK(stats.liveBytes == 0);
}

TEST_CASE("overlap cancellation and stale tokens cannot close another scope",
          "[native_allocator][present]") {
    auto first = lumen::platform::makeNativeFrameAllocationSource();
    auto second = lumen::platform::makeNativeFrameAllocationSource();
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    const auto* api = nativeApi();
    REQUIRE(api != nullptr);
    const auto staleToken = api->begin();
    api->cancel(staleToken);
    first->beginFrame(0);
    second->beginFrame(0);
    const auto busy = second->finishFrame();
    api->cancel(staleToken);
    void* allocation = std::malloc(13);
    std::free(allocation);
    const auto stats = first->finishFrame();
    CHECK_FALSE(busy.available);
    CHECK(busy.source == "glibc/busy");
    CHECK(stats.available);
    CHECK(stats.allocationCount == 1);
    CHECK(stats.allocatedBytes == 13);
    first->cancelFrame();
    first->cancelFrame();
    second->beginFrame(0);
    first->cancelFrame();
    allocation = std::malloc(5);
    std::free(allocation);
    const auto next = second->finishFrame();
    CHECK(next.available);
    CHECK(next.allocatedBytes == 5);
}

TEST_CASE("ledger exhaustion reports unavailable and recovers next frame",
          "[native_allocator][present]") {
    auto source = lumen::platform::makeNativeFrameAllocationSource();
    REQUIRE(source != nullptr);
    std::array<void*, 65537> allocations{};
    source->beginFrame(0);
    for (auto& allocation : allocations) allocation = std::malloc(1);
    const auto overflow = source->finishFrame();
    for (auto* allocation : allocations) std::free(allocation);
    CHECK_FALSE(overflow.available);
    CHECK(overflow.source == "glibc/incomplete");
    CHECK(overflow.allocationCount == 0);
    CHECK(overflow.allocatedBytes == 0);
    source->beginFrame(0);
    const auto empty = source->finishFrame();
    CHECK(empty.available);
    CHECK(empty.allocationCount == 0);
    CHECK(empty.liveBytes == 0);
}

TEST_CASE("AppShell uses real native allocation scopes",
          "[native_allocator][present]") {
    auto source = lumen::platform::makeNativeFrameAllocationSource();
    REQUIRE(source != nullptr);
    lumen::app::ShellConfig config;
    config.initialView = {200, 150};
    config.build = [] { return lumen::core::makeText("native allocations"); };
    lumen::app::AppShell shell(config);
    shell.setFrameAllocationSource(source.get());
    shell.setFrameStatsCapture(true);
    (void)shell.renderFrame();
    auto snapshot = shell.frameDebugSnapshot();
    CHECK(snapshot.frameAllocationAvailable);
    CHECK(snapshot.frameAllocationSource == "glibc/malloc");
    CHECK(snapshot.frameAllocationCount > 0);
    CHECK(snapshot.frameAllocatedBytes > 0);
    CHECK(snapshot.frameAllocationPeakBytes > 0);
    shell.markDirty();
    (void)shell.renderFrame();
    CHECK(shell.frameDebugSnapshot().frameAllocationAvailable);
    shell.setFrameAllocationSource(nullptr);
}

TEST_CASE("fork invalidates parent telemetry and leaves child allocator usable",
          "[native_allocator][present]") {
    auto source = lumen::platform::makeNativeFrameAllocationSource();
    REQUIRE(source != nullptr);
    std::atomic<bool> started{false}, stop{false};
    std::thread worker([&] {
        started.store(true, std::memory_order_release);
        while (!stop.load(std::memory_order_acquire)) {
            void* allocation = std::malloc(64);
            std::free(allocation);
        }
    });
    while (!started.load(std::memory_order_acquire)) {}
    source->beginFrame(0);
    const auto child = fork();
    if (child == 0) {
        void* allocation = std::malloc(32);
        const bool allocated = allocation != nullptr;
        std::free(allocation);
        auto childSource = lumen::platform::makeNativeFrameAllocationSource();
        if (!childSource) _exit(2);
        childSource->beginFrame(0);
        const auto empty = childSource->finishFrame();
        _exit(allocated && empty.available ? 0 : 3);
    }
    const auto stats = source->finishFrame();
    int status = 0;
    const auto waited = child > 0 ? waitpid(child, &status, 0) : -1;
    stop.store(true, std::memory_order_release);
    worker.join();
    REQUIRE(child > 0);
    REQUIRE(waited == child);
    CHECK(WIFEXITED(status));
    CHECK(WEXITSTATUS(status) == 0);
    CHECK_FALSE(stats.available);
    CHECK(stats.source == "glibc/incomplete");
    source->beginFrame(0);
    const auto retry = source->finishFrame();
    CHECK(retry.available);
}

TEST_CASE("runApp installs and detaches its native default source",
          "[native_allocator][present]") {
    const auto* api = nativeApi();
    REQUIRE(api != nullptr);
    bool autoScopeActive = false;
    lumen::app::ShellConfig config;
    config.initialView = {200, 150};
    config.build = [&] {
        const auto token = api->begin();
        autoScopeActive = token == 0;
        api->cancel(token);
        return lumen::core::makeText("native default");
    };
    lumen::app::AppShell shell(config);
    lumen::platform::FakeApplicationHost host;
    lumen::app::RunOptions options;
    options.maxFrames = 1;
    CHECK(lumen::app::runApp(shell, host, options) == 0);
    CHECK(autoScopeActive);
    shell.markDirty();
    (void)shell.renderFrame();
    CHECK_FALSE(shell.frameDebugSnapshot().frameAllocationAvailable);

    lumen::platform::FakeApplicationHost failedHost;
    options.fontFactory = []() -> std::shared_ptr<lumen::text::FontManager> {
        throw std::runtime_error("font setup failed");
    };
    REQUIRE_THROWS_AS(lumen::app::runApp(shell, failedHost, options), std::runtime_error);
    shell.markDirty();
    (void)shell.renderFrame();
    CHECK_FALSE(shell.frameDebugSnapshot().frameAllocationAvailable);
    CHECK(lumen::platform::makeNativeFrameAllocationSource() != nullptr);
}

TEST_CASE("runApp honors the explicitly supplied allocation source",
          "[native_allocator][present]") {
    class ExplicitSource final : public lumen::render::FrameAllocationSource {
      public:
        void beginFrame(std::uint64_t) override { ++begins; }
        lumen::render::FrameAllocationStats finishFrame() override {
            return {true, "explicit-source", 7, 32, 16, 8};
        }
        void cancelFrame() noexcept override {}
        unsigned begins{0};
    } source;
    const auto* api = nativeApi();
    REQUIRE(api != nullptr);
    bool nativeScopeActive = false;
    lumen::app::ShellConfig config;
    config.initialView = {200, 150};
    config.build = [&] {
        const auto token = api->begin();
        nativeScopeActive = token == 0;
        api->cancel(token);
        return lumen::core::makeText("explicit source");
    };
    lumen::app::AppShell shell(config);
    lumen::platform::FakeApplicationHost host;
    lumen::app::RunOptions options;
    options.maxFrames = 1;
    options.frameAllocationSource = &source;
    CHECK(lumen::app::runApp(shell, host, options) == 0);
    CHECK_FALSE(nativeScopeActive);
    CHECK(source.begins == 1);
    CHECK(shell.frameDebugSnapshot().frameAllocationSource == "explicit-source");
    shell.setFrameAllocationSource(nullptr);
}
