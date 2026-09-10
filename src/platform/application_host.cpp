#include "lumen/platform/application_host.h"

namespace lumen::platform {

const char* hostStageName() { return "8A application host"; }

}  // namespace lumen::platform

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
