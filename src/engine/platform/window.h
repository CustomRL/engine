#pragma once

struct SDL_Window;

namespace engine {

class Window {
public:
    Window(const char* title, int width, int height);
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    // False once the user has closed the window.
    bool pump();
    void* hwnd() const;
    void pixelSize(int& width, int& height) const;

private:
    SDL_Window* window_;
};

}
