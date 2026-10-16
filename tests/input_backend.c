#include "platform/input.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s (%s)\n", __FILE__, __LINE__, #x, SDL_GetError()); exit(1); } } while (0)

static int key(IgnPlatformInput *in, SDL_Scancode sc, bool down, bool repeat) {
    SDL_Event e = {0};
    e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    e.key.scancode = sc;
    e.key.repeat = repeat;
    return ign_input_event(in, &e);
}

static void keyboard(void) {
    IgnPlatformInput in = {.focused = true, .window = 7};
    IgnInputConfig c;
    ign_input_config_defaults(&c);
    bool rear;
    CHECK(ign_input_dos_scancode(SDL_SCANCODE_UP) == 0xc8);
    CHECK(ign_input_dos_scancode(SDL_SCANCODE_KP_8) == 0x48);
    CHECK(ign_input_dos_scancode(SDL_SCANCODE_RCTRL) == 0x9d);
    CHECK(ign_input_dos_scancode(SDL_SCANCODE_PRINTSCREEN) == 0xb7);
    CHECK(ign_input_dos_scancode(SDL_SCANCODE_UNKNOWN) == 0);
    CHECK(key(&in, SDL_SCANCODE_UP, true, false) == 0xc8);
    CHECK(key(&in, SDL_SCANCODE_UP, true, true) == 0xc8);
    CHECK(key(&in, SDL_SCANCODE_UP, true, false) == 0);
    CHECK(ign_input_key_down(&in, 0xc8));
    CHECK(ign_input_key_pressed(&in, 0xc8));
    CHECK(!ign_input_key_pressed(&in, 0xc8));
    CHECK(ign_input_key_repeat(&in, 0xc8));
    CHECK(!ign_input_key_repeat(&in, 0xc8));
    CHECK(ign_input_any_key(&in));
    CHECK(ign_input_read_key(&in) == 0xc8);
    CHECK(ign_input_read_key(&in) == 0xc8);
    CHECK(ign_input_read_key(&in) == 0);
    CHECK(ign_input_sample(&in, &c, true, &rear).accel);
    key(&in, SDL_SCANCODE_UP, false, false);
    CHECK(!ign_input_sample(&in, &c, true, &rear).accel);
    CHECK(!ign_input_any_key(&in));
    c.legacy_keys = false;
    c.keys[2] = 0x20; /* Saved key remap: D is accelerate, not steer. */
    key(&in, SDL_SCANCODE_D, true, false);
    IgnInput out = ign_input_sample(&in, &c, true, &rear);
    CHECK(out.accel && !out.right);
    key(&in, SDL_SCANCODE_D, false, false);
    key(&in, SDL_SCANCODE_SLASH, true, false);
    key(&in, SDL_SCANCODE_SLASH, false, false); /* Tap entirely between physics ticks. */
    CHECK(ign_input_sample(&in, &c, false, &rear).gear_shift == 1);
    CHECK(ign_input_sample(&in, &c, true, &rear).gear_shift == 1);
    CHECK(ign_input_sample(&in, &c, true, &rear).gear_shift == 0);
    key(&in, SDL_SCANCODE_PERIOD, true, false);
    key(&in, SDL_SCANCODE_SLASH, true, false);
    CHECK(ign_input_sample(&in, &c, true, &rear).gear_shift == 0);
    ign_input_reset(&in);
    SDL_Event text = {0};
    text.type = SDL_EVENT_TEXT_INPUT;
    text.text.text = "prefixbanarne";
    ign_input_event(&in, &text);
    CHECK(ign_input_typed(&in, "BANARNE"));
    CHECK(!ign_input_typed(&in, "BANARNE"));
    CHECK(!ign_input_typed(&in, ""));
    c.legacy_keys = true;
    key(&in, SDL_SCANCODE_W, true, false);
    key(&in, SDL_SCANCODE_SPACE, true, false);
    key(&in, SDL_SCANCODE_R, true, false);
    out = ign_input_sample(&in, &c, true, &rear);
    CHECK(out.accel && out.turbo && rear);
    SDL_Event e = {0};
    e.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    e.window.windowID = 8;
    ign_input_event(&in, &e);
    CHECK(in.focused && in.keys[SDL_SCANCODE_W]);
    e.window.windowID = 7;
    ign_input_event(&in, &e);
    CHECK(!in.focused && !in.keys[SDL_SCANCODE_W]);
    CHECK(key(&in, SDL_SCANCODE_W, true, false) == 0);
    c.auto_accel = true;
    CHECK(!ign_input_sample(&in, &c, true, &rear).accel);
    e.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
    ign_input_event(&in, &e);
    CHECK(ign_input_sample(&in, &c, true, &rear).accel);
    key(&in, SDL_SCANCODE_DOWN, true, false);
    CHECK(!ign_input_sample(&in, &c, true, &rear).accel);
    ign_input_reset(&in);
    for (int i = 0; i < IGN_INPUT_KEY_QUEUE + 12; i++) {
        key(&in, SDL_SCANCODE_A, true, false);
        key(&in, SDL_SCANCODE_A, false, false);
    }
    for (int i = 0; i < IGN_INPUT_KEY_QUEUE; i++) CHECK(ign_input_read_key(&in) == 0x1e);
    CHECK(!ign_input_read_key(&in));
}

static void axes(void) {
    IgnInputAxis c = {-32768, 0, 32767, 4096};
    CHECK(ign_input_axis(-32768, c) == -100);
    CHECK(ign_input_axis(32767, c) == 100);
    CHECK(ign_input_axis(-4096, c) == 0);
    CHECK(ign_input_axis(4096, c) == 0);
    CHECK(ign_input_axis(18300, c) >= 49 && ign_input_axis(18300, c) <= 50);
    CHECK(ign_input_axis(-99999, c) == -100);
    CHECK(ign_input_axis(99999, c) == 100);
    CHECK(ign_input_axis(123, (IgnInputAxis){0, 0, 0, 0}) == 0);
    CHECK(ign_input_axis(123, (IgnInputAxis){-1, 0, 1, -1}) == 0);
    CHECK(ign_input_axis(32767, (IgnInputAxis){-32768, -32768, 32767, 2048}) == 100);
    CHECK(ign_input_axis(-32768, (IgnInputAxis){-32768, 32767, 32767, 2048}) == -100);
}

static SDL_JoystickID attach(bool pad) {
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = pad ? SDL_JOYSTICK_TYPE_GAMEPAD : SDL_JOYSTICK_TYPE_WHEEL;
    desc.naxes = pad ? SDL_GAMEPAD_AXIS_COUNT : 3;
    desc.nbuttons = pad ? SDL_GAMEPAD_BUTTON_COUNT : 6;
    desc.nhats = pad ? 0 : 1;
    desc.axis_mask = (1u << desc.naxes) - 1;
    desc.button_mask = (1u << desc.nbuttons) - 1;
    desc.name = pad ? "Ignition test gamepad" : "Ignition test wheel";
    SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
    CHECK(id);
    return id;
}

static int slot_for(IgnPlatformInput *in, SDL_JoystickID id) {
    for (int i = 0; i < IGN_INPUT_DEVICES; i++) if (in->devices[i].id == id) return i;
    CHECK(false);
    return -1;
}

static void pump(IgnPlatformInput *in) {
    SDL_Event e;
    SDL_UpdateJoysticks();
    while (SDL_PollEvent(&e)) ign_input_event(in, &e);
}

static void axis(IgnPlatformInput *in, SDL_JoystickID id, bool pad, int a, int value) {
    SDL_Event e = {0};
    e.type = pad ? SDL_EVENT_GAMEPAD_AXIS_MOTION : SDL_EVENT_JOYSTICK_AXIS_MOTION;
    e.jaxis.which = id;
    e.jaxis.axis = (Uint8)a;
    e.jaxis.value = (Sint16)value;
    ign_input_event(in, &e);
}

static int button_event(IgnPlatformInput *in, SDL_JoystickID id, bool pad, int b, bool down) {
    SDL_Event e = {0};
    e.type = pad ? (down ? SDL_EVENT_GAMEPAD_BUTTON_DOWN : SDL_EVENT_GAMEPAD_BUTTON_UP) :
                   (down ? SDL_EVENT_JOYSTICK_BUTTON_DOWN : SDL_EVENT_JOYSTICK_BUTTON_UP);
    e.jbutton.which = id;
    e.jbutton.button = (Uint8)b;
    return ign_input_event(in, &e);
}

static void devices(void) {
    CHECK(SDL_Init(SDL_INIT_EVENTS | SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD));
    SDL_JoystickID raw = attach(false);
    IgnPlatformInput in;
    CHECK(ign_input_init(&in, 0));
    pump(&in);
    int raw_slot = slot_for(&in, raw);
    CHECK(!in.devices[raw_slot].gamepad);
    IgnInputConfig c;
    ign_input_config_defaults(&c);
    c.controls = 1; c.device = raw_slot; c.manual_gear = true;
    bool rear;
    axis(&in, raw, false, 0, -32768);
    axis(&in, raw, false, 1, -32768);
    IgnInput out = ign_input_sample(&in, &c, true, &rear);
    CHECK(out.analog && out.a_steer == -100 && out.a_accel == 100 && !out.a_brake && out.manual_gear);
    axis(&in, raw, false, 1, 32767);
    out = ign_input_sample(&in, &c, true, &rear);
    CHECK(out.a_brake == 100 && out.a_accel == 0);
    c.auto_accel = true;
    out = ign_input_sample(&in, &c, true, &rear);
    CHECK(out.a_brake == 100 && out.a_accel == 0);
    CHECK(button_event(&in, raw, false, 0, true) == 0x1c);
    CHECK(button_event(&in, raw, false, 0, true) == 0);
    button_event(&in, raw, false, 0, false);
    button_event(&in, raw, false, 5, true);
    button_event(&in, raw, false, 5, false);
    CHECK(ign_input_sample(&in, &c, false, &rear).gear_shift == 1);
    CHECK(ign_input_sample(&in, &c, true, &rear).gear_shift == 1);
    CHECK(ign_input_sample(&in, &c, true, &rear).gear_shift == 0);
    ign_input_reset(&in);
    c.auto_accel = false; c.controls = 2;
    CHECK(ign_input_calibrate_axis(&in, raw_slot, 1, (IgnInputAxis){-32768, -32768, 32767, 2048}));
    CHECK(ign_input_calibrate_axis(&in, raw_slot, 2, (IgnInputAxis){-32768, 32767, 32767, 2048}));
    CHECK(!ign_input_calibrate_axis(&in, raw_slot, 8, (IgnInputAxis){-32768, 0, 32767, 0}));
    out = ign_input_sample(&in, &c, true, &rear);
    CHECK(!out.a_accel && !out.a_brake);
    axis(&in, raw, false, 1, 32767);
    axis(&in, raw, false, 2, -32768);
    out = ign_input_sample(&in, &c, true, &rear);
    CHECK(out.a_accel == 100 && out.a_brake == 100);
    ign_input_reset(&in);
    out = ign_input_sample(&in, &c, true, &rear);
    CHECK(!out.a_accel && !out.a_brake);
    CHECK(SDL_DetachVirtualJoystick(raw));
    pump(&in);
    CHECK(!in.devices[raw_slot].id);
    key(&in, SDL_SCANCODE_UP, true, false);
    out = ign_input_sample(&in, &c, true, &rear);
    CHECK(!out.analog && out.accel);
    ign_input_reset(&in);
    SDL_JoystickID pad = attach(true);
    pump(&in);
    c.device = slot_for(&in, pad); c.controls = 1;
    CHECK(in.devices[c.device].gamepad);
    axis(&in, pad, true, SDL_GAMEPAD_AXIS_LEFTX, 32767);
    axis(&in, pad, true, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 32767);
    axis(&in, pad, true, SDL_GAMEPAD_AXIS_LEFT_TRIGGER, 16384);
    axis(&in, pad, false, 0, -32768); /* The duplicate raw stream must not override mapped events. */
    button_event(&in, pad, true, SDL_GAMEPAD_BUTTON_WEST, true);
    button_event(&in, pad, true, SDL_GAMEPAD_BUTTON_NORTH, true);
    button_event(&in, pad, true, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, true);
    out = ign_input_sample(&in, &c, true, &rear);
    CHECK(out.a_steer == 100 && out.a_accel == 100 && out.a_brake > 40 && out.a_brake < 51);
    CHECK(out.turbo && rear && out.gear_shift == 1);
    CHECK(ign_input_sample(&in, &c, true, &rear).gear_shift == 0);
    SDL_Event e = {0};
    e.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    ign_input_event(&in, &e);
    axis(&in, pad, true, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, 32767);
    out = ign_input_sample(&in, &c, true, &rear);
    CHECK(!out.analog && !out.accel && !out.turbo && !out.gear_shift);
    e.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
    ign_input_event(&in, &e);
    out = ign_input_sample(&in, &c, true, &rear);
    CHECK(out.analog && !out.a_accel && !out.a_brake && !out.turbo);
    CHECK(SDL_DetachVirtualJoystick(pad));
    pump(&in);
    ign_input_shutdown(&in);
    ign_input_shutdown(&in);
    SDL_Quit();
}

int main(void) {
    keyboard();
    axes();
    devices();
    puts("input backend: keyboard, calibration, focus, gears, virtual joystick/gamepad and hotplug passed");
    return 0;
}
