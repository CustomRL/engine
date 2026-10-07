#pragma once

#include "engine/platform/input.h"

#include <vector>

struct SDL_Window;

namespace engine {

enum class DisplayMode { Fullscreen, Windowed, Borderless };

struct DisplaySize {
    int width, height;
};

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
    // The system's mouse cursor over the window, shown or hidden; a call that changes nothing does nothing
    void showCursor(bool visible);
    // Check runs: input as if the user gave it, handed to input() at once with no system-wide input and no move of the real cursor. The
    // first mouse call makes cursor() report the synthetic position (pixels as pixelSize counts them) from then on, the real cursor's no
    // longer; a button's number is SDL's (1 left)
    void pushMouseMove(float x, float y);
    void pushMouseButton(int button, bool down);
    void pushKey(int scancode, bool down);
    // On, pump drops what the user does (keys, mouse, focus changes, pads), so a run sees only what the push calls give it
    void setPushedInputOnly(bool on);
    void setTitle(const char* title);
    // The sizes in pixels the window's display offers as fullscreen modes, each once, smallest first
    std::vector<DisplaySize> displayModes() const;
    // The window at width x height pixels in a display mode: Fullscreen switches the display to the nearest of its modes, Borderless is a
    // frameless window of that size centred on the display, Windowed a framed, resizable one centred on it. Returns once the system has done it.
    void setDisplay(int width, int height, DisplayMode mode);

private:
    // SDL itself, declared first so it quits after every member that holds SDL objects is gone
    struct Sdl {
        Sdl();
        ~Sdl();
    } sdl_;
    SDL_Window* window_;
    Input input_;
    bool cursorShown_ = true;
    bool syntheticCursor_ = false, pushedMove_ = false, pushedOnly_ = false;
    float cursorX_ = 0, cursorY_ = 0;  // the synthetic cursor, in the window's units
};

}
