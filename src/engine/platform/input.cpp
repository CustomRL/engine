#include "engine/platform/input.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <iterator>
#include <utility>

namespace engine {

static_assert(SDL_SCANCODE_COUNT == MouseButton);
static_assert(SDL_GAMEPAD_BUTTON_COUNT == std::size(Gamepad{}.buttons));
static_assert(SDL_GAMEPAD_AXIS_COUNT == std::size(Gamepad{}.axes));

Input::Input() {
    std::fill(std::begin(pressed_), std::end(pressed_), -1.0);
}

Input::~Input() {
    if (pad_)
        SDL_CloseGamepad(pad_);
}

void Input::handle(const SDL_Event& event) {
    const double at = event.common.timestamp / 1e9;
    auto press = [&](int key) {
        if (pressed_[key] < 0)  // key repeats keep the first press's time
            pressed_[key] = at;
    };
    switch (event.type) {
    case SDL_EVENT_KEY_DOWN:
        press(event.key.scancode);
        break;
    case SDL_EVENT_KEY_UP:
        pressed_[event.key.scancode] = -1;
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (event.button.button < KeyCount - MouseButton)
            press(MouseButton + event.button.button);
        break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (event.button.button < KeyCount - MouseButton)
            pressed_[MouseButton + event.button.button] = -1;
        break;
    case SDL_EVENT_MOUSE_MOTION:
        mouseX_ += event.motion.xrel;
        mouseY_ += event.motion.yrel;
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        wheel_ += event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -event.wheel.y : event.wheel.y;
        break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:  // a key let go in another window never sends its release here
        std::fill(std::begin(pressed_), std::end(pressed_), -1.0);
        break;
    case SDL_EVENT_GAMEPAD_ADDED:
        if (!pad_)
            pad_ = SDL_OpenGamepad(event.gdevice.which);
        break;
    case SDL_EVENT_GAMEPAD_REMOVED:
        if (pad_ && SDL_GetGamepadID(pad_) == event.gdevice.which) {
            SDL_CloseGamepad(pad_);
            pad_ = nullptr;
            openGamepad();
        }
        break;
    default:
        break;
    }
}

void Input::takeMouseMotion(float& dx, float& dy) {
    dx = mouseX_;
    dy = mouseY_;
    mouseX_ = mouseY_ = 0;
}

float Input::takeWheel() {
    return std::exchange(wheel_, 0.0f);
}

const Gamepad* Input::gamepad() {
    if (!pad_)
        return nullptr;
    for (int a = 0; a < SDL_GAMEPAD_AXIS_COUNT; a++)
        state_.axes[a] = std::max(SDL_GetGamepadAxis(pad_, static_cast<SDL_GamepadAxis>(a)) / 32767.0f, -1.0f);
    for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; b++)
        state_.buttons[b] = SDL_GetGamepadButton(pad_, static_cast<SDL_GamepadButton>(b));
    const SDL_GamepadType type = SDL_GetGamepadType(pad_);
    state_.playstation = type == SDL_GAMEPAD_TYPE_PS3 || type == SDL_GAMEPAD_TYPE_PS4 || type == SDL_GAMEPAD_TYPE_PS5;
    return &state_;
}

void Input::openGamepad() {
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    if (ids && count > 0)
        pad_ = SDL_OpenGamepad(ids[0]);
    SDL_free(ids);
}

}
