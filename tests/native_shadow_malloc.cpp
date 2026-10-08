#include <cstddef>

extern "C" void* __libc_malloc(std::size_t) noexcept;
extern "C" void* malloc(std::size_t size) noexcept {
    return __libc_malloc(size);
}
