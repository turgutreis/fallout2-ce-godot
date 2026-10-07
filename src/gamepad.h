#ifndef FALLOUT_GAMEPAD_H_
#define FALLOUT_GAMEPAD_H_

#include <SDL.h>

namespace fallout {

// Initializes SDL GameController subsystem and opens available controllers.
bool gamepadInit();

// Closes open controllers and shuts down GameController subsystem.
void gamepadFree();

// Handles SDL controller events (device added/removed, button, axis).
void gamepadHandleEvent(const SDL_Event* event);

// Retrieves current mouse delta, button state, and scroll wheel delta from gamepad.
// Called during mouse polling.
void gamepadGetState(int* out_dx, int* out_dy, int* out_buttons, int* out_wheel_y);

} // namespace fallout

#endif /* FALLOUT_GAMEPAD_H_ */
