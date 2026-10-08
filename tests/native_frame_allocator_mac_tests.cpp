// macOS contracts: docs/lumen-frame-allocator-design.md, sections 2-5.
// Assertions execute after finish, outside the sampled allocator interval.
#include <catch2/catch_test_macros.hpp>

#include <SDL3/SDL_stdinc.h>

#include <array>
#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <dlfcn.h>
#include <limits>
#include <malloc/malloc.h>
#include <new>
#include <stdexcept>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

#include "lumen/app/app_shell.h"
#include "lumen/platform/fake_host.h"
#include "lumen/platform/frame_allocator.h"
#include "native_frame_allocator_api.h"
#include "native_malloc_type_mac.h"

namespace {
using lumen::platform::detail::NativeFrameAllocatorApi;

const NativeFrameAllocatorApi* nativeApi() {
    using Getter = const NativeFrameAllocatorApi* (*)() noexcept;
    auto getter = reinterpret_cast<Getter>(dlsym(RTLD_DEFAULT, "lumen_native_frame_allocator_v1"));
    return getter ? getter() : nullptr;
}

void* SDLCALL directZoneMalloc(std::size_t bytes) {
    auto* zone = malloc_default_zone();
    return zone->malloc(zone, bytes);
}
void* SDLCALL directZoneCalloc(std::size_t count, std::size_t bytes) {
    auto* zone = malloc_default_zone();
    return zone->calloc(zone, count, bytes);
}
void* SDLCALL directZoneRealloc(void* pointer, std::size_t bytes) {
    auto* zone = malloc_default_zone();
    return zone->realloc(zone, pointer, bytes);
}
void SDLCALL directZoneFree(void* pointer) {
    if (pointer) {
        auto* zone = malloc_zone_from_ptr(pointer);
        zone->free(zone, pointer);
    }
}
}  // namespace

TEST_CASE("macOS factory requires a complete startup interpose", "[native_allocator][absent]") {
    CHECK(lumen::platform::makeNativeFrameAllocationSource() == nullptr);
}

TEST_CASE("macOS factory rejects a dylib loaded after startup", "[native_allocator][late_loaded]") {
    void* handle = dlopen(LUMEN_NATIVE_ALLOCATOR_PATH, RTLD_NOW | RTLD_GLOBAL);
    REQUIRE(handle != nullptr);
    CHECK(nativeApi() != nullptr);
    CHECK(lumen::platform::makeNativeFrameAllocationSource() == nullptr);
    // Startup profilers and their atfork callbacks remain resident to exit.
}

TEST_CASE("macOS factory rejects SDL callbacks bypassing public libmalloc", "[native_allocator][sdl_untracked]") {
    REQUIRE(SDL_SetMemoryFunctions(directZoneMalloc, directZoneCalloc,
                                   directZoneRealloc, directZoneFree));
    CHECK(lumen::platform::makeNativeFrameAllocationSource() == nullptr);
}

TEST_CASE("macOS malloc scopes count requested bytes and exclude old objects", "[native_allocator][present]") {
    auto source = lumen::platform::makeNativeFrameAllocationSource();
    REQUIRE(source != nullptr);
    source->beginFrame(0);
    void* first = std::malloc(16);
    void* second = std::calloc(3, 4);
    void* grown = std::realloc(first, 24);
    std::free(second);
    const auto stats = source->finishFrame();
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    REQUIRE(grown != nullptr);
    CHECK(stats.available);
    CHECK(stats.source == "libmalloc/malloc");
    CHECK(stats.allocationCount == 3);
    CHECK(stats.allocatedBytes == 52);
    CHECK(stats.peakBytes == 36);
    CHECK(stats.liveBytes == 24);
    source->beginFrame(0);
    std::free(grown);
    void* next = std::malloc(7);
    std::free(next);
    const auto following = source->finishFrame();
    CHECK(following.available);
    CHECK(following.allocatedBytes == 7);
    CHECK(following.liveBytes == 0);
}

TEST_CASE("macOS preserves realloc failure and zero-size ownership semantics", "[native_allocator][present]") {
    auto source = lumen::platform::makeNativeFrameAllocationSource();
    REQUIRE(source != nullptr);
    volatile std::size_t huge = std::numeric_limits<std::size_t>::max();
    source->beginFrame(0);
    void* original = std::malloc(24);
    void* failed = std::realloc(original, huge);
    const int failureErrno = errno;
    void* overflow = std::calloc(huge, 2);
    void* aligned = original;
    const int invalidAlignment = posix_memalign(&aligned, 3, 32);
    const auto stats = source->finishFrame();
    REQUIRE(original != nullptr);
    CHECK(failed == nullptr);
    CHECK(failureErrno == ENOMEM);
    CHECK(overflow == nullptr);
    CHECK(invalidAlignment == EINVAL);
    CHECK(aligned == original);
    CHECK(stats.available);
    CHECK(stats.allocationCount == 1);
    CHECK(stats.liveBytes == 24);
    std::free(original);

    source->beginFrame(0);
    original = std::malloc(29);
    void* zero = std::realloc(original, 0);
    const auto zeroStats = source->finishFrame();
    REQUIRE(original != nullptr);
    CHECK(zeroStats.available);
    CHECK(zeroStats.allocationCount == (zero ? 2 : 1));
    CHECK(zeroStats.allocatedBytes == 29);
    CHECK(zeroStats.liveBytes == (zero ? 0 : 29));
    std::free(zero ? zero : original);

    source->beginFrame(0);
    original = std::malloc(19);
    failed = reallocf(original, huge);
    const auto freeing = source->finishFrame();
    CHECK(failed == nullptr);
    CHECK(freeing.available);
    CHECK(freeing.allocationCount == 1);
    CHECK(freeing.liveBytes == 0);
}

TEST_CASE("macOS aligned C++ and SDL allocation families share one ledger", "[native_allocator][present]") {
    auto source = lumen::platform::makeNativeFrameAllocationSource();
    REQUIRE(source != nullptr);
    source->beginFrame(0);
    void* plain = ::operator new(19);
    ::operator delete(plain, 19);
    void* array = ::operator new[](37);
    ::operator delete[](array);
    void* aligned = ::operator new(64, std::align_val_t{64});
    ::operator delete(aligned, std::align_val_t{64});
    aligned = ::operator new[](128, std::align_val_t{64});
    ::operator delete[](aligned, 128, std::align_val_t{64});
    void* sdl = SDL_malloc(29);
    SDL_free(sdl);
    sdl = SDL_calloc(3, 17);
    void* grown = SDL_realloc(sdl, 71);
    SDL_free(grown ? grown : sdl);
    void* cAligned = std::aligned_alloc(64, 64);
    void* posix = nullptr;
    const int status = posix_memalign(&posix, 64, 37);
    void* page = valloc(11);
    std::free(cAligned);
    std::free(posix);
    std::free(page);
    const auto stats = source->finishFrame();
    REQUIRE(grown != nullptr);
    REQUIRE(cAligned != nullptr);
    REQUIRE(status == 0);
    REQUIRE(page != nullptr);
    CHECK(stats.available);
    CHECK(stats.allocationCount == 10);
    CHECK(stats.allocatedBytes == 511);
    CHECK(stats.peakBytes == 128);
    CHECK(stats.liveBytes == 0);
}

TEST_CASE("macOS public zones and typed entry points retire scoped owners", "[native_allocator][present]") {
    auto source = lumen::platform::makeNativeFrameAllocationSource();
    REQUIRE(source != nullptr);
    auto* zone = malloc_create_zone(0, 0);
    REQUIRE(zone != nullptr);
    source->beginFrame(0);
    void* first = malloc_zone_malloc(zone, 13);
    void* second = malloc_zone_calloc(zone, 2, 11);
    void* grown = malloc_zone_realloc(zone, first, 31);
    void* aligned = malloc_zone_memalign(zone, 64, 41);
    void* page = malloc_zone_valloc(zone, 17);
    malloc_zone_free(zone, second);
    void* typed = lumenTypeZoneMalloc(zone, 23, 0);
    typed = lumenTypeZoneRealloc(zone, typed, 47, 0);
    lumenTypeZoneFree(zone, typed, 0);
    void* typedZero = lumenTypeZoneCalloc(zone, 3, 7, 0);
    void* typedAligned = lumenTypeZoneMemalign(zone, 64, 19, 0);
    void* typedPage = lumenTypeZoneValloc(zone, 29, 0);
    lumenTypeZoneFree(zone, typedZero, 0);
    lumenTypeZoneFree(zone, typedAligned, 0);
    lumenTypeZoneFree(zone, typedPage, 0);
    malloc_destroy_zone(zone);
    const auto stats = source->finishFrame();
    REQUIRE(grown != nullptr);
    REQUIRE(aligned != nullptr);
    REQUIRE(page != nullptr);
    REQUIRE(typed != nullptr);
    REQUIRE(typedZero != nullptr);
    REQUIRE(typedAligned != nullptr);
    REQUIRE(typedPage != nullptr);
    CHECK(stats.available);
    CHECK(stats.allocationCount == 10);
    CHECK(stats.allocatedBytes == 263);
    CHECK(stats.liveBytes == 0);

    source->beginFrame(0);
    first = lumenTypeMalloc(11, 0);
    second = lumenTypeCalloc(2, 13, 0);
    grown = lumenTypeRealloc(first, 37, 0);
    aligned = lumenTypeAlignedAlloc(64, 64, 0);
    const int status = lumenTypePosixMemalign(&typed, 64, 19, 0);
    page = lumenTypeValloc(7, 0);
    lumenTypeFree(grown ? grown : first, 0);
    lumenTypeFree(second, 0);
    lumenTypeFree(aligned, 0);
    lumenTypeFree(status == 0 ? typed : nullptr, 0);
    lumenTypeFree(page, 0);
    const auto typeStats = source->finishFrame();
    REQUIRE(grown != nullptr);
    REQUIRE(status == 0);
    CHECK(typeStats.available);
    CHECK(typeStats.allocationCount == 6);
    CHECK(typeStats.allocatedBytes == 164);
    CHECK(typeStats.liveBytes == 0);
}

TEST_CASE("macOS batch requests count objects before mutating frees", "[native_allocator][present]") {
    auto source = lumen::platform::makeNativeFrameAllocationSource();
    REQUIRE(source != nullptr);
    std::array<void*, 8> pointers{};
    auto* zone = malloc_default_zone();
    source->beginFrame(0);
    const auto count = malloc_zone_batch_malloc(zone, 16, pointers.data(), pointers.size());
    malloc_zone_batch_free(zone, pointers.data(), count);
    const auto stats = source->finishFrame();
    REQUIRE(count > 0);
    CHECK(stats.available);
    CHECK(stats.allocationCount == count);
    CHECK(stats.allocatedBytes == count * 16);
    CHECK(stats.peakBytes == count * 16);
    CHECK(stats.liveBytes == 0);
}

#if defined(LUMEN_HAS_MALLOC_ZONE_OPTIONS)
TEST_CASE("macOS newer zone options are tracked only when installed by the runtime", "[native_allocator][present]") {
    auto source = lumen::platform::makeNativeFrameAllocationSource();
    REQUIRE(source != nullptr);
    const bool present = lumenZoneOptions != nullptr && lumenTypeZoneOptions != nullptr;
    auto* zone = malloc_default_zone();
    bool cleared = true;
    source->beginFrame(0);
    if (present) {
        void* first = lumenZoneOptions(zone, 64, 16, MALLOC_ZONE_MALLOC_OPTION_CLEAR);
        void* second = lumenTypeZoneOptions(zone, 64, 8, 0, MALLOC_ZONE_MALLOC_OPTION_CLEAR);
        cleared = first && second;
        if (cleared) {
            for (std::size_t i = 0; i < 16; ++i) cleared &= static_cast<char*>(first)[i] == 0;
            for (std::size_t i = 0; i < 8; ++i) cleared &= static_cast<char*>(second)[i] == 0;
        }
        std::free(first);
        std::free(second);
    }
    const auto stats = source->finishFrame();
    CHECK(cleared);
    CHECK(stats.available);
    CHECK(stats.allocationCount == (present ? 2 : 0));
    CHECK(stats.allocatedBytes == (present ? 24 : 0));
    CHECK(stats.peakBytes == (present ? 24 : 0));
    CHECK(stats.liveBytes == 0);
}
#endif

TEST_CASE("macOS scopes count cross-thread frees and newly created threads", "[native_allocator][present]") {
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
    source->beginFrame(0);
    std::thread newborn([] { std::free(std::malloc(23)); });
    newborn.join();
    CHECK(source->finishFrame().available);
}

TEST_CASE("macOS scope tokens and capacity recover without retaining partial results", "[native_allocator][present]") {
    auto source = lumen::platform::makeNativeFrameAllocationSource();
    auto other = lumen::platform::makeNativeFrameAllocationSource();
    const auto* api = nativeApi();
    REQUIRE(source != nullptr);
    REQUIRE(other != nullptr);
    REQUIRE(api != nullptr);
    const auto stale = api->begin();
    api->cancel(stale);
    source->beginFrame(0);
    other->beginFrame(0);
    const auto busy = other->finishFrame();
    api->cancel(stale);
    std::free(std::malloc(13));
    const auto current = source->finishFrame();
    CHECK_FALSE(busy.available);
    CHECK(busy.source == "libmalloc/busy");
    CHECK(current.available);
    CHECK(current.allocatedBytes >= 13);
    source->cancelFrame();
    source->cancelFrame();
    std::array<void*, 65537> pointers{};
    source->beginFrame(0);
    for (auto& pointer : pointers) pointer = std::malloc(1);
    const auto overflow = source->finishFrame();
    for (auto* pointer : pointers) std::free(pointer);
    CHECK_FALSE(overflow.available);
    CHECK(overflow.source == "libmalloc/incomplete");
    CHECK(overflow.allocatedBytes == 0);
    source->beginFrame(0);
    CHECK(source->finishFrame().available);
}

TEST_CASE("macOS AppShell and runApp use and detach native scopes", "[native_allocator][present]") {
    auto source = lumen::platform::makeNativeFrameAllocationSource();
    REQUIRE(source != nullptr);
    lumen::app::ShellConfig config;
    config.initialView = {200, 150};
    config.build = [] { return lumen::core::makeText("native macOS allocations"); };
    lumen::app::AppShell shell(config);
    shell.setFrameStatsCapture(true);
    shell.setFrameAllocationSource(source.get());
    (void)shell.renderFrame();
    const auto snapshot = shell.frameDebugSnapshot();
    CHECK(snapshot.frameAllocationAvailable);
    CHECK(snapshot.frameAllocationSource == "libmalloc/malloc");
    CHECK(snapshot.frameAllocationCount > 0);
    shell.setFrameAllocationSource(nullptr);
    shell.markDirty();
    lumen::platform::FakeApplicationHost host;
    lumen::app::RunOptions options;
    options.maxFrames = 1;
    CHECK(lumen::app::runApp(shell, host, options) == 0);
    CHECK(shell.frameDebugSnapshot().frameAllocationAvailable);
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
}

TEST_CASE("macOS fork invalidates telemetry and resets the child's lock and scope", "[native_allocator][present]") {
    auto source = lumen::platform::makeNativeFrameAllocationSource();
    REQUIRE(source != nullptr);
    std::atomic<bool> started{false}, stop{false};
    std::thread worker([&] {
        started.store(true, std::memory_order_release);
        while (!stop.load(std::memory_order_acquire)) std::free(std::malloc(64));
    });
    while (!started.load(std::memory_order_acquire)) {}
    source->beginFrame(0);
    const auto child = fork();
    if (child == 0) {
        auto childSource = lumen::platform::makeNativeFrameAllocationSource();
        if (!childSource) _exit(2);
        childSource->beginFrame(0);
        std::free(std::malloc(32));
        _exit(childSource->finishFrame().available ? 0 : 3);
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
    CHECK(stats.source == "libmalloc/incomplete");
    source->beginFrame(0);
    CHECK(source->finishFrame().available);
}
