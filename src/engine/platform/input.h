#pragma once

union SDL_Event;
struct SDL_Gamepad;

namespace engine {

// Keyboard keys are SDL scancodes; mouse buttons follow them, as MouseButton + SDL's button number (1 left, 2 middle, 3 right, 4, 5)
constexpr int MouseButton = 512;
constexpr int KeyCount = MouseButton + 6;

struct Gamepad {
    float axes[6] = {};     // SDL_GamepadAxis order: left x, left y, right x, right y (-1..1, y down), left and right trigger (0..1)
    bool buttons[26] = {};  // SDL_GamepadButton order
    bool playstation = false;
};

class Input {
public:
    Input();
    ~Input();
    Input(const Input&) = delete;
    Input& operator=(const Input&) = delete;

    // Window::pump hands it every event
    void handle(const SDL_Event& event);

    // When the key went down, in engine::seconds(), or a negative value while it is up
    double pressedAt(int key) const { return pressed_[key]; }
    bool held(int key) const { return pressed_[key] >= 0; }
    // The mouse's movement since the last call, in pixels, y growing downward
    void takeMouseMotion(float& dx, float& dy);
    // The wheel's turn since the last call, in notches, positive away from the user
    float takeWheel();
    // The first connected gamepad as of now, or null without one
    const Gamepad* gamepad();

private:
    void openGamepad();

    double pressed_[KeyCount];
    float mouseX_ = 0;
    float mouseY_ = 0;
    float wheel_ = 0;
    SDL_Gamepad* pad_ = nullptr;
    Gamepad state_;
};

}
