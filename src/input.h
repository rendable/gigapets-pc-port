// Keyboard/gamepad bindings, key names, and persistence of the control settings.
#pragma once

#include "base.h"

// Remappable game controls. Bit index within GPIO Port A Data (see the
// 0x3D01 read handler below) matches enum order by construction, so the
// input poll can just loop 0..GAME_BUTTON_COUNT instead of one hardcoded
// IsKeyDown() per button.
enum GameButton { BTN_LEFT = 0, BTN_RIGHT, BTN_UP, BTN_DOWN, BTN_SELECT, BTN_BACK, BTN_MENU, GAME_BUTTON_COUNT };

struct GameButtonInfo { const char* name; int default_key; };

static const GameButtonInfo GAME_BUTTONS[GAME_BUTTON_COUNT] = {
    { "D-Pad Left",   KEY_LEFT },
    { "D-Pad Right",  KEY_RIGHT },
    { "D-Pad Up",     KEY_UP },
    { "D-Pad Down",   KEY_DOWN },
    { "Select",       KEY_Z },
    { "Back/Cancel",  KEY_X },
    { "Menu",         KEY_ENTER },
};

// Default (and, until rebound, current) gamepad button per GameButton, same
// order as GAME_BUTTONS. g_gamepad_binding is the live, user-remappable
// value the GPIO read handler (0x3D01, below) actually checks; this array
// is only the startup default, same relationship as GAME_BUTTONS[].
// default_key vs g_key_binding[] for the keyboard side. BTN_LEFT/RIGHT/UP/
// DOWN also always accept the left analog stick in the GPIO handler in
// addition to whatever button is bound - not representable as a single
// GamepadButton, so it's not part of the binding/display, just an always-on
// extra.
static const int GAME_BUTTON_GAMEPAD_DEFAULT[GAME_BUTTON_COUNT] = {
    GAMEPAD_BUTTON_LEFT_FACE_LEFT, GAMEPAD_BUTTON_LEFT_FACE_RIGHT, GAMEPAD_BUTTON_LEFT_FACE_UP, GAMEPAD_BUTTON_LEFT_FACE_DOWN,
    GAMEPAD_BUTTON_RIGHT_FACE_DOWN, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT, GAMEPAD_BUTTON_MIDDLE_RIGHT
};

extern int g_key_binding[GAME_BUTTON_COUNT];
extern int g_gamepad_binding[GAME_BUTTON_COUNT];
extern bool g_awaiting_gamepad_rebind;
extern int g_mod_menu_key;
extern int g_mod_menu_gamepad;

const char* get_key_display_name(int key);
void save_keybinds();
void load_keybinds();
const char* get_gamepad_button_name(int btn);
int get_gamepad_button_pressed(int gamepad);
void save_gamepad_binds();
void load_gamepad_binds();
