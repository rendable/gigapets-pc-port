// Shared overlay-UI resources: UI font, HUD textures, shaders, and the HUD camera.
#pragma once

#include "base.h"

extern Texture2D screen_texture;
extern Texture2D hud_money_a, hud_money_b, hud_halo, hud_pitchfork, hud_star_a, hud_star_b, hud_lightbulb_a, hud_lightbulb_b;
extern Font g_ui_font;
extern bool g_ui_font_is_custom;
extern Camera2D g_hud_cam;
extern Shader crt_shader, sharp_shader;
extern int crt_output_size_loc, sharp_source_size_loc, sharp_output_scale_loc;

void ui_draw_text(const char* text, int x, int y, int font_size, Color color);
int ui_measure_text(const char* text, int font_size);
