#include "engine/platform/window.h"

#include "engine/core/fatal.h"

#include <SDL3/SDL.h>

#include <algorithm>

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

std::vector<DisplaySize> Window::displayModes() const {
    std::vector<DisplaySize> sizes;
    int count = 0;
    SDL_DisplayMode** modes = SDL_GetFullscreenDisplayModes(SDL_GetDisplayForWindow(window_), &count);
    for (int i = 0; modes && i < count; i++)
        if (std::none_of(sizes.begin(), sizes.end(), [&](const DisplaySize& s) { return s.width == modes[i]->w && s.height == modes[i]->h; }))
            sizes.push_back({modes[i]->w, modes[i]->h});
    SDL_free(modes);
    std::sort(sizes.begin(), sizes.end(), [](const DisplaySize& a, const DisplaySize& b) {
        return a.width != b.width ? a.width < b.width : a.height < b.height;
    });
    return sizes;
}

void Window::setDisplay(int width, int height, DisplayMode mode) {
    const SDL_DisplayID display = SDL_GetDisplayForWindow(window_);
    if (mode == DisplayMode::Fullscreen) {
        SDL_DisplayMode nearest;
        if (SDL_GetClosestFullscreenDisplayMode(display, width, height, 0, true, &nearest))
            SDL_SetWindowFullscreenMode(window_, &nearest);
        SDL_SetWindowFullscreen(window_, true);
    } else {
        SDL_SetWindowFullscreen(window_, false);
        SDL_SyncWindow(window_);  // out of fullscreen before the frame and the size change
        SDL_SetWindowBordered(window_, mode == DisplayMode::Windowed);
        SDL_SetWindowResizable(window_, mode == DisplayMode::Windowed);
        const float density = SDL_GetWindowPixelDensity(window_);  // the window's units are pixels over this
        SDL_SetWindowSize(window_, static_cast<int>(width / density + 0.5f), static_cast<int>(height / density + 0.5f));
        SDL_SetWindowPosition(window_, SDL_WINDOWPOS_CENTERED_DISPLAY(display), SDL_WINDOWPOS_CENTERED_DISPLAY(display));
    }
    SDL_SyncWindow(window_);
}

}
