#pragma once

#include <cstddef>
#include <malloc/malloc.h>

// Older SDKs mark typed entry points unavailable despite exporting them from
// libsystem_malloc. These private weak aliases are for optional instrumentation,
// never app allocation policy. The factory requires every binding at runtime.
extern "C" {
void* lumenTypeMalloc(std::size_t, malloc_type_id_t)
    __asm("_malloc_type_malloc") __attribute__((weak_import));
void* lumenTypeCalloc(std::size_t, std::size_t, malloc_type_id_t)
    __asm("_malloc_type_calloc") __attribute__((weak_import));
void lumenTypeFree(void*, malloc_type_id_t)
    __asm("_malloc_type_free") __attribute__((weak_import));
void* lumenTypeRealloc(void*, std::size_t, malloc_type_id_t)
    __asm("_malloc_type_realloc") __attribute__((weak_import));
void* lumenTypeAlignedAlloc(std::size_t, std::size_t, malloc_type_id_t)
    __asm("_malloc_type_aligned_alloc") __attribute__((weak_import));
void* lumenTypeValloc(std::size_t, malloc_type_id_t)
    __asm("_malloc_type_valloc") __attribute__((weak_import));
int lumenTypePosixMemalign(void**, std::size_t, std::size_t, malloc_type_id_t)
    __asm("_malloc_type_posix_memalign") __attribute__((weak_import));
void* lumenTypeZoneMalloc(malloc_zone_t*, std::size_t, malloc_type_id_t)
    __asm("_malloc_type_zone_malloc") __attribute__((weak_import));
void* lumenTypeZoneCalloc(malloc_zone_t*, std::size_t, std::size_t, malloc_type_id_t)
    __asm("_malloc_type_zone_calloc") __attribute__((weak_import));
void* lumenTypeZoneRealloc(malloc_zone_t*, void*, std::size_t, malloc_type_id_t)
    __asm("_malloc_type_zone_realloc") __attribute__((weak_import));
void* lumenTypeZoneMemalign(malloc_zone_t*, std::size_t, std::size_t, malloc_type_id_t)
    __asm("_malloc_type_zone_memalign") __attribute__((weak_import));
void* lumenTypeZoneValloc(malloc_zone_t*, std::size_t, malloc_type_id_t)
    __asm("_malloc_type_zone_valloc") __attribute__((weak_import));
void lumenTypeZoneFree(malloc_zone_t*, void*, malloc_type_id_t)
    __asm("_malloc_type_zone_free") __attribute__((weak_import));
#if defined(LUMEN_HAS_MALLOC_ZONE_OPTIONS)
void* lumenZoneOptions(malloc_zone_t*, std::size_t, std::size_t, malloc_zone_malloc_options_t)
    __asm("_malloc_zone_malloc_with_options") __attribute__((weak_import));
void* lumenTypeZoneOptions(malloc_zone_t*, std::size_t, std::size_t, malloc_type_id_t,
                          malloc_zone_malloc_options_t)
    __asm("_malloc_type_zone_malloc_with_options") __attribute__((weak_import));
#endif
}
