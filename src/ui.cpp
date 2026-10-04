// Shared overlay-UI resources: UI font, HUD textures, shaders, and the HUD camera.

#include "common.h"

Texture2D screen_texture;
Texture2D hud_money_a, hud_money_b, hud_halo, hud_pitchfork, hud_star_a, hud_star_b, hud_lightbulb_a, hud_lightbulb_b;

// All overlay text (mod menu, HUD, FPS counter) draws through this instead
// of raylib's built-in font, which is a small blocky bitmap font - loaded
// at a high base size so it scales down cleanly to whatever size each
// DrawText call asks for. Falls back to the default font if no system font
// is found, so this still runs (just blocky again) on a machine without it.
Font g_ui_font;
bool g_ui_font_is_custom = false; // only true if LoadFontEx succeeded - UnloadFont on GetFontDefault()'s result would be wrong

// Maps the fixed virtual UI space (WIDE_W*DEFAULT_WINDOW_SCALE x
// NATIVE_H*DEFAULT_WINDOW_SCALE) that all overlay UI (mod menu, stat HUD,
// FPS counter) is laid out in to whatever the real window/fullscreen size
// currently is - recomputed once per frame (see the main loop), used both
// for drawing (BeginMode2D(g_hud_cam)) and for converting real mouse
// coordinates into that same space for hit-testing.
Camera2D g_hud_cam;

void ui_draw_text(const char* text, int x, int y, int font_size, Color color) {
    DrawTextEx(g_ui_font, text, Vector2{ (float)x, (float)y }, (float)font_size, 1.0f, color);
}

int ui_measure_text(const char* text, int font_size) {
    return (int)MeasureTextEx(g_ui_font, text, (float)font_size, 1.0f).x;
}

Shader crt_shader, sharp_shader;
int crt_output_size_loc, sharp_source_size_loc, sharp_output_scale_loc;

// Computed once per frame and reused for both mouse-coordinate
// conversion (mod-menu hit-testing, below) and drawing the overlay
// UI (stat HUD/mod menu, further down) - both need the same
// mapping from the fixed virtual UI space to whatever the real
// window/fullscreen size currently is.
void ui_update_hud_camera() {
int sw = GetScreenWidth(), sh = GetScreenHeight();
float scale = std::min((float)sw / WIDE_W, (float)sh / NATIVE_H);
float offsetX = (sw - WIDE_W * scale) / 2.0f;
float offsetY = (sh - NATIVE_H * scale) / 2.0f;
float hud_zoom = scale / DEFAULT_WINDOW_SCALE;
g_hud_cam = Camera2D{ Vector2{ offsetX, offsetY }, Vector2{ 0, 0 }, 0.0f, hud_zoom };
}
