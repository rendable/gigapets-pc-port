// On-screen overlays: FPS counter and the always-on pet stat HUD.

#include "common.h"

float fps_frame_times_ms[FPS_WINDOW] = {};

int fps_frame_index = 0;
int fps_frame_count = 0;

void hud_record_frame_time(float frame_ms) {
fps_frame_times_ms[fps_frame_index] = frame_ms;
fps_frame_index = (fps_frame_index + 1) % FPS_WINDOW;
if (fps_frame_count < FPS_WINDOW) fps_frame_count++;
}

void hud_draw_fps(float frame_ms) {
if (g_show_fps_hud) {
    float sorted_ms[FPS_WINDOW];
    memcpy(sorted_ms, fps_frame_times_ms, sizeof(float) * fps_frame_count);
    std::sort(sorted_ms, sorted_ms + fps_frame_count, std::greater<float>());
    int worst_count = fps_frame_count / 100;
    if (worst_count < 1) worst_count = 1;
    float worst_sum = 0.0f;
    for (int i = 0; i < worst_count; i++) worst_sum += sorted_ms[i];
    float worst_avg_ms = worst_sum / worst_count;
    float one_percent_low_fps = worst_avg_ms > 0.0f ? (1000.0f / worst_avg_ms) : 0.0f;
    float current_fps = frame_ms > 0.0f ? (1000.0f / frame_ms) : 0.0f;

    char buf[64];
    sprintf(buf, "FPS %.0f  1%%Low %.0f", current_fps, one_percent_low_fps);
    DrawRectangle(0, 0, 200, 24, Color{0, 0, 0, 180});
    ui_draw_text(buf, 4, 4, 20, GREEN);
}
}

void hud_draw_stats() {
// Always-on HUD (top-right): money + the 4 personality icons/values
// using real game icon graphics, plus Hunger/Mood/Sleepiness/Hygeine
// as label+value text (shown as words like "Stuffed" in-game, raw
// number here since it's more useful alongside mod-menu editing).
if (g_show_stat_hud && ram[GAME_STATE_ADDR] == GAME_STATE_IN_ROOM) {
    const float isc = 0.75f; // icon scale - shrinks 32px icons to 24px for a tighter panel
    float money_a_w = hud_money_a.width * isc, money_b_w = hud_money_b.width * isc;
    float halo_w = hud_halo.width * isc, pitchfork_w = hud_pitchfork.width * isc;
    float star_w = hud_star_a.width * isc + hud_star_b.width * isc;
    float lightbulb_w = hud_lightbulb_a.width * isc + hud_lightbulb_b.width * isc;
    const int gap = 3; // icon-to-number and number-to-next-icon gap

    char money_s[16], halo_s[16], star_s[16], pitchfork_s[16], lightbulb_s[16];
    char hunger_s[24], mood_s[24], sleep_s[24], hyg_s[24], health_s[40];
    sprintf(money_s, "%u", ram[CHEAT_MONEY_BASE]);
    sprintf(halo_s, "%u", ram[CHEAT_HALOS]);
    sprintf(star_s, "%u", ram[CHEAT_STARS]);
    sprintf(pitchfork_s, "%u", ram[CHEAT_PITCHFORKS]);
    sprintf(lightbulb_s, "%u", ram[CHEAT_LIGHTBULBS]);
    sprintf(hunger_s, "Hunger %u", ram[CHEAT_HUNGER]);
    sprintf(mood_s, "Mood %u", ram[CHEAT_MOOD]);
    sprintf(sleep_s, "Sleepiness %u", ram[CHEAT_SLEEPINESS]);
    sprintf(hyg_s, "Hygeine %u", ram[CHEAT_HYGEINE]);

    // Health doubles as the sickness-type field - only worth taking
    // up HUD space when the pet is actually NOT healthy.
    uint16_t health_val = ram[CHEAT_HEALTH];
    bool show_health = health_val != SICKNESS_HEALTHY_VALUE;
    if (show_health) {
        const char* name = (health_val < SICKNESS_NAME_COUNT) ? SICKNESS_NAMES[health_val] : "Unknown";
        sprintf(health_s, "Health: %s", name);
    }

    // Pass 1: measure each row's real content width so the panel
    // hugs whatever's actually there instead of a fixed guess.
    float row_money_w = money_a_w + money_b_w + gap + ui_measure_text(money_s, 16);
    float halo_col_w = halo_w + gap + ui_measure_text(halo_s, 14);
    float row1_w = halo_col_w + gap * 2 + star_w + gap + ui_measure_text(star_s, 14);
    float pitchfork_col_w = pitchfork_w + gap + ui_measure_text(pitchfork_s, 14);
    float row2_w = pitchfork_col_w + gap * 2 + lightbulb_w + gap + ui_measure_text(lightbulb_s, 14);
    float row_text_w = (float)ui_measure_text(sleep_s, 14); // longest of the four labels
    float row_health_w = show_health ? (float)ui_measure_text(health_s, 14) : 0.0f;

    float content_w = row_money_w;
    if (row1_w > content_w) content_w = row1_w;
    if (row2_w > content_w) content_w = row2_w;
    if (row_text_w > content_w) content_w = row_text_w;
    if (row_health_w > content_w) content_w = row_health_w;

    int hud_w = (int)content_w + 10;
    int hx = (int)(WIDE_W * DEFAULT_WINDOW_SCALE) - hud_w - 6, hy = 6;
    int hud_h = 26 + 24 + 28 + 16 * 4 + 6 + (show_health ? 16 : 0);
    DrawRectangle(hx - 5, hy - 3, hud_w, hud_h, Color{0, 0, 0, 160});

    // Pass 2: draw using the same measured widths for positioning.
    DrawTextureEx(hud_money_a, Vector2{ (float)hx, (float)hy }, 0, isc, WHITE);
    DrawTextureEx(hud_money_b, Vector2{ hx + money_a_w, (float)hy }, 0, isc, WHITE);
    ui_draw_text(money_s, hx + (int)(money_a_w + money_b_w) + gap, hy + 5, 16, WHITE);
    int hy2 = hy + 26;

    DrawTextureEx(hud_halo, Vector2{ (float)hx, (float)hy2 }, 0, isc, WHITE);
    ui_draw_text(halo_s, hx + (int)halo_w + gap, hy2 + 5, 14, WHITE);
    float col2_x = hx + halo_col_w + gap * 2;
    DrawTextureEx(hud_star_a, Vector2{ col2_x, (float)hy2 }, 0, isc, WHITE);
    DrawTextureEx(hud_star_b, Vector2{ col2_x + hud_star_a.width * isc, (float)hy2 }, 0, isc, WHITE);
    ui_draw_text(star_s, (int)(col2_x + star_w) + gap, hy2 + 5, 14, WHITE);
    hy2 += 24;

    DrawTextureEx(hud_pitchfork, Vector2{ (float)hx, (float)hy2 }, 0, isc, WHITE);
    ui_draw_text(pitchfork_s, hx + (int)pitchfork_w + gap, hy2 + 5, 14, WHITE);
    float col2_x2 = hx + pitchfork_col_w + gap * 2;
    DrawTextureEx(hud_lightbulb_a, Vector2{ col2_x2, (float)hy2 }, 0, isc, WHITE);
    DrawTextureEx(hud_lightbulb_b, Vector2{ col2_x2 + hud_lightbulb_a.width * isc, (float)hy2 }, 0, isc, WHITE);
    ui_draw_text(lightbulb_s, (int)(col2_x2 + lightbulb_w) + gap, hy2 + 5, 14, WHITE);
    hy2 += 28;

    ui_draw_text(hunger_s, hx, hy2, 14, WHITE); hy2 += 16;
    ui_draw_text(mood_s, hx, hy2, 14, WHITE); hy2 += 16;
    ui_draw_text(sleep_s, hx, hy2, 14, WHITE); hy2 += 16;
    ui_draw_text(hyg_s, hx, hy2, 14, WHITE);
    if (show_health) { hy2 += 16; ui_draw_text(health_s, hx, hy2, 14, RED); }
}
}
