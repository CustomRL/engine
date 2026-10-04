#pragma once

#include "engine/platform/input.h"

struct SDL_Window;

namespace engine {

class Window {
public:
    Window(const char* title, int width, int height);
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    // Hands every pending event to input(). False once the user has closed the window.
    bool pump();
    Input& input() { return input_; }
    void* hwnd() const;
    void pixelSize(int& width, int& height) const;
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
