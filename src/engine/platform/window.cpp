#include "engine/platform/window.h"

#include "engine/core/fatal.h"

#include <SDL3/SDL.h>

namespace engine {

Window::Sdl::Sdl() {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD))
        fatal("SDL_Init: %s", SDL_GetError());
}

Window::Sdl::~Sdl() {
    SDL_Quit();
}

Window::Window(const char* title, int width, int height, bool borderless) {
    window_ = SDL_CreateWindow(title, width, height,
                               (borderless ? SDL_WINDOW_BORDERLESS : SDL_WINDOW_RESIZABLE) | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window_)
        fatal("SDL_CreateWindow: %s", SDL_GetError());
}

Window::~Window() {
    SDL_DestroyWindow(window_);
}

bool Window::pump() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT)
            return false;
        input_.handle(event);
    }
    return true;
}

void* Window::hwnd() const {
    return SDL_GetPointerProperty(SDL_GetWindowProperties(window_), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
}

void Window::pixelSize(int& width, int& height) const {
    SDL_GetWindowSizeInPixels(window_, &width, &height);
}

void Window::cursor(float& x, float& y) const {
    SDL_GetMouseState(&x, &y);  // window coordinates, which a high pixel density scales to pixels
    const float density = SDL_GetWindowPixelDensity(window_);
    x *= density;
    y *= density;
}

void Window::showCursor(bool visible) {
    if (visible == cursorShown_)
        return;
    cursorShown_ = visible;
    if (visible)
        SDL_ShowCursor();
    else
        SDL_HideCursor();
}

void Window::setTitle(const char* title) {
    SDL_SetWindowTitle(window_, title);
}

}
