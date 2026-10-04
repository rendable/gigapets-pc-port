// Keyboard/gamepad bindings, key names, and persistence of the control settings.

#include "common.h"

int g_key_binding[GAME_BUTTON_COUNT];

int g_gamepad_binding[GAME_BUTTON_COUNT] = {0};

bool g_awaiting_gamepad_rebind = false;

// Mod-menu toggle is host-side UI, not a real GigaPets button - kept
// deliberately separate from g_key_binding/g_gamepad_binding (GAME_BUTTON_COUNT)
// rather than added as an 8th entry there, since that array's values get
// OR'd directly into the bitmask fed to the emulated ROM's input register;
// an 8th slot would risk corrupting real button input. Persisted via the
// same two files using button_id==GAME_BUTTON_COUNT as a sentinel record.
int g_mod_menu_key = KEY_TAB;
int g_mod_menu_gamepad = GAMEPAD_BUTTON_LEFT_TRIGGER_2;

const char* get_key_display_name(int key) {
    // GetKeyName doesn't exist in raylib; hand-roll the common cases used by
    // this project's default bindings and anything a user is likely to pick.
    switch (key) {
        case KEY_LEFT: return "Left";
        case KEY_RIGHT: return "Right";
        case KEY_UP: return "Up";
        case KEY_DOWN: return "Down";
        case KEY_ENTER: return "Enter";
        case KEY_SPACE: return "Space";
        case KEY_TAB: return "Tab";
        case KEY_ESCAPE: return "Escape";
        case KEY_LEFT_SHIFT: return "L-Shift";
        case KEY_RIGHT_SHIFT: return "R-Shift";
        case KEY_LEFT_CONTROL: return "L-Ctrl";
        case KEY_RIGHT_CONTROL: return "R-Ctrl";
        default: break;
    }
    if (key >= KEY_A && key <= KEY_Z) return TextFormat("%c", 'A' + (key - KEY_A));
    if (key >= KEY_ZERO && key <= KEY_NINE) return TextFormat("%c", '0' + (key - KEY_ZERO));
    if (key >= KEY_F1 && key <= KEY_F12) return TextFormat("F%d", 1 + (key - KEY_F1));
    return TextFormat("Key %d", key);
}

void save_keybinds() {
    FILE* f = fopen(app_path("resources/data/gigapets_keybinds.dat").c_str(), "wb");
    if (!f) return;
    for (int b = 0; b < GAME_BUTTON_COUNT; b++) {
        int32_t button_id = b, key = g_key_binding[b];
        fwrite(&button_id, sizeof(int32_t), 1, f);
        fwrite(&key, sizeof(int32_t), 1, f);
    }
    { int32_t button_id = GAME_BUTTON_COUNT, key = g_mod_menu_key;
      fwrite(&button_id, sizeof(int32_t), 1, f);
      fwrite(&key, sizeof(int32_t), 1, f); }
    fclose(f);
}

void load_keybinds() {
    FILE* f = fopen(app_path("resources/data/gigapets_keybinds.dat").c_str(), "rb");
    if (!f) return;
    int32_t button_id, key;
    while (fread(&button_id, sizeof(int32_t), 1, f) == 1 && fread(&key, sizeof(int32_t), 1, f) == 1) {
        if (button_id >= 0 && button_id < GAME_BUTTON_COUNT) g_key_binding[button_id] = key;
        else if (button_id == GAME_BUTTON_COUNT) g_mod_menu_key = key;
    }
    fclose(f);
}

// Generic positional name (D-Pad/face/trigger/stick), not tied to any one
// controller brand - raylib already normalizes Xbox/PlayStation/generic pads
// to the same GamepadButton values, so one name table covers all of them.
const char* get_gamepad_button_name(int btn) {
    switch (btn) {
        case GAMEPAD_BUTTON_LEFT_FACE_UP: return "D-Pad Up";
        case GAMEPAD_BUTTON_LEFT_FACE_DOWN: return "D-Pad Down";
        case GAMEPAD_BUTTON_LEFT_FACE_LEFT: return "D-Pad Left";
        case GAMEPAD_BUTTON_LEFT_FACE_RIGHT: return "D-Pad Right";
        case GAMEPAD_BUTTON_RIGHT_FACE_UP: return "Top Face";
        case GAMEPAD_BUTTON_RIGHT_FACE_DOWN: return "Bottom Face";
        case GAMEPAD_BUTTON_RIGHT_FACE_LEFT: return "Left Face";
        case GAMEPAD_BUTTON_RIGHT_FACE_RIGHT: return "Right Face";
        case GAMEPAD_BUTTON_LEFT_TRIGGER_1: return "L1";
        case GAMEPAD_BUTTON_LEFT_TRIGGER_2: return "L2";
        case GAMEPAD_BUTTON_RIGHT_TRIGGER_1: return "R1";
        case GAMEPAD_BUTTON_RIGHT_TRIGGER_2: return "R2";
        case GAMEPAD_BUTTON_MIDDLE_LEFT: return "Select";
        case GAMEPAD_BUTTON_MIDDLE: return "Guide";
        case GAMEPAD_BUTTON_MIDDLE_RIGHT: return "Start";
        case GAMEPAD_BUTTON_LEFT_THUMB: return "L-Stick";
        case GAMEPAD_BUTTON_RIGHT_THUMB: return "R-Stick";
        default: return "N/A";
    }
}

int get_gamepad_button_pressed(int gamepad) {
    for (int b = GAMEPAD_BUTTON_LEFT_FACE_UP; b <= GAMEPAD_BUTTON_RIGHT_THUMB; b++) {
        if (IsGamepadButtonPressed(gamepad, b)) return b;
    }
    return -1;
}

struct GamepadBindRecord { int32_t button_id; int32_t gamepad_button; };

void save_gamepad_binds() {
    FILE* f = fopen(app_path("resources/data/gigapets_gamepad_binds.dat").c_str(), "wb");
    if (!f) return;
    for (int b = 0; b < GAME_BUTTON_COUNT; b++) {
        GamepadBindRecord rec = { b, g_gamepad_binding[b] };
        fwrite(&rec, sizeof(rec), 1, f);
    }
    { GamepadBindRecord rec = { GAME_BUTTON_COUNT, g_mod_menu_gamepad };
      fwrite(&rec, sizeof(rec), 1, f); }
    fclose(f);
}

void load_gamepad_binds() {
    FILE* f = fopen(app_path("resources/data/gigapets_gamepad_binds.dat").c_str(), "rb");
    if (!f) return;
    GamepadBindRecord rec;
    while (fread(&rec, sizeof(rec), 1, f) == 1) {
        if (rec.button_id >= 0 && rec.button_id < GAME_BUTTON_COUNT) g_gamepad_binding[rec.button_id] = rec.gamepad_button;
        else if (rec.button_id == GAME_BUTTON_COUNT) g_mod_menu_gamepad = rec.gamepad_button;
    }
    fclose(f);
}
