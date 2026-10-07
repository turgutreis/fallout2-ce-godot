#include "gamepad.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "combat.h"
#include "input.h"
#include "kb.h"
#include "mouse.h"

namespace fallout {

static const int kAxisDeadzone = 6000;
static const int kTriggerThreshold = 10000;
static const float kMinCursorSpeed = 2.0f;
static const float kMaxCursorSpeed = 22.0f;
static const unsigned int kDpadRepeatInitialDelayMs = 250;
static const unsigned int kDpadRepeatIntervalMs = 120;

static std::vector<SDL_GameController*> gControllers;

static bool gButtonALeftClick = false;
static bool gButtonXRightClick = false;
static bool gTriggerRLeftClick = false;
static bool gTriggerLRightClick = false;

static bool gDpadUpHeld = false;
static bool gDpadDownHeld = false;
static unsigned int gDpadLastTick = 0;
static unsigned int gDpadNextRepeatDelay = kDpadRepeatInitialDelayMs;

static float gSubpixelAccumX = 0.0f;
static float gSubpixelAccumY = 0.0f;

static void openController(int joystickIndex)
{
    if (SDL_IsGameController(joystickIndex)) {
        SDL_GameController* controller = SDL_GameControllerOpen(joystickIndex);
        if (controller != nullptr) {
            gControllers.push_back(controller);
        }
    }
}

static void closeController(SDL_JoystickID joystickId)
{
    for (auto it = gControllers.begin(); it != gControllers.end(); ++it) {
        SDL_Joystick* joystick = SDL_GameControllerGetJoystick(*it);
        if (joystick != nullptr && SDL_JoystickInstanceID(joystick) == joystickId) {
            SDL_GameControllerClose(*it);
            gControllers.erase(it);
            break;
        }
    }
}

bool gamepadInit()
{
    if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) {
        return false;
    }

    int numJoysticks = SDL_NumJoysticks();
    for (int i = 0; i < numJoysticks; ++i) {
        openController(i);
    }

    return true;
}

void gamepadFree()
{
    for (SDL_GameController* controller : gControllers) {
        if (controller != nullptr) {
            SDL_GameControllerClose(controller);
        }
    }
    gControllers.clear();

    SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
}

void gamepadHandleEvent(const SDL_Event* event)
{
    if (event == nullptr) {
        return;
    }

    switch (event->type) {
    case SDL_CONTROLLERDEVICEADDED:
        openController(event->cdevice.which);
        break;

    case SDL_CONTROLLERDEVICEREMOVED:
        closeController(event->cdevice.which);
        break;

    case SDL_CONTROLLERBUTTONDOWN:
        switch (event->cbutton.button) {
        case SDL_CONTROLLER_BUTTON_A:
            gButtonALeftClick = true;
            break;
        case SDL_CONTROLLER_BUTTON_X:
            gButtonXRightClick = true;
            break;
        case SDL_CONTROLLER_BUTTON_B:
            enqueueInputEvent(KEY_LOWERCASE_I); // Inventory
            break;
        case SDL_CONTROLLER_BUTTON_Y:
            enqueueInputEvent(KEY_LOWERCASE_P); // Pip-Boy
            break;
        case SDL_CONTROLLER_BUTTON_START:
            enqueueInputEvent(KEY_ESCAPE); // Options / Menu / Cancel
            break;
        case SDL_CONTROLLER_BUTTON_BACK:
            enqueueInputEvent(KEY_LOWERCASE_C); // Character Sheet
            break;
        case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
            enqueueInputEvent(KEY_LOWERCASE_B); // Switch active weapon/item
            break;
        case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
            if (isInCombat()) {
                enqueueInputEvent(KEY_RETURN); // End Combat Turn
            } else {
                enqueueInputEvent(KEY_LOWERCASE_A); // Enter Combat Mode
            }
            break;
        case SDL_CONTROLLER_BUTTON_LEFTSTICK:
            enqueueInputEvent(KEY_1); // Toggle Sneak
            break;
        case SDL_CONTROLLER_BUTTON_RIGHTSTICK:
            enqueueInputEvent(KEY_LOWERCASE_M); // Cycle Mouse Cursor Mode
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
            enqueueInputEvent(KEY_TAB); // Automap
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
            enqueueInputEvent(KEY_LOWERCASE_S); // Skilldex
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_UP:
            gDpadUpHeld = true;
            gDpadLastTick = SDL_GetTicks();
            gDpadNextRepeatDelay = kDpadRepeatInitialDelayMs;
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
            gDpadDownHeld = true;
            gDpadLastTick = SDL_GetTicks();
            gDpadNextRepeatDelay = kDpadRepeatInitialDelayMs;
            break;
        default:
            break;
        }
        break;

    case SDL_CONTROLLERBUTTONUP:
        switch (event->cbutton.button) {
        case SDL_CONTROLLER_BUTTON_A:
            gButtonALeftClick = false;
            break;
        case SDL_CONTROLLER_BUTTON_X:
            gButtonXRightClick = false;
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_UP:
            gDpadUpHeld = false;
            break;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
            gDpadDownHeld = false;
            break;
        default:
            break;
        }
        break;

    case SDL_CONTROLLERAXISMOTION:
        if (event->caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT) {
            gTriggerRLeftClick = (event->caxis.value > kTriggerThreshold);
        } else if (event->caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT) {
            gTriggerLRightClick = (event->caxis.value > kTriggerThreshold);
        }
        break;

    default:
        break;
    }
}

void gamepadGetState(int* out_dx, int* out_dy, int* out_buttons, int* out_wheel_y)
{
    if (out_dx != nullptr) *out_dx = 0;
    if (out_dy != nullptr) *out_dy = 0;
    if (out_buttons != nullptr) *out_buttons = 0;
    if (out_wheel_y != nullptr) *out_wheel_y = 0;

    if (gControllers.empty()) {
        return;
    }

    // Combine button states
    if (out_buttons != nullptr) {
        if (gButtonALeftClick || gTriggerRLeftClick) {
            *out_buttons |= MOUSE_STATE_LEFT_BUTTON_DOWN;
        }
        if (gButtonXRightClick || gTriggerLRightClick) {
            *out_buttons |= MOUSE_STATE_RIGHT_BUTTON_DOWN;
        }
    }

    // Handle D-pad scroll wheel emulation
    if (out_wheel_y != nullptr) {
        unsigned int now = SDL_GetTicks();
        if (gDpadUpHeld) {
            if (now - gDpadLastTick >= gDpadNextRepeatDelay) {
                *out_wheel_y = 1;
                gDpadLastTick = now;
                gDpadNextRepeatDelay = kDpadRepeatIntervalMs;
            }
        } else if (gDpadDownHeld) {
            if (now - gDpadLastTick >= gDpadNextRepeatDelay) {
                *out_wheel_y = -1;
                gDpadLastTick = now;
                gDpadNextRepeatDelay = kDpadRepeatIntervalMs;
            }
        }
    }

    // Query active controller sticks (check Left and Right stick, pick the larger deflection)
    int bestRawX = 0;
    int bestRawY = 0;
    float bestMag = 0.0f;

    for (SDL_GameController* controller : gControllers) {
        if (controller == nullptr) continue;

        // Left stick
        int lx = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTX);
        int ly = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTY);
        float lmag = std::sqrt((float)(lx * lx + ly * ly));
        if (lmag > bestMag) {
            bestMag = lmag;
            bestRawX = lx;
            bestRawY = ly;
        }

        // Right stick
        int rx = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_RIGHTX);
        int ry = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_RIGHTY);
        float rmag = std::sqrt((float)(rx * rx + ry * ry));
        if (rmag > bestMag) {
            bestMag = rmag;
            bestRawX = rx;
            bestRawY = ry;
        }
    }

    if (bestMag > (float)kAxisDeadzone && out_dx != nullptr && out_dy != nullptr) {
        float normalized = (bestMag - (float)kAxisDeadzone) / (32767.0f - (float)kAxisDeadzone);
        if (normalized > 1.0f) normalized = 1.0f;

        // Quadratic response curve for precision at low tilt and speed at high tilt
        float speed = kMinCursorSpeed + (kMaxCursorSpeed - kMinCursorSpeed) * (normalized * normalized);

        float dirX = (float)bestRawX / bestMag;
        float dirY = (float)bestRawY / bestMag;

        gSubpixelAccumX += dirX * normalized * speed;
        gSubpixelAccumY += dirY * normalized * speed;

        int stepX = (int)gSubpixelAccumX;
        int stepY = (int)gSubpixelAccumY;

        gSubpixelAccumX -= (float)stepX;
        gSubpixelAccumY -= (float)stepY;

        *out_dx = stepX;
        *out_dy = stepY;
    } else {
        // Clear subpixel residues when sticks are released
        gSubpixelAccumX = 0.0f;
        gSubpixelAccumY = 0.0f;
    }
}

} // namespace fallout
