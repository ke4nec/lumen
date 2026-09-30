#include <cstdio>
#include <cstring>
#include <exception>
#include <fstream>
#include <string>

#include "designer_app.h"
#include "lumen/platform/sdl3_host.h"

namespace {

struct Options {
    bool headless{false};
    std::string filename{};
};

Options parseOptions(int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        if (std::strcmp(argv[index], "--headless") == 0) {
            options.headless = true;
        } else if (std::strcmp(argv[index], "--file") == 0 &&
                   index + 1 < argc) {
            options.filename = argv[++index];
        }
    }
    return options;
}

int runHeadless(lumen::designer_app::DesignerApp& app) {
    app.shell().setView(lumen::core::Size{1280.0F, 800.0F});
    const auto frame = app.shell().renderFrame();
    std::printf("frame0 %016llx\n",
                static_cast<unsigned long long>(frame));
    std::printf("document %d\n", app.workbench().document().has_value() ? 1 : 0);
    std::printf("diagnostics %zu\n", app.workbench().diagnostics().size());
    return app.workbench().frame().hasFrame() ? 0 : 1;
}

int runWindowed(lumen::designer_app::DesignerApp& app) {
    lumen::platform::Sdl3ApplicationHost host;
    lumen::app::RunOptions options;
    options.windowDesc.title = "Lumen Designer";
    options.windowDesc.width = 1280;
    options.windowDesc.height = 800;
    return lumen::app::runApp(app.shell(), host, options);
}

}  // namespace

int main(int argc, char** argv) {
    const Options options = parseOptions(argc, argv);
    lumen::designer_app::DesignerApp app;
    app.attach();
    if (!options.filename.empty()) {
        (void)app.loadFile(options.filename);
    }
    try {
        return options.headless ? runHeadless(app) : runWindowed(app);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "fatal: %s\n", error.what());
        return 1;
    }
}
