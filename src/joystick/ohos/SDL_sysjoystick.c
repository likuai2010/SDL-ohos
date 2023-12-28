/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2023 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/
#include "SDL_internal.h"

#ifdef SDL_JOYSTICK_OHOS

#include <stdio.h> /* For the definition of NULL */

#include "SDL_sysjoystick_c.h"
#include "../SDL_joystick_c.h"
#include "../../events/SDL_keyboard_c.h"
//#include "../../core/android/SDL_android.h"
#include "../hidapi/SDL_hidapijoystick_c.h"


/* As of platform android-14, android/keycodes.h is missing these defines */
#ifndef AKEYCODE_BUTTON_1
#define AKEYCODE_BUTTON_1  188
#define AKEYCODE_BUTTON_2  189
#define AKEYCODE_BUTTON_3  190
#define AKEYCODE_BUTTON_4  191
#define AKEYCODE_BUTTON_5  192
#define AKEYCODE_BUTTON_6  193
#define AKEYCODE_BUTTON_7  194
#define AKEYCODE_BUTTON_8  195
#define AKEYCODE_BUTTON_9  196
#define AKEYCODE_BUTTON_10 197
#define AKEYCODE_BUTTON_11 198
#define AKEYCODE_BUTTON_12 199
#define AKEYCODE_BUTTON_13 200
#define AKEYCODE_BUTTON_14 201
#define AKEYCODE_BUTTON_15 202
#define AKEYCODE_BUTTON_16 203
#endif

#define OHOS_ACCELEROMETER_NAME      "Android Accelerometer"
#define OHOS_ACCELEROMETER_DEVICE_ID 0 // fix INT_MIN
#define OHOS_MAX_NBUTTONS            36

static SDL_joylist_item *JoystickByDeviceId(int device_id);

static SDL_joylist_item *SDL_joylist = NULL;
static SDL_joylist_item *SDL_joylist_tail = NULL;
static int numjoysticks = 0;

/* Function to convert Android keyCodes into SDL ones.
 * This code manipulation is done to get a sequential list of codes.
 * FIXME: This is only suited for the case where we use a fixed number of buttons determined by ANDROID_MAX_NBUTTONS
 */

static int keycode_to_SDL(int keycode)
{
    /* FIXME: If this function gets too unwieldy in the future, replace with a lookup table */
    int button = 0;
    /* This is here in case future generations, probably with six fingers per hand,
     * happily add new cases up above and forget to update the max number of buttons.
     */
    SDL_assert(button < OHOS_MAX_NBUTTONS);
    return button;
}

static SDL_Scancode button_to_scancode(int button)
{
    switch (button) {
    case SDL_GAMEPAD_BUTTON_SOUTH:
        return SDL_SCANCODE_RETURN;
    case SDL_GAMEPAD_BUTTON_EAST:
        return SDL_SCANCODE_ESCAPE;
    case SDL_GAMEPAD_BUTTON_BACK:
        return SDL_SCANCODE_ESCAPE;
    case SDL_GAMEPAD_BUTTON_START:
        return SDL_SCANCODE_MENU;
    case SDL_GAMEPAD_BUTTON_DPAD_UP:
        return SDL_SCANCODE_UP;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
        return SDL_SCANCODE_DOWN;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
        return SDL_SCANCODE_LEFT;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
        return SDL_SCANCODE_RIGHT;
    }

    /* Unsupported button */
    return SDL_SCANCODE_UNKNOWN;
}

int OHOS_OnPadDown(int device_id, int keycode)
{
    Uint64 timestamp = SDL_GetTicksNS();
    SDL_joylist_item *item;
    int button = keycode_to_SDL(keycode);
    if (button >= 0) {
        SDL_LockJoysticks();
        item = JoystickByDeviceId(device_id);
        if (item && item->joystick) {
            SDL_SendJoystickButton(timestamp, item->joystick, button, SDL_PRESSED);
        } else {
            SDL_SendKeyboardKey(timestamp, SDL_PRESSED, button_to_scancode(button));
        }
        SDL_UnlockJoysticks();
        return 0;
    }

    return -1;
}

int OHOS_OnPadUp(int device_id, int keycode)
{
    Uint64 timestamp = SDL_GetTicksNS();
    SDL_joylist_item *item;
    int button = keycode_to_SDL(keycode);
    if (button >= 0) {
        SDL_LockJoysticks();
        item = JoystickByDeviceId(device_id);
        if (item && item->joystick) {
            SDL_SendJoystickButton(timestamp, item->joystick, button, SDL_RELEASED);
        } else {
            SDL_SendKeyboardKey(timestamp, SDL_RELEASED, button_to_scancode(button));
        }
        SDL_UnlockJoysticks();
        return 0;
    }

    return -1;
}

int OHOS_OnJoy(int device_id, int axis, float value)
{
    Uint64 timestamp = SDL_GetTicksNS();
    /* Android gives joy info normalized as [-1.0, 1.0] or [0.0, 1.0] */
    SDL_joylist_item *item;

    SDL_LockJoysticks();
    item = JoystickByDeviceId(device_id);
    if (item && item->joystick) {
        SDL_SendJoystickAxis(timestamp, item->joystick, axis, (Sint16)(32767. * value));
    }
    SDL_UnlockJoysticks();

    return 0;
}

int OHOS_OnHat(int device_id, int hat_id, int x, int y)
{
    Uint64 timestamp = SDL_GetTicksNS();
    const int DPAD_UP_MASK = (1 << SDL_GAMEPAD_BUTTON_DPAD_UP);
    const int DPAD_DOWN_MASK = (1 << SDL_GAMEPAD_BUTTON_DPAD_DOWN);
    const int DPAD_LEFT_MASK = (1 << SDL_GAMEPAD_BUTTON_DPAD_LEFT);
    const int DPAD_RIGHT_MASK = (1 << SDL_GAMEPAD_BUTTON_DPAD_RIGHT);

    if (x >= -1 && x <= 1 && y >= -1 && y <= 1) {
        SDL_joylist_item *item;

        SDL_LockJoysticks();
        item = JoystickByDeviceId(device_id);
        if (item && item->joystick) {
            int dpad_state = 0;
            int dpad_delta;
            if (x < 0) {
                dpad_state |= DPAD_LEFT_MASK;
            } else if (x > 0) {
                dpad_state |= DPAD_RIGHT_MASK;
            }
            if (y < 0) {
                dpad_state |= DPAD_UP_MASK;
            } else if (y > 0) {
                dpad_state |= DPAD_DOWN_MASK;
            }

            dpad_delta = (dpad_state ^ item->dpad_state);
            if (dpad_delta) {
                if (dpad_delta & DPAD_UP_MASK) {
                    SDL_SendJoystickButton(timestamp, item->joystick, SDL_GAMEPAD_BUTTON_DPAD_UP, (dpad_state & DPAD_UP_MASK) ? SDL_PRESSED : SDL_RELEASED);
                }
                if (dpad_delta & DPAD_DOWN_MASK) {
                    SDL_SendJoystickButton(timestamp, item->joystick, SDL_GAMEPAD_BUTTON_DPAD_DOWN, (dpad_state & DPAD_DOWN_MASK) ? SDL_PRESSED : SDL_RELEASED);
                }
                if (dpad_delta & DPAD_LEFT_MASK) {
                    SDL_SendJoystickButton(timestamp, item->joystick, SDL_GAMEPAD_BUTTON_DPAD_LEFT, (dpad_state & DPAD_LEFT_MASK) ? SDL_PRESSED : SDL_RELEASED);
                }
                if (dpad_delta & DPAD_RIGHT_MASK) {
                    SDL_SendJoystickButton(timestamp, item->joystick, SDL_GAMEPAD_BUTTON_DPAD_RIGHT, (dpad_state & DPAD_RIGHT_MASK) ? SDL_PRESSED : SDL_RELEASED);
                }
                item->dpad_state = dpad_state;
            }
        }
        SDL_UnlockJoysticks();
        return 0;
    }

    return -1;
}

int OHOS_AddJoystick(int device_id, const char *name, const char *desc, int vendor_id, int product_id, SDL_bool is_accelerometer, int button_mask, int naxes, int axis_mask, int nhats)
{
    SDL_joylist_item *item;
    SDL_JoystickGUID guid;
    int i;
    int result = -1;

    SDL_LockJoysticks();

    if (!SDL_GetHintBoolean(SDL_HINT_TV_REMOTE_AS_JOYSTICK, SDL_TRUE)) {
        /* Ignore devices that aren't actually controllers (e.g. remotes), they'll be handled as keyboard input */
        if (naxes < 2 && nhats < 1) {
            goto done;
        }
    }

    if (JoystickByDeviceId(device_id) != NULL || !name) {
        goto done;
    }

#ifdef SDL_JOYSTICK_HIDAPI
    if (HIDAPI_IsDevicePresent(vendor_id, product_id, 0, name)) {
        /* The HIDAPI driver is taking care of this device */
        goto done;
    }
#endif

#ifdef DEBUG_JOYSTICK
    SDL_Log("Joystick: %s, descriptor %s, vendor = 0x%.4x, product = 0x%.4x, %d axes, %d hats\n", name, desc, vendor_id, product_id, naxes, nhats);
#endif

    if (nhats > 0) {
        /* Hat is translated into DPAD buttons */
        button_mask |= ((1 << SDL_GAMEPAD_BUTTON_DPAD_UP) |
                        (1 << SDL_GAMEPAD_BUTTON_DPAD_DOWN) |
                        (1 << SDL_GAMEPAD_BUTTON_DPAD_LEFT) |
                        (1 << SDL_GAMEPAD_BUTTON_DPAD_RIGHT));
        nhats = 0;
    }

    guid = SDL_CreateJoystickGUID(SDL_HARDWARE_BUS_BLUETOOTH, vendor_id, product_id, 0, NULL, desc, 0, 0);

    /* Update the GUID with capability bits */
    {
        Uint16 *guid16 = (Uint16 *)guid.data;
        guid16[6] = SDL_SwapLE16(button_mask);
        guid16[7] = SDL_SwapLE16(axis_mask);
    }

    item = (SDL_joylist_item *)SDL_malloc(sizeof(SDL_joylist_item));
    if (!item) {
        goto done;
    }

    SDL_zerop(item);
    item->guid = guid;
    item->device_id = device_id;
    item->name = SDL_CreateJoystickName(vendor_id, product_id, NULL, name);
    if (!item->name) {
        SDL_free(item);
        goto done;
    }

    item->is_accelerometer = is_accelerometer;
    if (button_mask == 0xFFFFFFFF) {
        item->nbuttons = OHOS_MAX_NBUTTONS;
    } else {
        for (i = 0; i < sizeof(button_mask) * 8; ++i) {
            if (button_mask & (1 << i)) {
                item->nbuttons = i + 1;
            }
        }
    }
    item->naxes = naxes;
    item->nhats = nhats;
    item->device_instance = SDL_GetNextObjectID();
    if (!SDL_joylist_tail) {
        SDL_joylist = SDL_joylist_tail = item;
    } else {
        SDL_joylist_tail->next = item;
        SDL_joylist_tail = item;
    }

    /* Need to increment the joystick count before we post the event */
    ++numjoysticks;

    SDL_PrivateJoystickAdded(item->device_instance);

    result = numjoysticks;

#ifdef DEBUG_JOYSTICK
    SDL_Log("Added joystick %s with device_id %d", item->name, device_id);
#endif

done:
    SDL_UnlockJoysticks();

    return result;
}

int OHOS_RemoveJoystick(int device_id)
{
    SDL_joylist_item *item = SDL_joylist;
    SDL_joylist_item *prev = NULL;
    int result = -1;

    SDL_LockJoysticks();

    /* Don't call JoystickByDeviceId here or there'll be an infinite loop! */
    while (item) {
        if (item->device_id == device_id) {
            break;
        }
        prev = item;
        item = item->next;
    }

    if (!item) {
        goto done;
    }

    if (item->joystick) {
        item->joystick->hwdata = NULL;
    }

    if (prev) {
        prev->next = item->next;
    } else {
        SDL_assert(SDL_joylist == item);
        SDL_joylist = item->next;
    }
    if (item == SDL_joylist_tail) {
        SDL_joylist_tail = prev;
    }

    /* Need to decrement the joystick count before we post the event */
    --numjoysticks;

    SDL_PrivateJoystickRemoved(item->device_instance);

    result = numjoysticks;

#ifdef DEBUG_JOYSTICK
    SDL_Log("Removed joystick with device_id %d", device_id);
#endif

    SDL_free(item->name);
    SDL_free(item);

done:
    SDL_UnlockJoysticks();

    return result;
}

static void OHOS_JoystickDetect(void);

static int OHOS_JoystickInit(void)
{
    //ANDROID_JoystickDetect();

    if (SDL_GetHintBoolean(SDL_HINT_ACCELEROMETER_AS_JOYSTICK, SDL_TRUE)) {
        /* Default behavior, accelerometer as joystick */
        OHOS_AddJoystick(OHOS_ACCELEROMETER_DEVICE_ID, OHOS_ACCELEROMETER_NAME, OHOS_ACCELEROMETER_NAME, 0, 0, SDL_TRUE, 0, 3, 0x0003, 0);
    }
    return 0;
}

static int OHOS_JoystickGetCount(void)
{
    return numjoysticks;
}

static void OHOS_JoystickDetect(void)
{
    /* Support for device connect/disconnect is API >= 16 only,
     * so we poll every three seconds
     * Ref: http://developer.android.com/reference/android/hardware/input/InputManager.InputDeviceListener.html
     */
    static Uint64 timeout = 0;
    Uint64 now = SDL_GetTicks();
    if (!timeout || now >= timeout) {
        timeout = now + 3000;
        // TODO fix
        //Android_JNI_PollInputDevices();
    }
}

static SDL_joylist_item *GetJoystickByDevIndex(int device_index)
{
    SDL_joylist_item *item = SDL_joylist;

    if ((device_index < 0) || (device_index >= numjoysticks)) {
        return NULL;
    }

    while (device_index > 0) {
        SDL_assert(item != NULL);
        device_index--;
        item = item->next;
    }

    return item;
}

static SDL_joylist_item *JoystickByDeviceId(int device_id)
{
    SDL_joylist_item *item = SDL_joylist;

    while (item) {
        if (item->device_id == device_id) {
            return item;
        }
        item = item->next;
    }

    /* Joystick not found, try adding it */
   // ANDROID_JoystickDetect();

    while (item) {
        if (item->device_id == device_id) {
            return item;
        }
        item = item->next;
    }

    return NULL;
}

static const char *OHOS_JoystickGetDeviceName(int device_index)
{
    return GetJoystickByDevIndex(device_index)->name;
}

static const char *OHOS_JoystickGetDevicePath(int device_index)
{
    return NULL;
}

static int OHOS_JoystickGetDeviceSteamVirtualGamepadSlot(int device_index)
{
    return -1;
}

static int OHOS_JoystickGetDevicePlayerIndex(int device_index)
{
    return -1;
}

static void OHOS_JoystickSetDevicePlayerIndex(int device_index, int player_index)
{
}

static SDL_JoystickGUID OHOS_JoystickGetDeviceGUID(int device_index)
{
    return GetJoystickByDevIndex(device_index)->guid;
}

static SDL_JoystickID OHOS_JoystickGetDeviceInstanceID(int device_index)
{
    return GetJoystickByDevIndex(device_index)->device_instance;
}

static int OHOS_JoystickOpen(SDL_Joystick *joystick, int device_index)
{
    SDL_joylist_item *item = GetJoystickByDevIndex(device_index);

    if (!item) {
        return SDL_SetError("No such device");
    }

    if (item->joystick) {
        return SDL_SetError("Joystick already opened");
    }

    joystick->instance_id = item->device_instance;
    joystick->hwdata = (struct joystick_hwdata *)item;
    item->joystick = joystick;
    joystick->nhats = item->nhats;
    joystick->nbuttons = item->nbuttons;
    joystick->naxes = item->naxes;

    return 0;
}

static int OHOS_JoystickRumble(SDL_Joystick *joystick, Uint16 low_frequency_rumble, Uint16 high_frequency_rumble)
{
    return SDL_Unsupported();
}

static int OHOS_JoystickRumbleTriggers(SDL_Joystick *joystick, Uint16 left_rumble, Uint16 right_rumble)
{
    return SDL_Unsupported();
}

static Uint32 OHOS_JoystickGetCapabilities(SDL_Joystick *joystick)
{
    return 0;
}

static int OHOS_JoystickSetLED(SDL_Joystick *joystick, Uint8 red, Uint8 green, Uint8 blue)
{
    return SDL_Unsupported();
}

static int OHOS_JoystickSendEffect(SDL_Joystick *joystick, const void *data, int size)
{
    return SDL_Unsupported();
}

static int OHOS_JoystickSetSensorsEnabled(SDL_Joystick *joystick, SDL_bool enabled)
{
    return SDL_Unsupported();
}

static void OHOS_JoystickUpdate(SDL_Joystick *joystick)
{
    SDL_joylist_item *item = (SDL_joylist_item *)joystick->hwdata;

    if (!item) {
        return;
    }

    if (item->is_accelerometer) {
        int i;
        Sint16 value;
        float values[3];
        Uint64 timestamp = SDL_GetTicksNS();
        //  TODO fix
//        if (Android_JNI_GetAccelerometerValues(values)) {
//            for (i = 0; i < 3; i++) {
//                if (values[i] > 1.0f) {
//                    values[i] = 1.0f;
//                } else if (values[i] < -1.0f) {
//                    values[i] = -1.0f;
//                }
//
//                value = (Sint16)(values[i] * 32767.0f);
//                SDL_SendJoystickAxis(timestamp, item->joystick, i, value);
//            }
//        }
    }
}

static void OHOS_JoystickClose(SDL_Joystick *joystick)
{
    SDL_joylist_item *item = (SDL_joylist_item *)joystick->hwdata;
    if (item) {
        item->joystick = NULL;
    }
}

static void OHOS_JoystickQuit(void)
{
/* We don't have any way to scan for joysticks at init, so don't wipe the list
 * of joysticks here in case this is a reinit.
 */
#if 0
    SDL_joylist_item *item = NULL;
    SDL_joylist_item *next = NULL;

    for (item = SDL_joylist; item; item = next) {
        next = item->next;
        SDL_free(item->name);
        SDL_free(item);
    }

    SDL_joylist = SDL_joylist_tail = NULL;

    numjoysticks = 0;
#endif /* 0 */
}

static SDL_bool OHOS_JoystickGetGamepadMapping(int device_index, SDL_GamepadMapping *out)
{
    return SDL_FALSE;
}

SDL_JoystickDriver SDL_OHOS_JoystickDriver = {
    OHOS_JoystickInit,
    OHOS_JoystickGetCount,
    OHOS_JoystickDetect,
    OHOS_JoystickGetDeviceName,
    OHOS_JoystickGetDevicePath,
    OHOS_JoystickGetDeviceSteamVirtualGamepadSlot,
    OHOS_JoystickGetDevicePlayerIndex,
    OHOS_JoystickSetDevicePlayerIndex,
    OHOS_JoystickGetDeviceGUID,
    OHOS_JoystickGetDeviceInstanceID,
    OHOS_JoystickOpen,
    OHOS_JoystickRumble,
    OHOS_JoystickRumbleTriggers,
    OHOS_JoystickGetCapabilities,
    OHOS_JoystickSetLED,
    OHOS_JoystickSendEffect,
    OHOS_JoystickSetSensorsEnabled,
    OHOS_JoystickUpdate,
    OHOS_JoystickClose,
    OHOS_JoystickQuit,
    OHOS_JoystickGetGamepadMapping
};

#endif /* SDL_JOYSTICK_ANDROID */
