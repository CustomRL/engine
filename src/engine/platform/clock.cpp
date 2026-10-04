#include "engine/platform/clock.h"

#include <SDL3/SDL_timer.h>

namespace engine {

double seconds() {
    return SDL_GetTicksNS() / 1e9;
}

}
