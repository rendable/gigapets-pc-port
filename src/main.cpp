// Program entry point: startup, main loop, shutdown.

#include "common.h"

int main() {
    g_selftest = getenv("GIGAPETS_SELFTEST") != nullptr;
    srand(g_selftest ? 1234u : (unsigned)time(NULL));
    g_app_dir = GetApplicationDirectory();

    FILE* rom_f = fopen(app_path("resources/data/rom.u7").c_str(), "rb");
    if (!rom_f) {
        // Windowed-subsystem build has no console, so a printf here would
        // be invisible and the app would just silently vanish on first run.
        show_error_dialog(
            "ROM not found.\n\nRename your Giga Pets Explorer ROM to \"rom.u7\" and place it in the\n\"resources\\data\" folder next to GigaPetsPC.exe, then run again.");
        return 1;
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
        return 1;
    }
    fread(rom, 2, 0x400000, rom_f);
    fclose(rom_f);

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

    InitAudioDevice();
    SetAudioStreamBufferSizeDefault(AUDIO_STREAM_CHUNK);
    audio_stream = LoadAudioStream(70312, 16, 2);
    PlayAudioStream(audio_stream);
    audio_reset();

    machine_config config;
    unsp_20_device cpu(config, "unsp", nullptr, 0);
    cpu_ptr = &cpu;
    cpu.set_bootvectorbase(0xFFF0);
    cpu.set_vectorbase(0xFFF0);
    cpu.device_start();
    cpu.device_reset();

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
    Color* framebuffer = new Color[WIDE_W * NATIVE_H];

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

    // Fixed-timestep simulation clock. Accumulates real elapsed time each
    // real frame and drains it in exact 1/60s steps, so the emulated game
    // always advances at the same rate regardless of how fast (or slow) we
    // happen to be rendering.
    const double SIM_DT = 1.0 / 60.0;
    double sim_accumulator = 0.0;

    while (!WindowShouldClose()) {
        // Computed once per frame and reused for both mouse-coordinate
        // conversion (mod-menu hit-testing, below) and drawing the overlay
        // UI (stat HUD/mod menu, further down) - both need the same
        // mapping from the fixed virtual UI space to whatever the real
        // window/fullscreen size currently is.
        {
            int sw = GetScreenWidth(), sh = GetScreenHeight();
            float scale = std::min((float)sw / WIDE_W, (float)sh / NATIVE_H);
            float offsetX = (sw - WIDE_W * scale) / 2.0f;
            float offsetY = (sh - NATIVE_H * scale) / 2.0f;
            float hud_zoom = scale / DEFAULT_WINDOW_SCALE;
            g_hud_cam = Camera2D{ Vector2{ offsetX, offsetY }, Vector2{ 0, 0 }, 0.0f, hud_zoom };
        }

        // Reused below for both the sim accumulator and audio pacing - one
        // GetFrameTime() call per real frame, not one each.
        double frame_time = GetFrameTime();
        if (frame_time > 0.25) frame_time = 0.25; // clamp a stall/breakpoint so it doesn't dump huge catch-up into the sim
        if (g_selftest) { frame_time = SIM_DT; selftest_apply_script(g_frame); }
        sim_accumulator += frame_time;

        while (sim_accumulator >= SIM_DT) {
            sim_accumulator -= SIM_DT;

            if (watchdog_enabled) {
                watchdog_frames_left--;
                if (watchdog_frames_left <= 0) {
                    memset(ram, 0, sizeof(ram));
                    memset(io, 0, sizeof(io));
                    memset(video_regs, 0, sizeof(video_regs));
                    audio_reset();
                    cpu.device_reset();
                    watchdog_enabled = false;
                    watchdog_frames_left = 0;
                }
            }

            if (g_test_menu_seq_active) {
                g_test_menu_seq_frame++;
                // Safety cap in case the chime never fires for some reason -
                // the normal end-of-sequence path is rel>=64 in the GPIO
                // override above.
                if (g_test_menu_seq_frame >= 900) g_test_menu_seq_active = false;
            }

            if (video_regs[0x62] & 1) { video_regs[0x63] |= 1; check_video_irq(); }
            // Real unSP instructions cost variable cycles (2-12+), not a flat 1 -
            // budget against actual cycles consumed so this matches real
            // hardware throughput per frame instead of running far more
            // instructions than real silicon would in 1/60s.
            long cycle_budget = 27000000 / 60;
            while (cycle_budget > 0) {
                // GROUND TRUTH (captured via the call-site tracer below,
                // frame-by-frame while spawned): the real culprit is
                // HandleMiniPetRoomInputAndStoryLogic's call at ROM
                // 0x0130c8, which fires UNCONDITIONALLY every tick once
                // tracking is active (its only guard is
                // IsMiniPetTrackingEnabled(), not the link-cable flag - the
                // 0x1b19/MiniPet_HandleOverworldInput theory was a
                // different, uninvolved function). It always pushes
                // args (1,0), which inside FeedOrAdvanceMiniPet's
                // (paramFar,paramClose) convention takes the "no valid
                // target" branch and force-writes the tracking flag to 4
                // (despawn) - confirmed byte-for-byte at ROM 0x046235.
                // FeedOrAdvanceMiniPet's own top-of-function gate
                // (`if (DAT_001a37 != 0x7b) return;`, the read at ROM
                // 0x04617d) is what's supposed to make this a no-op once a
                // real minipet is idle/parked, but our synthetic Follow
                // state legitimately needs anim0 holding a live walk-pose
                // frame ID (see PlayMiniPetAnimForRoom), so it never reads
                // as the sentinel and this fires every tick instead.
                //
                // First attempt forced ram[MINIPET_ANIM_STRUCT_ADDR]=0x7B
                // directly - that "fixed" the despawn but broke Follow
                // itself: it overwrites the SAME memory every real minipet
                // reader (rendering, IsAnimationBusy, AdvanceSpriteAnimFrame)
                // also uses, so the sprite got stuck permanently repeating
                // the zap-ring frame instead of ever showing a walk pose -
                // that's what looked like "spamming the zap animation" and
                // the color drift (the ring anim uses a palette baked into
                // that same struct's other fields, restored to a stale
                // value every tick).
                //
                // Fix: leave memory alone and fake only the CPU register
                // this one comparison reads into. r1=[0x1a37] executes at
                // 0x04617d; on the step right after (PC now at 0x04617f,
                // the `cmp r1,0x7b`), override r1 back to 0x7b before that
                // compare runs. Nothing else that reads the real struct
                // value is touched.
                static bool force_r1_sentinel = false;
                if (force_r1_sentinel) {
                    cpu.set_r(unsp_12_device::REG_R1, 0x7B);
                    force_r1_sentinel = false;
                }
                if (g_minipet_spawned && full_pc() == 0x04617D) {
                    force_r1_sentinel = true;
                }

                // KNOWN ISSUE - Mystery Island travel: the dock trigger only
                // works if a minipet was already tracked when the room last
                // loaded (g_activeStoryObjectId, ram[0x1AA5], must read 0x68
                // there). Spawning a minipet from the Mod Menu mid-visit does
                // not retroactively update it, so the player has to leave and
                // re-enter the area (or go in and out of a building) first.
                // A real fix needs LoadRoom's per-area argument semantics
                // worked out (3 args beyond room id + position are not yet
                // understood).

                // Real hidden test-menu unlock, found by a user on real
                // hardware/MAME (not something we're bypassing - this is a
                // genuine dev-debug gate baked into the ROM). Real MAME
                // recipe: `bp 3c9e8,1,{maincpu.pb@1a4e=1;g}` - a breakpoint
                // at ROM 0x3C9E8 that force-writes RAM 0x1a4e=1 then resumes.
                // 0x3C9E8 is already bank<<16|offset (bank 3, offset 0xC9E8),
                // same convention as full_pc() elsewhere in this file, and
                // maincpu.pb is the same word-addressed RAM space used
                // throughout this port - no unit conversion needed for
                // either side. Still requires the real input combo on top
                // (hold Left+Select before the Hasbro screen for the chime,
                // release, then Up, Down, Menu, Cancel in that exact order)
                // - that part is genuine ROM logic, unaffected by this hook.
                if (full_pc() == 0x03C9E8) {
                    ram[0x1A4E] = 1;
                }
                // PlaySoundEffect's real prologue (verified via disassembly):
                // push bp,sp; sp-=2; bp=sp+1; r1=bp+5; r2=[bp+5]. At
                // 0x04D856 (right after that last instruction executes),
                // r2 holds the sound id argument (p0). 0x59 is the chime -
                // used to time the release relative to the real threshold
                // instead of a guessed fixed duration.
                if (full_pc() == 0x04D856) {
                    uint16_t sound_id = cpu.get_r(unsp_12_device::REG_R2);
                    if (sound_id == 0x59 && g_test_menu_seq_active && g_test_menu_chime_frame < 0) {
                        g_test_menu_chime_frame = g_test_menu_seq_frame;
                    }
                }
                // Real "Exit Test Mode" (PowerDownHardware, ROM 0x04F658)
                // then spins forever - real hardware just turns off. Restore
                // the pre-test-mode EEPROM snapshot (undoing TestRomChecksum's
                // real erase-everything side effect) and reboot instead of
                // hanging, so the player gets their save back and lands on
                // the main menu like turning the device back on would.
                if (full_pc() == 0x04F658 && g_test_mode_backup_valid) {
                    memcpy(eeprom_data, g_test_mode_eeprom_backup, sizeof(eeprom_data));
                    eeprom_save();
                    g_test_mode_backup_valid = false;
                    memset(ram, 0, sizeof(ram));
                    memset(io, 0, sizeof(io));
                    memset(video_regs, 0, sizeof(video_regs));
                    audio_reset();
                    cpu.device_reset();
                    break;
                }

                // MYSTERY ISLAND ACCESS - TABLED, not fixed. Root cause
                // narrowed to g_activeStoryObjectId (ram[0x1AA5]) needing to
                // read 0x68 near the dock trigger, but that only happens if
                // a minipet was already tracked BEFORE the room last loaded
                // (mod-menu spawning mid-visit doesn't retroactively update
                // it - confirmed reload/re-enter-area fixes it). A real fix
                // needs LoadRoom's exact per-area argument semantics (3
                // unexplained args beyond room id + position), which needs
                // more live MAME tracing - see session notes. Workaround:
                // spawn a minipet via mod menu, then leave and re-enter the
                // area before traveling.

                cpu.step(1);
                long cycles_this_instr = 1 - cpu.icount();
                cycle_budget -= cycles_this_instr;

                // ROOT CAUSE of the ~10% slow-music tempo bug: audio used to
                // be generated in one big lump-sum batch AFTER this whole
                // cpu.step() loop finished for the frame (confirmed via
                // tempo_check.log/audio_rate_check.log that BOTH sim-tick
                // pacing and raw sample-generation RATE were already exactly
                // correct - ratio 1.0000/0.9998 - so the bug isn't a
                // throughput/pacing miscalculation at all). The real problem
                // was ORDER: ~1172 samples generate per frame, easily
                // spanning many full BEAT_BASE_COUNT cycles, so many
                // separate "beat expired" IRQ4 assertions could fire back to
                // back while the CPU wasn't running at all (it had already
                // used up its cycle budget for the frame) - the CPU would
                // only ever see "IRQ4 currently asserted" ONCE it resumed
                // next frame, coalescing what should have been several
                // separate Music_SequencerTick() calls (via IrqHandlerAudio)
                // into far fewer, under-advancing the sequencer. Generating
                // exactly one sample per 384 CPU cycles, interleaved here,
                // matches real hardware's own clock relationship (a
                // TIMER_CALLBACK_MEMBER on a genuine per-cycle-derived timer
                // in MAME, not a per-frame batch) and lets the very next
                // cpu.step() call see each beat/IRQ promptly and
                // individually, the same way real silicon does.
                audio_cycle_debt += (double)cycles_this_instr;
                while (audio_cycle_debt >= 384.0) {
                    audio_cycle_debt -= 384.0;
                    int16_t one_sample[2];
                    generate_audio_frame(one_sample, 1);
                    audio_queue_push(one_sample, 1);
                }

                // Neuter the real hardware's auto-power-off idle timer at its
                // actual source, instead of reactively detecting the hang it
                // used to cause. WaitForNextTick (ROM 0x01d2b9) computes idle
                // time via CheckIdleTimeElapsed, compares it against a
                // 480-second (8 minute) threshold with FloatCompare, and
                // calls IdleTimeoutScreen (0x01bf0e) when it's exceeded -
                // which on real hardware halts forever in a genuine self-jump
                // at 0x01bf49, waiting for a physical power button. Real
                // hardware needs that; a PC app the user closes with a normal
                // window control does not - there's no separate "off" state
                // to preserve. FloatCompare's result lands in r1 (raw
                // disassembly: "call 0x04f85c; sp+=4; cmp r1,0x1; jg
                // 0x01d320 /*IdleTimeoutScreen*/"), so forcing r1<=1 right
                // before that compare - same register-fake technique as the
                // minipet despawn fix, not a persistent memory write -
                // makes the jg never fire. IdleTimeoutScreen's own body,
                // the self-jump inside it, and the eeprom-safe full-reset
                // recovery this replaced are now all permanently
                // unreachable, so removed rather than left as dead code.
                if (full_pc() == 0x01D31C) {
                    cpu.set_r(unsp_12_device::REG_R1, 0);
                }

                // Quit-freeze fix (screen frozen, music still playing,
                // reported after hitting Quit). Ground-truthed via a PC
                // sampling trace: execution gets permanently stuck spinning
                // inside WaitForNextTick's vblank-tick wait (ROM
                // 0x01d2ce-0x01d2e0), which busy-waits on ram[0x13]/[0x14]
                // advancing - a counter only IrqHandlerVideo increments,
                // gated on video IRQ enable (MMIO 0x2862 bit0, see
                // check_video_irq() above). WaitForNextTick itself
                // conditionally calls Link_UpdateAndSync (0x046d9b) first,
                // which under 3 GPIO-pin conditions on 0x3D01 (bits
                // 0x400/0x100/0x200 - link-cable-detect pins, distinct from
                // the low 7 button bits this port emulates on the same
                // address) proceeds into Cart_WriteBytes. That function
                // calls Cart_SuspendAudioForTiming (0x0469fb, saves+zeroes
                // 0x2862 among others) but - confirmed via full disassembly
                // of its single exit path at 0x046d0d - never calls the
                // matching Cart_RestoreAudioAfterTiming (0x046a29): video
                // IRQs stay disabled forever once this runs, so the very
                // next WaitForNextTick call hangs permanently. On real
                // hardware with no link cable physically connected, those
                // GPIO pins float/pull to a state that fails the checks and
                // bails out before Suspend is ever called; this port's GPIO
                // emulation for that address apparently doesn't reproduce
                // that idle state, so the ROM incorrectly believes a link
                // partner is present. Rather than rework link-cable GPIO
                // emulation (this port doesn't implement real inter-device
                // linking anyway), force the final gate check to always
                // fail closed - same register-fake technique as the
                // idle-timeout fix above: 0x046c3f is "cmp r1,0" testing
                // the 0x200 bit already masked into r1 by the preceding
                // instruction, so zeroing r1 here guarantees the safe
                // no-link-partner bailout every time, matching real
                // hardware's default (unplugged) behavior without touching
                // button input or any other GPIO bit on this address.
                if (full_pc() == 0x046c3f) {
                    cpu.set_r(unsp_12_device::REG_R1, 0);
                }

                // GeneratePaletteBlendTable (0x01cdb7, called from
                // WaitForNextTick while a fade is active) waits for the
                // vblank tick counter (ram 0x13/0x14) to change before each
                // of 256 colors - correct on real hardware, where a vblank
                // IRQ can preempt at any instruction boundary mid-spin. This
                // port only advances that tick once per fully-exhausted
                // per-frame cycle budget, so a wait that re-snapshots and
                // re-enters within the same budget window could never see
                // it change (confirmed empirically: 7400+ spins with zero
                // progress in one captured frame). Forcing the loop's own
                // "did it change" comparison (0x01ce31, "cmp r1,r3") to read
                // as unequal lets the ROM's own completion logic drive the
                // outcome exactly as if a tick had arrived - real, verified
                // fix for this specific architectural mismatch, independent
                // of the Quit-freeze below.
                if (full_pc() == 0x01CE31) {
                    cpu.set_r(unsp_12_device::REG_R1, cpu.get_r(unsp_12_device::REG_R3) + 1);
                }
            }

            // Room/area transition detection - see LOCATION_ID_ADDR comment
            // above. GAME_CURRENT_AREA_INDEX_ADDR (0x21E3) is NOT used here
            // despite the name/comment inherited from an earlier, unverified
            // recovery pass: Ghidra shows every reference to it is a READ
            // from per-slot helpers (SetAreaPositionX/Y, GetSlotParam1/2,
            // DrawActiveSlotSprites) with zero writers in ROM - it's a
            // transient "active sprite/object slot" index, not a room ID,
            // and changes on essentially every tick regardless of the
            // player's own movement. Using it here made location_transitioned
            // true almost constantly, permanently suppressing the speed
            // cheat. Location ID + the raw per-tick delta clamp below are
            // the real safety net.
            uint16_t current_location_id = ram[LOCATION_ID_ADDR];
            bool location_transitioned = g_have_last_location_id && current_location_id != g_last_location_id;
            if (location_transitioned) g_location_change_grace_frames = 2;
            bool suppress_amplify = g_location_change_grace_frames > 0;
            if (g_location_change_grace_frames > 0) g_location_change_grace_frames--;
            g_last_location_id = current_location_id; g_have_last_location_id = true;

            // Movement speed cheat: amplify whatever delta the game's own
            // movement code just applied to the player's world position this
            // tick, rather than faking extra input presses (which the ROM's
            // own per-tick movement/animation logic isn't built to receive
            // more than one of). Suppressed across room/area transitions
            // (see above) and clamped against an already-huge raw delta as a
            // second safety net - either would otherwise fling the player
            // outside the new room's valid bounds instead of just moving
            // them faster.
            int speed_mult = MOVEMENT_SPEED_LEVELS[g_movement_speed_idx];
            int16_t cur_x = (int16_t)ram[PLAYER_WORLD_X];
            int16_t cur_y = (int16_t)ram[PLAYER_WORLD_Y];
            if (g_have_last_player_pos && speed_mult > 1 && !suppress_amplify) {
                int16_t dx = cur_x - g_last_player_x;
                int16_t dy = cur_y - g_last_player_y;
                if (abs((int)dx) > MOVEMENT_TELEPORT_THRESHOLD) dx = 0;
                if (abs((int)dy) > MOVEMENT_TELEPORT_THRESHOLD) dy = 0;
                if (g_minipet_spawned && (dx != 0 || dy != 0)) {
                    // Backfill the intermediate normal-speed steps this
                    // amplified jump is skipping over, so the minipet's
                    // trail-following distance stays constant regardless of
                    // speed multiplier - see minipet_trail_record_step().
                    //
                    // Capped against the real trail buffer's current free
                    // space (same formula as GetMiniPetTrailFreeSpace, ROM
                    // 0x045f3e): the buffer only has 60 slots and the ROM's
                    // own read side only advances 1-2 slots/tick regardless
                    // of player speed, so writing more backfill entries per
                    // tick than that lets the write side lap the read side
                    // and corrupt the circular buffer - the "minipet just
                    // keeps teleporting" symptom above ~2x. Keeping a 30-slot
                    // margin above the real 24-slot fallback threshold means
                    // our own bulk writes never spuriously trip Follow's
                    // "fall back to zap-in" check either.
                    uint16_t read_idx = ram[MINIPET_TRAIL_READ_IDX_ADDR];
                    uint16_t write_idx = ram[MINIPET_TRAIL_WRITE_IDX_ADDR];
                    int free_space = (read_idx <= write_idx) ? (0x3D - (write_idx - read_idx)) : ((read_idx - write_idx) + 1);
                    int safe_budget = free_space - 30;
                    if (safe_budget < 0) safe_budget = 0;
                    int steps = std::min(speed_mult - 1, safe_budget);
                    for (int step = 1; step <= steps; step++) {
                        minipet_trail_record_step((int16_t)(g_last_player_x + dx * step), (int16_t)(g_last_player_y + dy * step));
                    }
                }
                if (dx != 0) cur_x = (int16_t)(cur_x + dx * (speed_mult - 1));
                if (dy != 0) cur_y = (int16_t)(cur_y + dy * (speed_mult - 1));
                ram[PLAYER_WORLD_X] = (uint16_t)cur_x;
                ram[PLAYER_WORLD_Y] = (uint16_t)cur_y;
            }
            g_last_player_x = cur_x;
            g_last_player_y = cur_y;
            g_have_last_player_pos = true;

            // Snapshot the sprite table once per completed tick - the pair
            // of snapshots either side of "now" is what render interpolation
            // below blends between using the leftover sim_accumulator
            // fraction.
            g_interp_prev = g_interp_curr;
            for (int i = 0; i < SPRITE_SLOT_COUNT; i++) {
                g_interp_curr.tile[i] = ram[SPRITE_TABLE_ADDR + i * 4 + 0];
                g_interp_curr.x[i] = (int16_t)ram[SPRITE_TABLE_ADDR + i * 4 + 1];
                g_interp_curr.y[i] = (int16_t)ram[SPRITE_TABLE_ADDR + i * 4 + 2];
                g_interp_curr.attr[i] = ram[SPRITE_TABLE_ADDR + i * 4 + 3];
            }
            g_have_interp_snapshot = true;

            g_frame++;
        }

        float frame_ms = (float)frame_time * 1000.0f;
        fps_frame_times_ms[fps_frame_index] = frame_ms;
        fps_frame_index = (fps_frame_index + 1) % FPS_WINDOW;
        if (fps_frame_count < FPS_WINDOW) fps_frame_count++;

        // Frozen stats/items: re-assert every tick so game logic (e.g.
        // Hunger ticking down over time) can't change them.
        for (int i = 0; i < CHEAT_STAT_COUNT; i++) {
            if (g_stat_frozen[i]) ram[CHEAT_STATS[i].addr] = g_stat_frozen_value[i];
        }
        for (int i = 0; i < INVENTORY_ITEM_COUNT; i++) {
            if (g_inv_frozen[i]) ram[INVENTORY_ITEMS[i].addr] = g_inv_frozen_value[i];
        }
        for (int i = 0; i < g_custom_mod_count; i++) {
            if (g_custom_mods[i].frozen) ram[g_custom_mods[i].addr] = g_custom_mods[i].frozen_value;
        }

        // Audio is generated cycle-interleaved inside the cpu.step() loop
        // above (1 sample per 384 CPU cycles, matching real hardware's own
        // clock relationship) instead of in one lump-sum batch here - see
        // the comment at the interleaved site. This just drains our
        // software queue into the OS audio stream at whatever pace it
        // wants more data.
        audio_queue_feed_stream(audio_stream);

        // Tab always works as a fallback even if the user rebinds the
        // primary key/gamepad button to something else, so a bad rebind
        // can never lock them out of the menu that would let them fix it.
        bool mod_menu_toggle_kb = IsKeyPressed(KEY_TAB) || (g_mod_menu_key != KEY_TAB && IsKeyPressed(g_mod_menu_key));
        bool mod_menu_toggle_gp = IsGamepadAvailable(0) && IsGamepadButtonPressed(0, g_mod_menu_gamepad);
        if ((mod_menu_toggle_kb || mod_menu_toggle_gp) && g_editing_row < 0 && g_awaiting_keybind_for < 0) g_mod_menu_open = !g_mod_menu_open;
        if (g_mod_menu_open) {
            // Same fixed virtual UI space the stat HUD/FPS counter already
            // use (see g_hud_cam, computed once at the top of this loop),
            // NOT the real window size - everything drawn under
            // BeginMode2D(g_hud_cam) is scaled from this space into
            // whatever the actual window/fullscreen size
            // is, so feeding this the real size double-scales/mispositions
            // it once that differs from the default (e.g. fullscreen).
            compute_mod_menu_layout((int)(WIDE_W * DEFAULT_WINDOW_SCALE), (int)(NATIVE_H * DEFAULT_WINDOW_SCALE));

            if (g_editing_row >= 0) {
                int ch;
                while ((ch = GetCharPressed()) != 0) {
                    if (ch >= '0' && ch <= '9' && g_edit_buffer_len < (int)sizeof(g_edit_buffer) - 1) {
                        g_edit_buffer[g_edit_buffer_len++] = (char)ch;
                        g_edit_buffer[g_edit_buffer_len] = '\0';
                    }
                }
                if (IsKeyPressed(KEY_BACKSPACE) && g_edit_buffer_len > 0) {
                    g_edit_buffer_len--;
                    g_edit_buffer[g_edit_buffer_len] = '\0';
                }
                bool gp_ok_edit = IsGamepadAvailable(0);
                if (IsKeyPressed(KEY_ENTER) || (gp_ok_edit && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN))) commit_edit_row();
                // Typing a custom number still needs a real keyboard (no
                // on-screen keypad), but a controller-only player must be
                // able to back out of this prompt without one - same fixed
                // "Right Face = cancel/back" convention as the dropdown and
                // rebind-capture cancel below.
                if (IsKeyPressed(KEY_ESCAPE) || (gp_ok_edit && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT))) cancel_edit_row();
            } else if (g_custom_add_phase != CUSTOM_ADD_NONE) {
                // Two-step add flow: type a hex address, Enter, then type a
                // name, Enter. Same GetCharPressed() text-capture pattern as
                // the numeric edit box above, just with a different allowed
                // character set per phase (hex digits vs. any printable
                // character for the name).
                int ch;
                while ((ch = GetCharPressed()) != 0) {
                    bool allowed = (g_custom_add_phase == CUSTOM_ADD_ADDRESS)
                        ? ((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F'))
                        : (ch >= 32 && ch <= 125);
                    if (allowed && g_custom_add_buffer_len < (int)sizeof(g_custom_add_buffer) - 1) {
                        g_custom_add_buffer[g_custom_add_buffer_len++] = (char)ch;
                        g_custom_add_buffer[g_custom_add_buffer_len] = '\0';
                    }
                }
                if (IsKeyPressed(KEY_BACKSPACE) && g_custom_add_buffer_len > 0) {
                    g_custom_add_buffer_len--;
                    g_custom_add_buffer[g_custom_add_buffer_len] = '\0';
                }
                if (IsKeyPressed(KEY_ENTER) && g_custom_add_buffer_len > 0) {
                    if (g_custom_add_phase == CUSTOM_ADD_ADDRESS) {
                        unsigned long parsed = strtoul(g_custom_add_buffer, nullptr, 16);
                        if (parsed >= sizeof(ram) / sizeof(ram[0])) parsed = sizeof(ram) / sizeof(ram[0]) - 1;
                        g_custom_add_pending_addr = (uint16_t)parsed;
                        g_custom_add_phase = CUSTOM_ADD_NAME;
                        g_custom_add_buffer[0] = 0; g_custom_add_buffer_len = 0;
                    } else if (g_custom_mod_count < MAX_CUSTOM_MODS) {
                        CustomModEntry& e = g_custom_mods[g_custom_mod_count++];
                        strncpy(e.name, g_custom_add_buffer, sizeof(e.name) - 1); e.name[sizeof(e.name) - 1] = 0;
                        e.addr = g_custom_add_pending_addr;
                        e.favorite = false; e.frozen = false; e.frozen_value = 0;
                        save_custom_mods();
                        g_custom_add_phase = CUSTOM_ADD_NONE;
                    } else {
                        g_custom_add_phase = CUSTOM_ADD_NONE; // list full, silently drop
                    }
                }
                if (IsKeyPressed(KEY_ESCAPE)) g_custom_add_phase = CUSTOM_ADD_NONE;
            } else if (row_is_custom(g_mod_menu_selection) && IsKeyPressed(KEY_DELETE)) {
                // Remove the selected custom entry - shift the rest down to
                // keep the array/rows contiguous, then persist.
                int i = g_mod_menu_selection - ROW_CUSTOM_START;
                for (int j = i; j < g_custom_mod_count - 1; j++) g_custom_mods[j] = g_custom_mods[j + 1];
                g_custom_mod_count--;
                save_custom_mods();
            } else if (g_open_dropdown_stat >= 0) {
                // Preset dropdown open: only its own option list is live -
                // any click (hit or miss) closes it, matching normal
                // dropdown UX.
                Vector2 mouse = GetScreenToWorld2D(GetMousePosition(), g_hud_cam);
                const CheatStatRow& row = CHEAT_STATS[g_open_dropdown_stat];
                int opt_h = 20;
                Rectangle list_rect{ g_dropdown_anchor.x, g_dropdown_anchor.y + g_dropdown_anchor.height,
                                      240, (float)(row.preset_count * opt_h) };
                if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                    if (CheckCollisionPointRec(mouse, list_rect)) {
                        int option = (int)((mouse.y - list_rect.y) / opt_h);
                        if (option >= 0 && option < row.preset_count) {
                            set_stat_value(g_open_dropdown_stat, row.presets[option].value);
                        }
                    }
                    g_open_dropdown_stat = -1;
                }
                bool gp_ok_dd = IsGamepadAvailable(0);
                if (IsKeyPressed(KEY_DOWN) || (gp_ok_dd && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_DOWN))) {
                    g_dropdown_gp_index = (g_dropdown_gp_index + 1) % row.preset_count;
                }
                if (IsKeyPressed(KEY_UP) || (gp_ok_dd && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_UP))) {
                    g_dropdown_gp_index = (g_dropdown_gp_index - 1 + row.preset_count) % row.preset_count;
                }
                if (IsKeyPressed(KEY_ENTER) || (gp_ok_dd && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN))) {
                    set_stat_value(g_open_dropdown_stat, row.presets[g_dropdown_gp_index].value);
                    g_open_dropdown_stat = -1;
                }
                if (IsKeyPressed(KEY_ESCAPE) || (gp_ok_dd && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT))) g_open_dropdown_stat = -1;
            } else if (g_awaiting_keybind_for >= 0 && g_awaiting_gamepad_rebind) {
                // Gamepad capture: only keyboard Escape cancels - any
                // gamepad button, including whatever's bound to Back, gets
                // bound (rebinding IS the point of this capture mode).
                if (IsKeyPressed(KEY_ESCAPE)) {
                    g_awaiting_keybind_for = -1;
                } else {
                    int btn = get_gamepad_button_pressed(0);
                    if (btn >= 0) {
                        if (g_awaiting_keybind_for == GAME_BUTTON_COUNT) {
                            g_mod_menu_gamepad = btn;
                            save_gamepad_binds();
                        } else {
                            g_gamepad_binding[g_awaiting_keybind_for] = btn;
                            save_gamepad_binds();
                        }
                        g_awaiting_keybind_for = -1;
                    }
                }
            } else if (g_awaiting_keybind_for >= 0) {
                // Capture the next key pressed and bind it, whatever it is -
                // except Escape/Tab, which are reserved (Tab already toggles
                // this menu by default - still true even after rebinding,
                // since Tab stays reserved as a safety net) and instead
                // cancel the rebind.
                int key = GetKeyPressed();
                if (key == KEY_ESCAPE || key == KEY_TAB) {
                    g_awaiting_keybind_for = -1;
                } else if (key != 0) {
                    if (g_awaiting_keybind_for == GAME_BUTTON_COUNT) {
                        g_mod_menu_key = key;
                        save_keybinds();
                    } else {
                        g_key_binding[g_awaiting_keybind_for] = key;
                        save_keybinds();
                    }
                    g_awaiting_keybind_for = -1;
                }
            } else {
                // Full controller navigation uses a fixed D-Pad convention
                // (like the existing confirm button below), not the
                // player's own remapped in-game bindings - the mod menu is
                // host-side UI, so it should navigate the same way
                // regardless of how they've customized actual gameplay
                // controls.
                bool gp_ok = IsGamepadAvailable(0);
                bool nav_down = IsKeyPressed(KEY_DOWN) || (gp_ok && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_DOWN));
                bool nav_up = IsKeyPressed(KEY_UP) || (gp_ok && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_UP));
                if (nav_down || nav_up) {
                    int dir = nav_down ? 1 : -1;
                    int pos = 0;
                    for (int i = 0; i < g_mod_menu_layout.logical_count; i++) {
                        if (g_mod_menu_layout.logical_order[i] == g_mod_menu_selection) { pos = i; break; }
                    }
                    pos = (pos + dir + g_mod_menu_layout.logical_count) % g_mod_menu_layout.logical_count;
                    g_mod_menu_selection = g_mod_menu_layout.logical_order[pos];
                    // Keep the new selection inside the visible scroll window
                    // (takes effect next frame's compute_mod_menu_layout).
                    if (pos < g_mod_menu_scroll) g_mod_menu_scroll = pos;
                    else if (pos >= g_mod_menu_scroll + g_mod_menu_layout.main_row_count) {
                        g_mod_menu_scroll = pos - g_mod_menu_layout.main_row_count + 1;
                    }
                }
                if (IsKeyPressed(KEY_PAGE_DOWN) || (gp_ok && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_TRIGGER_1))) g_mod_menu_scroll += 10;
                if (IsKeyPressed(KEY_PAGE_UP) || (gp_ok && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_TRIGGER_1))) g_mod_menu_scroll -= 10;
                if (IsKeyPressed(KEY_LEFT) || (gp_ok && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_LEFT))) adjust_row(g_mod_menu_selection, -1);
                if (IsKeyPressed(KEY_RIGHT) || (gp_ok && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_RIGHT))) adjust_row(g_mod_menu_selection, 1);
                // Menu confirm accepts either Enter (keyboard) or the
                // gamepad's bottom face button (a fixed, universal menu-
                // navigation convention - independent of the player's own
                // remapped in-game Select binding), so a controller-only
                // player can also open a Controls row's capture prompt.
                bool confirm_kb = IsKeyPressed(KEY_ENTER);
                bool confirm_gp = IsGamepadAvailable(0) && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN);
                if (confirm_kb || confirm_gp) {
                    if (row_has_value(g_mod_menu_selection)) begin_edit_row(g_mod_menu_selection);
                    else if (g_mod_menu_selection == ROW_CUSTOM_ADD) {
                        g_custom_add_phase = CUSTOM_ADD_ADDRESS;
                        g_custom_add_buffer[0] = 0; g_custom_add_buffer_len = 0;
                    }
                    else if (row_is_expand_header(g_mod_menu_selection)) adjust_row(g_mod_menu_selection, 1);
                    else if (row_is_keybind(g_mod_menu_selection)) {
                        g_awaiting_keybind_for = g_mod_menu_selection - ROW_CONTROLS_START;
                        g_awaiting_gamepad_rebind = confirm_gp && !confirm_kb;
                    }
                    else if (g_mod_menu_selection == ROW_MOD_MENU_KEY) {
                        g_awaiting_keybind_for = GAME_BUTTON_COUNT;
                        g_awaiting_gamepad_rebind = confirm_gp && !confirm_kb;
                    }
                }

                Vector2 mouse = GetScreenToWorld2D(GetMousePosition(), g_hud_cam);
                // Mouse wheel always scrolls the list (never adjusts a row's
                // value - use the chevrons or type a value for that)
                // whenever hovering anywhere over either panel.
                float wheel = GetMouseWheelMove();
                if (wheel != 0.0f && (CheckCollisionPointRec(mouse, g_mod_menu_layout.main_panel)
                                       || CheckCollisionPointRec(mouse, g_mod_menu_layout.fav_panel)
                                       || CheckCollisionPointRec(mouse, g_mod_menu_layout.frozen_panel))) {
                    g_mod_menu_scroll -= (int)(wheel * 3);
                }

                ModMenuRowRect* row_sets[2] = { g_mod_menu_layout.main_rows, g_mod_menu_layout.fav_rows };
                int row_counts[2] = { g_mod_menu_layout.main_row_count, g_mod_menu_layout.fav_row_count };
                for (int set = 0; set < 2; set++) {
                    for (int i = 0; i < row_counts[set]; i++) {
                        ModMenuRowRect& r = row_sets[set][i];
                        if (!CheckCollisionPointRec(mouse, r.full)) continue;
                        g_mod_menu_selection = r.row_index;
                        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                            if (r.row_index == ROW_CUSTOM_ADD) {
                                g_custom_add_phase = CUSTOM_ADD_ADDRESS;
                                g_custom_add_buffer[0] = 0; g_custom_add_buffer_len = 0;
                            } else if (row_is_expand_header(r.row_index)) {
                                adjust_row(r.row_index, 1);
                            } else if (row_is_keybind(r.row_index)) {
                                g_awaiting_keybind_for = r.row_index - ROW_CONTROLS_START;
                                // Column detection: keyboard name draws at
                                // x+155, gamepad at x+215 (draw_mod_menu_row) -
                                // clicking past the gamepad column's start
                                // rebinds that side instead of keyboard.
                                g_awaiting_gamepad_rebind = mouse.x >= r.full.x + 215;
                            } else if (r.row_index == ROW_MOD_MENU_KEY) {
                                g_awaiting_keybind_for = GAME_BUTTON_COUNT;
                                g_awaiting_gamepad_rebind = mouse.x >= r.full.x + 215;
                            } else {
                                bool has_value = row_has_value(r.row_index);
                                bool has_preset = !row_is_inv_item(r.row_index) && r.row_index >= ROW_STATS_START
                                    && CHEAT_STATS[r.row_index - ROW_STATS_START].preset_count > 0;
                                if (has_value && CheckCollisionPointRec(mouse, r.fav)) toggle_favorite_row(r.row_index);
                                else if (has_value && CheckCollisionPointRec(mouse, r.freeze)) toggle_freeze_row(r.row_index);
                                else if (has_preset && CheckCollisionPointRec(mouse, r.preset_btn)) {
                                    g_open_dropdown_stat = r.row_index - ROW_STATS_START;
                                    g_dropdown_anchor = r.preset_btn;
                                    g_dropdown_gp_index = 0;
                                }
                                else if (CheckCollisionPointRec(mouse, r.arrow_ll)) adjust_row(r.row_index, -10);
                                else if (CheckCollisionPointRec(mouse, r.arrow_l)) adjust_row(r.row_index, -1);
                                else if (CheckCollisionPointRec(mouse, r.arrow_r)) adjust_row(r.row_index, 1);
                                else if (CheckCollisionPointRec(mouse, r.arrow_rr)) adjust_row(r.row_index, 10);
                                else if (has_value && CheckCollisionPointRec(mouse, r.value)) begin_edit_row(r.row_index);
                            }
                        }
                    }
                }
            }
        }

        // Render interpolation: temporarily substitute blended sprite
        // positions in place of the real (current-tick) values, build the
        // framebuffer with draw_page/draw_sprites completely unchanged, then
        // restore the real values immediately after - same "substitute, use,
        // restore" shape used elsewhere in this file (e.g. frozen stats),
        // safe here since it's plain data, no code execution involved.
        uint16_t saved_sprite_xy[SPRITE_SLOT_COUNT * 2];
        bool did_interp_substitute = g_interp_enabled && g_have_interp_snapshot;
        if (did_interp_substitute) {
            for (int i = 0; i < SPRITE_SLOT_COUNT; i++) {
                saved_sprite_xy[i * 2 + 0] = ram[SPRITE_TABLE_ADDR + i * 4 + 1];
                saved_sprite_xy[i * 2 + 1] = ram[SPRITE_TABLE_ADDR + i * 4 + 2];
            }

            float alpha = (float)(sim_accumulator / SIM_DT);
            if (alpha < 0.0f) alpha = 0.0f;
            if (alpha > 1.0f) alpha = 1.0f;

            for (int i = 0; i < SPRITE_SLOT_COUNT; i++) {
                // Tile+attr matching, not a position-delta continuity check -
                // confirmed by testing: the latter caused broad jitter
                // (including on fully static objects) from an unconfirmed
                // walk-cycle-animation-snapping theory. Don't reintroduce
                // that without new evidence it's needed.
                bool same_object = g_interp_curr.tile[i] != 0
                    && g_interp_curr.tile[i] == g_interp_prev.tile[i]
                    && g_interp_curr.attr[i] == g_interp_prev.attr[i];
                int16_t ix = g_interp_curr.x[i], iy = g_interp_curr.y[i];
                if (same_object) {
                    int16_t dx = (int16_t)(g_interp_curr.x[i] - g_interp_prev.x[i]);
                    int16_t dy = (int16_t)(g_interp_curr.y[i] - g_interp_prev.y[i]);
                    ix = (int16_t)((float)g_interp_prev.x[i] + (float)dx * alpha);
                    iy = (int16_t)((float)g_interp_prev.y[i] + (float)dy * alpha);
                }
                ram[SPRITE_TABLE_ADDR + i * 4 + 1] = (uint16_t)ix;
                ram[SPRITE_TABLE_ADDR + i * 4 + 2] = (uint16_t)iy;
            }
            // Background scroll is deliberately NOT interpolated (confirmed
            // by testing - the floor visibly jittered while the sprite-
            // interpolated player looked correct). This game streams new
            // tile data into the edges of a toroidal tile buffer once per
            // real tick, keyed to that tick's exact scroll position - our
            // fractional in-between scroll values don't have correctly-
            // streamed tile content to go with them, so smoothing the
            // scroll register was showing mismatched edge tiles right as
            // new columns streamed in. Sprites have no such dependency.
        }

        for (int i = 0; i < WIDE_W * NATIVE_H; i++) framebuffer[i] = Color{0, 0, 0, 0};

        for (int priority = 0; priority < 4; priority++) {
            draw_page(framebuffer, WIDE_W, WIDE_MARGIN_L, WIDE_MARGIN_R, 2, &video_regs[0x18], &video_regs[0x16], video_regs[0x21], priority);
            draw_page(framebuffer, WIDE_W, WIDE_MARGIN_L, WIDE_MARGIN_R, 1, &video_regs[0x12], &video_regs[0x10], video_regs[0x20], priority);

            draw_sprites(framebuffer, WIDE_W, WIDE_MARGIN_L, priority);
        }

        if (did_interp_substitute) {
            for (int i = 0; i < SPRITE_SLOT_COUNT; i++) {
                ram[SPRITE_TABLE_ADDR + i * 4 + 1] = saved_sprite_xy[i * 2 + 0];
                ram[SPRITE_TABLE_ADDR + i * 4 + 2] = saved_sprite_xy[i * 2 + 1];
            }
        }

        // Opt-in hidden test-menu auto-unlock. Forces a real reset (same
        // path the watchdog uses) so the sequence below runs against a
        // guaranteed-fresh boot, then drives the exact real Left+Select
        // hold / Up,Down,Menu,Back sequence automatically - see the
        // g_test_menu_seq_active GPIO override and the full_pc()==0x3C9E8
        // RAM-arm hook for the two halves of this.
        if (IsKeyPressed(KEY_F9)) {
            memcpy(g_test_mode_eeprom_backup, eeprom_data, sizeof(eeprom_data));
            g_test_mode_backup_valid = true;
            memset(ram, 0, sizeof(ram));
            memset(io, 0, sizeof(io));
            memset(video_regs, 0, sizeof(video_regs));
            audio_reset();
            cpu.device_reset();
            g_test_menu_seq_active = true;
            g_test_menu_seq_frame = 0;
            g_test_menu_chime_frame = -1;
        }

        if (IsKeyPressed(KEY_F8)) {
            FILE* sf = fopen("sprite_dump.log", "w");
            if (sf) {
                fprintf(sf, "spritegfx_addr_reg(0x22)=0x%04X video_regs42=0x%04X\n", video_regs[0x22], video_regs[0x42]);
                uint32_t sprite_addr = 0x2C00;
                for (int i = 0; i < 256; i++) {
                    uint16_t tile = ram[sprite_addr + i * 4 + 0];
                    if (!tile) continue;
                    int16_t x = (int16_t)ram[sprite_addr + i * 4 + 1];
                    int16_t y = (int16_t)ram[sprite_addr + i * 4 + 2];
                    uint16_t attr = ram[sprite_addr + i * 4 + 3];
                    fprintf(sf, "slot=%d tile=0x%04X x=%d y=%d attr=0x%04X prio=%d bpp=%d w=%d h=%d\n",
                        i, tile, x, y, attr, (attr & 0x3000) >> 12, attr & 0x3,
                        8 << ((attr & 0x0030) >> 4), 8 << ((attr & 0x00c0) >> 6));
                }
                fclose(sf);
            }

            static std::set<std::string> exported_clusters;
            export_visible_sprite_clusters("extracted_sprites", exported_clusters);
        }

        // Continuous live sprite capture (F7 toggles on/off). Same export
        // logic as F8 above, but re-run every rendered frame while active,
        // into its own folder + dedup set so it doesn't mix with F8's
        // one-shot output. Dedup set is cleared each time capture starts,
        // so re-toggling begins a fresh session instead of silently
        // skipping objects already seen in a previous capture.
        static std::set<std::string> live_capture_exported;
        if (IsKeyPressed(KEY_F11)) toggle_expand_window();
        if (IsKeyPressed(KEY_F7)) {
            g_live_sprite_capture = !g_live_sprite_capture;
            if (g_live_sprite_capture) {
                live_capture_exported.clear();
                std::filesystem::create_directory("sprite_capture");
            }
        }
        if (g_live_sprite_capture) {
            export_visible_sprite_clusters("sprite_capture", live_capture_exported);
        }

        UpdateTexture(screen_texture, framebuffer);
        BeginDrawing();
        ClearBackground(BLACK);

        int sw = GetScreenWidth();
        int sh = GetScreenHeight();
        float scale = std::min((float)sw / WIDE_W, (float)sh / NATIVE_H);
        // Crisp is point-sampled (no blending at all - that's what makes it
        // "no filter"), so at a non-integer scale it's forced to make some
        // source pixels cover one more/fewer screen pixel than their
        // neighbors - not a filter artifact, just what zero interpolation
        // means when the math doesn't divide evenly. Snapping to the
        // largest integer multiple that still fits gives every native
        // pixel a perfectly uniform square block, at the cost of a thin
        // letterboxed border instead of an exact fill.
        if (g_render_filter == FILTER_CRISP && scale > 1.0f) scale = (float)(int)scale;
        float destW = WIDE_W * scale;
        float destH = NATIVE_H * scale;
        float offsetX = (sw - destW) / 2.0f;
        float offsetY = (sh - destH) / 2.0f;

        // Crisp already snaps the RENDERED content to an exact integer
        // multiple so every pixel is a uniform block - but the window
        // itself was left at whatever size it already was, so that
        // leftover fractional space still showed up as a black border.
        // Snapping the actual OS window to match removes it outright.
        // IsWindowResized() fires on every intermediate frame of a live
        // drag, not just once at the end - snapping immediately on every
        // one of those fought the drag itself, making the window unable
        // to grow past its current integer multiple (it kept getting
        // floored back mid-drag before reaching the next size up).
        // Debouncing: (re)start a short timer on every resize event, and
        // only actually snap once it counts down without being reset
        // again - i.e. once dragging has actually paused. Also fires
        // once right when switching into Crisp, and never while fullscreen
        // OR maximized (a fixed computed size makes no sense for either -
        // maximize was getting treated as just another resize, so it
        // un-maximized the window down to the snapped size, and since a
        // resize doesn't reposition the window, it stayed pinned wherever
        // maximize had left it instead of re-centering - looked like the
        // window "snapped to the corner").
        {
            static float resize_settle_timer = -1.0f;
            static int last_render_filter_for_resize = -1;
            bool switched_to_crisp = (g_render_filter == FILTER_CRISP && last_render_filter_for_resize != FILTER_CRISP);
            last_render_filter_for_resize = g_render_filter;
            if (g_render_filter == FILTER_CRISP && !IsWindowFullscreen() && !IsWindowMaximized()) {
                if (IsWindowResized() || switched_to_crisp) resize_settle_timer = 0.25f;
                if (resize_settle_timer > 0.0f) {
                    resize_settle_timer -= (float)frame_time;
                    if (resize_settle_timer <= 0.0f) {
                        int snapW = (int)destW, snapH = (int)destH;
                        if (snapW > 0 && snapH > 0 && (snapW != sw || snapH != sh)) SetWindowSize(snapW, snapH);
                    }
                }
            } else {
                resize_settle_timer = -1.0f;
            }
        }

        bool use_crt = g_render_filter == FILTER_CRT;
        bool use_sharp = g_render_filter == FILTER_SHARP;
        if (use_crt) {
            float output_size[2] = { destW, destH };
            SetShaderValue(crt_shader, crt_output_size_loc, output_size, SHADER_UNIFORM_VEC2);
            BeginShaderMode(crt_shader);
        } else if (use_sharp) {
            float source_size[2] = { (float)WIDE_W, (float)NATIVE_H };
            float output_scale[2] = { destW / WIDE_W, destH / NATIVE_H };
            SetShaderValue(sharp_shader, sharp_source_size_loc, source_size, SHADER_UNIFORM_VEC2);
            SetShaderValue(sharp_shader, sharp_output_scale_loc, output_scale, SHADER_UNIFORM_VEC2);
            BeginShaderMode(sharp_shader);
        }
        
        DrawTexturePro(screen_texture, Rectangle{0, 0, (float)WIDE_W, (float)NATIVE_H}, Rectangle{offsetX, offsetY, destW, destH}, Vector2{0, 0}, 0.0f, WHITE);

        if (use_crt || use_sharp) EndShaderMode();

        // Debug overlays + always-on stat HUD are laid out in a virtual
        // (WIDE_W*DEFAULT_WINDOW_SCALE) x (NATIVE_H*DEFAULT_WINDOW_SCALE)
        // space - i.e. the window's own default size, NOT the tiny native
        // game texture - and drawn through this camera (computed once per
        // frame at the top of the loop, and also used there to convert
        // mouse coordinates for mod-menu hit-testing) so they stay glued
        // to the letterboxed game viewport's corners and scale 1:1 at the
        // default window size, growing proportionally beyond that.
        BeginMode2D(g_hud_cam);
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
        // Quest Arrow: points toward the door leading to the current
        // quest's delivery-target area (g_contentPoolTable/0x1E68 - despite
        // Ghidra's inherited "QuestGiver" naming, this is the actual
        // delivery target, set once at quest-roll time and stable through
        // the whole quest, correctly distinguishing "bring to me" from
        // "bring to someone else" per real testing). 0x1E4F!=0xFFFF matches
        // the real game's own gate for "is a quest objective active" (same
        // check DrawQuestTurnInSummary uses before showing its own text).
        // v1: direct area connections only, via the empirically-gathered
        // DOOR_LINKS table - no arrow shown if the target area isn't
        // directly reachable from here, or if already in the target area.
        if (g_quest_arrow_enabled) {
            bool gate = ram[GAME_STATE_ADDR] == GAME_STATE_IN_ROOM && ram[0x1E4F] != 0xFFFF;
            uint16_t pool_idx = ram[0x1E68];
            uint16_t target_area = rom[0xDAEC + pool_idx * 16]; // real per-NPC area, lives in ROM not RAM
            uint16_t cur_area = ram[LOCATION_ID_ADDR];
            bool door_found = false;
            int16_t door_x = 0, door_y = 0;
            if (gate && cur_area != target_area) {
                int16_t px = (int16_t)ram[PLAYER_WORLD_X], py = (int16_t)ram[PLAYER_WORLD_Y];
                door_found = find_route_door(cur_area, target_area, px, py, &door_x, &door_y);
                if (door_found) {
                    float angle = atan2f((float)(door_y - py), (float)(door_x - px));
                    float acx = WIDE_W / 2.0f, acy = 36.0f;
                    float c = cosf(angle), s = sinf(angle);
                    // DrawTriangle/DrawTriangleFan fill would not render
                    // solid here even with culling disabled - real cause
                    // never pinned down. Angled side-pieces for the head
                    // (DrawPoly, then two angled DrawRectanglePro bars)
                    // both came out disconnected/malformed. Simplest
                    // foolproof construction: every single piece below
                    // uses the exact same rotation (angle_deg) and is
                    // placed at successive distances along that one
                    // direction line - a "staircase" of rectangles
                    // narrowing toward the tip approximates a point, with
                    // no separate angle math anywhere to get wrong or
                    // drift out of sync.
                    float angle_deg = angle * RAD2DEG;
                    float shaft_len = 14.0f, shaft_thick = 6.0f;
                    DrawRectanglePro(Rectangle{ acx, acy, shaft_len, shaft_thick }, Vector2{ 0, shaft_thick / 2 }, angle_deg, WHITE);
                    static const float SEG_W[5] = { 18, 13.5f, 9.5f, 6, 2.5f };
                    float seg_len = 3.0f, dist = shaft_len;
                    for (int i = 0; i < 5; i++) {
                        float sx = acx + c * dist, sy = acy + s * dist;
                        DrawRectanglePro(Rectangle{ sx, sy, seg_len, SEG_W[i] }, Vector2{ 0, SEG_W[i] / 2 }, angle_deg, WHITE);
                        dist += seg_len;
                    }
                }
            }
        }
        if (g_mod_menu_open) {
            Rectangle mp = g_mod_menu_layout.main_panel;
            DrawRectangle((int)mp.x, (int)mp.y, (int)mp.width, (int)mp.height, Color{0, 0, 0, 220});
            DrawRectangleLines((int)mp.x, (int)mp.y, (int)mp.width, (int)mp.height, GREEN);
            ui_draw_text(g_awaiting_keybind_for >= 0 ? "MOD MENU (Esc cancels rebind)" : "MOD MENU (Tab to close)",
                     (int)mp.x + 10, (int)mp.y + 8, 15, GREEN);
            // Fav/Freeze checkbox legend - space for this was reserved
            // (mh's +14) since whenever this was added, but the actual text
            // was never drawn, leaving the yellow box explained only in the
            // Favorites panel's empty-state hint and the blue Freeze box
            // never explained anywhere at all.
            ui_draw_text("Fav", (int)mp.x + 10, (int)mp.y + 26, 11, YELLOW);
            ui_draw_text("Freeze", (int)mp.x + 258 + s_label_extra_w + s_value_extra_w - 10, (int)mp.y + 26, 11, SKYBLUE);
            for (int i = 0; i < g_mod_menu_layout.main_row_count; i++) {
                const ModMenuRowRect& r = g_mod_menu_layout.main_rows[i];
                draw_mod_menu_row(r);
                if (g_mod_menu_layout.separator_after_row[r.row_index]) {
                    int ly = (int)(r.full.y + r.full.height - 2);
                    DrawRectangle((int)r.full.x, ly, (int)r.full.width, 2, Color{180, 180, 180, 255});
                }
            }
            if (g_mod_menu_layout.has_scroll) {
                char scroll_buf[32];
                sprintf(scroll_buf, "%d-%d / %d (PgUp/PgDn)", g_mod_menu_scroll + 1,
                        g_mod_menu_scroll + g_mod_menu_layout.main_row_count, g_mod_menu_layout.logical_count);
                ui_draw_text(scroll_buf, (int)mp.x + 10, (int)(mp.y + mp.height - 16), 11, GRAY);
            }

            Rectangle fp = g_mod_menu_layout.fav_panel;
            DrawRectangle((int)fp.x, (int)fp.y, (int)fp.width, (int)fp.height, Color{0, 0, 0, 220});
            DrawRectangleLines((int)fp.x, (int)fp.y, (int)fp.width, (int)fp.height, YELLOW);
            ui_draw_text("FAVORITES", (int)fp.x + 10, (int)fp.y + 8, 15, YELLOW);
            if (g_mod_menu_layout.fav_row_count == 0) {
                ui_draw_text("Click the yellow box\nnext to a stat to pin it here", (int)fp.x + 10, (int)fp.y + 32, 13, GRAY);
            } else {
                for (int i = 0; i < g_mod_menu_layout.fav_row_count; i++) {
                    draw_mod_menu_row(g_mod_menu_layout.fav_rows[i]);
                }
            }

            // Frozen panel - explanatory only, unlike Favorites it never
            // lists rows (freezing already shows on the item's own row via
            // the filled blue checkbox - this is just the "what does that
            // blue box mean" explanation Favorites gets from its own
            // empty-state hint).
            Rectangle zp = g_mod_menu_layout.frozen_panel;
            DrawRectangle((int)zp.x, (int)zp.y, (int)zp.width, (int)zp.height, Color{0, 0, 0, 220});
            DrawRectangleLines((int)zp.x, (int)zp.y, (int)zp.width, (int)zp.height, SKYBLUE);
            ui_draw_text("FROZEN", (int)zp.x + 10, (int)zp.y + 8, 15, SKYBLUE);
            ui_draw_text("Click the blue box\nnext to a stat to freeze it", (int)zp.x + 10, (int)zp.y + 32, 13, GRAY);

            if (g_open_dropdown_stat >= 0) {
                const CheatStatRow& row = CHEAT_STATS[g_open_dropdown_stat];
                int opt_h = 20;
                int lx = (int)g_dropdown_anchor.x, ly = (int)(g_dropdown_anchor.y + g_dropdown_anchor.height);
                int lw = 240, lh = row.preset_count * opt_h;
                DrawRectangle(lx, ly, lw, lh, Color{20, 20, 20, 240});
                DrawRectangleLines(lx, ly, lw, lh, YELLOW);
                for (int i = 0; i < row.preset_count; i++) {
                    if (i == g_dropdown_gp_index) {
                        DrawRectangle(lx, ly + i * opt_h, lw, opt_h, Color{80, 80, 0, 255});
                    }
                    char opt_buf[48];
                    sprintf(opt_buf, "%d: %s", row.presets[i].value, row.presets[i].label);
                    ui_draw_text(opt_buf, lx + 4, ly + i * opt_h + 2, 13, WHITE);
                }
            }
        }
        EndMode2D();
        bool selftest_done = g_selftest && selftest_checkpoint(g_frame, framebuffer);
        EndDrawing();
        if (selftest_done) break;
    }

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
    return 0;
}
