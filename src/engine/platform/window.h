#pragma once

#include "engine/platform/input.h"

struct SDL_Window;

namespace engine {

class Window {
public:
    // borderless: no frame or title bar, as a game's borderless display mode opens it
    Window(const char* title, int width, int height, bool borderless = false);
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    // Hands every pending event to input(). False once the user has closed the window.
    bool pump();
    Input& input() { return input_; }
    void* hwnd() const;
    void pixelSize(int& width, int& height) const;
    // The mouse cursor's position over the window as of the last pump, in pixels as pixelSize counts them
    void cursor(float& x, float& y) const;
    void setTitle(const char* title);

private:
    // SDL itself, declared first so it quits after every member that holds SDL objects is gone
    struct Sdl {
        Sdl();
        ~Sdl();
    } sdl_;
    SDL_Window* window_;
    Input input_;
};

}
