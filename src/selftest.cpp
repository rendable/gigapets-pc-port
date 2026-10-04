// Deterministic regression harness (GIGAPETS_SELFTEST=1). See tools/selftest.ps1.

#include "common.h"

// Deterministic regression-test mode (env var GIGAPETS_SELFTEST=1). Fixed
// timestep, fixed RNG seed, scripted input, and hashes of the emulated state
// written to selftest_hashes.txt - see the selftest section above main().
bool g_selftest = false;
uint32_t g_selftest_audio_hash = 2166136261u;

// Scripted controller input for GIGAPETS_SELFTEST mode: a fixed pulse pattern
// of Select / Down / Right presses so menus and dialogue get exercised the
// same way every run.
uint16_t selftest_buttons(long frame) {
    uint16_t low = 0;
    if (frame >= 180) {
        long p = frame % 90;
        if (p < 3) low |= (1 << BTN_SELECT);
        else if (p >= 30 && p < 33) low |= (1 << BTN_DOWN);
        else if (p >= 60 && p < 63) low |= (1 << BTN_RIGHT);
    }
    return low;
}

// ---------------------------------------------------------------------------
// Deterministic regression harness (GIGAPETS_SELFTEST=1).
//
// Runs the emulator with a fixed timestep, fixed RNG seed and scripted input,
// drives the Mod Menu / cheat / filter code at fixed frames, and writes hashes
// of the emulated state (framebuffer, RAM, IO, video regs, generated audio,
// CPU registers, and the final on-screen pixels) to selftest_hashes.txt at
// fixed checkpoints, then exits. Two builds of the same behavior produce
// identical hashes, so refactors can be verified without playing the game.
// Requires a ROM and an empty resources/data/ (no existing save).
// ---------------------------------------------------------------------------
static uint32_t fnv1a(const void* data, size_t bytes, uint32_t h = 2166136261u) {
    const uint8_t* p = (const uint8_t*)data;
    for (size_t i = 0; i < bytes; i++) h = (h ^ p[i]) * 16777619u;
    return h;
}

void selftest_apply_script(long frame) {
    switch (frame) {
    case 1000:
        g_show_stat_hud = true;
        adjust_row(ROW_QUEST_ARROW, 1);
        // Even rows get pushed past their max, odd rows below their min, so
        // the clamping in set_stat_value is exercised at both ends.
        for (int i = 0; i < CHEAT_STAT_COUNT; i++) {
            if (i % 3 == 0) set_stat_value(i, CHEAT_STATS[i].max_val + 1000);
            else if (i % 3 == 1) set_stat_value(i, CHEAT_STATS[i].min_val - 1000);
            else set_stat_value(i, CHEAT_STATS[i].min_val + (CHEAT_STATS[i].max_val - CHEAT_STATS[i].min_val) / 3);
        }
        adjust_row(ROW_INVENTORY_ITEM_START + 0, 5);
        set_inv_value(1, 100000);
        set_inv_value(2, -50);
        toggle_freeze_row(ROW_STATS_START + 1);
        toggle_favorite_row(ROW_STATS_START + 2);
        break;
    case 1100:
        adjust_row(ROW_MINIPET, 2);
        adjust_row(ROW_MINIPET_ACTION, 1);
        adjust_row(ROW_NOCLIP, 1);
        adjust_row(ROW_MOVE_SPEED, 1);
        adjust_row(ROW_DISABLE_SHADOWS, 1);
        break;
    case 1500:
        strcpy(g_custom_mods[0].name, "Selftest");
        g_custom_mods[0].addr = CHEAT_MONEY_BASE;
        g_custom_mods[0].favorite = true;
        g_custom_mods[0].frozen = false;
        g_custom_mods[0].frozen_value = 0;
        g_custom_mod_count = 1;
        g_mod_menu_open = true;
        g_controls_expanded = g_extractor_expanded = g_stats_expanded = g_custom_expanded = true;
        for (int c = 0; c < INVENTORY_CATEGORY_COUNT; c++) g_category_expanded[c] = true;
        g_mod_menu_selection = ROW_STATS_START + 2;
        adjust_row(ROW_FILTER, 1); // Smooth
        break;
    case 2100:
        adjust_row(ROW_FILTER, 1); // Sharp
        g_mod_menu_selection = ROW_FILTER;
        break;
    case 2700:
        adjust_row(ROW_FILTER, 1); // CRT
        g_mod_menu_open = false;
        adjust_row(ROW_INTERPOLATION, 1);
        break;
    case 3300:
        adjust_row(ROW_FILTER, 1); // wraps back to Crisp
        break;
    default:
        break;
    }
}

// Called once per rendered frame, just before EndDrawing() so the on-screen
// pixels can be read back. Returns true after the final checkpoint.
bool selftest_checkpoint(long frame, const Color* framebuffer) {
    static const long CHECKPOINTS[] = { 600, 1200, 1800, 2400, 3000, 3600 };
    static const int COUNT = sizeof(CHECKPOINTS) / sizeof(CHECKPOINTS[0]);
    static FILE* out = nullptr;
    for (int i = 0; i < COUNT; i++) {
        if (frame != CHECKPOINTS[i]) continue;
        if (!out) out = fopen(app_path("selftest_hashes.txt").c_str(), "w");
        rlDrawRenderBatchActive();
        Image shot = LoadImageFromScreen();
        uint32_t screen_hash = fnv1a(shot.data, (size_t)shot.width * shot.height * 4);
        UnloadImage(shot);
        uint32_t cpu_hash = 2166136261u;
        for (int r = 0; r < 8; r++) { uint32_t v = cpu_ptr->get_r(r); cpu_hash = fnv1a(&v, sizeof(v), cpu_hash); }
        if (out) {
            fprintf(out, "frame=%ld fb=%08x ram=%08x io=%08x vid=%08x audio=%08x cpu=%08x screen=%08x\n", frame,
                fnv1a(framebuffer, sizeof(Color) * WIDE_W * NATIVE_H),
                fnv1a(ram, sizeof(ram)), fnv1a(io, sizeof(io)), fnv1a(video_regs, sizeof(video_regs)),
                g_selftest_audio_hash, cpu_hash, screen_hash);
            fflush(out);
        }
        if (i == COUNT - 1) { if (out) fclose(out); out = nullptr; return true; }
    }
    return false;
}
