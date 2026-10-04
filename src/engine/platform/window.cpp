#include "engine/platform/window.h"

#include "engine/core/fatal.h"

#include <SDL3/SDL.h>

namespace engine {

Window::Window(const char* title, int width, int height) {
    if (!SDL_Init(SDL_INIT_VIDEO))
        fatal("SDL_Init: %s", SDL_GetError());
    window_ = SDL_CreateWindow(title, width, height, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window_)
        fatal("SDL_CreateWindow: %s", SDL_GetError());
}

Window::~Window() {
    SDL_DestroyWindow(window_);
    SDL_Quit();
}

bool Window::pump() {
    SDL_Event event;
    while (SDL_PollEvent(&event))
        if (event.type == SDL_EVENT_QUIT)
            return false;
    return true;
}

void* Window::hwnd() const {
    return SDL_GetPointerProperty(SDL_GetWindowProperties(window_), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
}

void Window::pixelSize(int& width, int& height) const {
    SDL_GetWindowSizeInPixels(window_, &width, &height);
}

}
