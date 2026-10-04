// Program entry point: startup, main loop, shutdown.
//
// Each frame: advance the emulated machine by however many 1/60s ticks of real time have elapsed,
// let the Mod Menu react to input, render the game into a framebuffer, present it, then draw the
// overlays (HUD, quest arrow, Mod Menu) on top.

#include "common.h"

// Reads resources/data/rom.u7 into rom[]. Shows an error dialog and returns false if it is missing
// or the wrong size.
static bool load_rom() {
    FILE* rom_f = fopen(app_path("resources/data/rom.u7").c_str(), "rb");
    if (!rom_f) {
        // Windowed-subsystem build has no console, so a printf here would
        // be invisible and the app would just silently vanish on first run.
        show_error_dialog(
            "ROM not found.\n\nRename your Giga Pets Explorer ROM to \"rom.u7\" and place it in the\n\"resources\\data\" folder next to GigaPetsPC.exe, then run again.");
        return false;
    }
    fseek(rom_f, 0, SEEK_END);
    long rom_size = ftell(rom_f);
    fseek(rom_f, 0, SEEK_SET);
    if (rom_size != 2L * 0x400000) {
        fclose(rom_f);
        char msg[256];
        snprintf(msg, sizeof(msg),
            "This doesn't look like the right ROM file.\n\nrom.u7 is %ld bytes, but the Giga Pets Explorer ROM should be exactly %ld bytes (8 MB).\n\nMake sure you copied the full, unmodified ROM dump.",
            rom_size, 2L * 0x400000);
        show_error_dialog(msg);
        return false;
    }
    fread(rom, 2, 0x400000, rom_f);
    fclose(rom_f);
    return true;
}

// Loads the save file and every persisted user setting from resources/data/.
static void load_user_data() {
    std::filesystem::create_directories(app_path("resources/data"));
    eeprom_load();
    load_cheat_state();
    load_custom_mods();
    find_noclip_patch_addresses();
    g_shadow_patch_original = rom[SHADOW_PATCH_ADDR];
    for (int b = 0; b < GAME_BUTTON_COUNT; b++) g_key_binding[b] = GAME_BUTTONS[b].default_key;
    load_keybinds();
    for (int b = 0; b < GAME_BUTTON_COUNT; b++) g_gamepad_binding[b] = GAME_BUTTON_GAMEPAD_DEFAULT[b];
    load_gamepad_binds();
}

static void init_audio() {
    InitAudioDevice();
    SetAudioStreamBufferSizeDefault(AUDIO_STREAM_CHUNK);
    audio_stream = LoadAudioStream(70312, 16, 2);
    PlayAudioStream(audio_stream);
    audio_reset();
}

// Creates the window and loads everything drawn with raylib: UI font, screen texture, HUD icons
// and the CRT/Sharp shaders.
static void init_window_and_assets() {
    SetConfigFlags(g_selftest ? FLAG_WINDOW_RESIZABLE : (FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT));
    InitWindow((int)(WIDE_W * DEFAULT_WINDOW_SCALE), (int)(NATIVE_H * DEFAULT_WINDOW_SCALE), TextFormat("GigaPets PC Port v%s", APP_VERSION));
    // Raylib's default exit key is Escape, which would quit the game whenever
    // the player hits Esc to cancel a Mod Menu edit. The window's X button
    // and the in-game Quit still close it normally.
    SetExitKey(KEY_NULL);
    SetTargetFPS(g_selftest ? 0 : FPS_LIMIT_OPTIONS[g_fps_limit_idx]);

    // No font is bundled - try common Windows system fonts first, in order
    // from cleanest to most likely to exist, before giving up and falling
    // back to raylib's own blocky default (still fully usable, just the
    // original look).
    static const char* UI_FONT_CANDIDATES[] = {
        "C:/Windows/Fonts/segoeui.ttf",
        "C:/Windows/Fonts/tahoma.ttf",
        "C:/Windows/Fonts/arial.ttf",
    };
    for (const char* path : UI_FONT_CANDIDATES) {
        if (FileExists(path)) { g_ui_font = LoadFontEx(path, 48, NULL, 0); break; }
    }
    if (g_ui_font.texture.id == 0) g_ui_font = GetFontDefault();
    else { SetTextureFilter(g_ui_font.texture, TEXTURE_FILTER_BILINEAR); g_ui_font_is_custom = true; }

    Image screen_image = GenImageColor(WIDE_W, NATIVE_H, BLACK);
    screen_texture = LoadTextureFromImage(screen_image);
    UnloadImage(screen_image);

    hud_money_a = LoadTexture(app_path("resources/hud_icons/money_a.png").c_str());
    hud_money_b = LoadTexture(app_path("resources/hud_icons/money_b.png").c_str());
    hud_halo = LoadTexture(app_path("resources/hud_icons/halo.png").c_str());
    hud_pitchfork = LoadTexture(app_path("resources/hud_icons/pitchfork.png").c_str());
    hud_star_a = LoadTexture(app_path("resources/hud_icons/star_a.png").c_str());
    hud_star_b = LoadTexture(app_path("resources/hud_icons/star_b.png").c_str());
    hud_lightbulb_a = LoadTexture(app_path("resources/hud_icons/lightbulb_a.png").c_str());
    hud_lightbulb_b = LoadTexture(app_path("resources/hud_icons/lightbulb_b.png").c_str());

    crt_shader = LoadShader(NULL, app_path("resources/shaders/crt.fs").c_str());
    crt_output_size_loc = GetShaderLocation(crt_shader, "outputSize");
    sharp_shader = LoadShader(NULL, app_path("resources/shaders/sharp.fs").c_str());
    sharp_source_size_loc = GetShaderLocation(sharp_shader, "sourceSize");
    sharp_output_scale_loc = GetShaderLocation(sharp_shader, "outputScale");
}

// Function-key shortcuts: F7/F8 sprite export, F9 hidden test menu, F11 expand window.
static void handle_hotkeys() {
    if (IsKeyPressed(KEY_F9)) test_menu_unlock_begin();
    sprite_export_handle_hotkeys();
    if (IsKeyPressed(KEY_F11)) toggle_expand_window();
}

static void save_and_shutdown(Color* framebuffer) {
    save_cheat_state();
    save_keybinds();
    save_gamepad_binds();
    if (g_ui_font_is_custom) UnloadFont(g_ui_font);
    UnloadTexture(screen_texture);
    UnloadTexture(hud_money_a); UnloadTexture(hud_money_b);
    UnloadTexture(hud_halo); UnloadTexture(hud_pitchfork);
    UnloadTexture(hud_star_a); UnloadTexture(hud_star_b);
    UnloadTexture(hud_lightbulb_a); UnloadTexture(hud_lightbulb_b);
    UnloadShader(crt_shader);
    UnloadShader(sharp_shader);
    delete[] framebuffer;
    UnloadAudioStream(audio_stream);
    CloseAudioDevice();
    CloseWindow();
}

int main() {
    g_selftest = getenv("GIGAPETS_SELFTEST") != nullptr;
    srand(g_selftest ? 1234u : (unsigned)time(NULL));
    g_app_dir = GetApplicationDirectory();

    if (!load_rom()) return 1;
    load_user_data();
    init_audio();

    machine_config config;
    unsp_20_device cpu(config, "unsp", nullptr, 0);
    cpu_ptr = &cpu;
    cpu.set_bootvectorbase(0xFFF0);
    cpu.set_vectorbase(0xFFF0);
    cpu.device_start();
    cpu.device_reset();

    init_window_and_assets();
    Color* framebuffer = new Color[WIDE_W * NATIVE_H];

    // Fixed-timestep simulation clock. Accumulates real elapsed time each
    // real frame and drains it in exact 1/60s steps, so the emulated game
    // always advances at the same rate regardless of how fast (or slow) we
    // happen to be rendering.
    const double SIM_DT = 1.0 / 60.0;
    double sim_accumulator = 0.0;

    while (!WindowShouldClose()) {
        ui_update_hud_camera();

        // Reused below for both the sim accumulator and audio pacing - one
        // GetFrameTime() call per real frame, not one each.
        double frame_time = GetFrameTime();
        if (frame_time > 0.25) frame_time = 0.25; // clamp a stall/breakpoint so it doesn't dump huge catch-up into the sim
        if (g_selftest) { frame_time = SIM_DT; selftest_apply_script(g_frame); }
        sim_accumulator += frame_time;

        while (sim_accumulator >= SIM_DT) {
            sim_accumulator -= SIM_DT;
            sim_tick();
        }

        float frame_ms = (float)frame_time * 1000.0f;
        hud_record_frame_time(frame_ms);

        // Frozen stats/items: re-assert every frame so game logic (e.g.
        // Hunger ticking down over time) can't change them.
        cheats_reassert_frozen();

        // Audio is generated cycle-interleaved inside the CPU stepping loop
        // (see audio_run_cycles) instead of in one lump-sum batch here. This
        // just drains our software queue into the OS audio stream at
        // whatever pace it wants more data.
        audio_queue_feed_stream(audio_stream);

        mod_menu_update();

        render_game_frame(framebuffer, sim_accumulator / SIM_DT);
        handle_hotkeys();
        present_game_frame(framebuffer, frame_time);

        // Overlays are laid out in a virtual UI space and drawn through g_hud_cam so they stay
        // glued to the letterboxed game viewport (see ui_update_hud_camera).
        BeginMode2D(g_hud_cam);
        hud_draw_fps(frame_ms);
        hud_draw_stats();
        quest_arrow_draw();
        mod_menu_draw_overlay();
        EndMode2D();

        bool selftest_done = g_selftest && selftest_checkpoint(g_frame, framebuffer);
        EndDrawing();
        if (selftest_done) break;
    }

    save_and_shutdown(framebuffer);
    return 0;
}
