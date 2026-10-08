#include <cstdlib>
#include <new>

// This DLL uses its own static CRT; only its matching exports release objects.
extern "C" void* lumen_test_crt_allocate(std::size_t bytes) {
    return ::operator new(bytes, std::align_val_t{64});
}
extern "C" void lumen_test_crt_release(void* pointer) {
    ::operator delete(pointer, std::align_val_t{64});
}
