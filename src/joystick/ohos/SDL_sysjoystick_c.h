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

#ifndef SDL_sysjoystick_c_h_
#define SDL_sysjoystick_c_h_
// 目前 OHOS 未开发 input EVENT接口
#include "../SDL_sysjoystick.h"

extern int OHOS_OnPadDown(int device_id, int keycode);
extern int OHOS_OnPadUp(int device_id, int keycode);
extern int OHOS_OnJoy(int device_id, int axisnum, float value);
extern int OHOS_OnHat(int device_id, int hat_id, int x, int y);
extern int OHOS_AddJoystick(int device_id, const char *name, const char *desc, int vendor_id, int product_id, SDL_bool is_accelerometer, int button_mask, int naxes, int axis_mask, int nhats);
extern int OHOS_RemoveJoystick(int device_id);

/* A linked list of available joysticks */
typedef struct SDL_joylist_item
{
    int device_instance;
    int device_id; /* OHOS's device id */
    char *name;    /* "SideWinder 3D Pro" or whatever */
    SDL_JoystickGUID guid;
    SDL_bool is_accelerometer;
    SDL_Joystick *joystick;
    int nbuttons, naxes, nhats;
    int dpad_state;

    struct SDL_joylist_item *next;
} SDL_joylist_item;

typedef SDL_joylist_item joystick_hwdata;

#endif /* SDL_sysjoystick_c_h_ */

#endif /* SDL_JOYSTICK_OHOS */
