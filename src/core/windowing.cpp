#include "lumen/core/windowing.h"

namespace lumen::core {

const char* appLifecycleName(AppLifecycle lifecycle) {
    switch (lifecycle) {
        case AppLifecycle::Launching:
            return "launching";
        case AppLifecycle::Active:
            return "active";
        case AppLifecycle::Inactive:
            return "inactive";
        case AppLifecycle::Background:
            return "background";
        case AppLifecycle::Suspended:
            return "suspended";
        case AppLifecycle::Terminating:
            return "terminating";
    }
    return "unknown";
}

}  // namespace lumen::core
