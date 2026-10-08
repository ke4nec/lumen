#include "native_allocation_ledger.h"
#include "native_interpose_mac.h"
#include "native_malloc_type_mac.h"

#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <malloc/malloc.h>
#include <pthread.h>

// Legacy export, absent from current public headers and some SDKs.
extern "C" void vfree(void*) __attribute__((weak_import));

namespace {

using lumen::platform::detail::NativeAllocationSample;
using lumen::platform::detail::NativeFrameAllocatorApi;

constinit lumen::platform::detail::NativeAllocationLedger<> ledger;
pthread_mutex_t ledgerMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_key_t hookKey{};
char hookMarker{};
std::atomic<bool> ready{false};

// Darwin's first access to C++ thread_local can allocate. A pthread key uses
// libpthread's existing per-thread TSD storage, including on newly born threads.
class HookScope {
  public:
    HookScope() noexcept {
        const int savedErrno = errno;
        if (ready.load(std::memory_order_acquire) && pthread_getspecific(hookKey) == nullptr) {
            if (pthread_setspecific(hookKey, &hookMarker) == 0) {
                pthread_mutex_lock(&ledgerMutex);
                locked_ = true;
            } else {
                ready.store(false, std::memory_order_release);
            }
        }
        errno = savedErrno;
    }
    ~HookScope() {
        const int savedErrno = errno;
        if (locked_) {
            pthread_mutex_unlock(&ledgerMutex);
            pthread_setspecific(hookKey, nullptr);
        }
        errno = savedErrno;
    }
    [[nodiscard]] bool locked() const noexcept { return locked_; }
  private:
    bool locked_{false};
};

void record(void* pointer, std::size_t bytes) noexcept {
    if (pointer == nullptr) return;
    const int savedErrno = errno;
    const auto* owner = malloc_zone_from_ptr(pointer);
    if (owner == nullptr) ledger.invalidate();
    ledger.record(reinterpret_cast<std::uintptr_t>(pointer), bytes,
                  reinterpret_cast<std::uintptr_t>(owner));
    errno = savedErrno;
}

template <typename Function, typename... Args>
void* allocate(Function original, std::size_t bytes, Args... args) noexcept {
    HookScope scope;
    // Passing the original address avoids Clang's typed-malloc call rewrite.
    // dyld excludes this image from its own interpose, so it calls libmalloc.
    void* result = original(args...);
    if (scope.locked()) record(result, bytes);
    return result;
}

template <typename Function, typename... Args>
void release(Function original, void* pointer, Args... args) noexcept {
    HookScope scope;
    if (scope.locked()) ledger.forget(reinterpret_cast<std::uintptr_t>(pointer));
    original(args...);
}

template <typename Function, typename... Args>
void* reallocate(Function original, void* pointer, std::size_t bytes,
                 bool freesOnFailure, bool zeroFailureUncertain, Args... args) noexcept {
    HookScope scope;
    const auto address = reinterpret_cast<std::uintptr_t>(pointer);
    void* result = original(args...);
    if (scope.locked()) {
        // Unlike glibc, Apple's realloc(p, 0) replaces p only on success.
        if (result != nullptr || (freesOnFailure && bytes != 0)) ledger.forget(address);
        if (!result && pointer && bytes == 0 && zeroFailureUncertain) ledger.invalidate();
        record(result, bytes);
    }
    return result;
}

void* trackedMalloc(std::size_t bytes) noexcept {
    return allocate(&malloc, bytes, bytes);
}
void* trackedCalloc(std::size_t count, std::size_t bytes) noexcept {
    return allocate(&calloc, count * bytes, count, bytes);
}
void trackedFree(void* pointer) noexcept { release(&free, pointer, pointer); }
void trackedVfree(void* pointer) noexcept { release(&vfree, pointer, pointer); }
void* trackedRealloc(void* pointer, std::size_t bytes) noexcept {
    return reallocate(&realloc, pointer, bytes, false, false, pointer, bytes);
}
void* trackedReallocf(void* pointer, std::size_t bytes) noexcept {
    return reallocate(&reallocf, pointer, bytes, true, false, pointer, bytes);
}
void* trackedAlignedAlloc(std::size_t alignment, std::size_t bytes) noexcept {
    return allocate(&aligned_alloc, bytes, alignment, bytes);
}
void* trackedValloc(std::size_t bytes) noexcept {
    return allocate(&valloc, bytes, bytes);
}
int trackedPosixMemalign(void** pointer, std::size_t alignment, std::size_t bytes) noexcept {
    HookScope scope;
    const int result = posix_memalign(pointer, alignment, bytes);
    if (scope.locked() && result == 0) record(*pointer, bytes);
    return result;
}
void* trackedZoneMalloc(malloc_zone_t* zone, std::size_t bytes) noexcept {
    return allocate(&malloc_zone_malloc, bytes, zone, bytes);
}
void* trackedZoneCalloc(malloc_zone_t* zone, std::size_t count, std::size_t bytes) noexcept {
    return allocate(&malloc_zone_calloc, count * bytes, zone, count, bytes);
}
void* trackedZoneValloc(malloc_zone_t* zone, std::size_t bytes) noexcept {
    return allocate(&malloc_zone_valloc, bytes, zone, bytes);
}
void* trackedZoneMemalign(malloc_zone_t* zone, std::size_t alignment, std::size_t bytes) noexcept {
    return allocate(&malloc_zone_memalign, bytes, zone, alignment, bytes);
}
void* trackedZoneRealloc(malloc_zone_t* zone, void* pointer, std::size_t bytes) noexcept {
    return reallocate(&malloc_zone_realloc, pointer, bytes, false, true, zone, pointer, bytes);
}
void trackedZoneFree(malloc_zone_t* zone, void* pointer) noexcept {
    release(&malloc_zone_free, pointer, zone, pointer);
}
unsigned trackedZoneBatchMalloc(malloc_zone_t* zone, std::size_t bytes,
                                void** pointers, unsigned count) noexcept {
    HookScope scope;
    const auto allocated = malloc_zone_batch_malloc(zone, bytes, pointers, count);
    if (scope.locked()) {
        for (unsigned i = 0; i < allocated; ++i) record(pointers[i], bytes);
    }
    return allocated;
}
void trackedZoneBatchFree(malloc_zone_t* zone, void** pointers, unsigned count) noexcept {
    HookScope scope;
    if (scope.locked()) {
        // libmalloc is allowed to overwrite the caller's pointer array.
        for (unsigned i = 0; i < count; ++i) {
            ledger.forget(reinterpret_cast<std::uintptr_t>(pointers[i]));
        }
    }
    malloc_zone_batch_free(zone, pointers, count);
}
void trackedDestroyZone(malloc_zone_t* zone) noexcept {
    HookScope scope;
    if (scope.locked()) {
        ledger.forgetOwner(reinterpret_cast<std::uintptr_t>(zone));
        ledger.forget(reinterpret_cast<std::uintptr_t>(zone));
    }
    malloc_destroy_zone(zone);
}

void* trackedTypeMalloc(std::size_t bytes, malloc_type_id_t type) noexcept {
    return allocate(&lumenTypeMalloc, bytes, bytes, type);
}
void* trackedTypeCalloc(std::size_t count, std::size_t bytes, malloc_type_id_t type) noexcept {
    return allocate(&lumenTypeCalloc, count * bytes, count, bytes, type);
}
void trackedTypeFree(void* pointer, malloc_type_id_t type) noexcept {
    release(&lumenTypeFree, pointer, pointer, type);
}
void* trackedTypeRealloc(void* pointer, std::size_t bytes, malloc_type_id_t type) noexcept {
    return reallocate(&lumenTypeRealloc, pointer, bytes, false, false, pointer, bytes, type);
}
void* trackedTypeAlignedAlloc(std::size_t alignment, std::size_t bytes,
                            malloc_type_id_t type) noexcept {
    return allocate(&lumenTypeAlignedAlloc, bytes, alignment, bytes, type);
}
void* trackedTypeValloc(std::size_t bytes, malloc_type_id_t type) noexcept {
    return allocate(&lumenTypeValloc, bytes, bytes, type);
}
int trackedTypePosixMemalign(void** pointer, std::size_t alignment, std::size_t bytes,
                            malloc_type_id_t type) noexcept {
    HookScope scope;
    const int result = lumenTypePosixMemalign(pointer, alignment, bytes, type);
    if (scope.locked() && result == 0) record(*pointer, bytes);
    return result;
}
void* trackedTypeZoneMalloc(malloc_zone_t* zone, std::size_t bytes, malloc_type_id_t type) noexcept {
    return allocate(&lumenTypeZoneMalloc, bytes, zone, bytes, type);
}
void* trackedTypeZoneCalloc(malloc_zone_t* zone, std::size_t count, std::size_t bytes,
                          malloc_type_id_t type) noexcept {
    return allocate(&lumenTypeZoneCalloc, count * bytes, zone, count, bytes, type);
}
void* trackedTypeZoneValloc(malloc_zone_t* zone, std::size_t bytes, malloc_type_id_t type) noexcept {
    return allocate(&lumenTypeZoneValloc, bytes, zone, bytes, type);
}
void* trackedTypeZoneMemalign(malloc_zone_t* zone, std::size_t alignment, std::size_t bytes,
                            malloc_type_id_t type) noexcept {
    return allocate(&lumenTypeZoneMemalign, bytes, zone, alignment, bytes, type);
}
void* trackedTypeZoneRealloc(malloc_zone_t* zone, void* pointer, std::size_t bytes,
                           malloc_type_id_t type) noexcept {
    return reallocate(&lumenTypeZoneRealloc, pointer, bytes, false, true, zone, pointer, bytes, type);
}
void trackedTypeZoneFree(malloc_zone_t* zone, void* pointer, malloc_type_id_t type) noexcept {
    release(&lumenTypeZoneFree, pointer, zone, pointer, type);
}

#if defined(LUMEN_HAS_MALLOC_ZONE_OPTIONS)
// A newer SDK can build a profiler for macOS 14. Weak replacees are skipped by
// dyld on hosts that predate these APIs; bindingsValid checks every present API.
void* trackedZoneOptions(malloc_zone_t* zone, std::size_t alignment, std::size_t bytes,
                        malloc_zone_malloc_options_t options) noexcept {
    return allocate(&lumenZoneOptions, bytes, zone, alignment, bytes, options);
}
void* trackedTypeZoneOptions(malloc_zone_t* zone, std::size_t alignment, std::size_t bytes,
                            malloc_type_id_t type, malloc_zone_malloc_options_t options) noexcept {
    return allocate(&lumenTypeZoneOptions, bytes, zone, alignment, bytes, type, options);
}
#endif

// One list owns both the dyld tuples and complete-family binding validation.
#define LUMEN_MAC_ALLOCATOR_HOOKS(X) \
    X(trackedMalloc, malloc) \
    X(trackedCalloc, calloc) \
    X(trackedFree, free) \
    X(trackedRealloc, realloc) \
    X(trackedReallocf, reallocf) \
    X(trackedAlignedAlloc, aligned_alloc) \
    X(trackedValloc, valloc) \
    X(trackedPosixMemalign, posix_memalign) \
    X(trackedZoneMalloc, malloc_zone_malloc) \
    X(trackedZoneCalloc, malloc_zone_calloc) \
    X(trackedZoneValloc, malloc_zone_valloc) \
    X(trackedZoneMemalign, malloc_zone_memalign) \
    X(trackedZoneRealloc, malloc_zone_realloc) \
    X(trackedZoneFree, malloc_zone_free) \
    X(trackedZoneBatchMalloc, malloc_zone_batch_malloc) \
    X(trackedZoneBatchFree, malloc_zone_batch_free) \
    X(trackedDestroyZone, malloc_destroy_zone)

#define LUMEN_MAC_TYPED_HOOKS(X) \
    X(trackedTypeMalloc, lumenTypeMalloc, malloc_type_malloc) \
    X(trackedTypeCalloc, lumenTypeCalloc, malloc_type_calloc) \
    X(trackedTypeFree, lumenTypeFree, malloc_type_free) \
    X(trackedTypeRealloc, lumenTypeRealloc, malloc_type_realloc) \
    X(trackedTypeAlignedAlloc, lumenTypeAlignedAlloc, malloc_type_aligned_alloc) \
    X(trackedTypeValloc, lumenTypeValloc, malloc_type_valloc) \
    X(trackedTypePosixMemalign, lumenTypePosixMemalign, malloc_type_posix_memalign) \
    X(trackedTypeZoneMalloc, lumenTypeZoneMalloc, malloc_type_zone_malloc) \
    X(trackedTypeZoneCalloc, lumenTypeZoneCalloc, malloc_type_zone_calloc) \
    X(trackedTypeZoneValloc, lumenTypeZoneValloc, malloc_type_zone_valloc) \
    X(trackedTypeZoneMemalign, lumenTypeZoneMemalign, malloc_type_zone_memalign) \
    X(trackedTypeZoneRealloc, lumenTypeZoneRealloc, malloc_type_zone_realloc) \
    X(trackedTypeZoneFree, lumenTypeZoneFree, malloc_type_zone_free)

#define LUMEN_INTERPOSE(replacement, original) LUMEN_DYLD_INTERPOSE(replacement, original)
LUMEN_MAC_ALLOCATOR_HOOKS(LUMEN_INTERPOSE)
LUMEN_INTERPOSE(trackedVfree, vfree)
#define LUMEN_INTERPOSE_TYPED(replacement, original, symbol) LUMEN_INTERPOSE(replacement, original)
LUMEN_MAC_TYPED_HOOKS(LUMEN_INTERPOSE_TYPED)
#undef LUMEN_INTERPOSE_TYPED
#if defined(LUMEN_HAS_MALLOC_ZONE_OPTIONS)
LUMEN_INTERPOSE(trackedZoneOptions, lumenZoneOptions)
LUMEN_INTERPOSE(trackedTypeZoneOptions, lumenTypeZoneOptions)
#endif
#undef LUMEN_INTERPOSE

template <typename Original, typename Replacement>
bool bindingMatches(const char* name, Original original, Replacement replacement) noexcept {
    const auto* resolved = dlsym(RTLD_DEFAULT, name);
    if (original == nullptr) return resolved == nullptr;
    Dl_info info{};
    return resolved == reinterpret_cast<const void*>(replacement) &&
        dladdr(reinterpret_cast<const void*>(original), &info) != 0 && info.dli_fname &&
        std::strcmp(info.dli_fname, "/usr/lib/system/libsystem_malloc.dylib") == 0;
}

bool bindingsValid() noexcept {
    if (!ready.load(std::memory_order_acquire)) return false;
#define LUMEN_CHECK_BINDING(replacement, original) \
    if (!bindingMatches(#original, &original, &replacement)) return false;
    LUMEN_MAC_ALLOCATOR_HOOKS(LUMEN_CHECK_BINDING)
    LUMEN_CHECK_BINDING(trackedVfree, vfree)
#define LUMEN_CHECK_TYPED(replacement, original, symbol) \
    if (&original == nullptr || !bindingMatches(#symbol, &original, &replacement)) return false;
    LUMEN_MAC_TYPED_HOOKS(LUMEN_CHECK_TYPED)
#undef LUMEN_CHECK_TYPED
#if defined(LUMEN_HAS_MALLOC_ZONE_OPTIONS)
    if (!bindingMatches("malloc_zone_malloc_with_options", &lumenZoneOptions, &trackedZoneOptions) ||
        !bindingMatches("malloc_type_zone_malloc_with_options", &lumenTypeZoneOptions,
                        &trackedTypeZoneOptions)) return false;
#else
    // An older SDK cannot hook newer public entry points on a newer runtime.
    if (dlsym(RTLD_DEFAULT, "malloc_zone_malloc_with_options") ||
        dlsym(RTLD_DEFAULT, "malloc_type_zone_malloc_with_options")) return false;
#endif
#undef LUMEN_CHECK_BINDING
    return true;
}
#undef LUMEN_MAC_ALLOCATOR_HOOKS
#undef LUMEN_MAC_TYPED_HOOKS

void beforeFork() noexcept {
    pthread_setspecific(hookKey, &hookMarker);
    pthread_mutex_lock(&ledgerMutex);
    ledger.invalidate();
}
void afterForkParent() noexcept {
    pthread_mutex_unlock(&ledgerMutex);
    pthread_setspecific(hookKey, nullptr);
}
void afterForkChild() noexcept {
    ledger.abandon();
    pthread_mutex_unlock(&ledgerMutex);
    pthread_setspecific(hookKey, nullptr);
}
__attribute__((constructor)) void initialize() noexcept {
    if (pthread_key_create(&hookKey, nullptr) != 0) return;
    if (pthread_atfork(beforeFork, afterForkParent, afterForkChild) != 0) return;
    ready.store(true, std::memory_order_release);
}

std::uint64_t begin() noexcept {
    pthread_mutex_lock(&ledgerMutex);
    const auto token = ledger.begin();
    pthread_mutex_unlock(&ledgerMutex);
    return token;
}
NativeAllocationSample finish(std::uint64_t token) noexcept {
    pthread_mutex_lock(&ledgerMutex);
    const auto result = ledger.finish(token);
    pthread_mutex_unlock(&ledgerMutex);
    return result;
}
void cancel(std::uint64_t token) noexcept { (void)finish(token); }
std::uint64_t allocationId(std::uintptr_t address, std::uint64_t token) noexcept {
    pthread_mutex_lock(&ledgerMutex);
    const auto result = ledger.allocationId(address, token);
    pthread_mutex_unlock(&ledgerMutex);
    return result;
}

}  // namespace

extern "C" __attribute__((visibility("default")))
const NativeFrameAllocatorApi* lumen_native_frame_allocator_v1() noexcept {
    static const NativeFrameAllocatorApi api{
        1, sizeof(NativeFrameAllocatorApi), bindingsValid, begin, finish, cancel, allocationId};
    return &api;
}
