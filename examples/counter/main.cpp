#include <cstdio>

#include <SDL3/SDL.h>

// Stage 0/1 empty-window sample. Proves the SDL3 platform baseline works:
// window creation, event pump, drawable-size tracking and clean shutdown.
// Layout/rendering integration arrives in Stage 2/3; this sample deliberately
// stays dependency-free apart from SDL3.
int main(int /*argc*/, char** /*argv*/) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::printf("SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("Lumen Counter - Stage 0/1", 800, 600,
                                          SDL_WINDOW_RESIZABLE |
                                              SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (window == nullptr) {
        std::printf("SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (renderer == nullptr) {
        std::printf("SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    auto logSizes = [window]() {
        int logicalWidth = 0;
        int logicalHeight = 0;
        int pixelWidth = 0;
        int pixelHeight = 0;
        SDL_GetWindowSize(window, &logicalWidth, &logicalHeight);
        SDL_GetWindowSizeInPixels(window, &pixelWidth, &pixelHeight);
        std::printf("logical=%dx%d drawable=%dx%d\n", logicalWidth,
                    logicalHeight, pixelWidth, pixelHeight);
    };
    logSizes();

    bool running = true;
    while (running) {
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_EVENT_QUIT:
                    running = false;
                    break;
                case SDL_EVENT_WINDOW_RESIZED:
                case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                    // SDL3 may change pixel size after creation; re-query
                    // drawable size per docs/lumen-gui-framework-plan.md §2.
                    logSizes();
                    break;
                default:
                    break;
            }
        }

        SDL_SetRenderDrawColor(renderer, 24, 24, 27, 255);
        SDL_RenderClear(renderer);
        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
