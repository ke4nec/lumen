#include <SDL3/SDL.h>

// Linux LD_PRELOAD fixture: fail every GL swap and forbid re-entering SDL's
// renderer path. A successful fallback must use a native software surface.
extern "C" bool SDLCALL SDL_GL_SwapWindow(SDL_Window*) {
    return SDL_SetError("injected persistent GL swap failure");
}

extern "C" SDL_Renderer* SDLCALL SDL_CreateRenderer(SDL_Window*, const char*) {
    SDL_SetError("injected SDL renderer unavailable");
    return nullptr;
}

#ifdef LUMEN_TEST_FAIL_SOFTWARE_PRESENT
extern "C" bool SDLCALL SDL_UpdateWindowSurface(SDL_Window*) {
    return SDL_SetError("injected software present failure");
}
#endif
