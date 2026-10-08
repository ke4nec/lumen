#include <cstdlib>
#include "../src/platform/native_interpose_mac.h"

static void* shadowMalloc(std::size_t bytes) { return malloc(bytes); }
LUMEN_DYLD_INTERPOSE(shadowMalloc, malloc)
