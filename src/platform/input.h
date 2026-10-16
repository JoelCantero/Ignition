#ifndef IGN_PLATFORM_INPUT_H
#define IGN_PLATFORM_INPUT_H

#include <SDL3/SDL.h>
#include "../game/car.h"

#define IGN_INPUT_DEVICES 2
#define IGN_INPUT_AXES 8
#define IGN_INPUT_KEY_QUEUE 64

/* Native replacement, not an emulation of IRQ1/BIOS or the timing-sensitive DOS game port.
 * DOS set-1 identities are retained so IGN_DOS.BTZ key bindings remain usable. */
typedef struct {
    int minimum, center, maximum;
    int deadzone;
} IgnInputAxis;

typedef struct {
    SDL_JoystickID id;
    SDL_Gamepad *gamepad;
    SDL_Joystick *joystick;
    Sint16 axes[IGN_INPUT_AXES];
    Sint16 pedal_rest[2];
    Uint64 buttons, pressed;
    Uint8 hat;
    Uint8 active_axes;
    IgnInputAxis calibration[IGN_INPUT_AXES];
} IgnInputDevice;

typedef struct {
    Uint32 window;
    bool initialized, focused;
    bool keys[SDL_SCANCODE_COUNT];
    Uint8 held[256], pressed[256], repeated[256], queue[IGN_INPUT_KEY_QUEUE];
    char typed[IGN_INPUT_KEY_QUEUE];
    unsigned read, write;
    IgnInputDevice devices[IGN_INPUT_DEVICES];
} IgnPlatformInput;

typedef struct {
    Uint8 keys[8]; /* left, right, accelerator, brake, upshift, downshift, turbo, rear */
    int controls;  /* saved settings: 0 keyboard, 1 joystick, 2 pedals, 3 second joystick */
    int device;
    bool auto_accel, manual_gear, legacy_keys;
} IgnInputConfig;

bool ign_input_init(IgnPlatformInput *input, Uint32 window);
void ign_input_shutdown(IgnPlatformInput *input);
void ign_input_reset(IgnPlatformInput *input);
void ign_input_clear_edges(IgnPlatformInput *input);
/* Returns one frontend DOS key on a fresh keyboard/controller navigation edge, otherwise zero.
 * Call once for every SDL event, including hotplug and window events. */
int ign_input_event(IgnPlatformInput *input, const SDL_Event *event);
int ign_input_dos_scancode(SDL_Scancode scancode);
bool ign_input_key_down(const IgnPlatformInput *input, int dos);
bool ign_input_key_pressed(IgnPlatformInput *input, int dos);
bool ign_input_key_repeat(IgnPlatformInput *input, int dos);
bool ign_input_any_key(const IgnPlatformInput *input);
int ign_input_read_key(IgnPlatformInput *input);
/* ASCII command/cheat matcher over SDL's text stream; a match clears the bounded history. */
bool ign_input_typed(IgnPlatformInput *input, const char *word);
int ign_input_axis(int value, IgnInputAxis calibration); /* signed -100..100, invalid calibration = 0 */
bool ign_input_calibrate_axis(IgnPlatformInput *input, int device, int axis, IgnInputAxis calibration);
void ign_input_config_defaults(IgnInputConfig *config);
/* Consumes gear edges only when consume_edges is true (once per physics tick, not render frame).
 * Keyboard bindings remain a fallback when a selected device is absent. */
IgnInput ign_input_sample(IgnPlatformInput *input, const IgnInputConfig *config, bool consume_edges,
                          bool *rear);

#endif
