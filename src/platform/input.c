#include "input.h"

#include <string.h>

/* 0x55e04: extended set-1 keys occupy the upper half of the original 256-byte state table.
 * PrintScreen/Pause are normalized SDL keys, not reproduced multi-byte IRQ sequences. */
int ign_input_dos_scancode(SDL_Scancode sc) {
    static const struct { SDL_Scancode sdl; Uint8 dos; } map[] = {
        {SDL_SCANCODE_ESCAPE, 0x01}, {SDL_SCANCODE_1, 0x02}, {SDL_SCANCODE_2, 0x03}, {SDL_SCANCODE_3, 0x04},
        {SDL_SCANCODE_4, 0x05}, {SDL_SCANCODE_5, 0x06}, {SDL_SCANCODE_6, 0x07}, {SDL_SCANCODE_7, 0x08},
        {SDL_SCANCODE_8, 0x09}, {SDL_SCANCODE_9, 0x0a}, {SDL_SCANCODE_0, 0x0b}, {SDL_SCANCODE_MINUS, 0x0c},
        {SDL_SCANCODE_EQUALS, 0x0d}, {SDL_SCANCODE_BACKSPACE, 0x0e}, {SDL_SCANCODE_TAB, 0x0f},
        {SDL_SCANCODE_Q, 0x10}, {SDL_SCANCODE_W, 0x11}, {SDL_SCANCODE_E, 0x12}, {SDL_SCANCODE_R, 0x13},
        {SDL_SCANCODE_T, 0x14}, {SDL_SCANCODE_Y, 0x15}, {SDL_SCANCODE_U, 0x16}, {SDL_SCANCODE_I, 0x17},
        {SDL_SCANCODE_O, 0x18}, {SDL_SCANCODE_P, 0x19}, {SDL_SCANCODE_LEFTBRACKET, 0x1a},
        {SDL_SCANCODE_RIGHTBRACKET, 0x1b}, {SDL_SCANCODE_RETURN, 0x1c}, {SDL_SCANCODE_LCTRL, 0x1d},
        {SDL_SCANCODE_A, 0x1e}, {SDL_SCANCODE_S, 0x1f}, {SDL_SCANCODE_D, 0x20}, {SDL_SCANCODE_F, 0x21},
        {SDL_SCANCODE_G, 0x22}, {SDL_SCANCODE_H, 0x23}, {SDL_SCANCODE_J, 0x24}, {SDL_SCANCODE_K, 0x25},
        {SDL_SCANCODE_L, 0x26}, {SDL_SCANCODE_SEMICOLON, 0x27}, {SDL_SCANCODE_APOSTROPHE, 0x28},
        {SDL_SCANCODE_GRAVE, 0x29}, {SDL_SCANCODE_LSHIFT, 0x2a}, {SDL_SCANCODE_BACKSLASH, 0x2b},
        {SDL_SCANCODE_Z, 0x2c}, {SDL_SCANCODE_X, 0x2d}, {SDL_SCANCODE_C, 0x2e}, {SDL_SCANCODE_V, 0x2f},
        {SDL_SCANCODE_B, 0x30}, {SDL_SCANCODE_N, 0x31}, {SDL_SCANCODE_M, 0x32}, {SDL_SCANCODE_COMMA, 0x33},
        {SDL_SCANCODE_PERIOD, 0x34}, {SDL_SCANCODE_SLASH, 0x35}, {SDL_SCANCODE_RSHIFT, 0x36},
        {SDL_SCANCODE_KP_MULTIPLY, 0x37}, {SDL_SCANCODE_LALT, 0x38}, {SDL_SCANCODE_SPACE, 0x39},
        {SDL_SCANCODE_CAPSLOCK, 0x3a}, {SDL_SCANCODE_F1, 0x3b}, {SDL_SCANCODE_F2, 0x3c}, {SDL_SCANCODE_F3, 0x3d},
        {SDL_SCANCODE_F4, 0x3e}, {SDL_SCANCODE_F5, 0x3f}, {SDL_SCANCODE_F6, 0x40}, {SDL_SCANCODE_F7, 0x41},
        {SDL_SCANCODE_F8, 0x42}, {SDL_SCANCODE_F9, 0x43}, {SDL_SCANCODE_F10, 0x44},
        {SDL_SCANCODE_NUMLOCKCLEAR, 0x45}, {SDL_SCANCODE_SCROLLLOCK, 0x46}, {SDL_SCANCODE_KP_7, 0x47},
        {SDL_SCANCODE_KP_8, 0x48}, {SDL_SCANCODE_KP_9, 0x49}, {SDL_SCANCODE_KP_MINUS, 0x4a},
        {SDL_SCANCODE_KP_4, 0x4b}, {SDL_SCANCODE_KP_5, 0x4c}, {SDL_SCANCODE_KP_6, 0x4d},
        {SDL_SCANCODE_KP_PLUS, 0x4e}, {SDL_SCANCODE_KP_1, 0x4f}, {SDL_SCANCODE_KP_2, 0x50},
        {SDL_SCANCODE_KP_3, 0x51}, {SDL_SCANCODE_KP_0, 0x52}, {SDL_SCANCODE_KP_PERIOD, 0x53},
        {SDL_SCANCODE_NONUSBACKSLASH, 0x56}, {SDL_SCANCODE_F11, 0x57}, {SDL_SCANCODE_F12, 0x58},
        {SDL_SCANCODE_KP_ENTER, 0x9c}, {SDL_SCANCODE_RCTRL, 0x9d}, {SDL_SCANCODE_KP_DIVIDE, 0xb5},
        {SDL_SCANCODE_PRINTSCREEN, 0xb7}, {SDL_SCANCODE_RALT, 0xb8},
        {SDL_SCANCODE_HOME, 0xc7}, {SDL_SCANCODE_UP, 0xc8}, {SDL_SCANCODE_PAGEUP, 0xc9},
        {SDL_SCANCODE_LEFT, 0xcb}, {SDL_SCANCODE_RIGHT, 0xcd}, {SDL_SCANCODE_END, 0xcf},
        {SDL_SCANCODE_DOWN, 0xd0}, {SDL_SCANCODE_PAGEDOWN, 0xd1}, {SDL_SCANCODE_INSERT, 0xd2},
        {SDL_SCANCODE_DELETE, 0xd3}, {SDL_SCANCODE_PAUSE, 0xc5},
    };
    for (size_t i = 0; i < SDL_arraysize(map); i++)
        if (map[i].sdl == sc) return map[i].dos;
    return 0;
}

void ign_input_reset(IgnPlatformInput *in) {
    memset(in->keys, 0, sizeof in->keys);
    memset(in->held, 0, sizeof in->held);
    memset(in->pressed, 0, sizeof in->pressed);
    memset(in->repeated, 0, sizeof in->repeated);
    memset(in->typed, 0, sizeof in->typed);
    in->read = in->write = 0;
    for (int i = 0; i < IGN_INPUT_DEVICES; i++) {
        IgnInputDevice *d = &in->devices[i];
        for (int a = 0; a < IGN_INPUT_AXES; a++)
            d->axes[a] = (Sint16)d->calibration[a].center;
        d->buttons = d->pressed = 0;
        d->hat = 0;
        d->active_axes = 0;
    }
}

void ign_input_clear_edges(IgnPlatformInput *in) {
    memset(in->pressed, 0, sizeof in->pressed);
    for (int i = 0; i < IGN_INPUT_DEVICES; i++) in->devices[i].pressed = 0;
}

static IgnInputDevice *device(IgnPlatformInput *in, SDL_JoystickID id) {
    for (int i = 0; i < IGN_INPUT_DEVICES; i++)
        if (id && in->devices[i].id == id) return &in->devices[i];
    return NULL;
}

static void close_device(IgnInputDevice *d) {
    if (d->gamepad) SDL_CloseGamepad(d->gamepad);
    else if (d->joystick) SDL_CloseJoystick(d->joystick);
    memset(d, 0, sizeof *d);
}

static void open_device(IgnPlatformInput *in, SDL_JoystickID id) {
    if (device(in, id)) return;
    IgnInputDevice *d = NULL;
    for (int i = 0; i < IGN_INPUT_DEVICES; i++)
        if (!in->devices[i].id) { d = &in->devices[i]; break; }
    if (!d) return;
    if (SDL_IsGamepad(id)) {
        d->gamepad = SDL_OpenGamepad(id);
        if (d->gamepad) d->joystick = SDL_GetGamepadJoystick(d->gamepad);
    } else {
        d->joystick = SDL_OpenJoystick(id);
    }
    if (!d->joystick) {
        SDL_LogWarn(SDL_LOG_CATEGORY_INPUT, "Cannot open input device %u: %s", id, SDL_GetError());
        close_device(d);
        return;
    }
    d->id = id;
    for (int a = 0; a < IGN_INPUT_AXES; a++) {
        d->calibration[a] = (IgnInputAxis){-32768, 0, 32767, 4096};
        if (d->gamepad && a >= SDL_GAMEPAD_AXIS_LEFT_TRIGGER)
            d->calibration[a] = (IgnInputAxis){-32768, 0, 32767, 2048};
        /* Start neutral: opening, hotplug and focus transitions never synthesize a gear edge. */
    }
    if (!d->gamepad)
        for (int a = 0; a < 2; a++)
            if (SDL_GetNumJoystickAxes(d->joystick) > a + 1)
                d->pedal_rest[a] = SDL_GetJoystickAxis(d->joystick, a + 1);
}

bool ign_input_init(IgnPlatformInput *in, Uint32 window) {
    memset(in, 0, sizeof *in);
    in->window = window;
    in->focused = true;
    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD | SDL_INIT_JOYSTICK)) return false;
    in->initialized = true;
    int count = 0;
    SDL_JoystickID *ids = SDL_GetJoysticks(&count);
    if (!ids) {
        ign_input_shutdown(in);
        return false;
    }
    for (int i = 0; i < count; i++) open_device(in, ids[i]);
    SDL_free(ids);
    return true;
}

void ign_input_shutdown(IgnPlatformInput *in) {
    for (int i = 0; i < IGN_INPUT_DEVICES; i++) close_device(&in->devices[i]);
    if (in->initialized) SDL_QuitSubSystem(SDL_INIT_GAMEPAD | SDL_INIT_JOYSTICK);
    memset(in, 0, sizeof *in);
}

static int enqueue(IgnPlatformInput *in, int key) {
    if (!key) return 0;
    /* Bounded FIFO; discard the oldest key if a stalled frontend cannot drain it. */
    if (in->write - in->read == IGN_INPUT_KEY_QUEUE) in->read++;
    in->queue[in->write++ % IGN_INPUT_KEY_QUEUE] = (Uint8)key;
    return key;
}

bool ign_input_key_down(const IgnPlatformInput *in, int dos) {
    return dos > 0 && dos < 256 && in->held[dos] != 0;
}

bool ign_input_key_pressed(IgnPlatformInput *in, int dos) {
    if (dos <= 0 || dos >= 256) return false;
    bool down = in->pressed[dos] != 0;
    in->pressed[dos] = 0;
    return down;
}

bool ign_input_key_repeat(IgnPlatformInput *in, int dos) {
    if (dos <= 0 || dos >= 256) return false;
    bool repeat = in->repeated[dos] != 0;
    in->repeated[dos] = 0;
    return repeat;
}

bool ign_input_any_key(const IgnPlatformInput *in) {
    for (int i = 1; i < 256; i++) if (in->held[i]) return true;
    return false;
}

bool ign_input_typed(IgnPlatformInput *in, const char *word) {
    if (!word || !*word || !strstr(in->typed, word)) return false;
    memset(in->typed, 0, sizeof in->typed);
    return true;
}

int ign_input_read_key(IgnPlatformInput *in) {
    if (in->read == in->write) return 0;
    return in->queue[in->read++ % IGN_INPUT_KEY_QUEUE];
}

static int navigation_button(bool gamepad, int button) {
    if (!gamepad) return button == 0 ? 0x1c : button == 1 ? 1 : 0;
    switch (button) {
    case SDL_GAMEPAD_BUTTON_SOUTH: case SDL_GAMEPAD_BUTTON_START: return 0x1c;
    case SDL_GAMEPAD_BUTTON_EAST: case SDL_GAMEPAD_BUTTON_BACK: return 1;
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return 0xc8;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return 0xd0;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return 0xcb;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return 0xcd;
    default: return 0;
    }
}

int ign_input_event(IgnPlatformInput *in, const SDL_Event *e) {
    if (e->type == SDL_EVENT_JOYSTICK_ADDED || e->type == SDL_EVENT_GAMEPAD_ADDED) {
        open_device(in, e->jdevice.which);
        return 0;
    }
    if (e->type == SDL_EVENT_JOYSTICK_REMOVED || e->type == SDL_EVENT_GAMEPAD_REMOVED) {
        IgnInputDevice *d = device(in, e->jdevice.which);
        if (d) close_device(d);
        return 0;
    }
    if (e->type == SDL_EVENT_GAMEPAD_REMAPPED) {
        IgnInputDevice *d = device(in, e->gdevice.which);
        if (d) close_device(d);
        open_device(in, e->gdevice.which);
        return 0;
    }
    if (e->type == SDL_EVENT_WINDOW_FOCUS_LOST || e->type == SDL_EVENT_WINDOW_FOCUS_GAINED) {
        if (in->window && e->window.windowID != in->window) return 0;
        ign_input_reset(in);
        in->focused = e->type == SDL_EVENT_WINDOW_FOCUS_GAINED;
        return 0;
    }
    if (!in->focused) return 0;
    if (e->type == SDL_EVENT_TEXT_INPUT) {
        if (in->window && e->text.windowID && e->text.windowID != in->window) return 0;
        for (const unsigned char *p = (const unsigned char *)e->text.text; p && *p; p++) {
            size_t length = strlen(in->typed);
            if (length == sizeof in->typed - 1) {
                memmove(in->typed, in->typed + 1, length);
                length--;
            }
            in->typed[length] = *p < 128 ? (char)SDL_toupper(*p) : (char)*p;
            in->typed[length + 1] = 0;
        }
        return 0;
    }
    if (e->type == SDL_EVENT_KEY_DOWN || e->type == SDL_EVENT_KEY_UP) {
        if (in->window && e->key.windowID && e->key.windowID != in->window) return 0;
        int sc = e->key.scancode, key = ign_input_dos_scancode((SDL_Scancode)sc);
        if (sc <= SDL_SCANCODE_UNKNOWN || sc >= SDL_SCANCODE_COUNT) return 0;
        bool down = e->type == SDL_EVENT_KEY_DOWN;
        bool fresh = down && !e->key.repeat && !in->keys[sc];
        in->keys[sc] = down;
        if (key) {
            in->held[key] = down;
            if (fresh) in->pressed[key] = 1;
            if (fresh || (down && e->key.repeat)) in->repeated[key] = 1;
        }
        return fresh || (down && e->key.repeat) ? enqueue(in, key) : 0;
    }
    bool pad = e->type == SDL_EVENT_GAMEPAD_AXIS_MOTION ||
               e->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN || e->type == SDL_EVENT_GAMEPAD_BUTTON_UP;
    bool raw = e->type == SDL_EVENT_JOYSTICK_AXIS_MOTION || e->type == SDL_EVENT_JOYSTICK_HAT_MOTION ||
               e->type == SDL_EVENT_JOYSTICK_BUTTON_DOWN || e->type == SDL_EVENT_JOYSTICK_BUTTON_UP;
    if (!pad && !raw) return 0;
    IgnInputDevice *d = device(in, e->jaxis.which);
    if (!d || pad != (d->gamepad != NULL)) return 0; /* SDL sends both streams for mapped devices. */
    if (e->type == SDL_EVENT_GAMEPAD_AXIS_MOTION || e->type == SDL_EVENT_JOYSTICK_AXIS_MOTION) {
        int a = e->jaxis.axis;
        if (a >= IGN_INPUT_AXES) return 0;
        int before = ign_input_axis(d->axes[a], d->calibration[a]);
        d->axes[a] = e->jaxis.value;
        d->active_axes |= (Uint8)(1u << a);
        int after = ign_input_axis(d->axes[a], d->calibration[a]);
        /* Navigation uses a wider threshold than steering; neutral crossing rearms it. */
        if (a <= 1 && (after > 50 || after < -50) && before <= 50 && before >= -50)
            return enqueue(in, a == 0 ? (after < 0 ? 0xcb : 0xcd) : (after < 0 ? 0xc8 : 0xd0));
    } else if (e->type == SDL_EVENT_JOYSTICK_HAT_MOTION) {
        if (e->jhat.hat) return 0;
        Uint8 fresh = e->jhat.value & ~d->hat;
        d->hat = e->jhat.value;
        return enqueue(in, fresh & SDL_HAT_UP ? 0xc8 : fresh & SDL_HAT_DOWN ? 0xd0 :
                           fresh & SDL_HAT_LEFT ? 0xcb : fresh & SDL_HAT_RIGHT ? 0xcd : 0);
    } else {
        int b = e->jbutton.button;
        if (b >= 64) return 0;
        Uint64 bit = (Uint64)1 << b;
        bool down = e->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN || e->type == SDL_EVENT_JOYSTICK_BUTTON_DOWN;
        bool fresh = down && !(d->buttons & bit);
        if (down) d->buttons |= bit;
        else d->buttons &= ~bit;
        if (fresh) {
            d->pressed |= bit;
            return enqueue(in, navigation_button(pad, b));
        }
    }
    return 0;
}

int ign_input_axis(int value, IgnInputAxis c) {
    if (c.minimum < -32768 || c.maximum > 32767 || c.minimum >= c.maximum ||
        c.minimum > c.center || c.center > c.maximum || c.deadzone < 0 ||
        c.deadzone >= c.maximum - c.minimum) return 0;
    if (value < c.minimum) value = c.minimum;
    if (value > c.maximum) value = c.maximum;
    int delta = value - c.center;
    if (delta > c.deadzone) return (delta - c.deadzone) * 100 / (c.maximum - c.center - c.deadzone);
    if (delta < -c.deadzone) return (delta + c.deadzone) * 100 / (c.center - c.minimum - c.deadzone);
    return 0;
}

bool ign_input_calibrate_axis(IgnPlatformInput *in, int slot, int axis, IgnInputAxis c) {
    if (slot < 0 || slot >= IGN_INPUT_DEVICES || !in->devices[slot].id ||
        axis < 0 || axis >= IGN_INPUT_AXES || c.minimum < -32768 || c.maximum > 32767 ||
        c.minimum >= c.maximum || c.minimum > c.center || c.center > c.maximum ||
        c.deadzone < 0 || c.deadzone >= c.maximum - c.minimum) return false;
    in->devices[slot].calibration[axis] = c;
    in->devices[slot].axes[axis] = (Sint16)c.center;
    if (axis == 1 || axis == 2) in->devices[slot].pedal_rest[axis - 1] = (Sint16)c.center;
    return true;
}

void ign_input_config_defaults(IgnInputConfig *c) {
    *c = (IgnInputConfig){.keys = {0xcb, 0xcd, 0xc8, 0xd0, 0x35, 0x34, 0x36, 0x28},
                          .legacy_keys = true};
}

static bool button(Uint64 buttons, int index) { return (buttons & ((Uint64)1 << index)) != 0; }

IgnInput ign_input_sample(IgnPlatformInput *in, const IgnInputConfig *c, bool consume, bool *rear) {
    IgnInput out = {.manual_gear = c->manual_gear};
    *rear = false;
    if (!in->focused) return out;
    bool k[8], edge[8];
    static const Uint8 legacy[8] = {0x1e, 0x20, 0x11, 0x1f, 0x12, 0x10, 0x39, 0x13};
    for (int i = 0; i < 8; i++) {
        k[i] = ign_input_key_down(in, c->keys[i]);
        edge[i] = c->keys[i] && in->pressed[c->keys[i]];
        if (c->legacy_keys) {
            k[i] |= ign_input_key_down(in, legacy[i]);
            edge[i] |= in->pressed[legacy[i]] != 0;
        }
    }
    out.left = k[0]; out.right = k[1];
    out.accel = k[2];
    out.brake = k[3]; out.turbo = k[6]; *rear = k[7];
    out.gear_shift = (int)edge[4] - (int)edge[5];
    int slot = c->device;
    if (c->controls == 3) slot = 1;
    IgnInputDevice *d = slot >= 0 && slot < IGN_INPUT_DEVICES ? &in->devices[slot] : NULL;
    if (c->controls && d && d->id) {
        out.analog = true;
        int steer = ign_input_axis(d->axes[0], d->calibration[0]);
        int accel = 0, brake = 0;
        if (d->gamepad) {
            accel = ign_input_axis(d->axes[SDL_GAMEPAD_AXIS_RIGHT_TRIGGER],
                                   d->calibration[SDL_GAMEPAD_AXIS_RIGHT_TRIGGER]);
            brake = ign_input_axis(d->axes[SDL_GAMEPAD_AXIS_LEFT_TRIGGER],
                                   d->calibration[SDL_GAMEPAD_AXIS_LEFT_TRIGGER]);
            if (accel < 0) accel = 0;
            if (brake < 0) brake = 0;
            if (button(d->buttons, SDL_GAMEPAD_BUTTON_SOUTH)) accel = 100;
            if (button(d->buttons, SDL_GAMEPAD_BUTTON_EAST)) brake = 100;
            if (button(d->buttons, SDL_GAMEPAD_BUTTON_DPAD_LEFT)) steer = -100;
            if (button(d->buttons, SDL_GAMEPAD_BUTTON_DPAD_RIGHT))
                steer = steer == -100 ? 0 : 100;
            out.turbo |= button(d->buttons, SDL_GAMEPAD_BUTTON_WEST);
            *rear |= button(d->buttons, SDL_GAMEPAD_BUTTON_NORTH);
            out.gear_shift += (int)button(d->pressed, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER) -
                              (int)button(d->pressed, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
        } else {
            int pedal = ign_input_axis(d->axes[1], d->calibration[1]);
            accel = pedal < 0 ? -pedal : 0;
            brake = pedal > 0 ? pedal : 0;
            /* PEDALS uses independent axes with their opening position as the released position.
             * SDL mapping databases provide the authoritative polarity for mapped gamepads instead. */
            if (c->controls == 2) {
                IgnInputAxis a = d->calibration[1], b = d->calibration[2];
                a.center = d->pedal_rest[0]; b.center = d->pedal_rest[1];
                pedal = d->active_axes & 2 ? ign_input_axis(d->axes[1], a) : 0;
                int other = d->active_axes & 4 ? ign_input_axis(d->axes[2], b) : 0;
                brake = other > 0 ? other : -other;
                accel = pedal > 0 ? pedal : -pedal;
            }
            if (d->hat & SDL_HAT_LEFT) steer = -100;
            if (d->hat & SDL_HAT_RIGHT) steer = steer == -100 ? 0 : 100;
            if (button(d->buttons, 0)) accel = 100;
            if (button(d->buttons, 1)) brake = 100;
            out.turbo |= button(d->buttons, 2);
            *rear |= button(d->buttons, 3);
            out.gear_shift += (int)button(d->pressed, 5) - (int)button(d->pressed, 4);
        }
        out.a_accel = out.accel || (c->auto_accel && !brake) ? 100 : accel;
        out.a_brake = out.brake ? 100 : brake;
        out.a_steer = out.left != out.right ? (out.right ? 100 : -100) : steer;
    }
    if (!out.analog && c->auto_accel && !out.brake) out.accel = true;
    if (out.gear_shift > 1) out.gear_shift = 1;
    if (out.gear_shift < -1) out.gear_shift = -1;
    if (consume) ign_input_clear_edges(in);
    return out;
}
