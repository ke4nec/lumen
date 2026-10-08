#include "lumen/platform/frame_allocator.h"

#if (defined(__linux__) || defined(__APPLE__)) && defined(LUMEN_DESKTOP_FRAME_ALLOCATOR_CLIENT)
#include <dlfcn.h>
#include <new>
#include <utility>

#include <SDL3/SDL_stdinc.h>

#include "native_frame_allocator_api.h"

namespace lumen::platform {
namespace {

using detail::NativeFrameAllocatorApi;

#if defined(__APPLE__)
constexpr auto nativeSource = "libmalloc/malloc";
constexpr auto busySource = "libmalloc/busy";
constexpr auto invalidBindingsSource = "libmalloc/bindings";
constexpr auto incompleteSource = "libmalloc/incomplete";
#else
constexpr auto nativeSource = "glibc/malloc";
constexpr auto busySource = "glibc/busy";
constexpr auto invalidBindingsSource = "glibc/bindings";
constexpr auto incompleteSource = "glibc/incomplete";
#endif

struct SdlMemoryFunctions {
    SDL_malloc_func allocate;
    SDL_calloc_func zeroAllocate;
    SDL_realloc_func reallocate;
    SDL_free_func release;
    bool operator==(const SdlMemoryFunctions&) const = default;
};

SdlMemoryFunctions sdlMemoryFunctions() {
    SdlMemoryFunctions functions{};
    SDL_GetMemoryFunctions(&functions.allocate, &functions.zeroAllocate,
                           &functions.reallocate, &functions.release);
    return functions;
}

class NativeFrameAllocationSource final : public render::FrameAllocationSource {
  public:
    explicit NativeFrameAllocationSource(const NativeFrameAllocatorApi& api)
        : api_(api), sdlFunctions_(sdlMemoryFunctions()) {}
    ~NativeFrameAllocationSource() override { cancelFrame(); }

    void beginFrame(std::uint64_t) override {
        cancelFrame();
        bindingsValid_ = sdlFunctions_ == sdlMemoryFunctions() && api_.bindingsValid();
        if (bindingsValid_) token_ = api_.begin();
    }

    render::FrameAllocationStats finishFrame() override {
        const auto token = std::exchange(token_, 0);
        if (token == 0) return {false, bindingsValid_ ? busySource : invalidBindingsSource};
        if (sdlFunctions_ != sdlMemoryFunctions()) {
            api_.cancel(token);
            return {false, invalidBindingsSource};
        }
        const auto sample = api_.finish(token);
        if (!sample.complete) return {false, incompleteSource};
        if (!api_.bindingsValid()) return {false, invalidBindingsSource};
        return {true, nativeSource, sample.allocationCount,
                sample.allocatedBytes, sample.peakBytes, sample.liveBytes};
    }

    void cancelFrame() noexcept override {
        if (const auto token = std::exchange(token_, 0)) api_.cancel(token);
    }

  private:
    const NativeFrameAllocatorApi& api_;
    SdlMemoryFunctions sdlFunctions_;
    std::uint64_t token_{0};
    bool bindingsValid_{true};
};

template <typename Release>
bool validateRelease(const NativeFrameAllocatorApi& api, std::uint64_t token,
                     void* pointer, Release release) {
    if (pointer == nullptr) return false;
    const auto address = reinterpret_cast<std::uintptr_t>(pointer);
    const auto allocationId = api.allocationId(address, token);
    release(pointer);
    return allocationId != 0 &&
        api.allocationId(address, token) != allocationId;
}

bool validateClientBindings(const NativeFrameAllocatorApi& api) {
    const auto token = api.begin();
    if (token == 0) return false;
    bool valid = true;
    try {
        void* plain = ::operator new(19);
        valid &= validateRelease(api, token, plain, [](void* p) { ::operator delete(p); });
        plain = ::operator new(41);
        valid &= validateRelease(api, token, plain, [](void* p) { ::operator delete(p, 41); });
        void* array = ::operator new[](23);
        valid &= validateRelease(api, token, array, [](void* p) { ::operator delete[](p); });
        array = ::operator new[](43);
        valid &= validateRelease(api, token, array, [](void* p) { ::operator delete[](p, 43); });
        void* aligned = ::operator new(64, std::align_val_t{64});
        valid &= validateRelease(api, token, aligned, [](void* p) {
            ::operator delete(p, std::align_val_t{64});
        });
        aligned = ::operator new(128, std::align_val_t{64});
        valid &= validateRelease(api, token, aligned, [](void* p) {
            ::operator delete(p, 128, std::align_val_t{64});
        });
        aligned = ::operator new[](128, std::align_val_t{64});
        valid &= validateRelease(api, token, aligned, [](void* p) {
            ::operator delete[](p, std::align_val_t{64});
        });
        aligned = ::operator new[](192, std::align_val_t{64});
        valid &= validateRelease(api, token, aligned, [](void* p) {
            ::operator delete[](p, 192, std::align_val_t{64});
        });
        void* sdl = SDL_malloc(29);
        valid &= validateRelease(api, token, sdl, SDL_free);
        sdl = SDL_calloc(3, 17);
        const auto oldAddress = reinterpret_cast<std::uintptr_t>(sdl);
        const auto oldId = api.allocationId(oldAddress, token);
        valid &= oldId != 0;
        void* grown = SDL_realloc(sdl, 71);
        const auto grownId = api.allocationId(reinterpret_cast<std::uintptr_t>(grown), token);
        valid &= grown != nullptr && grownId != 0 && grownId != oldId &&
            api.allocationId(oldAddress, token) != oldId;
        valid &= validateRelease(api, token, grown != nullptr ? grown : sdl, SDL_free);
    } catch (...) {
        valid = false;
    }
    const auto sample = api.finish(token);
    return valid && sample.complete;
}

}  // namespace

std::unique_ptr<render::FrameAllocationSource> makeNativeFrameAllocationSource() {
    using Getter = const NativeFrameAllocatorApi* (*)() noexcept;
    auto getter = reinterpret_cast<Getter>(
        dlsym(RTLD_DEFAULT, "lumen_native_frame_allocator_v1"));
    if (getter == nullptr) return nullptr;
    const auto* api = getter();
    if (api == nullptr || api->version != 1 ||
        api->size != sizeof(NativeFrameAllocatorApi) ||
        api->bindingsValid == nullptr || api->begin == nullptr ||
        api->finish == nullptr || api->cancel == nullptr || api->allocationId == nullptr ||
        !api->bindingsValid() || !validateClientBindings(*api)) {
        return nullptr;
    }
    return std::make_unique<NativeFrameAllocationSource>(*api);
}

}  // namespace lumen::platform

#else
namespace lumen::platform {
std::unique_ptr<render::FrameAllocationSource> makeNativeFrameAllocationSource() {
    return nullptr;
}
}  // namespace lumen::platform
#endif
