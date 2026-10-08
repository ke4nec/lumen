#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <MinHook.h>

#include <array>
#include <atomic>
#include <cstring>

#include "native_allocation_ledger.h"

namespace {
using lumen::platform::detail::NativeAllocationLedger;
using lumen::platform::detail::NativeAllocationSample;
using lumen::platform::detail::NativeFrameAllocatorApi;

using Allocate = PVOID (NTAPI*)(PVOID, ULONG, SIZE_T);
using Reallocate = PVOID (NTAPI*)(PVOID, ULONG, PVOID, SIZE_T);
using Free = BOOLEAN (NTAPI*)(PVOID, ULONG, PVOID);
using Destroy = PVOID (NTAPI*)(PVOID);
using Create = PVOID (NTAPI*)(ULONG, PVOID, SIZE_T, SIZE_T, PVOID, PVOID);

constinit NativeAllocationLedger<> ledger;
SRWLOCK ledgerLock = SRWLOCK_INIT;
INIT_ONCE initialization = INIT_ONCE_STATIC_INIT;
std::atomic<bool> ready{false};
DWORD recursionSlot = TLS_OUT_OF_INDEXES;
HMODULE ntdll{};
void* originalAllocate{};
void* originalReallocate{};
void* originalFree{};
void* originalDestroy{};
void* originalCreate{};

bool enterHook() noexcept {
    if (!ready.load(std::memory_order_acquire)) return false;
    const auto error = GetLastError();
    if (TlsGetValue(recursionSlot) != nullptr) {
        SetLastError(error);
        return false;
    }
    if (!TlsSetValue(recursionSlot, reinterpret_cast<void*>(1))) {
        ready.store(false, std::memory_order_release);
        SetLastError(error);
        return false;
    }
    AcquireSRWLockExclusive(&ledgerLock);
    SetLastError(error);
    return true;
}

void leaveHook(bool entered) noexcept {
    if (!entered) return;
    const auto error = GetLastError();
    ReleaseSRWLockExclusive(&ledgerLock);
    if (!TlsSetValue(recursionSlot, nullptr)) ready.store(false, std::memory_order_release);
    SetLastError(error);
}

// SEH termination handlers also run for HEAP_GENERATE_EXCEPTIONS. Ordinary C++
// destructors with /EHsc would leave the lock and recursion marker set.
PVOID NTAPI allocateHook(PVOID heap, ULONG flags, SIZE_T bytes) {
    const bool entered = enterHook();
    PVOID pointer{};
    __try {
        pointer = reinterpret_cast<Allocate>(originalAllocate)(heap, flags, bytes);
        if (entered) ledger.record(reinterpret_cast<std::uintptr_t>(pointer), bytes,
                                   reinterpret_cast<std::uintptr_t>(heap));
    } __finally {
        leaveHook(entered);
    }
    return pointer;
}

PVOID NTAPI reallocateHook(PVOID heap, ULONG flags, PVOID old, SIZE_T bytes) {
    const bool entered = enterHook();
    const auto oldAddress = reinterpret_cast<std::uintptr_t>(old);
    PVOID pointer{};
    __try {
        pointer = reinterpret_cast<Reallocate>(originalReallocate)(heap, flags, old, bytes);
        if (entered && pointer != nullptr) {
            ledger.forget(oldAddress);
            ledger.record(reinterpret_cast<std::uintptr_t>(pointer), bytes,
                          reinterpret_cast<std::uintptr_t>(heap));
        }
    } __finally {
        leaveHook(entered);
    }
    return pointer;
}

BOOLEAN NTAPI freeHook(PVOID heap, ULONG flags, PVOID pointer) {
    const bool entered = enterHook();
    const auto address = reinterpret_cast<std::uintptr_t>(pointer);
    BOOLEAN released{};
    __try {
        released = reinterpret_cast<Free>(originalFree)(heap, flags, pointer);
        if (entered && released) ledger.forget(address);
    } __finally {
        leaveHook(entered);
    }
    return released;
}

PVOID NTAPI destroyHook(PVOID heap) {
    const bool entered = enterHook();
    const auto owner = reinterpret_cast<std::uintptr_t>(heap);
    PVOID result{};
    __try {
        result = reinterpret_cast<Destroy>(originalDestroy)(heap);
        if (entered && result == nullptr) {
            ledger.forgetOwner(owner);
        }
    } __finally {
        leaveHook(entered);
    }
    return result;
}

PVOID NTAPI createHook(ULONG flags, PVOID base, SIZE_T reserve, SIZE_T commit,
                      PVOID lock, PVOID parameters) {
    const bool entered = enterHook();
    PVOID result{};
    __try {
        // Heap manager metadata is internal to creating the allocator itself.
        result = reinterpret_cast<Create>(originalCreate)(flags, base, reserve, commit, lock, parameters);
    } __finally {
        leaveHook(entered);
    }
    return result;
}

struct Hook {
    const char* name;
    void* replacement;
    void** original;
    void* target{};
    std::array<unsigned char, 37> binding{};
};

std::array hooks{
    Hook{"RtlAllocateHeap", reinterpret_cast<void*>(allocateHook), &originalAllocate},
    Hook{"RtlReAllocateHeap", reinterpret_cast<void*>(reallocateHook), &originalReallocate},
    Hook{"RtlFreeHeap", reinterpret_cast<void*>(freeHook), &originalFree},
    Hook{"RtlDestroyHeap", reinterpret_cast<void*>(destroyHook), &originalDestroy},
    Hook{"RtlCreateHeap", reinterpret_cast<void*>(createHook), &originalCreate}
};

bool readBinding(const Hook& hook, std::array<unsigned char, 37>& binding) noexcept {
    SIZE_T read{};
    // Include the five bytes above the entry used by MinHook's hot-patch path.
    const auto address = reinterpret_cast<std::uintptr_t>(hook.target) - 5;
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address),
        binding.data(), binding.size(), &read) && read == binding.size();
}

bool bindingsValid() noexcept {
    if (!ready.load(std::memory_order_acquire)) return false;
    for (const auto& hook : hooks) {
        std::array<unsigned char, 37> current{};
        if (reinterpret_cast<void*>(GetProcAddress(ntdll, hook.name)) != hook.target ||
            !readBinding(hook, current) || current != hook.binding) return false;
    }
    return true;
}

BOOL CALLBACK initialize(PINIT_ONCE, PVOID, PVOID*) noexcept {
    recursionSlot = TlsAlloc();
    // Higher TLS indexes allocate an extension block on a thread's first use.
    // Reserve a pre-existing TEB slot before installing any heap detour.
    if (recursionSlot >= TLS_MINIMUM_AVAILABLE) {
        if (recursionSlot != TLS_OUT_OF_INDEXES) TlsFree(recursionSlot);
        recursionSlot = TLS_OUT_OF_INDEXES;
        return TRUE;
    }
    ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll == nullptr || MH_Initialize() != MH_OK) return TRUE;
    for (auto& hook : hooks) {
        hook.target = reinterpret_cast<void*>(GetProcAddress(ntdll, hook.name));
        MEMORY_BASIC_INFORMATION memory{};
        if (hook.target == nullptr ||
            VirtualQuery(hook.target, &memory, sizeof(memory)) != sizeof(memory) ||
            memory.AllocationBase != ntdll ||
            MH_CreateHook(hook.target, hook.replacement, hook.original) != MH_OK ||
            MH_QueueEnableHook(hook.target) != MH_OK) {
            MH_Uninitialize();
            return TRUE;
        }
    }
    if (MH_ApplyQueued() != MH_OK) {
        // ApplyQueued can enable only part of the set before failing. Never
        // release trampolines that another thread may already be executing.
        MH_DisableHook(MH_ALL_HOOKS);
        return TRUE;
    }
    for (auto& hook : hooks) {
        if (!readBinding(hook, hook.binding)) {
            MH_DisableHook(MH_ALL_HOOKS);
            return TRUE;
        }
    }
    ready.store(true, std::memory_order_release);
    return TRUE;
}

std::uint64_t begin() noexcept {
    const bool entered = enterHook();
    const auto token = entered ? ledger.begin() : 0;
    leaveHook(entered);
    return token;
}

NativeAllocationSample finish(std::uint64_t token) noexcept {
    const bool entered = enterHook();
    const auto sample = entered ? ledger.finish(token) : NativeAllocationSample{};
    leaveHook(entered);
    return sample;
}

void cancel(std::uint64_t token) noexcept { (void)finish(token); }

std::uint64_t allocationId(std::uintptr_t address, std::uint64_t token) noexcept {
    const bool entered = enterHook();
    const auto id = entered ? ledger.containingAllocationId(address, token) : 0;
    leaveHook(entered);
    return id;
}

const NativeFrameAllocatorApi api{1, sizeof(NativeFrameAllocatorApi),
    bindingsValid, begin, finish, cancel, allocationId};
}  // namespace

extern "C" const NativeFrameAllocatorApi* lumen_native_frame_allocator_v1() noexcept {
    // Called by the SDK after pinning the DLL, outside the loader lock.
    InitOnceExecuteOnce(&initialization, initialize, nullptr, nullptr);
    return ready.load(std::memory_order_acquire) ? &api : nullptr;
}
