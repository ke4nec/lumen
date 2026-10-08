// Native heap contract: docs/lumen-frame-allocator-design.md, sections 2-5.
// Keep Catch2 assertions outside active scopes.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <catch2/catch_test_macros.hpp>
#include <SDL3/SDL_stdinc.h>

#include <array>
#include <atomic>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>
#include <thread>

#include "lumen/app/app_shell.h"
#include "lumen/platform/fake_host.h"
#include "lumen/platform/frame_allocator.h"
#include "native_frame_allocator_api.h"

namespace {
using lumen::platform::detail::NativeFrameAllocatorApi;

const NativeFrameAllocatorApi* nativeApi() {
    using Getter = const NativeFrameAllocatorApi* (*)() noexcept;
    const auto module = GetModuleHandleW(L"lumen-frame-allocator.dll");
    const auto getter = module == nullptr ? nullptr : reinterpret_cast<Getter>(
        GetProcAddress(module, "lumen_native_frame_allocator_v1"));
    return getter != nullptr ? getter() : nullptr;
}

DWORD generateHeapException(HANDLE heap) {
    __try {
        void* volatile pointer = HeapAlloc(heap, HEAP_GENERATE_EXCEPTIONS, 1024 * 1024);
        if (pointer != nullptr) HeapFree(heap, 0, pointer);
    } __except(GetExceptionCode() == STATUS_NO_MEMORY ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
        return STATUS_NO_MEMORY;
    }
    return 0;
}

void* SDLCALL virtualAllocate(std::size_t bytes) {
    if (bytes > std::numeric_limits<std::size_t>::max() - sizeof(std::size_t)) return nullptr;
    auto* base = static_cast<std::size_t*>(VirtualAlloc(nullptr, bytes + sizeof(std::size_t),
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (base == nullptr) return nullptr;
    *base = bytes;
    return base + 1;
}
void SDLCALL virtualRelease(void* pointer) {
    if (pointer != nullptr) VirtualFree(static_cast<std::size_t*>(pointer) - 1, 0, MEM_RELEASE);
}
void* SDLCALL virtualZeroAllocate(std::size_t count, std::size_t bytes) {
    if (bytes != 0 && count > std::numeric_limits<std::size_t>::max() / bytes) return nullptr;
    return virtualAllocate(count * bytes);
}
void* SDLCALL virtualReallocate(void* old, std::size_t bytes) {
    if (bytes == 0) { virtualRelease(old); return nullptr; }
    void* next = virtualAllocate(bytes);
    if (next != nullptr && old != nullptr) {
        const auto oldBytes = *(static_cast<std::size_t*>(old) - 1);
        std::memcpy(next, old, oldBytes < bytes ? oldBytes : bytes);
        virtualRelease(old);
    }
    return next;
}
}  // namespace

TEST_CASE("Windows allocator requires an explicit loadable DLL", "[native_allocator][absent]") {
    CHECK(lumen::platform::makeNativeFrameAllocationSource() == nullptr);
}

TEST_CASE("Windows factory rejects SDL allocations outside native heaps",
          "[native_allocator][sdl_untracked]") {
    REQUIRE(SDL_SetMemoryFunctions(virtualAllocate, virtualZeroAllocate, virtualReallocate, virtualRelease));
    CHECK(lumen::platform::makeNativeFrameAllocationSource() == nullptr);
}

TEST_CASE("Windows profiler declines extension TLS slots without installing hooks",
          "[native_allocator][tls_exhausted]") {
    std::array<DWORD, 128> slots{};
    std::size_t count = 0;
    for (; count < slots.size(); ++count) {
        slots[count] = TlsAlloc();
        if (slots[count] == TLS_OUT_OF_INDEXES || slots[count] >= TLS_MINIMUM_AVAILABLE) {
            ++count;
            break;
        }
    }
    auto source = lumen::platform::makeNativeFrameAllocationSource();
    for (std::size_t i = 0; i < count; ++i) {
        if (slots[i] != TLS_OUT_OF_INDEXES) TlsFree(slots[i]);
    }
    CHECK(source == nullptr);
}

TEST_CASE("Windows native heap requests report bytes and scoped generations",
          "[native_allocator][present]") {
    auto source = lumen::platform::makeNativeFrameAllocationSource();
    REQUIRE(source != nullptr);
    HANDLE heap = HeapCreate(0, 0, 0);
    REQUIRE(heap != nullptr);
    source->beginFrame(0);
    void* first = HeapAlloc(heap, 0, 16);
    void* second = HeapAlloc(heap, HEAP_ZERO_MEMORY, 12);
    void* grown = HeapReAlloc(heap, 0, first, 24);
    HeapFree(heap, 0, second);
    auto stats = source->finishFrame();
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    REQUIRE(grown != nullptr);
    CHECK(stats.available);
    CHECK(stats.source == "ntdll/heap");
    CHECK(stats.allocationCount == 3);
    CHECK(stats.allocatedBytes == 52);
    CHECK(stats.peakBytes == 36);
    CHECK(stats.liveBytes == 24);
    source->beginFrame(0);
    void* next = HeapAlloc(heap, 0, 7);
    HeapFree(heap, 0, grown);
    HeapFree(heap, 0, next);
    stats = source->finishFrame();
    CHECK(stats.available);
    CHECK(stats.allocationCount == 1);
    CHECK(stats.allocatedBytes == 7);
    CHECK(stats.liveBytes == 0);
    CHECK(HeapDestroy(heap));
}

TEST_CASE("Windows failed realloc preserves its object and zero realloc uses native semantics",
          "[native_allocator][present]") {
    auto source = lumen::platform::makeNativeFrameAllocationSource();
    REQUIRE(source != nullptr);
    HANDLE heap = HeapCreate(0, 0, 0);
    REQUIRE(heap != nullptr);
    source->beginFrame(1);
    void* pointer = HeapAlloc(heap, 0, 29);
    void* failed = HeapReAlloc(heap, 0, pointer, std::numeric_limits<SIZE_T>::max());
    auto stats = source->finishFrame();
    REQUIRE(pointer != nullptr);
    CHECK(failed == nullptr);
    CHECK(stats.available);
    CHECK(stats.allocationCount == 1);
    CHECK(stats.liveBytes == 29);
    source->beginFrame(1);
    void* zero = HeapReAlloc(heap, 0, pointer, 0);
    stats = source->finishFrame();
    CHECK(stats.available);
    CHECK(stats.allocationCount == (zero != nullptr ? 1 : 0));
    CHECK(stats.allocatedBytes == 0);
    CHECK(stats.liveBytes == 0);
    HeapFree(heap, 0, zero != nullptr ? zero : pointer);
    CHECK(HeapDestroy(heap));
}

TEST_CASE("Windows heap destruction retires scoped objects of that heap only",
          "[native_allocator][present]") {
    auto source = lumen::platform::makeNativeFrameAllocationSource();
    REQUIRE(source != nullptr);
    HANDLE destroyed = HeapCreate(0, 0, 0);
    HANDLE retained = HeapCreate(0, 0, 0);
    REQUIRE(destroyed != nullptr);
    REQUIRE(retained != nullptr);
    source->beginFrame(0);
    void* first = HeapAlloc(destroyed, 0, 23);
    void* second = HeapAlloc(retained, 0, 41);
    const bool released = HeapDestroy(destroyed);
    const auto stats = source->finishFrame();
    CHECK(first != nullptr);
    CHECK(second != nullptr);
    CHECK(released);
    CHECK(stats.available);
    CHECK(stats.allocatedBytes == 64);
    CHECK(stats.peakBytes == 64);
    CHECK(stats.liveBytes == 41);
    CHECK(HeapDestroy(retained));
}

TEST_CASE("Windows heap exception clears hook state and preserves last error",
          "[native_allocator][present]") {
    auto source = lumen::platform::makeNativeFrameAllocationSource();
    REQUIRE(source != nullptr);
    HANDLE heap = HeapCreate(0, 4096, 65536);
    REQUIRE(heap != nullptr);
    source->beginFrame(0);
    const auto exception = generateHeapException(heap);
    SetLastError(0x1234);
    void* next = HeapAlloc(heap, 0, 17);
    const auto error = GetLastError();
    HeapFree(heap, 0, next);
    const auto stats = source->finishFrame();
    CHECK(exception == STATUS_NO_MEMORY);
    CHECK(next != nullptr);
    CHECK(error == 0x1234);
    CHECK(stats.available);
    CHECK(stats.allocationCount == 1);
    CHECK(stats.allocatedBytes == 17);
    CHECK(stats.liveBytes == 0);
    CHECK(HeapDestroy(heap));
}

TEST_CASE("Windows heaps created during the frame leave no live scoped objects after destroy",
          "[native_allocator][present]") {
    auto source = lumen::platform::makeNativeFrameAllocationSource();
    REQUIRE(source != nullptr);
    source->beginFrame(0);
    HANDLE heap = HeapCreate(0, 0, 0);
    void* pointer = HeapAlloc(heap, 0, 31);
    const bool destroyed = HeapDestroy(heap);
    const auto stats = source->finishFrame();
    CHECK(heap != nullptr);
    CHECK(pointer != nullptr);
    CHECK(destroyed);
    CHECK(stats.available);
    CHECK(stats.allocationCount >= 1);
    CHECK(stats.allocatedBytes >= 31);
    CHECK(stats.liveBytes == 0);
}

TEST_CASE("Windows static CRT DLL C++ aligned and SDL requests are covered",
          "[native_allocator][present]") {
    const auto helper = LoadLibraryW(LUMEN_NATIVE_CRT_PATH);
    REQUIRE(helper != nullptr);
    using Allocate = void* (*)(std::size_t);
    using Release = void (*)(void*);
    auto allocate = reinterpret_cast<Allocate>(GetProcAddress(helper, "lumen_test_crt_allocate"));
    auto release = reinterpret_cast<Release>(GetProcAddress(helper, "lumen_test_crt_release"));
    REQUIRE(allocate != nullptr);
    REQUIRE(release != nullptr);
    auto source = lumen::platform::makeNativeFrameAllocationSource();
    REQUIRE(source != nullptr);
    const auto* api = nativeApi();
    REQUIRE(api != nullptr);
    const auto token = api->begin();
    void* plain = ::operator new(19);
    void* aligned = ::operator new(128, std::align_val_t{64});
    void* otherCrt = allocate(193);
    void* sdl = SDL_malloc(29);
    const auto plainId = api->allocationId(reinterpret_cast<std::uintptr_t>(plain), token);
    const auto alignedId = api->allocationId(reinterpret_cast<std::uintptr_t>(aligned), token);
    const auto otherId = api->allocationId(reinterpret_cast<std::uintptr_t>(otherCrt), token);
    const auto sdlId = api->allocationId(reinterpret_cast<std::uintptr_t>(sdl), token);
    ::operator delete(plain);
    ::operator delete(aligned, std::align_val_t{64});
    release(otherCrt);
    SDL_free(sdl);
    const auto retired = api->allocationId(reinterpret_cast<std::uintptr_t>(otherCrt), token);
    const auto stats = api->finish(token);
    CHECK(plainId != 0);
    CHECK(alignedId != 0);
    CHECK(otherId != 0);
    CHECK(sdlId != 0);
    CHECK(retired == 0);
    CHECK(stats.complete);
    CHECK(stats.allocationCount >= 4);
    CHECK(stats.allocatedBytes >= 369);
    CHECK(stats.liveBytes == 0);
    FreeLibrary(helper);
}

TEST_CASE("Windows hooks initialize new thread TLS and account cross-thread frees",
          "[native_allocator][present]") {
    auto source = lumen::platform::makeNativeFrameAllocationSource();
    REQUIRE(source != nullptr);
    HANDLE heap = HeapCreate(0, 0, 0);
    REQUIRE(heap != nullptr);
    std::atomic<int> phase{0};
    void* pointer{};
    std::thread worker([&] {
        phase.store(1, std::memory_order_release);
        while (phase.load(std::memory_order_acquire) != 2) {}
        pointer = HeapAlloc(heap, 0, 37);
        phase.store(3, std::memory_order_release);
        while (phase.load(std::memory_order_acquire) != 4) {}
    });
    while (phase.load(std::memory_order_acquire) != 1) {}
    source->beginFrame(0);
    phase.store(2, std::memory_order_release);
    while (phase.load(std::memory_order_acquire) != 3) {}
    HeapFree(heap, 0, pointer);
    auto stats = source->finishFrame();
    phase.store(4, std::memory_order_release);
    worker.join();
    CHECK(pointer != nullptr);
    CHECK(stats.available);
    CHECK(stats.allocationCount == 1);
    CHECK(stats.allocatedBytes == 37);
    CHECK(stats.liveBytes == 0);
    source->beginFrame(0);
    std::thread newThread([&] {
        void* object = HeapAlloc(heap, 0, 43);
        HeapFree(heap, 0, object);
    });
    newThread.join();
    stats = source->finishFrame();
    CHECK(stats.available);
    CHECK(stats.allocatedBytes >= 43);
    CHECK(stats.allocationCount >= 1);
    CHECK(HeapDestroy(heap));
}

TEST_CASE("Windows native scope rejects overlap stale cancels and recovers from capacity",
          "[native_allocator][present]") {
    auto source = lumen::platform::makeNativeFrameAllocationSource();
    auto second = lumen::platform::makeNativeFrameAllocationSource();
    REQUIRE(source != nullptr);
    REQUIRE(second != nullptr);
    source->beginFrame(0);
    second->beginFrame(0);
    const auto busy = second->finishFrame();
    source->cancelFrame();
    const auto* api = nativeApi();
    REQUIRE(api != nullptr);
    const auto old = api->begin();
    api->cancel(old);
    const auto next = api->begin();
    api->cancel(old);
    const auto valid = api->finish(next);
    CHECK_FALSE(busy.available);
    CHECK(busy.source == "ntdll/busy");
    CHECK(valid.complete);
    CHECK(next != old);

    // Static storage avoids using most of Windows' default 1 MiB thread stack.
    static std::array<void*, 65537> pointers{};
    HANDLE heap = HeapCreate(0, 0, 0);
    REQUIRE(heap != nullptr);
    source->beginFrame(0);
    for (auto& pointer : pointers) pointer = HeapAlloc(heap, 0, 1);
    const auto incomplete = source->finishFrame();
    for (auto pointer : pointers) HeapFree(heap, 0, pointer);
    CHECK_FALSE(incomplete.available);
    CHECK(incomplete.source == "ntdll/incomplete");
    source->beginFrame(0);
    const auto empty = source->finishFrame();
    CHECK(empty.available);
    CHECK(empty.allocationCount == 0);
    CHECK(HeapDestroy(heap));
}

TEST_CASE("Windows AppShell cancels native scope after submit exceptions",
          "[native_allocator][present]") {
    auto source = lumen::platform::makeNativeFrameAllocationSource();
    REQUIRE(source != nullptr);
    lumen::app::ShellConfig config;
    config.initialView = {200, 150};
    config.build = [] { return lumen::core::makeText("native allocations"); };
    lumen::app::AppShell shell(config);
    shell.setFrameAllocationSource(source.get());
    struct ThrowingRenderer : lumen::render::Renderer {
        void beginFrame(lumen::core::Size) override {}
        void save() override {}
        void restore() override {}
        void clipRect(lumen::core::Rect) override {}
        void drawRect(lumen::core::Rect, lumen::core::Color, lumen::core::CornerRadius) override {}
        void drawText(lumen::render::TextRun, lumen::core::TextStyle) override {}
        void drawImage(lumen::render::ImageId, lumen::core::Rect) override {}
        void endFrame() override {}
        void submit(const lumen::render::RenderCommandList&, const lumen::render::FrameInfo&) override {
            throw std::runtime_error("submit");
        }
    } renderer;
    shell.setRenderer(&renderer);
    CHECK_THROWS_AS(shell.renderFrame(), std::runtime_error);
    source->beginFrame(0);
    CHECK(source->finishFrame().available);
    shell.setRenderer(nullptr);
    shell.setFrameStatsCapture(true);
    shell.markDirty();
    (void)shell.renderFrame();
    CHECK(shell.frameDebugSnapshot().frameAllocationAvailable);
    CHECK(shell.frameDebugSnapshot().frameAllocationSource == "ntdll/heap");
    CHECK(shell.frameDebugSnapshot().frameAllocationCount > 0);
    shell.setFrameAllocationSource(nullptr);
}

TEST_CASE("Windows runApp installs whole-frame telemetry and detaches after return",
          "[native_allocator][present]") {
    auto source = lumen::platform::makeNativeFrameAllocationSource();
    REQUIRE(source != nullptr);
    const auto* api = nativeApi();
    REQUIRE(api != nullptr);
    bool autoScopeActive = false;
    lumen::app::ShellConfig config;
    config.initialView = {200, 150};
    config.build = [&] {
        const auto token = api->begin();
        autoScopeActive = token == 0;
        api->cancel(token);
        return lumen::core::makeText("native runApp");
    };
    lumen::app::AppShell shell(config);
    lumen::platform::FakeApplicationHost host;
    lumen::app::RunOptions options;
    options.maxFrames = 1;
    shell.setFrameStatsCapture(true);
    CHECK(lumen::app::runApp(shell, host, options) == 0);
    CHECK(autoScopeActive);
    shell.markDirty();
    (void)shell.renderFrame();
    CHECK_FALSE(shell.frameDebugSnapshot().frameAllocationAvailable);
}
