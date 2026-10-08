#include "native_frame_allocator_api.h"
#include "native_allocation_ledger.h"

#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <dlfcn.h>
#include <limits>
#include <malloc.h>
#include <pthread.h>
#include <unistd.h>

#if !defined(__GLIBC__)
#error "The optional Lumen allocator profiler currently requires glibc."
#endif

// glibc entry points bootstrap RTLD_NEXT resolution before its constructor.
extern "C" {
void* __libc_malloc(std::size_t) noexcept;
void* __libc_calloc(std::size_t, std::size_t) noexcept;
void* __libc_realloc(void*, std::size_t) noexcept;
void __libc_free(void*) noexcept;
void* __libc_memalign(std::size_t, std::size_t) noexcept;
void cfree(void*) noexcept;
void free_sized(void*, std::size_t) noexcept;
void free_aligned_sized(void*, std::size_t, std::size_t) noexcept;
}

namespace {

using lumen::platform::detail::NativeAllocationSample;
using lumen::platform::detail::NativeFrameAllocatorApi;

struct AllocatorFunctions {
    decltype(&malloc) allocate;
    decltype(&calloc) zeroAllocate;
    decltype(&realloc) reallocate;
    decltype(&free) release;
    decltype(&aligned_alloc) aligned;
    decltype(&memalign) memoryAligned;
    decltype(&posix_memalign) posixAligned;
    decltype(&valloc) pageAligned;
    decltype(&pvalloc) roundedPageAligned;
};

AllocatorFunctions functions{};
std::atomic<bool> ready{false};
thread_local bool insideHook{false};
pthread_mutex_t ledgerMutex = PTHREAD_MUTEX_INITIALIZER;

constinit lumen::platform::detail::NativeAllocationLedger<> ledger;

void record(void* pointer, std::size_t bytes) noexcept {
    ledger.record(reinterpret_cast<std::uintptr_t>(pointer), bytes);
}

void forget(std::uintptr_t address) noexcept {
    ledger.forget(address);
}

// Serialize the native call with its ledger update: another thread cannot free
// or reuse a just-returned address before it has been accounted. Allocator
// recursion delegates directly to glibc and is counted only by the outer call.
class HookScope {
  public:
    HookScope() noexcept {
        if (!insideHook && ready.load(std::memory_order_acquire)) {
            insideHook = true;
            pthread_mutex_lock(&ledgerMutex);
            locked_ = true;
        }
    }
    ~HookScope() {
        if (locked_) {
            pthread_mutex_unlock(&ledgerMutex);
            insideHook = false;
        }
    }
    [[nodiscard]] bool locked() const noexcept { return locked_; }
  private:
    bool locked_{false};
};

template <typename Function>
Function resolve(const char* name) noexcept {
    return reinterpret_cast<Function>(dlsym(RTLD_NEXT, name));
}

void beforeFork() noexcept {
    insideHook = true;
    pthread_mutex_lock(&ledgerMutex);
    ledger.invalidate();
}

void afterForkParent() noexcept {
    pthread_mutex_unlock(&ledgerMutex);
    insideHook = false;
}

void afterForkChild() noexcept {
    ledger.abandon();
    pthread_mutex_unlock(&ledgerMutex);
    insideHook = false;
}

__attribute__((constructor)) void initialize() noexcept {
    insideHook = true;
    functions = {
        resolve<decltype(&malloc)>("malloc"),
        resolve<decltype(&calloc)>("calloc"),
        resolve<decltype(&realloc)>("realloc"),
        resolve<decltype(&free)>("free"),
        resolve<decltype(&aligned_alloc)>("aligned_alloc"),
        resolve<decltype(&memalign)>("memalign"),
        resolve<decltype(&posix_memalign)>("posix_memalign"),
        resolve<decltype(&valloc)>("valloc"),
        resolve<decltype(&pvalloc)>("pvalloc")};
    const bool forkHandlers = pthread_atfork(beforeFork, afterForkParent, afterForkChild) == 0;
    insideHook = false;
    ready.store(forkHandlers && functions.allocate && functions.zeroAllocate &&
                    functions.reallocate && functions.release &&
                    functions.aligned && functions.memoryAligned &&
                    functions.posixAligned && functions.pageAligned &&
                    functions.roundedPageAligned,
                std::memory_order_release);
}

bool bindingsValid() noexcept {
    if (!ready.load(std::memory_order_acquire)) return false;
    // Reject another allocator in the chain, or a partially shadowed family.
    if (functions.allocate != &__libc_malloc ||
        functions.zeroAllocate != &__libc_calloc ||
        functions.reallocate != &__libc_realloc ||
        functions.release != &__libc_free) return false;
    return dlsym(RTLD_DEFAULT, "malloc") == reinterpret_cast<void*>(&malloc) &&
           dlsym(RTLD_DEFAULT, "calloc") == reinterpret_cast<void*>(&calloc) &&
           dlsym(RTLD_DEFAULT, "realloc") == reinterpret_cast<void*>(&realloc) &&
           dlsym(RTLD_DEFAULT, "free") == reinterpret_cast<void*>(&free) &&
           dlsym(RTLD_DEFAULT, "aligned_alloc") == reinterpret_cast<void*>(&aligned_alloc) &&
           dlsym(RTLD_DEFAULT, "memalign") == reinterpret_cast<void*>(&memalign) &&
           dlsym(RTLD_DEFAULT, "posix_memalign") == reinterpret_cast<void*>(&posix_memalign) &&
           dlsym(RTLD_DEFAULT, "valloc") == reinterpret_cast<void*>(&valloc) &&
           dlsym(RTLD_DEFAULT, "pvalloc") == reinterpret_cast<void*>(&pvalloc) &&
           dlsym(RTLD_DEFAULT, "cfree") == reinterpret_cast<void*>(&cfree) &&
           dlsym(RTLD_DEFAULT, "free_sized") == reinterpret_cast<void*>(&free_sized) &&
           dlsym(RTLD_DEFAULT, "free_aligned_sized") == reinterpret_cast<void*>(&free_aligned_sized);
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

extern "C" {

void* malloc(std::size_t size) noexcept {
    HookScope scope;
    void* result = ready.load(std::memory_order_acquire)
        ? functions.allocate(size) : __libc_malloc(size);
    if (scope.locked()) record(result, size);
    return result;
}

void* calloc(std::size_t count, std::size_t size) noexcept {
    HookScope scope;
    void* result = ready.load(std::memory_order_acquire)
        ? functions.zeroAllocate(count, size) : __libc_calloc(count, size);
    if (scope.locked()) record(result, count * size);
    return result;
}

void free(void* pointer) noexcept {
    const int savedErrno = errno;
    HookScope scope;
    if (scope.locked()) forget(reinterpret_cast<std::uintptr_t>(pointer));
    if (ready.load(std::memory_order_acquire)) functions.release(pointer);
    else __libc_free(pointer);
    errno = savedErrno;
}

void* realloc(void* pointer, std::size_t size) noexcept {
    HookScope scope;
    const auto oldAddress = reinterpret_cast<std::uintptr_t>(pointer);
    void* result = ready.load(std::memory_order_acquire)
        ? functions.reallocate(pointer, size) : __libc_realloc(pointer, size);
    if (scope.locked() && (result != nullptr || size == 0)) {
        forget(oldAddress);
        record(result, size);
    }
    return result;
}

void* aligned_alloc(std::size_t alignment, std::size_t size) noexcept {
    HookScope scope;
    void* result = ready.load(std::memory_order_acquire)
        ? functions.aligned(alignment, size) : __libc_memalign(alignment, size);
    if (scope.locked()) record(result, size);
    return result;
}

void* memalign(std::size_t alignment, std::size_t size) noexcept {
    HookScope scope;
    void* result = ready.load(std::memory_order_acquire)
        ? functions.memoryAligned(alignment, size) : __libc_memalign(alignment, size);
    if (scope.locked()) record(result, size);
    return result;
}

int posix_memalign(void** pointer, std::size_t alignment, std::size_t size) noexcept {
    const int savedErrno = errno;
    HookScope scope;
    int result;
    if (ready.load(std::memory_order_acquire)) {
        result = functions.posixAligned(pointer, alignment, size);
    } else if (alignment < sizeof(void*) || (alignment & (alignment - 1)) != 0) {
        result = EINVAL;
    } else {
        void* allocated = __libc_memalign(alignment, size);
        result = allocated != nullptr ? 0 : ENOMEM;
        if (result == 0) *pointer = allocated;
    }
    if (scope.locked() && result == 0) record(*pointer, size);
    errno = savedErrno;
    return result;
}

void* valloc(std::size_t size) noexcept {
    HookScope scope;
    void* result = ready.load(std::memory_order_acquire)
        ? functions.pageAligned(size) : __libc_memalign(getpagesize(), size);
    if (scope.locked()) record(result, size);
    return result;
}

void* pvalloc(std::size_t size) noexcept {
    HookScope scope;
    const auto page = static_cast<std::size_t>(getpagesize());
    if (size > std::numeric_limits<std::size_t>::max() - (page - 1)) {
        errno = ENOMEM;
        return nullptr;
    }
    const auto rounded = size == 0 ? page : (size + page - 1) / page * page;
    void* result = ready.load(std::memory_order_acquire)
        ? functions.roundedPageAligned(size) : __libc_memalign(page, rounded);
    if (scope.locked()) record(result, rounded);
    return result;
}

// C23 sized frees may bypass the public free symbol inside libc.
void free_sized(void* pointer, std::size_t) noexcept { free(pointer); }
void free_aligned_sized(void* pointer, std::size_t, std::size_t) noexcept {
    free(pointer);
}
void cfree(void* pointer) noexcept { free(pointer); }

const NativeFrameAllocatorApi* lumen_native_frame_allocator_v1() noexcept {
    static const NativeFrameAllocatorApi api{
        1, sizeof(NativeFrameAllocatorApi), bindingsValid, begin, finish, cancel, allocationId};
    return &api;
}

}  // extern "C"
