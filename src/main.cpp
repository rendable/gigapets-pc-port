// GigaPets PC Port - main application.
//
// Portions of this file are ported from MAME (https://github.com/mamedev/mame):
//   - SPG2xx audio:  src/devices/machine/spg2xx_audio.cpp
//       license: BSD-3-Clause, copyright-holders: Ryan Holtz, Jonathan Gevaryahu
//   - IMA ADPCM:     src/devices/sound/imaadpcm.cpp
//       license: BSD-3-Clause, copyright-holders: Andrew Gardner, Aaron Giles
//   - SPG2xx video/IO register behaviour: src/devices/machine/spg2xx_video.cpp,
//       spg2xx_io.cpp - license: BSD-3-Clause, copyright-holder: Ryan Holtz
// The unSP CPU core (unsp*.cpp/.h) is GPL-2.0+ (Segher Boessenkool, Ryan Holtz,
// David Haywood). The combined work is distributed under GPL-2.0-or-later; see
// LICENSE.

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <algorithm>
#include "raylib.h"
#include "rlgl.h"

#include "emu.h"
#include "unsp.h"

#include <filesystem>
#include <string>
#include <set>
#include <vector>
#include <map>
#include <functional>
std::string g_app_dir;
std::string app_path(const char* rel) { return g_app_dir + rel; }
uint16_t ram[0x3000];
uint16_t io[0x1000];
uint16_t rom[0x400000];
uint16_t video_regs[0x100];
unsp_20_device* cpu_ptr;
bool eeprom_locked = true; // moved up from its EEPROM-state block below so call_rom_function can see it
uint16_t audio_regs[0x200];
uint16_t audio_phase_regs[0x200];
uint16_t audio_ctrl_regs[0x20];

double audio_channel_rate[16];
double audio_channel_rate_accum[16];
uint8_t audio_sample_shift[16];
uint32_t audio_sample_count[16];
uint32_t audio_rampdown_frame[16];
uint32_t audio_envclk_frame[16];
uint32_t audio_envelope_addr_rt[16];
int32_t audio_ima_signal[16];
int32_t audio_ima_step[16];
uint16_t audio_adpcm36_remaining[16];
uint16_t audio_adpcm36_header[16];

uint16_t audio_curr_beat_base_count = 0;
int32_t audio_adpcm36_prevsamp[16][2];
// Per-channel registers (audio_regs, offset 0x000 in real spg2xx)
enum {
    AUDIO_WAVE_ADDR             = 0x000,

    AUDIO_MODE                  = 0x001,
    AUDIO_WADDR_HIGH_MASK       = 0x003f,
    AUDIO_LADDR_HIGH_MASK       = 0x0fc0,
    AUDIO_LADDR_HIGH_SHIFT      = 6,
    AUDIO_TONE_MODE_MASK        = 0x3000,
    AUDIO_TONE_MODE_SHIFT       = 12,
    AUDIO_TONE_MODE_SW          = 0,
    AUDIO_TONE_MODE_HW_ONESHOT  = 1,
    AUDIO_TONE_MODE_HW_LOOP     = 2,
    AUDIO_16M_MASK              = 0x4000,
    AUDIO_ADPCM_MASK            = 0x8000,

    AUDIO_LOOP_ADDR             = 0x002,

    AUDIO_PAN_VOL               = 0x003,
    AUDIO_PAN_VOL_MASK          = 0x7f7f,
    AUDIO_VOLUME_MASK           = 0x007f,
    AUDIO_PAN_MASK              = 0x7f00,
    AUDIO_PAN_SHIFT             = 8,

    AUDIO_ENVELOPE0             = 0x004,
    AUDIO_ENVELOPE_INC_MASK     = 0x007f,
    AUDIO_ENVELOPE_SIGN_MASK    = 0x0080,
    AUDIO_ENVELOPE_TARGET_MASK  = 0x7f00,
    AUDIO_ENVELOPE_TARGET_SHIFT = 8,
    AUDIO_ENVELOPE_REPEAT_PERIOD_MASK = 0x8000,

    AUDIO_ENVELOPE_DATA         = 0x005,
    AUDIO_ENVELOPE_DATA_MASK    = 0xff7f,
    AUDIO_EDD_MASK              = 0x007f,
    AUDIO_ENVELOPE_COUNT_MASK   = 0xff00,
    AUDIO_ENVELOPE_COUNT_SHIFT  = 8,

    AUDIO_ENVELOPE1             = 0x006,
    AUDIO_ENVELOPE_LOAD_MASK    = 0x00ff,
    AUDIO_ENVELOPE_RPT_MASK     = 0x0100,
    AUDIO_ENVELOPE_RPCNT_MASK   = 0xfe00,
    AUDIO_ENVELOPE_RPCNT_SHIFT  = 9,

    AUDIO_ENVELOPE_ADDR_HIGH    = 0x007,
    AUDIO_EADDR_HIGH_MASK       = 0x003f,

    AUDIO_ENVELOPE_ADDR         = 0x008,
    AUDIO_WAVE_DATA_PREV        = 0x009,

    AUDIO_ENVELOPE_LOOP_CTRL    = 0x00a,
    AUDIO_EAOFFSET_MASK         = 0x01ff,
    AUDIO_RAMPDOWN_OFFSET_MASK  = 0xfe00,
    AUDIO_RAMPDOWN_OFFSET_SHIFT = 9,

    AUDIO_WAVE_DATA             = 0x00b,

    AUDIO_ADPCM_SEL             = 0x00d,
    AUDIO_ADPCM_SEL_MASK        = 0xfe00,
    AUDIO_ADPCM36_MASK          = 0x8000,
};

// Per-channel phase/pitch registers (audio_phase_regs, offset 0x200 in real spg2xx)
enum {
    AUDIO_PHASE_HIGH            = 0x000,
    AUDIO_PHASE_HIGH_MASK       = 0x0007,

    AUDIO_PHASE_ACCUM_HIGH      = 0x001,
    AUDIO_PHASE_ACCUM_HIGH_MASK = 0x0007,

    AUDIO_TARGET_PHASE_HIGH     = 0x002,
    AUDIO_TARGET_PHASE_HIGH_MASK= 0x0007,

    AUDIO_RAMP_DOWN_CLOCK       = 0x003,
    AUDIO_RAMP_DOWN_CLOCK_MASK  = 0x0007,

    AUDIO_PHASE                 = 0x004,
    AUDIO_PHASE_ACCUM           = 0x005,
    AUDIO_TARGET_PHASE          = 0x006,

    AUDIO_PHASE_CTRL            = 0x007,

    AUDIO_CHAN_OFFSET_MASK      = 0xf0f,
};
int g_render_filter = 0;
int g_fps_limit_idx = 0;
const int FPS_LIMIT_OPTION_COUNT = 3;
const int FPS_LIMIT_OPTIONS[3] = { 60, 120, 0 };

int g_export_scale_idx = 3; // default index 3 -> 8x
const int EXPORT_SCALE_OPTION_COUNT = 4;
const int EXPORT_SCALE_OPTIONS[4] = { 1, 2, 4, 8 };
bool g_interp_enabled = false;
void save_fps_limit() {}

// Sprite table: 256 slots x 4 words (tile, x, y, attr) at 0x2C00 - see
// draw_sprites. Named here too since render interpolation needs to snapshot
// the whole table independently of the renderer.
static const uint32_t SPRITE_TABLE_ADDR = 0x2C00;
static const int SPRITE_SLOT_COUNT = 256;
struct InterpSnapshot {
    uint16_t tile[SPRITE_SLOT_COUNT];
    int16_t x[SPRITE_SLOT_COUNT];
    int16_t y[SPRITE_SLOT_COUNT];
    uint16_t attr[SPRITE_SLOT_COUNT];
};
InterpSnapshot g_interp_curr, g_interp_prev;
bool g_have_interp_snapshot = false;

// g_cameraScrollX/Y - confirmed via decompile of HandleMiniPetRoomInputAndStoryLogic
// (ROM 0x0130be, the real per-tick overworld input handler for normal,
// non-link-cable play): ordinary walking increments this by +-2/+-3 every
// tick (g_cameraScrollX +=/-= 2 or 3 depending on held direction and
// g_miniPetPromptState) - it's what actually moves during gameplay, not
// 0x1AB5/0x1AB6 (g_playerWorldPosX/Y), which this same decompile shows is
// only ever written once on room-transition/spawn, never during normal
// movement. Matches the user's own CT table's "Player X/Y" entry - the
// speed cheat below watches/amplifies this because it's genuinely the only
// address that changes while walking, despite the "camera scroll" name.
static const uint16_t PLAYER_WORLD_X = 0x1CC1;
static const uint16_t PLAYER_WORLD_Y = 0x1CC2;
static const uint16_t LOCATION_ID_ADDR = 0x1AA4;

// Real door-to-destination links, gathered empirically (the mechanism
// connecting a warp-trigger object to which new area actually loads was
// never resolved via static analysis - traced live instead: walked
// through each door while logging (fromArea, lastActiveStoryObjectId,
// toArea) at the moment of transition). Position (x,y) is that door
// object's own world position, pulled from the interactable-object table
// (ROM 0x6BFD-area-indexed, confirmed reliable via 6 separate live
// cross-checks this session, including this exact door set). Direct
// connections only, v1 scope - if the target area isn't directly reachable
// from the player's current area, no entry exists here.
struct DoorLink { uint16_t fromArea, toArea; int16_t x, y; };
static const DoorLink DOOR_LINKS[] = {
    { 0, 4, 351, 1323 },
    { 0, 7, 841, 1561 },
    { 0, 6, 1396, 440 }, // the only entrance that actually leads to Gigalympia's NPCs/quests -
    { 0, 1, 2475, 751 }, // the OTHER area0<->6 door pair (objectId 6/43) goes to a disconnected
    { 1, 9, 445, 571 },  // part of the area and is deliberately left out, not an oversight.
    { 1, 5, 2216, 292 },
    { 1, 0, 96, 726 },
    { 1, 3, 2175, 1568 },
    { 2, 3, 463, 1100 },
    { 3, 0, 1561, 429 },
    { 3, 1, 1261, 119 },
    { 3, 2, 349, 926 },
    { 4, 0, 226, 368 },
    { 5, 1, 1572, 1570 },
    { 6, 0, 1394, 1215 },
    { 7, 0, 876, 120 },
    { 9, 1, 960, 1568 },
};
static const int DOOR_LINK_COUNT = sizeof(DOOR_LINKS) / sizeof(DOOR_LINKS[0]);

// Returns the world position of the nearest known door in fromArea that
// leads toward toArea, or false if no direct connection is known (v1 -
// multi-hop routing through intermediate areas isn't built yet).
bool find_door_to_area(uint16_t fromArea, uint16_t toArea, int16_t playerX, int16_t playerY, int16_t* outX, int16_t* outY) {
    bool found = false;
    int32_t best_dist_sq = 0;
    for (int i = 0; i < DOOR_LINK_COUNT; i++) {
        if (DOOR_LINKS[i].fromArea != fromArea || DOOR_LINKS[i].toArea != toArea) continue;
        int32_t dx = DOOR_LINKS[i].x - playerX, dy = DOOR_LINKS[i].y - playerY;
        int32_t dist_sq = dx * dx + dy * dy;
        if (!found || dist_sq < best_dist_sq) {
            found = true;
            best_dist_sq = dist_sq;
            *outX = DOOR_LINKS[i].x;
            *outY = DOOR_LINKS[i].y;
        }
    }
    return found;
}

// Multi-hop routing: BFS over DOOR_LINKS to find a path from fromArea to
// toArea through intermediate areas when there's no direct connection,
// then points at the door for the FIRST hop of that path (walking through
// it re-triggers this same logic from the new area, giving the next hop).
// Reduces to a plain find_door_to_area lookup when the connection is
// direct, so this can just replace that call everywhere.
static const int MAX_ROUTABLE_AREAS = 64;
bool find_route_door(uint16_t fromArea, uint16_t toArea, int16_t playerX, int16_t playerY, int16_t* outX, int16_t* outY) {
    if (fromArea == toArea || fromArea >= MAX_ROUTABLE_AREAS || toArea >= MAX_ROUTABLE_AREAS) return false;
    int16_t prev[MAX_ROUTABLE_AREAS];
    bool visited[MAX_ROUTABLE_AREAS];
    for (int i = 0; i < MAX_ROUTABLE_AREAS; i++) { prev[i] = -1; visited[i] = false; }
    uint16_t queue[MAX_ROUTABLE_AREAS];
    int qh = 0, qt = 0;
    queue[qt++] = fromArea;
    visited[fromArea] = true;
    while (qh < qt) {
        uint16_t cur = queue[qh++];
        if (cur == toArea) break;
        for (int i = 0; i < DOOR_LINK_COUNT; i++) {
            if (DOOR_LINKS[i].fromArea != cur) continue;
            uint16_t nxt = DOOR_LINKS[i].toArea;
            if (nxt >= MAX_ROUTABLE_AREAS || visited[nxt]) continue;
            visited[nxt] = true;
            prev[nxt] = (int16_t)cur;
            queue[qt++] = nxt;
        }
    }
    if (!visited[toArea]) return false; // no known route at all, even multi-hop
    // Walk backward from toArea until the step right after fromArea -
    // that's the first hop's destination area.
    uint16_t step = toArea;
    while (prev[step] != -1 && prev[step] != (int16_t)fromArea) step = (uint16_t)prev[step];
    return find_door_to_area(fromArea, step, playerX, playerY, outX, outY);
}

bool g_quest_arrow_enabled = false;

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
// Bigger than the bare-minimum 2x - the mod menu/HUD text is a fixed pixel
// size in this same virtual space (see g_hud_cam), so raising this scales
// legibility up for everything uniformly without touching any individual
// font-size or row-layout constant.
static const float DEFAULT_WINDOW_SCALE = 2.5f;
static const int AUDIO_STREAM_CHUNK = 4096;
inline uint32_t full_pc() { return ((cpu_ptr->get_r(6) & 0x3f) << 16) | cpu_ptr->get_r(7); }
void check_video_irq(); // defined below - forward-declared for call_rom_function's tick pump

// Synchronously invokes a real ROM function, letting the ROM's own proven-
// correct code do the work (e.g. the real per-field EEPROM save routines)
// instead of reimplementing its logic in C++. Ground-truthed against this
// port's own borrowed unsp core (unsp_fxxx.cpp's CALL16 handler, unsp_other.
// cpp's generic POP path that "retf" decodes to): a far call pushes PC then
// SR (in that order - SR closer to the top, matching full_pc()'s own
// (SR&0x3f)<<16|PC packing) and sets PC/SR to the target; retf reverses
// that, SR then PC. push()/pop() themselves (unsp.cpp) store-then-decrement
// and increment-then-read, a full-descending stack. This replicates exactly
// that: pushes args in the same order the real ROM caller does, then a
// return frame built from the CPU's actual current PC/SR (not a synthetic
// address - genuinely where execution already was), runs until PC gets
// back there, and defensively force-restores PC/SR/SP afterward regardless
// of how the loop exited, so a stuck or misbehaving callee can never leave
// the emulated CPU in a bad state.
void call_rom_function(uint32_t target_full_addr, const std::vector<uint16_t>& args_in_push_order) {
    uint32_t orig_pc = cpu_ptr->get_r(unsp_12_device::REG_PC);
    uint32_t orig_sr = cpu_ptr->get_r(unsp_12_device::REG_SR);
    uint32_t orig_sp = cpu_ptr->get_r(unsp_12_device::REG_SP);
    // Bug found via the trace log: comparing full_pc() (PC banked with
    // SR's low 6 bits) against orig_pc (the raw 16-bit PC register alone,
    // no bank) can only ever match by coincidence when the bank happens
    // to be 0. Money's injection call happened to land there; item saves
    // didn't (their real return context sat inside WaitForNextTick's own
    // bank-1 code), so the loop-exit check could never fire and it spun
    // until the guard cap every time. Capturing the properly banked value
    // up front for the comparison fixes it for every bank, not just 0.
    uint32_t orig_full_pc = full_pc();

    uint32_t sp = orig_sp;
    for (uint16_t v : args_in_push_order) { ram[sp] = v; sp = (uint16_t)(sp - 1); }
    ram[sp] = (uint16_t)orig_pc; sp = (uint16_t)(sp - 1);
    ram[sp] = (uint16_t)orig_sr; sp = (uint16_t)(sp - 1);
    cpu_ptr->set_r(unsp_12_device::REG_SP, sp);
    cpu_ptr->set_r(unsp_12_device::REG_PC, (uint16_t)(target_full_addr & 0xFFFF));
    cpu_ptr->set_r(unsp_12_device::REG_SR, (orig_sr & 0xFFC0) | ((target_full_addr >> 16) & 0x3F));

    // The real bug, found via a GPIO-activity trace: the emulated 93C66
    // EEPROM's write-commit is gated on eeprom_locked (see eeprom_clk_write
    // below) - a real chip needs an explicit unlock/EWEN command before
    // any write actually takes effect. 528 real CS/CLK/DI toggles happened
    // during an injected item save (confirmed via the trace, proving the
    // protocol genuinely ran to completion), yet nothing persisted -
    // because eeprom_locked was still true, silently dropping the final
    // commit. A real purchase's full call chain had already unlocked the
    // chip earlier in the same session (or as part of that chain); this
    // injected call jumps straight into the save function and skips
    // whatever established that. eeprom_locked is our own emulated chip
    // state, not ROM code, so forcing it unlocked for just the duration
    // of this call - then restoring it - is the correct, safe fix.
    bool orig_eeprom_locked = eeprom_locked;
    eeprom_locked = false;

    for (int guard = 0; guard < 200000; guard++) {
        // Some real save paths (item saves' immediate EEPROM flush)
        // internally wait for the real vblank tick counter (ram
        // 0x13/0x14) to advance, the same way WaitForNextTick does -
        // which normally only happens once per real host frame via the
        // trigger below. This injected call runs as one uninterrupted
        // burst outside that per-frame loop, so without also pumping it
        // periodically here, a callee waiting on a tick would spin
        // until this loop's guard cap instead of ever seeing one arrive.
        if (guard % 7500 == 0 && (video_regs[0x62] & 1)) { video_regs[0x63] |= 1; check_video_irq(); }
        cpu_ptr->step(1);
        if (full_pc() == orig_full_pc) break;
    }
    eeprom_locked = orig_eeprom_locked;
    cpu_ptr->set_r(unsp_12_device::REG_SP, orig_sp);
    cpu_ptr->set_r(unsp_12_device::REG_PC, orig_pc);
    cpu_ptr->set_r(unsp_12_device::REG_SR, orig_sr);
}
// Hand-declared instead of #include <windows.h> to avoid its Rectangle/
// CloseWindow/DrawText/PlaySound clashes with raylib. user32.lib is already
// linked (CMakeLists.txt); kernel32.lib is linked implicitly by default.
extern "C" {
    __declspec(dllimport) void* __stdcall GetConsoleWindow(void);
    __declspec(dllimport) int __stdcall ShowWindow(void* hWnd, int nCmdShow);
    __declspec(dllimport) int __stdcall MessageBoxA(void* hWnd, const char* text, const char* caption, unsigned type);
}
#define SW_MINIMIZE 6

// Forward declaration: the audio engine's sample/envelope-table fetches call
// memory_read16() (same flat address space used by tile/sprite graphics),
// but memory_read16() itself dispatches into the audio register handlers
// defined below it - a genuine circular dependency, broken with this one
// declaration (the file otherwise defines everything strictly in call order).
uint16_t memory_read16(uint32_t addr);
static const int NATIVE_W = 320;
static const int NATIVE_H = 240;
static const int WIDE_W = 320;
static const int WIDE_MARGIN_L = (WIDE_W - NATIVE_W) / 2;
static const int WIDE_MARGIN_R = (WIDE_W - NATIVE_W) - WIDE_MARGIN_L;
// On-screen frame counter, shown in the corner each frame.
long g_frame = 0;
// FPS / 1% low tracking, for diagnosing audio stutter caused by the game
// loop actually running below 60fps in real wall-clock time.
static const int FPS_WINDOW = 240; // ~4s of history at 60fps
float fps_frame_times_ms[FPS_WINDOW] = {};
int fps_frame_index = 0;
int fps_frame_count = 0;
// Watchdog: real hardware resets the CPU if REG_WATCHDOG_CLEAR (0x3D24) isn't
// petted with 0x55AA within 750ms while enabled via REG_SYSTEM_CTRL bit 15.
// The ROM relies on this to bounce back to the boot vector (e.g. on quit),
// so it must be emulated or those flows hang forever.
bool watchdog_enabled = false;
int watchdog_frames_left = 0;
// Opt-in hidden test-menu auto-unlock (F9). Off by default - only runs when
// the player presses the hotkey, which forces a real reset (same path as
// the watchdog reset above) then drives the exact real button sequence
// during the resulting boot, instead of requiring precise manual timing.
// See the full_pc()==0x03C9E8 RAM-arm hook for the ROM-side half of this.
bool g_test_menu_seq_active = false;
int g_test_menu_seq_frame = 0;
bool g_live_sprite_capture = false; // F7 toggle - see export_visible_sprite_clusters
// Mod Menu toggle, applies to both F8 and F7 (export_visible_sprite_clusters).
// This is a player-centered camera - g_cameraScrollX/Y is literally the
// player's own world position (see PLAYER_WORLD_X/Y) - so the player's own
// sprite cluster lands at/near screen center every single frame by
// construction, unlike any other object. Used as the "is this the player"
// heuristic rather than tracking a specific sprite slot, since the ROM
// assigns sprite slots dynamically and there's no fixed player slot index.
bool g_extractor_skip_player = false;
// Set by the PlaySoundEffect(0x59) hook once the real chime actually fires
// (confirmed happens ~frame 182, not a fixed guess) - timing below is
// relative to this instead of a fixed hold duration, since releasing
// Left+Select before the real chime resets the test-mode flag to 0.
int g_test_menu_chime_frame = -1;
// Real "Exit Test Mode" calls PowerDownHardware() then spins in a genuine
// infinite do-nothing loop (byte-perfect ROM match, not our bug) - real
// hardware just turns off. It also runs TestRomChecksum, a real EEPROM
// diagnostic that deliberately erases the whole chip as part of its test
// (Eeprom_EraseRange) - real hardware behavior, but not something a PC
// player who found this via a cheat should lose their save over. Snapshot
// EEPROM before entering test mode and restore+reboot on the way out.
uint16_t g_test_mode_eeprom_backup[256];
bool g_test_mode_backup_valid = false;
// EEPROM (93C66, 16-bit words, 256 cells, 8-bit address) bit-banged over
// Port B: bit0=CS, bit1=CLK, bit2=DI (writes), bit3=DO (reads at 0x3D06).
// Protocol per the real 93Cxx serial EEPROM state machine: CS high arms it,
// then a start bit (DI=1) + 2-bit opcode + 8-bit address are clocked in MSB
// first; opcode 1=WRITE (clock in 16 data bits), 2=READ (clock out 16 bits),
// 0+top-2-address-bits 3=UNLOCK (erase/write enable), 0 0=LOCK.
uint16_t eeprom_data[256];
enum EepromState { EE_RESET, EE_WAIT_START, EE_WAIT_CMD, EE_READING, EE_WAIT_DATA, EE_WAIT_COMPLETE };
EepromState eeprom_state = EE_RESET;
bool eeprom_cs = false, eeprom_clk = false, eeprom_di = false;
uint32_t eeprom_cmd_addr_accum = 0;
int eeprom_bits_accum = 0;
uint32_t eeprom_shift = 0;
int eeprom_address = 0;

void eeprom_load() {
    // A real, never-written 93C66 reads back as all-1 bits (0xFFFF per word)
    // - the electrical erased state, not zero. The ROM's "is there a valid
    // save, or should I run first-time new-pet setup" check almost certainly
    // keys off that blank signature. Defaulting this array to C++'s implicit
    // all-zero for a brand new save file would make it look like an
    // existing-but-garbage save instead of a blank one, sending the ROM down
    // an unintended code path - a strong candidate for the wrong-spawn/
    // already-sick/stuck-on-new-game symptoms.
    memset(eeprom_data, 0xFF, sizeof(eeprom_data));
    FILE* f = fopen(app_path("resources/data/gigapets_save.eep").c_str(), "rb");
    if (f) { fread(eeprom_data, sizeof(uint16_t), 256, f); fclose(f); }
}

void eeprom_save() {
    // Write to a temp file then rename over the real one, so a crash or kill
    // mid-write can't leave a truncated/empty save (which loads as a fresh game).
    std::string path = app_path("resources/data/gigapets_save.eep");
    std::string tmp = path + ".tmp";
    FILE* f = fopen(tmp.c_str(), "wb");
    if (!f) return;
    size_t written = fwrite(eeprom_data, sizeof(uint16_t), 256, f);
    fclose(f);
    if (written != 256) { std::remove(tmp.c_str()); return; }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) std::remove(tmp.c_str());
}


void eeprom_cs_write(bool state) {
    if (state == eeprom_cs) return;
    bool rising = state;
    bool falling = !state;
    eeprom_cs = state;
    if (eeprom_state == EE_RESET) {
        if (rising) eeprom_state = EE_WAIT_START;
    } else if (falling) {
        eeprom_state = EE_RESET;
    }
}

void eeprom_di_write(bool state) {
    eeprom_di = state;
}

void eeprom_clk_write(bool state) {
    if (state == eeprom_clk) return;
    bool rising = state;
    eeprom_clk = state;
    if (!rising) return;

    switch (eeprom_state) {
        case EE_WAIT_START:
            if (eeprom_di) {
                eeprom_cmd_addr_accum = 0;
                eeprom_bits_accum = 0;
                eeprom_state = EE_WAIT_CMD;
            }
            break;
        case EE_WAIT_CMD: {
            eeprom_cmd_addr_accum = (eeprom_cmd_addr_accum << 1) | (eeprom_di ? 1 : 0);
            eeprom_bits_accum++;
            if (eeprom_bits_accum == 10) { // 2-bit opcode + 8-bit address
                int opcode = (eeprom_cmd_addr_accum >> 8) & 3;
                eeprom_address = eeprom_cmd_addr_accum & 0xFF;
                eeprom_bits_accum = 0;
                if (opcode == 1) { // WRITE
                    eeprom_shift = 0;
                    eeprom_state = EE_WAIT_DATA;
                } else if (opcode == 2) { // READ
                    eeprom_shift = 0;
                    eeprom_state = EE_READING;
                } else if (opcode == 0) { // LOCK/UNLOCK/WRITEALL/ERASEALL
                    int sub = (eeprom_address >> 6) & 3;
                    if (sub == 0) eeprom_locked = true;       // LOCK
                    else if (sub == 3) eeprom_locked = false; // UNLOCK (EWEN)
                    eeprom_state = EE_RESET;
                } else { // ERASE - not needed for this game, treat as a no-op completion
                    eeprom_state = EE_WAIT_COMPLETE;
                }
            }
            break;
        }
        case EE_READING:
            if (eeprom_bits_accum % 16 == 0) {
                // Real 93C66 sequential-read: as long as the host keeps
                // clocking past one word's 16 bits (CS still held), the chip
                // auto-increments its internal address register and streams
                // the NEXT word - no new start/opcode/address needed. This
                // never advanced eeprom_address, so any read spanning more
                // than one word (common - e.g. reading many packed save
                // fields in one continuous session) just re-read the FIRST
                // word forever. Fields that happened to fit inside that
                // first word came out correct; anything landing in a later
                // word (e.g. the pet illness ID, read last in an 8-word run)
                // silently got garbage from the wrong, un-advanced word.
                if (eeprom_bits_accum != 0) eeprom_address = (eeprom_address + 1) & 0xFF;
                uint16_t val = (eeprom_address < 256) ? eeprom_data[eeprom_address] : 0xFFFF;
                eeprom_shift = ((uint32_t)val) << 16;
            } else {
                eeprom_shift = (eeprom_shift << 1) | 1;
            }
            eeprom_bits_accum++;
            break;
        case EE_WAIT_DATA:
            eeprom_shift = (eeprom_shift << 1) | (eeprom_di ? 1 : 0);
            eeprom_bits_accum++;
            if (eeprom_bits_accum == 16) {
                if (!eeprom_locked && eeprom_address < 256) {
                    eeprom_data[eeprom_address] = (uint16_t)(eeprom_shift & 0xFFFF);
                    eeprom_save();
                }
                eeprom_state = EE_WAIT_COMPLETE;
            }
            break;
        default:
            break;
    }
}

bool eeprom_do_read() {
    if (eeprom_state == EE_READING) return (eeprom_shift & 0x80000000) != 0;
    return true; // pulled up / ready in every other state
}
// ===========================================================================
// SPG2xx Audio: real 16-channel ADPCM/PCM synth hardware, ported near-verbatim
// from mame/src/devices/machine/spg2xx_audio.cpp/.h and
// mame/src/devices/sound/imaadpcm.cpp/.h. Register map: 0x3000-0x31FF =
// per-channel regs (audio_r/audio_w), 0x3200-0x33FF = per-channel phase/pitch
// regs (audio_phase_r/audio_phase_w), 0x3400-0x37FF = global control regs
// (audio_ctrl_r/audio_ctrl_w). Sample/loop/envelope addresses are full 22-bit
// values the game assembles itself in these registers, read via the same
// flat memory_read16() already used for tile/sprite graphics - unrelated to
// read, etc). State 10 is specifically what gates RunPetRoomGameplayLoop
// (confirmed via xrefs to its call site, 0x03b986) - the function that reads
// Location ID/player position every frame while actually walking a pet
// around a room. Used to hide the HUD on the main menu and other non-
// gameplay screens, where the underlying stats are meaningless/blank.
static const uint16_t GAME_STATE_ADDR = 0x1A97;
static const uint16_t GAME_STATE_IN_ROOM = 10;
static const uint16_t CHEAT_MONEY_BASE = 0x0C4D;

static const uint16_t CHEAT_HALOS = 0x2182;
static const uint16_t CHEAT_PITCHFORKS = 0x2183;
static const uint16_t CHEAT_STARS = 0x2184;
static const uint16_t CHEAT_LIGHTBULBS = 0x2185;
static const uint16_t CHEAT_HUNGER = 0x2186;
static const uint16_t CHEAT_MOOD = 0x2189;
static const uint16_t CHEAT_SLEEPINESS = 0x218A;
static const uint16_t CHEAT_HYGEINE = 0x218B;
static const uint16_t CHEAT_HEALTH = 0x218D;

// Health doubles as the sickness-type field: 0-9 are specific illnesses,
// 10 = Healthy (no sickness), 11 = a generic "Sick" catch-all. Matches the
// in-game Personality screen showing "OK" for Health as a word, not a
// magnitude, the same way Hunger/Mood show words like "Stuffed"/"Ecstatic".
static const char* SICKNESS_NAMES[] = {
    "Dog Breath", "Gone Bananas", "Panda Pox", "Cold", "Molting",
    "Brain Freeze", "Cold Feet", "Brain Ache", "The Oogies", "Cooties",
    "Healthy", "Sick",
};
static const int SICKNESS_NAME_COUNT = sizeof(SICKNESS_NAMES) / sizeof(SICKNESS_NAMES[0]);
static const int SICKNESS_HEALTHY_VALUE = 10;

// A row can optionally carry a preset list (Cheat Engine's DropDownList
// entries, e.g. Quests Completed's named unlock thresholds) - these are
// quick-set shortcuts to known meaningful values, shown via a small "..."
// button that opens a floating picker; the row's normal arrows/wheel/type
// editing still work for any raw value, matching both requirements.
struct CheatPreset { int32_t value; const char* label; };
static const CheatPreset QUESTS_COMPLETED_PRESETS[] = {
    { 3,  "Unlock Downtown" },
    { 13, "Unlock the Beach" },
    { 15, "Unlock Giga Castle" },
    { 16, "Unlock Vacation Island" },
    { 41, "Unlock Gigalympia (Left Entrance)" },
    { 60, "Unlock the Jungle Bridge" },
    { 89, "Unlock Magma Mountain" },
    { 90, "Unlock Gigalympia (Right Entrance)" },
};
static const CheatPreset SICKNESS_PRESETS[] = {
    { 0,  "Dog Breath" },
    { 1,  "Gone Bananas" },
    { 2,  "Panda Pox" },
    { 3,  "Cold" },
    { 4,  "Molting" },
    { 5,  "Brain Freeze" },
    { 6,  "Cold Feet" },
    { 7,  "Brain Ache" },
    { 8,  "The Oogies" },
    { 9,  "Cooties" },
    { 10, "Healthy" },
    { 11, "Sick" },
};
// min_val/max_val per the actual in-game caps: Hunger/Mood/Sleepiness/
// Hygeine are 0-100, Health is 0-10, everything else is treated as
// effectively uncapped (0-65535, the full range a stored uint16_t can hold).
// presets/preset_count default to nullptr/0 when omitted from an
// initializer (aggregate init zero-fills trailing members).
struct CheatStatRow {
    const char* name; uint16_t addr; int32_t min_val; int32_t max_val;
    const CheatPreset* presets; int preset_count;
};
static const CheatStatRow CHEAT_STATS[] = {
    { "Money",       0x0C4D, 0, 65535 },
    { "Halos",       0x2182, 0, 65535 },
    { "Pitchforks",  0x2183, 0, 65535 },
    { "Stars",       0x2184, 0, 65535 },
    { "Lightbulbs",  0x2185, 0, 65535 },
    { "Hunger",      0x2186, 0, 100 },
    { "Health",      0x2187, 0, 100 },
    { "Sickness",    0x218D, 0, 11, SICKNESS_PRESETS, sizeof(SICKNESS_PRESETS) / sizeof(SICKNESS_PRESETS[0]) },
    { "Mood",        0x2189, 0, 100 },
    { "Sleepiness",  0x218A, 0, 100 },
    { "Hygeine",     0x218B, 0, 100 },
    { "Tricks",      0x218C, 0, 3 },
    { "Location ID", 0x1AA4, 0, 65535 },
    { "Player X",    0x1CC1, 0, 65535 },
    { "Player Y",    0x1CC2, 0, 65535 },
    { "Camera X",    0x1AB0, 0, 65535 },
    { "Camera Y",    0x1AB1, 0, 65535 },
    { "Player Color",0x1CC6, 0, 65535 },
    { "Quests Done", 0x1E16, 0, 65535, QUESTS_COMPLETED_PRESETS, sizeof(QUESTS_COMPLETED_PRESETS) / sizeof(QUESTS_COMPLETED_PRESETS[0]) },
};
static const int CHEAT_STAT_COUNT = sizeof(CHEAT_STATS) / sizeof(CHEAT_STATS[0]);
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
enum {
    AUDIO_CHANNEL_ENABLE            = 0x000,
    AUDIO_MAIN_VOLUME               = 0x001,
    AUDIO_MAIN_VOLUME_MASK          = 0x007f,
    AUDIO_CHANNEL_FIQ_ENABLE        = 0x002,
    AUDIO_CHANNEL_FIQ_STATUS        = 0x003,
    AUDIO_CHANNEL_FIQ_STATUS_MASK   = 0xffff,
    AUDIO_BEAT_BASE_COUNT           = 0x004,
    AUDIO_BEAT_BASE_COUNT_MASK      = 0x07ff,
    AUDIO_BEAT_COUNT                = 0x005,
    AUDIO_BEAT_COUNT_MASK           = 0x3fff,
    AUDIO_BIS_MASK                  = 0x4000,
    AUDIO_BIE_MASK                  = 0x8000,
    AUDIO_ENVCLK0                   = 0x006,
    AUDIO_ENVCLK0_HIGH              = 0x007,
    AUDIO_ENVCLK1                   = 0x008,
    AUDIO_ENVCLK1_HIGH              = 0x009,
    AUDIO_ENV_RAMP_DOWN             = 0x00a,
    AUDIO_ENV_RAMP_DOWN_MASK        = 0xffff,
    AUDIO_CHANNEL_STOP              = 0x00b,
    AUDIO_CHANNEL_ZERO_CROSS        = 0x00c,
    AUDIO_CHANNEL_ZERO_CROSS_MASK   = 0xffff,
    AUDIO_CONTROL                   = 0x00d,
    AUDIO_CONTROL_MASK              = 0x9fe8,
    AUDIO_CONTROL_NOINT_MASK        = 0x0200,
    AUDIO_CONTROL_VOLSEL_MASK       = 0x00c0,
    AUDIO_CONTROL_VOLSEL_SHIFT      = 6,
    AUDIO_COMPRESS_CTRL             = 0x00e,
    AUDIO_CHANNEL_STATUS            = 0x00f,
    AUDIO_WAVE_IN_L                 = 0x010,
    AUDIO_WAVE_IN_R                 = 0x011,
    AUDIO_WAVE_OUT_L                = 0x012,
    AUDIO_WAVE_OUT_R                = 0x013,
    AUDIO_CHANNEL_REPEAT            = 0x014,
    AUDIO_CHANNEL_REPEAT_MASK       = 0xffff,
    AUDIO_CHANNEL_ENV_MODE          = 0x015,
    AUDIO_CHANNEL_ENV_MODE_MASK     = 0xffff,
    AUDIO_CHANNEL_TONE_RELEASE      = 0x016,
    AUDIO_CHANNEL_TONE_RELEASE_MASK = 0xffff,
    AUDIO_CHANNEL_ENV_IRQ           = 0x017,
    AUDIO_CHANNEL_ENV_IRQ_MASK      = 0xffff,
    AUDIO_CHANNEL_PITCH_BEND        = 0x018,
    AUDIO_CHANNEL_PITCH_BEND_MASK   = 0xffff,
    AUDIO_SOFT_PHASE                = 0x019,
    AUDIO_ATTACK_RELEASE            = 0x01a,
    AUDIO_EQ_CUTOFF10               = 0x01b,
    AUDIO_EQ_CUTOFF10_MASK          = 0x7f7f,
    AUDIO_EQ_CUTOFF32               = 0x01c,
    AUDIO_EQ_CUTOFF32_MASK          = 0x7f7f,
    AUDIO_EQ_GAIN10                 = 0x01d,
    AUDIO_EQ_GAIN10_MASK            = 0x7f7f,
    AUDIO_EQ_GAIN32                 = 0x01e,
    AUDIO_EQ_GAIN32_MASK            = 0x7f7f,
};

inline bool audio_channel_status(int ch) { return audio_ctrl_regs[AUDIO_CHANNEL_STATUS] & (1 << ch); }
inline uint16_t audio_vol_sel() { return (audio_ctrl_regs[AUDIO_CONTROL] & AUDIO_CONTROL_VOLSEL_MASK) >> AUDIO_CONTROL_VOLSEL_SHIFT; }

inline uint16_t audio_wave_addr_high(int ch) { return audio_regs[(ch << 4) | AUDIO_MODE] & AUDIO_WADDR_HIGH_MASK; }
inline uint16_t audio_loop_addr_high(int ch) { return (audio_regs[(ch << 4) | AUDIO_MODE] & AUDIO_LADDR_HIGH_MASK) >> AUDIO_LADDR_HIGH_SHIFT; }
inline uint16_t audio_tone_mode(int ch) { return (audio_regs[(ch << 4) | AUDIO_MODE] & AUDIO_TONE_MODE_MASK) >> AUDIO_TONE_MODE_SHIFT; }
inline uint16_t audio_16bit_bit(int ch) { return (audio_regs[(ch << 4) | AUDIO_MODE] & AUDIO_16M_MASK) ? 1 : 0; }
inline uint16_t audio_adpcm_bit(int ch) { return (audio_regs[(ch << 4) | AUDIO_MODE] & AUDIO_ADPCM_MASK) ? 1 : 0; }

inline uint16_t audio_volume(int ch) { return audio_regs[(ch << 4) | AUDIO_PAN_VOL] & AUDIO_VOLUME_MASK; }
inline uint16_t audio_pan(int ch) { return (audio_regs[(ch << 4) | AUDIO_PAN_VOL] & AUDIO_PAN_MASK) >> AUDIO_PAN_SHIFT; }

inline uint16_t audio_envelope_inc(int ch) { return audio_regs[(ch << 4) | AUDIO_ENVELOPE0] & AUDIO_ENVELOPE_INC_MASK; }
inline uint16_t audio_envelope_sign(int ch) { return (audio_regs[(ch << 4) | AUDIO_ENVELOPE0] & AUDIO_ENVELOPE_SIGN_MASK) ? 1 : 0; }
inline uint16_t audio_envelope_target(int ch) { return (audio_regs[(ch << 4) | AUDIO_ENVELOPE0] & AUDIO_ENVELOPE_TARGET_MASK) >> AUDIO_ENVELOPE_TARGET_SHIFT; }

inline uint16_t audio_edd(int ch) { return audio_regs[(ch << 4) | AUDIO_ENVELOPE_DATA] & AUDIO_EDD_MASK; }
inline uint16_t audio_envelope_count(int ch) { return (audio_regs[(ch << 4) | AUDIO_ENVELOPE_DATA] & AUDIO_ENVELOPE_COUNT_MASK) >> AUDIO_ENVELOPE_COUNT_SHIFT; }
inline void audio_set_edd(int ch, uint8_t edd) { audio_regs[(ch << 4) | AUDIO_ENVELOPE_DATA] = (audio_regs[(ch << 4) | AUDIO_ENVELOPE_DATA] & ~AUDIO_EDD_MASK) | edd; }
inline void audio_set_envelope_count(int ch, uint16_t count) { audio_regs[(ch << 4) | AUDIO_ENVELOPE_DATA] = audio_edd(ch) | (count << AUDIO_ENVELOPE_COUNT_SHIFT); }

inline uint16_t audio_envelope_load(int ch) { return audio_regs[(ch << 4) | AUDIO_ENVELOPE1] & AUDIO_ENVELOPE_LOAD_MASK; }
inline uint16_t audio_envelope_repeat_bit(int ch) { return (audio_regs[(ch << 4) | AUDIO_ENVELOPE1] & AUDIO_ENVELOPE_RPT_MASK) ? 1 : 0; }
inline uint16_t audio_envelope_repeat_count(int ch) { return (audio_regs[(ch << 4) | AUDIO_ENVELOPE1] & AUDIO_ENVELOPE_RPCNT_MASK) >> AUDIO_ENVELOPE_RPCNT_SHIFT; }
inline void audio_set_envelope_repeat_count(int ch, uint16_t count) { audio_regs[(ch << 4) | AUDIO_ENVELOPE1] = (audio_regs[(ch << 4) | AUDIO_ENVELOPE1] & ~AUDIO_ENVELOPE_RPCNT_MASK) | ((count << AUDIO_ENVELOPE_RPCNT_SHIFT) & AUDIO_ENVELOPE_RPCNT_MASK); }

inline uint16_t audio_envelope_addr_high(int ch) { return audio_regs[(ch << 4) | AUDIO_ENVELOPE_ADDR_HIGH] & AUDIO_EADDR_HIGH_MASK; }

inline uint16_t audio_eaoffset(int ch) { return audio_regs[(ch << 4) | AUDIO_ENVELOPE_LOOP_CTRL] & AUDIO_EAOFFSET_MASK; }
inline uint16_t audio_rampdown_offset(int ch) { return (audio_regs[(ch << 4) | AUDIO_ENVELOPE_LOOP_CTRL] & AUDIO_RAMPDOWN_OFFSET_MASK) >> AUDIO_RAMPDOWN_OFFSET_SHIFT; }

inline uint16_t audio_adpcm36_bit(int ch) { return (audio_regs[(ch << 4) | AUDIO_ADPCM_SEL] & AUDIO_ADPCM36_MASK) ? 1 : 0; }

inline uint16_t audio_phase_high(int ch) { return audio_phase_regs[(ch << 4) | AUDIO_PHASE_HIGH] & AUDIO_PHASE_HIGH_MASK; }
inline uint32_t audio_phase(int ch) { return ((uint32_t)audio_phase_high(ch) << 16) | audio_phase_regs[(ch << 4) | AUDIO_PHASE]; }
inline uint16_t audio_rampdown_clock(int ch) { return audio_phase_regs[(ch << 4) | AUDIO_RAMP_DOWN_CLOCK] & AUDIO_RAMP_DOWN_CLOCK_MASK; }

inline uint32_t audio_wave_addr(int ch) { return ((uint32_t)audio_wave_addr_high(ch) << 16) | audio_regs[(ch << 4) | AUDIO_WAVE_ADDR]; }
inline uint32_t audio_loop_addr(int ch) { return ((uint32_t)audio_loop_addr_high(ch) << 16) | audio_regs[(ch << 4) | AUDIO_LOOP_ADDR]; }
inline uint32_t audio_envelope_addr(int ch) { return ((uint32_t)audio_envelope_addr_high(ch) << 16) | audio_regs[(ch << 4) | AUDIO_ENVELOPE_ADDR]; }
inline void audio_set_wave_addr(int ch, uint32_t addr) {
    audio_regs[(ch << 4) | AUDIO_MODE] &= ~AUDIO_WADDR_HIGH_MASK;
    audio_regs[(ch << 4) | AUDIO_MODE] |= (addr >> 16) & AUDIO_WADDR_HIGH_MASK;
    audio_regs[(ch << 4) | AUDIO_WAVE_ADDR] = addr & 0xffff;
}
inline void audio_inc_wave_addr(int ch) { audio_set_wave_addr(ch, audio_wave_addr(ch) + 1); }

static const uint32_t s_rampdown_frame_counts[8] = {
    13*4, 13*16, 13*64, 13*256, 13*1024, 13*4096, 13*8192, 13*8192
};
static const uint32_t s_envclk_frame_counts[16] = {
    4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 8192, 8192, 8192, 8192
};
inline uint32_t audio_get_rampdown_frame_count(int ch) { return s_rampdown_frame_counts[audio_rampdown_clock(ch)]; }
inline uint32_t audio_get_envelope_clock(int ch) {
    if (ch < 4) return (audio_ctrl_regs[AUDIO_ENVCLK0] >> (ch << 2)) & 0xf;
    else if (ch < 8) return (audio_ctrl_regs[AUDIO_ENVCLK0_HIGH] >> ((ch - 4) << 2)) & 0xf;
    else if (ch < 12) return (audio_ctrl_regs[AUDIO_ENVCLK1] >> ((ch - 8) << 2)) & 0xf;
    else return (audio_ctrl_regs[AUDIO_ENVCLK1_HIGH] >> ((ch - 12) << 2)) & 0xf;
}
inline uint32_t audio_get_envclk_frame_count(int ch) { return s_envclk_frame_counts[audio_get_envelope_clock(ch)]; }

// Standard IMA ADPCM (mame/src/devices/sound/imaadpcm.cpp) - self-contained,
// no MAME dependencies.
static int32_t s_ima_diff_lookup[89 * 16];
static bool s_ima_tables_computed = false;
void audio_ima_compute_tables() {
    if (s_ima_tables_computed) return;
    s_ima_tables_computed = true;
    static const int8_t nbl2bit[16][4] = {
        { 1, 0, 0, 0}, { 1, 0, 0, 1}, { 1, 0, 1, 0}, { 1, 0, 1, 1},
        { 1, 1, 0, 0}, { 1, 1, 0, 1}, { 1, 1, 1, 0}, { 1, 1, 1, 1},
        {-1, 0, 0, 0}, {-1, 0, 0, 1}, {-1, 0, 1, 0}, {-1, 0, 1, 1},
        {-1, 1, 0, 0}, {-1, 1, 0, 1}, {-1, 1, 1, 0}, {-1, 1, 1, 1}
    };
    for (int step = -8; step <= 80; step++) {
        double stepval_d = floor(16.0 * pow(11.0 / 10.0, (double)step));
        int stepval = (int)(stepval_d < 32767.0 ? stepval_d : 32767.0);
        if (step == -5 || step == -4) stepval++;
        for (int nib = 0; nib < 16; nib++) {
            s_ima_diff_lookup[(step + 8) * 16 + nib] = nbl2bit[nib][0] *
                (stepval * nbl2bit[nib][1] + stepval / 2 * nbl2bit[nib][2] + stepval / 4 * nbl2bit[nib][3] + stepval / 8);
        }
    }
}
static const int8_t s_ima_index_shift[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };
int16_t audio_ima_clock(int ch, uint8_t nibble) {
    audio_ima_signal[ch] += s_ima_diff_lookup[audio_ima_step[ch] * 16 + (nibble & 15)];
    if (audio_ima_signal[ch] > 32767) audio_ima_signal[ch] = 32767;
    else if (audio_ima_signal[ch] < -32768) audio_ima_signal[ch] = -32768;
    audio_ima_step[ch] += s_ima_index_shift[nibble & 7];
    if (audio_ima_step[ch] > 88) audio_ima_step[ch] = 88;
    else if (audio_ima_step[ch] < 0) audio_ima_step[ch] = 0;
    return (int16_t)audio_ima_signal[ch];
}

// Custom "ADPCM36" variant: 2-tap predictive filter keyed by a per-block
// header nibble.
uint16_t audio_decode_adpcm36_nybble(int ch, uint8_t data) {
    int32_t shift = audio_adpcm36_header[ch] & 0xf;
    int16_t filter = (audio_adpcm36_header[ch] & 0x3f0) >> 4;
    int16_t f0 = filter | ((filter & 0x20) ? ~0x3f : 0);
    int32_t f1 = 0;
    int16_t sdata = data << 12;
    sdata = (sdata >> shift) + (((audio_adpcm36_prevsamp[ch][0] * f0) + (audio_adpcm36_prevsamp[ch][1] * f1) + 32) >> 12);
    audio_adpcm36_prevsamp[ch][1] = audio_adpcm36_prevsamp[ch][0];
    audio_adpcm36_prevsamp[ch][0] = sdata;
    return (uint16_t)sdata ^ 0x8000;
}

void audio_stop_channel(int ch) {
    audio_ctrl_regs[AUDIO_CHANNEL_STATUS] &= ~(1 << ch);
    audio_regs[(ch << 4) | AUDIO_MODE] &= ~AUDIO_ADPCM_MASK;
    audio_ctrl_regs[AUDIO_CHANNEL_TONE_RELEASE] &= ~(1 << ch);
    audio_ctrl_regs[AUDIO_ENV_RAMP_DOWN] &= ~(1 << ch);
}

void audio_start_channel(int ch) {
    audio_ctrl_regs[AUDIO_CHANNEL_STATUS] |= (1 << ch);
    audio_envelope_addr_rt[ch] = audio_envelope_addr(ch);
    audio_set_envelope_count(ch, audio_envelope_load(ch));

    audio_ima_signal[ch] = 0;
    audio_ima_step[ch] = 0;
    audio_sample_shift[ch] = 0;
    audio_sample_count[ch] = 0;

    if (audio_adpcm36_bit(ch)) {
        audio_adpcm36_remaining[ch] = 0;
        audio_adpcm36_header[ch] = 0;
        audio_adpcm36_prevsamp[ch][0] = 0;
        audio_adpcm36_prevsamp[ch][1] = 0;
    }
}

void audio_loop_channel(int ch) {
    audio_set_wave_addr(ch, audio_loop_addr(ch));
    audio_sample_shift[ch] = 0;
}

bool audio_fetch_sample(int ch) {
    const uint32_t channel_mask = ch << 4;
    audio_regs[channel_mask | AUDIO_WAVE_DATA_PREV] = audio_regs[channel_mask | AUDIO_WAVE_DATA];

    const uint32_t wave_data_reg = channel_mask | AUDIO_WAVE_DATA;
    const uint16_t tone_mode = audio_tone_mode(ch);

    if (audio_adpcm36_bit(ch) && tone_mode != 0 && audio_adpcm36_remaining[ch] == 0) {
        audio_adpcm36_header[ch] = memory_read16(audio_wave_addr(ch));
        audio_adpcm36_remaining[ch] = 8;
        audio_inc_wave_addr(ch);
    }

    uint16_t raw_sample = tone_mode ? memory_read16(audio_wave_addr(ch)) : audio_regs[wave_data_reg];

    if (audio_adpcm_bit(ch) || audio_adpcm36_bit(ch)) {
        if (tone_mode != 0 && raw_sample == 0xffff) {
            if (tone_mode == AUDIO_TONE_MODE_HW_ONESHOT) {
                audio_sample_count[ch] = 0;
                audio_ctrl_regs[AUDIO_CHANNEL_STOP] |= (1 << ch);
                audio_stop_channel(ch);
                return false;
            } else {
                audio_sample_count[ch] = 0;
                audio_loop_channel(ch);
                audio_regs[(ch << 4) | AUDIO_MODE] &= ~AUDIO_ADPCM_MASK;
            }
        } else {
            audio_regs[wave_data_reg] = raw_sample;
            audio_regs[wave_data_reg] >>= audio_sample_shift[ch];
            const uint8_t adpcm_sample = (uint8_t)(audio_regs[wave_data_reg] & 0x000f);
            if (audio_adpcm36_bit(ch))
                audio_regs[wave_data_reg] = audio_decode_adpcm36_nybble(ch, adpcm_sample);
            else
                audio_regs[wave_data_reg] = (uint16_t)(audio_ima_clock(ch, adpcm_sample)) ^ 0x8000;
        }
        audio_sample_count[ch]++;
    }
    else if (audio_16bit_bit(ch)) {
        if (tone_mode != 0 && raw_sample == 0xffff) {
            if (tone_mode == AUDIO_TONE_MODE_HW_ONESHOT) {
                audio_sample_count[ch] = 0;
                audio_stop_channel(ch);
                return false;
            } else {
                audio_sample_count[ch] = 0;
                audio_loop_channel(ch);
            }
        } else {
            audio_regs[wave_data_reg] = raw_sample;
        }
        audio_sample_count[ch]++;
    }
    else {
        // 8-bit mode
        if (tone_mode != 0) {
            if (audio_sample_shift[ch])
                raw_sample &= 0xff00;
            else
                raw_sample <<= 8;
            raw_sample |= raw_sample >> 8;

            if (raw_sample == 0xffff) {
                if (tone_mode == AUDIO_TONE_MODE_HW_ONESHOT) {
                    audio_sample_count[ch] = 0;
                    audio_ctrl_regs[AUDIO_CHANNEL_STOP] |= (1 << ch);
                    audio_stop_channel(ch);
                    return false;
                } else {
                    audio_sample_count[ch] = 0;
                    audio_loop_channel(ch);
                }
            } else {
                audio_regs[wave_data_reg] = raw_sample;
            }
        }
        audio_sample_count[ch]++;
    }

    return true;
}

bool audio_advance_channel(int ch) {
    audio_channel_rate_accum[ch] += audio_channel_rate[ch];
    uint32_t samples_to_advance = 0;
    while (audio_channel_rate_accum[ch] >= 70312.5) {
        audio_channel_rate_accum[ch] -= 70312.5;
        samples_to_advance++;
    }

    if (!samples_to_advance) return true;

    bool playing = true;
    for (uint32_t s = 0; s < samples_to_advance; s++) {
        playing = audio_fetch_sample(ch);
        if (!playing) break;

        if (audio_adpcm_bit(ch) || audio_adpcm36_bit(ch)) {
            audio_sample_shift[ch] += 4;
            if (audio_sample_shift[ch] >= 16) {
                audio_sample_shift[ch] = 0;
                audio_inc_wave_addr(ch);
                if (audio_adpcm36_bit(ch)) audio_adpcm36_remaining[ch]--;
            }
        } else if (audio_16bit_bit(ch)) {
            audio_inc_wave_addr(ch);
        } else {
            audio_sample_shift[ch] += 8;
            if (audio_sample_shift[ch] >= 16) {
                audio_sample_shift[ch] = 0;
                audio_inc_wave_addr(ch);
            }
        }
    }
    return playing;
}

void audio_rampdown_tick(int ch) {
    const uint8_t old_edd = (uint8_t)audio_edd(ch);
    uint8_t new_edd = old_edd - (uint8_t)audio_rampdown_offset(ch);
    if (new_edd > old_edd) new_edd = 0;

    if (new_edd) {
        audio_regs[(ch << 4) | AUDIO_ENVELOPE_DATA] &= ~AUDIO_EDD_MASK;
        audio_regs[(ch << 4) | AUDIO_ENVELOPE_DATA] |= new_edd & AUDIO_EDD_MASK;
        audio_rampdown_frame[ch] = audio_get_rampdown_frame_count(ch);
    } else {
        audio_ctrl_regs[AUDIO_CHANNEL_STOP] |= (1 << ch);
        audio_stop_channel(ch);
    }
}

bool audio_envelope_tick(int ch) {
    const uint16_t channel_mask = ch << 4;
    uint16_t new_count = audio_envelope_count(ch);
    const uint16_t curr_edd = audio_edd(ch);
    bool edd_changed = false;
    if (new_count > 0) {
        new_count--;
        audio_set_envelope_count(ch, new_count);
    }

    if (new_count == 0) {
        const uint16_t target = audio_envelope_target(ch);
        uint16_t new_edd = curr_edd;
        const uint16_t inc = audio_envelope_inc(ch);

        if (new_edd != target) {
            if (audio_envelope_sign(ch)) {
                new_edd -= inc;
                if (new_edd > curr_edd) new_edd = 0;
                else if (new_edd < target) new_edd = target;

                if (new_edd == 0) {
                    audio_ctrl_regs[AUDIO_CHANNEL_STOP] |= (1 << ch);
                    audio_stop_channel(ch);
                    return true;
                }
            } else {
                new_edd += inc;
                if (new_edd >= target) new_edd = target;
            }
        }

        if (new_edd == target) {
            new_edd = target;
            if (audio_envelope_repeat_bit(ch)) {
                const uint16_t repeat_count = audio_envelope_repeat_count(ch) - 1;
                if (repeat_count == 0) {
                    audio_regs[channel_mask | AUDIO_ENVELOPE0] = memory_read16(audio_envelope_addr_rt[ch]);
                    audio_regs[channel_mask | AUDIO_ENVELOPE1] = memory_read16(audio_envelope_addr_rt[ch] + 1);
                    audio_regs[channel_mask | AUDIO_ENVELOPE_LOOP_CTRL] = memory_read16(audio_envelope_addr_rt[ch] + 2);
                    audio_envelope_addr_rt[ch] = audio_envelope_addr(ch) + audio_eaoffset(ch);
                } else {
                    audio_set_envelope_repeat_count(ch, repeat_count);
                }
            } else {
                audio_regs[channel_mask | AUDIO_ENVELOPE0] = memory_read16(audio_envelope_addr_rt[ch]);
                audio_regs[channel_mask | AUDIO_ENVELOPE1] = memory_read16(audio_envelope_addr_rt[ch] + 1);
                audio_envelope_addr_rt[ch] += 2;
            }
            new_count = audio_envelope_load(ch);
            audio_set_envelope_count(ch, new_count);
        } else {
            new_count = audio_envelope_load(ch);
            audio_set_envelope_count(ch, new_count);
        }

        audio_set_edd(ch, (uint8_t)new_edd);
        edd_changed = true;
    }
    return edd_changed;
}

void check_audio_irq() {
    if ((audio_ctrl_regs[AUDIO_BEAT_COUNT] & (AUDIO_BIS_MASK | AUDIO_BIE_MASK)) == (AUDIO_BIS_MASK | AUDIO_BIE_MASK)) {
        cpu_ptr->execute_set_input(UNSP_IRQ4_LINE, 1);
    } else {
        cpu_ptr->execute_set_input(UNSP_IRQ4_LINE, 0);
    }
}

// Beat IRQ ticks at the same native 70312.5Hz rate as sample processing, so
// it's ticked once per generated sample below instead of via a separate timer.
void audio_beat_tick() {
    if (audio_curr_beat_base_count > 0) audio_curr_beat_base_count--;

    if (audio_curr_beat_base_count == 0) {
        audio_curr_beat_base_count = audio_ctrl_regs[AUDIO_BEAT_BASE_COUNT];

        uint16_t beat_count = audio_ctrl_regs[AUDIO_BEAT_COUNT] & AUDIO_BEAT_COUNT_MASK;
        if (beat_count > 0) {
            beat_count--;
            audio_ctrl_regs[AUDIO_BEAT_COUNT] = (audio_ctrl_regs[AUDIO_BEAT_COUNT] & ~AUDIO_BEAT_COUNT_MASK) | beat_count;
        }
        if (beat_count == 0 && (audio_ctrl_regs[AUDIO_BEAT_COUNT] & AUDIO_BIE_MASK)) {
            audio_ctrl_regs[AUDIO_BEAT_COUNT] |= AUDIO_BIS_MASK;
            check_audio_irq();
        }
    }
}

// Ported from sound_stream_update: the per-sample mixer. Writes interleaved
// stereo int16_t pairs into out[0..num_frames*2).
void generate_audio_frame(int16_t* out, int num_frames) {
    for (int i = 0; i < num_frames; i++) {
        int32_t left_total = 0, right_total = 0;

        for (int ch = 0; ch < 16; ch++) {
            if (!audio_channel_status(ch)) continue;

            bool playing = audio_advance_channel(ch);
            if (playing) {
                int32_t sample = (int16_t)(audio_regs[(ch << 4) | AUDIO_WAVE_DATA] ^ 0x8000);
                if (!(audio_ctrl_regs[AUDIO_CONTROL] & AUDIO_CONTROL_NOINT_MASK)) {
                    int32_t prev_sample = (int16_t)(audio_regs[(ch << 4) | AUDIO_WAVE_DATA_PREV] ^ 0x8000);
                    int16_t lerp_factor = (int16_t)((audio_channel_rate_accum[ch] / 70312.5) * 256.0);
                    prev_sample = (prev_sample * (0x100 - lerp_factor)) >> 8;
                    sample = (sample * lerp_factor) >> 8;
                    sample += prev_sample;
                }

                sample = (sample * (int32_t)audio_edd(ch)) >> 7;

                int32_t vol = audio_volume(ch);
                int32_t pan = audio_pan(ch);

                int32_t pan_left, pan_right;
                if (pan < 0x40) {
                    pan_left = 0x7f * vol;
                    pan_right = pan * 2 * vol;
                } else {
                    pan_left = (0x7f - pan) * 2 * vol;
                    pan_right = 0x7f * vol;
                }

                left_total += ((int16_t)sample * (int16_t)pan_left) >> 14;
                right_total += ((int16_t)sample * (int16_t)pan_right) >> 14;

                const uint16_t mask = (1 << ch);
                if (audio_ctrl_regs[AUDIO_ENV_RAMP_DOWN] & mask) {
                    if (audio_rampdown_frame[ch] > 0) audio_rampdown_frame[ch]--;
                    if (audio_rampdown_frame[ch] == 0) audio_rampdown_tick(ch);
                } else if (!(audio_ctrl_regs[AUDIO_CHANNEL_ENV_MODE] & mask)) {
                    if (audio_envclk_frame[ch] > 0) audio_envclk_frame[ch]--;
                    if (audio_envclk_frame[ch] == 0) {
                        audio_envelope_tick(ch);
                        audio_envclk_frame[ch] = audio_get_envclk_frame_count(ch);
                    }
                }
            }
        }

        if (audio_ctrl_regs[AUDIO_WAVE_IN_L]) left_total += (int32_t)(audio_ctrl_regs[AUDIO_WAVE_IN_L] - 0x8000);
        if (audio_ctrl_regs[AUDIO_WAVE_IN_R]) right_total += (int32_t)(audio_ctrl_regs[AUDIO_WAVE_IN_R] - 0x8000);

        switch (audio_vol_sel()) {
            case 0: left_total >>= 4; right_total >>= 4; break;
            default: left_total >>= 2; right_total >>= 2; break;
        }

        int32_t left_final = (int16_t)((left_total * (int16_t)audio_ctrl_regs[AUDIO_MAIN_VOLUME]) >> 7);
        int32_t right_final = (int16_t)((right_total * (int16_t)audio_ctrl_regs[AUDIO_MAIN_VOLUME]) >> 7);

        out[i * 2 + 0] = (int16_t)left_final;
        out[i * 2 + 1] = (int16_t)right_final;

        audio_beat_tick();
    }
}

uint16_t audio_r(uint32_t offset) { return audio_regs[offset]; }

void audio_w(uint32_t offset, uint16_t data) {
    switch (offset & AUDIO_CHAN_OFFSET_MASK) {
        case AUDIO_PAN_VOL: audio_regs[offset] = data & AUDIO_PAN_VOL_MASK; break;
        case AUDIO_ENVELOPE_DATA: audio_regs[offset] = data & AUDIO_ENVELOPE_DATA_MASK; break;
        case AUDIO_ADPCM_SEL: audio_regs[offset] = data & AUDIO_ADPCM_SEL_MASK; break;
        default: audio_regs[offset] = data; break;
    }
}

uint16_t audio_phase_r(uint32_t offset) { return audio_phase_regs[offset]; }

void audio_phase_w(uint32_t offset, uint16_t data) {
    // Runtime-state arrays are sized 16 (the real channel count); the raw
    // register arrays match real hardware's 0x200-word span (up to 32
    // channel-slots), so this index is defensively clamped to stay in
    // bounds even if the game ever addresses a phantom upper channel.
    const uint16_t channel = ((offset & 0x01f0) >> 4) & 0xF;

    // Ported from spg2xx_audio_device::audio_phase_w (mame/src/devices/
    // machine/spg2xx_audio.cpp): every offset actually stores, not just
    // PHASE_HIGH - some ROM code writes a phase word and then busy-spins
    // reading it back until the readback matches, which hangs forever if
    // the write silently did nothing. PHASE_HIGH/PHASE writes also derive
    // the channel's real playback rate from the phase value - the one place
    // audio_channel_rate[] actually gets set, so this was also why nothing
    // played.
    switch (offset & AUDIO_CHAN_OFFSET_MASK) {
        case AUDIO_PHASE_HIGH:
            audio_phase_regs[offset] = data & AUDIO_PHASE_HIGH_MASK;
            audio_channel_rate[channel] = ((double)audio_phase(channel) * 140625.0 * 2.0) / (double)(1 << 19);
            audio_channel_rate_accum[channel] = 0.0;
            break;
        case AUDIO_PHASE_ACCUM_HIGH:
            audio_phase_regs[offset] = data & AUDIO_PHASE_ACCUM_HIGH_MASK;
            break;
        case AUDIO_TARGET_PHASE_HIGH:
            audio_phase_regs[offset] = data & AUDIO_TARGET_PHASE_HIGH_MASK;
            break;
        case AUDIO_RAMP_DOWN_CLOCK:
            audio_phase_regs[offset] = data & AUDIO_RAMP_DOWN_CLOCK_MASK;
            break;
        case AUDIO_PHASE:
            audio_phase_regs[offset] = data;
            audio_channel_rate[channel] = ((double)audio_phase(channel) * 140625.0 * 2.0) / (double)(1 << 19);
            audio_channel_rate_accum[channel] = 0.0;
            break;
        default:
            audio_phase_regs[offset] = data;
            break;
    }
}

enum RenderFilter { FILTER_CRISP = 0, FILTER_SMOOTH = 1, FILTER_SHARP = 2, FILTER_CRT = 3, FILTER_COUNT = 4 };

struct InventoryCategory {
    const char* name;
    int start;  // index into INVENTORY_ITEMS
    int count;
};

struct InventoryItem {
    const char* name;
    uint16_t addr;
};

static const InventoryItem INVENTORY_ITEMS[] = {
    // Potions
    { "Ash Potion", 0x0C63 },
    { "Black and White Potion", 0x0C64 },
    { "Gold Potion", 0x0C65 },
    { "Grow Potion", 0x0C66 },
    { "Radioactive Potion", 0x0C67 },
    // Food Items (Still need to separate minipet foods)
    { "Alfalfa Mousse", 0x0C68 },
    { "Angel Food Cake", 0x0C86 },
    { "Bacon Balls", 0x0C69 },
    { "Bamboo Licorice", 0x0C6A },
    { "Banana Split", 0x0C6B },
    { "Biscuit", 0x0C7D },
    { "Buffalo Jerky", 0x0C6C },
    { "Cabbage Bar", 0x0C8E },
    { "Chewy Chow", 0x0C6D },
    { "Coconut Quiche", 0x0C6E },
    { "Cotton Candy", 0x0C8F },
    { "Devil's Food Cake", 0x0C87 },
    { "Dim Some", 0x0C88 },
    { "Einsteinios", 0x0C8B },
    { "Escargot", 0x0C90 },
    { "Fiber Sticks", 0x0C91 },
    { "Fried Bananas", 0x0C6F },
    { "Fried Chocolate", 0x0C7F },
    { "Granola Sprouts", 0x0C7E },
    { "Green Salad", 0x0C84 },
    { "Hagfish Hummus", 0x0C93 },
    { "Lettuce Nuggets", 0x0C70 },
    { "Main Dish", 0x0C81 },
    { "Mango Pops", 0x0C71 },
    { "Meat Loaf", 0x0C7C },
    { "Monkey Meal", 0x0C72 },
    { "Muffin Sprouts", 0x0C73 },
    { "Mulch Meal", 0x0C94 },
    { "Nice Drops", 0x0C8C },
    { "Panda Chips", 0x0C74 },
    { "Parsley Pate", 0x0C95 },
    { "Passion Fruit Salad", 0x0C75 },
    { "Pixie Twigs", 0x0C96 },
    { "Pretty Pies", 0x0C8A },
    { "Roasted Scarab", 0x0C97 },
    { "Romaine Brulee", 0x0C98 },
    { "Shiny Gold", 0x0C92 },
    { "Sour Grapes", 0x0C89 },
    { "Soy Chop", 0x0C76 },
    { "Spinach Smoothie", 0x0C77 },
    { "Steak Puree", 0x0C82 },
    { "Steak Tar Tar", 0x0C78 },
    { "Sugar Ball", 0x0C80 },
    { "Sunshine Bisque", 0x0C99 },
    { "Sushi a la Mode", 0x0C9A },
    { "Table Scraps", 0x0C79 },
    { "Taco Fries", 0x0C7A },
    { "Taco Fries Giga Meal", 0x0C7B },
    { "Tofu", 0x0C83 },
    { "Tuna Smoothie", 0x0C9B },
    { "Vitamins", 0x0C85 },
    { "Wicked Wafers", 0x0C8D },
    // Medicine (still need to separate minipet medicine)
    { "Aspirin", 0x0C9D },
    { "Chicken Soup", 0x0C9F },
    { "Chili Pill", 0x0CA0 },
    { "Cooty B Gone Spray", 0x0CA1 },
    { "EucalyptX", 0x0CA2 },
    { "Four Leaf Clover", 0x0CA4 },
    { "Fur Oil", 0x0CA3 },
    { "Hair Aid", 0x0CAC },
    { "Happy Juice", 0x0C9E },
    { "Heat Pack", 0x0CA5 },
    { "Ickicide", 0x0CA6 },
    { "Molt Cream", 0x0CAA },
    { "Oogy Tonic", 0x0CA7 },
    { "Puppermints", 0x0CA8 },
    { "Sane Salve", 0x0CA9 },
    { "Sea Bath", 0x0C9C },
    { "Sun Cream", 0x0CAB },
    { "Tummy OK", 0x0CAD },
    // House Decorations
    { "Ball of Yarn", 0x0CC8 },
    { "Bamboo Wall", 0x0CED },
    { "Baseball", 0x0CC9 },
    { "Basic Bed", 0x0CC0 },
    { "Basic Food Dish", 0x0CBC },
    { "Basic Window", 0x0CC4 },
    { "Basketball", 0x0CCA },
    { "Beach Ball", 0x0CCB },
    { "Blue Brick Wall", 0x0CE9 },
    { "Bonsai Plant", 0x0CD3 },
    { "Brick Wall", 0x0CE7 },
    { "Checker Floor", 0x0CF1 },
    { "Chew Toy", 0x0CCC },
    { "Chimes", 0x0CE0 },
    { "Crabbage Toy", 0x0CDB },
    { "Diner Bed", 0x0CC1 },
    { "Disco Ball", 0x0CDF },
    { "Dodge Ball", 0x0CCD },
    { "Dog Doll", 0x0CB1 },
    { "Dream Catcher", 0x0CE1 },
    { "Fancy Bed", 0x0CC2 },
    { "Fancy Food Dish", 0x0CBD },
    { "Fuzzy Ball", 0x0CCE },
    { "Giraffe Doll", 0x0CB2 },
    { "Grass Floor", 0x0CF3 },
    { "Green Bamboo Wall", 0x0CEE },
    { "Green Brick Wall", 0x0CE8 },
    { "Green Floor", 0x0CEF },
    { "Hackey Sack", 0x0CCF },
    { "Hamster Doll", 0x0CB3 },
    { "Hanging Lantern", 0x0CE2 },
    { "Hanging Plant", 0x0CE3 },
    { "Kitten Doll", 0x0CB4 },
    { "Lamp", 0x0CD5 },
    { "Lizard Doll", 0x0CB5 },
    { "Magic Orb", 0x0CD0 },
    { "Maori Head", 0x0CD6 },
    { "Metal Food Dish", 0x0CBE },
    { "Monkey Doll", 0x0CB6 },
    { "Ornate Wood Carving", 0x0D07 },
    { "Panda Doll", 0x0CB7 },
    { "Pixie Doll", 0x0CB8 },
    { "Potted Plant", 0x0CDC },
    { "Puffball Doll", 0x0CB9 },
    { "Purple Brick Wall", 0x0CEA },
    { "Purple Checker Floor", 0x0CF2 },
    { "Purple Vase", 0x0CD4 },
    { "Rabbit Doll", 0x0CBA },
    { "Red Brick Wall", 0x0CEC },
    { "Scorpion Doll", 0x0CBB },
    { "Snow Globe", 0x0CD7 },
    { "Special Stuffed Dog", 0x0CAE },
    { "Special Stuffed Monkey", 0x0CAF },
    { "Special Stuffed Panda", 0x0CB0 },
    { "Stress Ball", 0x0CD1 },
    { "Taco Fries Figurine", 0x0CD8 },
    { "Tennis Ball", 0x0CD2 },
    { "Tiki Bed", 0x0CC3 },
    { "Tiki Man Figurine", 0x0CD9 },
    { "Tiki Window", 0x0CC7 },
    { "Top", 0x0CDD },
    { "Trophy", 0x0CDE },
    { "Vase", 0x0CDA },
    { "Wall With Green Trim", 0x0CE5 },
    { "Wall With Purple Trim", 0x0CE6 },
    { "White Walls", 0x0CE4 },
    { "Window With Blinds", 0x0CC5 },
    { "Window With Curtains", 0x0CC6 },
    { "Wood Food Dish", 0x0CBF },
    { "Wooden Floor", 0x0CF0 },
    { "Yellow Brick Wall", 0x0CEB },
    // King's Lost Items
    { "King's Crown", 0x0CFB },
    { "King's Diamond Collar", 0x0CFC },
    { "King's Golden Shield", 0x0D02 },
    { "King's Jeweled Box", 0x0CFD },
    { "King's Magnificent Statue", 0x0CFF },
    { "King's Medallion", 0x0CFE },
    { "King's Royal Cloak", 0x0D04 },
    { "King's Royal Crest", 0x0D03 },
    { "King's Royal Water Dish", 0x0D06 },
    { "King's Scepter", 0x0D01 },
    { "King's Signet Ring", 0x0D05 },
    { "King's Sword", 0x0D00 },
    // Treasure Maps
    { "Taco Fries Place Map", 0x0D0D },
    { "King's Crown Treasure Map", 0x0D0E },
    { "Treasure Map 1", 0x0D0F },
    { "Treasure Map 2", 0x0D10 },
    { "Treasure Map 3", 0x0D11 },
    { "Treasure Map 4", 0x0D12 },
    { "Treasure Map 5", 0x0D13 },
    { "Treasure Map 6", 0x0D14 },
    { "Treasure Map 7", 0x0D15 },
    { "Treasure Map 8", 0x0D16 },
    { "Treasure Map 9", 0x0D17 },
    { "Treasure Map 10", 0x0D18 },
    // Rare Flowers
    { "Alien Flower", 0x0D3B },
    { "Blue Violet", 0x0D30 },
    { "Buttercup", 0x0D31 },
    { "Cow Belle", 0x0D32 },
    { "Daffodil", 0x0D33 },
    { "Dandelion", 0x0D34 },
    { "Magnolia", 0x0D35 },
    { "Pansy", 0x0D36 },
    { "Petunia", 0x0D37 },
    { "Regal Rose", 0x0D38 },
    { "Snow Orchid", 0x0D39 },
    { "White Lily", 0x0D3A },
    // Fishing Items
    { "Angel Fish", 0x0D3F },
    { "Blow Fish", 0x0D40 },
    { "Cat Fish", 0x0D41 },
    { "Gold Fish", 0x0D42 },
    { "Jelly Fish", 0x0D43 },
    { "Old Boot", 0x0D44 },
    { "Old Tire", 0x0D46 },
    { "Seaweed", 0x0D45 },
    { "Stick Fish", 0x0D3E },
    { "Wet Stone", 0x0D56 },
    // Miscellaneous
    { "Bag O' Bad", 0x0C58 },
    { "Bag o' Bucks", 0x0D5A },
    { "Big Bag O' Bad", 0x0C59 },
    { "Big Sack O' Stars", 0x0C5D },
    { "Garden Book 1", 0x0D58 },
    { "Garden Book 2", 0x0D59 },
    { "Giga Island Map", 0x0C52 },
    { "Goody Bag", 0x0C54 },
    { "Got Away", 0x0C51 },
    { "Great Goody Bag", 0x0C55 },
    { "Halo", 0x0C53 },
    { "Huge Bag O' Halos", 0x0C56 },
    { "Huge Bag of Lightbulbs", 0x0C62 },
    { "Huge Bag of Pitchforks", 0x0C5A },
    { "Huge Sack O' Stars", 0x0C5E },
    { "Large Bag o' Bucks", 0x0D5B },
    { "Lightbulb", 0x0C5F },
    { "Mystery Bag", 0x0D5C },
    { "Pitchfork", 0x0C57 },
    { "Sack O' Smarts", 0x0C60 },
    { "Sack O' Stars", 0x0C5C },
    { "Star", 0x0C5B },
    { "Super Sack O' Smarts", 0x0C61 },
    // Quest Collectibles
    { "Arrow Head", 0x0D22 },
    { "Bamboo Stick", 0x0D23 },
    { "Banana", 0x0D24 },
    { "BBQ Bean", 0x0D47 },
    { "Big Fancy Key", 0x0CF5 },
    { "Blue Flower", 0x0D1A },
    { "Blue Gem", 0x0D1D },
    { "Bone", 0x0D25 },
    { "Bronze Coin", 0x0D28 },
    { "Can", 0x0D54 },
    { "Carrot", 0x0D26 },
    { "Coconut", 0x0D27 },
    { "Corkscrew", 0x0CF6 },
    { "Crabbage", 0x0CF7 },
    { "Crown Wanted Poster", 0x0CF8 },
    { "Dinosaur Bone", 0x0D4D },
    { "Fossil", 0x0D4F },
    { "Friendship Token", 0x0CF4 },
    { "Furrywuzzig", 0x0CF9 },
    { "Giga Berry Plant", 0x0D2B },
    { "Gold Coin", 0x0D50 },
    { "Green Apple", 0x0D20 },
    { "Green Gem", 0x0D1E },
    { "Hive of Fireflies", 0x0D3D },
    { "Jade Stone", 0x0D2C },
    { "Key to the City", 0x0CFA },
    { "Magic Plant", 0x0D49 },
    { "Molten Hot Sauce", 0x0D48 },
    { "Old Bottle", 0x0D53 },
    { "Old Trinket", 0x0D4E },
    { "Old Vase", 0x0D51 },
    { "Orange Flower", 0x0D1B },
    { "Pack Rat Poster", 0x0D09 },
    { "Piece of Trash", 0x0D55 },
    { "Plastic Box", 0x0D52 },
    { "Rag Doll", 0x0D08 },
    { "Red Apple", 0x0D21 },
    { "Red Flower", 0x0D1C },
    { "Red Gem", 0x0D1F },
    { "Rose", 0x0D2D },
    { "Royal Badge", 0x0D0A },
    { "Royal Invitation", 0x0D0B },
    { "Salt Crystals", 0x0D4A },
    { "Silver Coin", 0x0D29 },
    { "Silver Weed Plant", 0x0D2A },
    { "Skeleton Key", 0x0D0C },
    { "Soup Stone", 0x0D4B },
    { "Stick", 0x0D2E },
    { "Sunflower", 0x0D2F },
    { "Sweet Pinewood", 0x0D3C },
    { "Taco Fries Secret Recipe", 0x0D19 },
    { "Tangy Root", 0x0D4C },
    { "Treasure Chest", 0x0D57 },
};
static const int INVENTORY_ITEM_COUNT = sizeof(INVENTORY_ITEMS) / sizeof(INVENTORY_ITEMS[0]);

static const InventoryCategory INVENTORY_CATEGORIES[] = {
    { "Potions", 0, 5 },
    { "Food Items (Still need to separate minipet foods)", 5, 52 },
    { "Medicine (still need to separate minipet medicine)", 57, 18 },
    { "House Decorations", 75, 71 },
    { "King's Lost Items", 146, 12 },
    { "Treasure Maps", 158, 12 },
    { "Rare Flowers", 170, 12 },
    { "Fishing Items", 182, 10 },
    { "Miscellaneous", 192, 23 },
    { "Quest Collectibles", 215, 53 },
};
static const int INVENTORY_CATEGORY_COUNT = sizeof(INVENTORY_CATEGORIES) / sizeof(INVENTORY_CATEGORIES[0]);
// Row layout: 0 = Filter, 1 = Movement Speed, 2 = No-Clip, 3.. = CHEAT_STATS,
// then one row per inventory category (header, togglable), then - only for
// categories currently expanded - the flat inventory items themselves. The
// item range always reserves space for every item regardless of expand
// state, so a row_index numerically identifies the same item no matter
// what's currently shown (needed for favorites/persistence to keep working
// regardless of collapse state).
// Controls sits first, as its own collapsible category (mirrors the
// Inventory categories below) so the 7 button rows stay out of the way
// until a user actually wants to remap something.
static const int ROW_CONTROLS_CAT = 0;
static const int ROW_CONTROLS_START = 1;
static const int ROW_MOD_MENU_KEY = ROW_CONTROLS_START + GAME_BUTTON_COUNT;
static const int ROW_FILTER = ROW_MOD_MENU_KEY + 1;
static const int ROW_MOVE_SPEED = ROW_FILTER + 1;
static const int ROW_NOCLIP = ROW_MOVE_SPEED + 1;
static const int ROW_MINIPET = ROW_NOCLIP + 1;
static const int ROW_MINIPET_ACTION = ROW_MINIPET + 1;
static const int ROW_FPS_LIMIT = ROW_MINIPET_ACTION + 1;
static const int ROW_INTERPOLATION = ROW_FPS_LIMIT + 1;
static const int ROW_FPS_COUNTER = ROW_INTERPOLATION + 1;
static const int ROW_STAT_HUD = ROW_FPS_COUNTER + 1;
static const int ROW_QUEST_ARROW = ROW_STAT_HUD + 1;
// Collapsible "Extractor" category - sprite-capture-related toggles that
// only matter while actively using F7/F8, grouped out of the way like the
// Controls/Inventory categories below.
static const int ROW_EXTRACTOR_CAT = ROW_QUEST_ARROW + 1;
static const int ROW_EXPORT_SCALE = ROW_EXTRACTOR_CAT + 1;
static const int ROW_EXTRACTOR_SKIP_PLAYER = ROW_EXPORT_SCALE + 1;
static const int ROW_DISABLE_SHADOWS = ROW_EXTRACTOR_SKIP_PLAYER + 1;
// Collapsible "Stats" category - just the pet care stats (Halos through
// Tricks); Money and the debug/utility values (Location ID, Player/Camera
// X/Y, Quests Done) stay always-visible at the top level.
static const int ROW_STATS_CAT = ROW_DISABLE_SHADOWS + 1;
static const int ROW_STATS_START = ROW_STATS_CAT + 1;
static const int ROW_INVENTORY_CAT_START = ROW_STATS_START + CHEAT_STAT_COUNT;
static const int ROW_INVENTORY_ITEM_START = ROW_INVENTORY_CAT_START + INVENTORY_CATEGORY_COUNT;
// Collapsible "Custom" category - user-added address/name pairs, typed in
// live via the Mod Menu itself (not hardcoded), persisted locally.
static const int MAX_CUSTOM_MODS = 24;
static const int ROW_CUSTOM_CAT = ROW_INVENTORY_ITEM_START + INVENTORY_ITEM_COUNT;
static const int ROW_CUSTOM_ADD = ROW_CUSTOM_CAT + 1;
static const int ROW_CUSTOM_START = ROW_CUSTOM_ADD + 1;
// Upper bound on distinct row_index values (used to size the logical-order
// scratch array) - NOT the number of rows visible in any given frame, which
// depends on which categories are currently expanded.
static const int MOD_MENU_TOTAL_ROW_SLOTS = ROW_CUSTOM_START + MAX_CUSTOM_MODS;
static const int MOD_MENU_MAX_VISIBLE_ROWS = 22; // main-panel scroll window cap

struct ModMenuRowRect {
    // arrow_ll/rr = double chevron (step 10), arrow_l/r = single chevron (step 1).
    Rectangle full, fav, arrow_ll, arrow_l, value, arrow_r, arrow_rr, freeze, preset_btn;
    int row_index; // ROW_FILTER/ROW_MOVE_SPEED/ROW_NOCLIP, ROW_STATS_START+i,
                    // ROW_INVENTORY_CAT_START+c, or ROW_INVENTORY_ITEM_START+i
};
struct ModMenuLayout {
    Rectangle main_panel;
    ModMenuRowRect main_rows[MOD_MENU_MAX_VISIBLE_ROWS];
    int main_row_count;
    Rectangle fav_panel;
    ModMenuRowRect fav_rows[CHEAT_STAT_COUNT + INVENTORY_ITEM_COUNT + MAX_CUSTOM_MODS];
    int fav_row_count;
    // Frozen panel is explanatory only (no row listing, unlike Favorites) -
    // just a fixed-size box, no per-item rects needed.
    Rectangle frozen_panel;
    int logical_order[MOD_MENU_TOTAL_ROW_SLOTS];
    int logical_count;
    bool has_scroll;
    // Indexed by row_index - true for the last row of a currently-expanded
    // collapsible section, so a thin line can be drawn under it to
    // visually separate it from whatever (unrelated) row follows.
    bool separator_after_row[MOD_MENU_TOTAL_ROW_SLOTS];
};
ModMenuLayout g_mod_menu_layout;
int g_mod_menu_scroll = 0;

bool g_show_stat_hud = false;
bool g_show_fps_hud = false;
bool g_mod_menu_open = false;
bool g_category_expanded[INVENTORY_CATEGORY_COUNT] = {false};
bool g_inv_favorite[INVENTORY_ITEM_COUNT] = {false};
bool g_stat_favorite[CHEAT_STAT_COUNT] = {false};
bool g_inv_frozen[INVENTORY_ITEM_COUNT] = {false};
uint16_t g_inv_frozen_value[INVENTORY_ITEM_COUNT] = {0};
bool g_stat_frozen[CHEAT_STAT_COUNT] = {false};
uint16_t g_stat_frozen_value[CHEAT_STAT_COUNT] = {0};

int g_mod_menu_selection = 0;
bool g_controls_expanded = false;
bool g_extractor_expanded = false;
bool g_stats_expanded = false;
bool g_custom_expanded = false;

// User-added Mod Menu entries: address + name typed in live, not
// hardcoded. Own favorite/freeze state per entry (can't reuse
// CheatStatePersistRecord's fixed CHEAT_STATS/INVENTORY_ITEMS arrays since
// these addresses are only known at runtime).
struct CustomModEntry { char name[24]; uint16_t addr; bool favorite; bool frozen; uint16_t frozen_value; };
CustomModEntry g_custom_mods[MAX_CUSTOM_MODS];
int g_custom_mod_count = 0;

enum CustomAddPhase { CUSTOM_ADD_NONE, CUSTOM_ADD_ADDRESS, CUSTOM_ADD_NAME };
CustomAddPhase g_custom_add_phase = CUSTOM_ADD_NONE;
char g_custom_add_buffer[24] = {};
int g_custom_add_buffer_len = 0;
uint16_t g_custom_add_pending_addr = 0;

struct CheatStatePersistRecord { uint16_t addr; uint8_t favorite; uint8_t frozen; uint16_t frozen_value; };

void save_cheat_state() {
    FILE* f = fopen(app_path("resources/data/gigapets_cheat_state.dat").c_str(), "wb");
    if (!f) return;
    int32_t counts[2] = { CHEAT_STAT_COUNT, INVENTORY_ITEM_COUNT };
    fwrite(counts, sizeof(int32_t), 2, f);
    for (int i = 0; i < CHEAT_STAT_COUNT; i++) {
        CheatStatePersistRecord rec{ CHEAT_STATS[i].addr, (uint8_t)g_stat_favorite[i], (uint8_t)g_stat_frozen[i], g_stat_frozen_value[i] };
        fwrite(&rec, sizeof(rec), 1, f);
    }
    for (int i = 0; i < INVENTORY_ITEM_COUNT; i++) {
        CheatStatePersistRecord rec{ INVENTORY_ITEMS[i].addr, (uint8_t)g_inv_favorite[i], (uint8_t)g_inv_frozen[i], g_inv_frozen_value[i] };
        fwrite(&rec, sizeof(rec), 1, f);
    }
    fclose(f);
}

// Keyed by each stat/item's real RAM address rather than its array
// position. The old scheme only checked that the TOTAL COUNT matched
// before trusting raw positional data - safe against additions/removals
// (count changes, triggering a reset) but NOT against pure reordering
// (same count, different order), which silently misapplied old favorite/
// freeze flags to the wrong stat when Health and Sickness were split
// apart at the same count. Matching by address is immune to reordering,
// insertion, or removal in any combination - a saved record for an
// address that no longer exists is just skipped.
void load_cheat_state() {
    FILE* f = fopen(app_path("resources/data/gigapets_cheat_state.dat").c_str(), "rb");
    if (!f) return;
    int32_t counts[2] = {0, 0};
    if (fread(counts, sizeof(int32_t), 2, f) != 2) { fclose(f); return; }
    for (int32_t r = 0; r < counts[0]; r++) {
        CheatStatePersistRecord rec;
        if (fread(&rec, sizeof(rec), 1, f) != 1) break;
        for (int i = 0; i < CHEAT_STAT_COUNT; i++) {
            if (CHEAT_STATS[i].addr == rec.addr) {
                g_stat_favorite[i] = rec.favorite != 0;
                g_stat_frozen[i] = rec.frozen != 0;
                g_stat_frozen_value[i] = rec.frozen_value;
                break;
            }
        }
    }
    for (int32_t r = 0; r < counts[1]; r++) {
        CheatStatePersistRecord rec;
        if (fread(&rec, sizeof(rec), 1, f) != 1) break;
        for (int i = 0; i < INVENTORY_ITEM_COUNT; i++) {
            if (INVENTORY_ITEMS[i].addr == rec.addr) {
                g_inv_favorite[i] = rec.favorite != 0;
                g_inv_frozen[i] = rec.frozen != 0;
                g_inv_frozen_value[i] = rec.frozen_value;
                break;
            }
        }
    }
    fclose(f);
}

void save_custom_mods() {
    FILE* f = fopen(app_path("resources/data/gigapets_custom_mods.dat").c_str(), "wb");
    if (!f) return;
    int32_t count = g_custom_mod_count;
    fwrite(&count, sizeof(count), 1, f);
    fwrite(g_custom_mods, sizeof(CustomModEntry), g_custom_mod_count, f);
    fclose(f);
}

void load_custom_mods() {
    FILE* f = fopen(app_path("resources/data/gigapets_custom_mods.dat").c_str(), "rb");
    if (!f) return;
    int32_t count = 0;
    if (fread(&count, sizeof(count), 1, f) == 1) {
        if (count < 0) count = 0;
        if (count > MAX_CUSTOM_MODS) count = MAX_CUSTOM_MODS;
        g_custom_mod_count = (int)fread(g_custom_mods, sizeof(CustomModEntry), count, f);
        // Drop entries from a corrupt/hand-edited file that would index
        // past ram[] or carry an unterminated name.
        int kept = 0;
        for (int i = 0; i < g_custom_mod_count; i++) {
            if (g_custom_mods[i].addr >= sizeof(ram) / sizeof(ram[0])) continue;
            g_custom_mods[i].name[sizeof(g_custom_mods[i].name) - 1] = 0;
            g_custom_mods[kept++] = g_custom_mods[i];
        }
        g_custom_mod_count = kept;
    }
    fclose(f);
}

// Ground-truthed live against real MAME (watchpoint on the money address
// during an actual shop purchase): the ROM saves per-field, immediately,
// as part of the action that changed the value - AdjustPlayerMoney (ROM
// 0x02dc02) calls SaveMoneyToSlot(0x02e4a8) right after updating money,
// completely independent of Quit. A mod-menu edit was just a raw ram[]
// poke with no equivalent save call, which is the real reason money/item
// changes never persisted - not a Quit-timing issue at all. Calling the
// real save function ourselves (via call_rom_function) makes a mod-menu
// edit behave exactly like the real in-game action that changes this
// value, using the ROM's own proven-correct save-bit-packing logic
// instead of reimplementing it. Args match SaveMoneyToSlot's real
// calling convention, pushed in the same order its real caller uses:
// field(=0) first/deep, then slot last/close (verified via its own
// prologue: bp+8 = slot).
static const uint16_t MONEY_STAT_ADDR = 0x0C4D;
void set_stat_value(int stat_idx, int32_t val) {
    if (val < CHEAT_STATS[stat_idx].min_val) val = CHEAT_STATS[stat_idx].min_val;
    if (val > CHEAT_STATS[stat_idx].max_val) val = CHEAT_STATS[stat_idx].max_val;
    ram[CHEAT_STATS[stat_idx].addr] = (uint16_t)val;
    if (CHEAT_STATS[stat_idx].addr == MONEY_STAT_ADDR) {
        call_rom_function(0x02E4A8, { 0, ram[0x1AFF] });
    }
    if (g_stat_frozen[stat_idx]) {
        g_stat_frozen_value[stat_idx] = (uint16_t)val;
        save_cheat_state();
    }
}

// King's Lost Items, Treasure Maps, and Rare Flowers are one-of-a-kind
// collectibles in-game (own it or don't) - capped at 1 rather than the
// usual uint16_t range. Matched by category name (not index) so this stays
// correct even if the generated INVENTORY_CATEGORIES table is ever
// regenerated in a different order.
bool inv_item_capped_at_one(int item_idx) {
    for (int c = 0; c < INVENTORY_CATEGORY_COUNT; c++) {
        const InventoryCategory& cat = INVENTORY_CATEGORIES[c];
        if (item_idx < cat.start || item_idx >= cat.start + cat.count) continue;
        return strcmp(cat.name, "King's Lost Items") == 0
            || strcmp(cat.name, "Treasure Maps") == 0
            || strcmp(cat.name, "Rare Flowers") == 0;
    }
    return false;
}

// Same as set_stat_value, for a flat inventory item index. No general
// per-item min/max is known from the CT table, so this clamps to the full
// uint16_t range like the "uncapped" stats, except the one-per-item
// categories above.
void set_inv_value(int item_idx, int32_t new_val) {
    int32_t max_val = inv_item_capped_at_one(item_idx) ? 1 : 65535;
    if (new_val < 0) new_val = 0;
    if (new_val > max_val) new_val = max_val;
    ram[INVENTORY_ITEMS[item_idx].addr] = (uint16_t)new_val;
    // Same fix as set_stat_value's money case, for items: the real
    // in-game item-gain path (Item_IncrementOwnedAndSave, ROM 0x02da4e,
    // ground-truthed live via a real MAME purchase) calls
    // SaveItemFieldToSlot(0x02e5c0) immediately after updating the
    // quantity. Args pushed in the real caller's order: itemIdx first/
    // deep, field(=0) middle, slot last/close (verified via the
    // function's own prologue: bp+7 = slot).
    //
    // itemIdx here is NOT our own INVENTORY_ITEMS array position - found
    // via a same-session save+immediately-reload round-trip test that it
    // always came back 0 regardless of item. SaveItemFieldToSlot computes
    // the item's own RAM address internally as 0xC51+itemIdx (confirmed in
    // its disassembly), so the ROM's real item-ID numbering IS "address
    // minus 0xC51" directly - but INVENTORY_ITEMS is ordered by category
    // (potions, figurines, etc.), not by that same address order, so our
    // array position and the ROM's real ID only coincidentally matched for
    // early entries. Deriving the real ID from the address itself (which
    // is independently known-correct - it's what every existing read/
    // write already uses) fixes it for every item, not just this one.
    call_rom_function(0x02E5C0, { (uint16_t)(INVENTORY_ITEMS[item_idx].addr - 0xC51), 0, ram[0x1AFF] });
    if (g_inv_frozen[item_idx]) {
        g_inv_frozen_value[item_idx] = (uint16_t)new_val;
        save_cheat_state();
    }
}
int g_editing_row = -1;
char g_edit_buffer[8] = {};
int g_edit_buffer_len = 0;
int g_open_dropdown_stat = -1; // stat index whose preset dropdown is open, -1 = none
Rectangle g_dropdown_anchor = {};
int g_dropdown_gp_index = 0; // gamepad-highlighted option row inside the open dropdown
int g_awaiting_keybind_for = -1; // GameButton index waiting on the next keypress to rebind, -1 = none

// Movement speed: applied as a post-frame delta amplification (see the main
// loop, right after the CPU's per-frame instruction budget is spent) rather
// than by patching game logic - the game's own ~7 collision-checked
// sub-steps per frame already moved the player once this frame; we just
// scale that observed delta up further. This means the "extra" portion of
// movement at speeds above 1x does not get its own collision check until
// the *next* frame's normal movement re-evaluates from the new position -
// an acceptable tradeoff for a cheat, but it can clip slightly into walls
// at higher multipliers before the game's own collision catches up.
static const int MOVEMENT_SPEED_LEVELS[] = { 1, 2, 3, 4, 5, 6, 7, 8 };
static const int MOVEMENT_SPEED_LEVEL_COUNT = sizeof(MOVEMENT_SPEED_LEVELS) / sizeof(MOVEMENT_SPEED_LEVELS[0]);
int g_movement_speed_idx = 0;
int16_t g_last_player_x = 0;
int16_t g_last_player_y = 0;
bool g_have_last_player_pos = false;

// Minipets: on real hardware, which of the 8 accessory-handheld species
// appears is decided entirely by which physical minipet toy is link-cable-
// connected - there is no in-game picker. This mod-menu row exists to
// simulate that (pick a species directly) since the PC port obviously has
// no such hardware. Everything below is a byte-verified reconstruction of
// the real spawn RAM effects (InitMiniPetZapAnimation @ 0x045ed0 +
// PlayMiniPetZapAnimAlt @ 0x046667 + PlaySpriteAnimation @ 0x04dc51, all
// traced via live decompile+disassembly this session), replicated as
// direct data writes rather than an injected CPU call - the previous
// attempt at a live call corrupted rendering via `ds:`-relative addressing
// context a synthetic call can't reproduce (see prior investigation notes).
// Likewise, writing the tracking flag alone without this setup is the
// original "zap spam" bug: the animation struct never gets populated, so
// the state machine's busy-check reads false from tick one and the states
// ping-pong every tick instead of playing a real zap-in and settling into
// Follow.
static const uint16_t MINIPET_TRACKING_FLAG_ADDR = 0x1980;
static const uint16_t MINIPET_TRAIL_BASE = 0x1983;   // 20 slots x 3 words {X, Y, poseId}
static const int MINIPET_TRAIL_SLOT_COUNT = 20;
static const uint16_t MINIPET_TRAIL_READ_IDX_ADDR = 0x197d;
static const uint16_t MINIPET_TRAIL_WRITE_IDX_ADDR = 0x197e;
static const uint16_t MINIPET_ANIM_STRUCT_ADDR = 0x1a37; // SpriteAnimState, 10 words
// Also read by InitMiniPetZapAnimation's real species-lookup - confirmed
// dead/always-0-in-practice on real hardware too (GetMiniPetState() has no
// writers anywhere in this ROM, so it always returns its power-on default),
// which is why this is safe to drive directly instead: nothing else
// legitimately owns it. Only survives if the struct is already the 0x7B
// sentinel - see spawn_minipet()'s comment on why that matters.
static const uint16_t GAME_CURRENT_ROOM_ID_ADDR = 0x1a96;
// g_linkFeatureAvailable - real hardware's "minipet handheld is physically
// link-cabled to the console" flag (set/reset by Link_UpdateAndSync from
// live cart-port I/O). MiniPet_HandleOverworldInput reads this every tick
// once tracking is active (FeedOrAdvanceMiniPet call at ROM 0x0146d8): with
// this nonzero AND tracking active it's a genuine no-op (real Follow just
// ticks normally), but with this at 0 (our default, since we have no real
// cart port) it takes the "handheld unplugged" branch, which the decompiler
// hid behind a fake trampoline return - confirmed via raw disassembly at
// 0x046231-0x046235, it force-writes the tracking flag to 4 (despawn)
// almost immediately after Follow begins. That same cascade's dead
// GetMiniPetState()-remap (0x0461cc) also stomps GAME_CURRENT_ROOM_ID_ADDR
// back toward 0 whenever it runs, which is why species selection looked
// scrambled - one root cause explains both bugs. Simulate "handheld always
// connected" by keeping this pinned to 1 for the whole spawned lifetime.
static const uint16_t MINIPET_LINK_AVAILABLE_ADDR = 0x1b19;
// Species -> animation frame ID, literal values from PlayMiniPetZapAnimAlt's
// own stack-local table (not a ROM table - baked into the function itself).
static const uint16_t MINIPET_ANIM_FRAME_IDS[8] = { 0x57, 0x71, 0x6c, 0x121, 0x135, 0x150, 0x155, 0x13a };
// Labels only, not the species index itself - the array position is what
// gets written to GAME_CURRENT_ROOM_ID_ADDR and drives the real ROM sprite
// lookup (PlayMiniPetWalkAnim's table). The original alphabetical guess was
// never ground-truthed against the ROM's actual internal ordering; empirical
// in-game testing corrected indices 0/2/5/7, and all 8 (including 1/3/4/6 -
// Dragon Lizard/Pixie/Puffball/Scorpion) are now user-confirmed correct.
static const char* MINIPET_NAMES[8] = { "Hamster", "Dragon Lizard", "Tomcat", "Pixie", "Puffball", "Bunny", "Scorpion", "Pup" };
int g_minipet_picker_idx = 0;  // 0-7, browsing only - no side effect, doesn't spawn/despawn anything
bool g_minipet_spawned = false; // separate action row toggles this

void spawn_minipet(int species_idx) {
    if (species_idx < 0 || species_idx > 7) return;
    int16_t anchor_x = (int16_t)ram[PLAYER_WORLD_X];
    int16_t anchor_y = (int16_t)ram[PLAYER_WORLD_Y];

    // Trail buffer: slot 0 seeds at the player's position; each later slot's
    // X is the previous slot's X + 2 (a ramp, not identical - confirmed via
    // disassembly), Y and poseId constant across all 20 slots.
    ram[MINIPET_TRAIL_BASE + 0] = (uint16_t)anchor_x;
    ram[MINIPET_TRAIL_BASE + 1] = (uint16_t)(anchor_y - 6);
    ram[MINIPET_TRAIL_BASE + 2] = 0;
    for (int i = 1; i < MINIPET_TRAIL_SLOT_COUNT; i++) {
        uint16_t prev_x = ram[MINIPET_TRAIL_BASE + (i - 1) * 3];
        ram[MINIPET_TRAIL_BASE + i * 3 + 0] = (uint16_t)(prev_x + 2);
        ram[MINIPET_TRAIL_BASE + i * 3 + 1] = (uint16_t)(anchor_y - 6);
        ram[MINIPET_TRAIL_BASE + i * 3 + 2] = 0;
    }
    ram[MINIPET_TRAIL_READ_IDX_ADDR] = MINIPET_TRAIL_SLOT_COUNT - 1;
    ram[MINIPET_TRAIL_WRITE_IDX_ADDR] = 0;

    // Animation struct: field 0 (animId) MUST be the sentinel 0x7B, not a
    // species-specific frame ID - confirmed via live MAME testing + full
    // disassembly of FeedOrAdvanceMiniPet (ROM 0x046172), which runs every
    // single tick once tracking is nonzero and treats "struct[0] != 0x7B" as
    // an unhandled found-item-reaction event: it overwrites the struct with
    // its own animId=0x7B (exact match to what was observed clobbering this
    // struct one tick after spawning), resets g_currentRoomId, and rewrites
    // the tracking flag - a cascade that ends in the animation getting
    // force-despawned. The species-specific frame ID (from
    // MINIPET_ANIM_FRAME_IDS) is only valid transiently during the brief
    // real zap-in ceremony screen (EnterActivityAndRunLoop), which hands off
    // with the struct already reset to this sentinel before normal per-tick
    // logic (TickMiniPetZapAndFollowState et al) ever sees it - skipping
    // straight to the sentinel here skips that one-time visual but lands
    // directly in the stable, correctly-tracked state.
    ram[MINIPET_ANIM_STRUCT_ADDR + 0] = 0x7B;
    ram[MINIPET_ANIM_STRUCT_ADDR + 1] = 2;
    ram[MINIPET_ANIM_STRUCT_ADDR + 2] = 0;
    ram[MINIPET_ANIM_STRUCT_ADDR + 3] = 0;
    ram[MINIPET_ANIM_STRUCT_ADDR + 4] = 14;
    ram[MINIPET_ANIM_STRUCT_ADDR + 5] = 2;
    ram[MINIPET_ANIM_STRUCT_ADDR + 6] = 0x1000;
    ram[MINIPET_ANIM_STRUCT_ADDR + 7] = 0;
    ram[MINIPET_ANIM_STRUCT_ADDR + 8] = 0;
    ram[MINIPET_ANIM_STRUCT_ADDR + 9] = 0;

    // g_currentRoomId doubles as the species index - confirmed ground truth
    // via PlayMiniPetAnimForRoom/PlayMiniPetWalkAnim's decompile (not
    // PlayMiniPetZapAnimAlt as originally guessed - that table is never
    // actually reached since flag never passes through state 3 here).
    ram[GAME_CURRENT_ROOM_ID_ADDR] = (uint16_t)species_idx;

    // Simulate "handheld physically connected" - harmless to keep even
    // though the real despawn fix turned out to be the sentinel-gate patch
    // in the main tick loop, not this flag.
    ram[MINIPET_LINK_AVAILABLE_ADDR] = 1;

    ram[MINIPET_TRACKING_FLAG_ADDR] = 2; // matches InitMiniPetZapAnimation's own real write exactly
}

// Replicates TickMiniPetZapAndFollowState's own trail-buffer write exactly
// (ROM 0x046267-0x046289: decrement writeIndex, wrap 0->0x3b, write
// {X, Y-6, poseId=0}) so the movement-speed cheat can backfill the
// intermediate steps a real normal-speed walk would have recorded. Without
// this, the amplified per-tick jump gets recorded as a single trail entry,
// and since the follower always trails by a fixed NUMBER of slots (not a
// fixed distance), each slot then represents proportionally more real
// distance - the pet visibly lags farther behind the faster you move.
void minipet_trail_record_step(int16_t x, int16_t y) {
    uint16_t write_index = ram[MINIPET_TRAIL_WRITE_IDX_ADDR];
    write_index = (write_index == 0) ? 0x3b : (write_index - 1);
    ram[MINIPET_TRAIL_WRITE_IDX_ADDR] = write_index;
    ram[MINIPET_TRAIL_BASE + write_index * 3 + 0] = (uint16_t)x;
    ram[MINIPET_TRAIL_BASE + write_index * 3 + 1] = (uint16_t)(y - 6);
    ram[MINIPET_TRAIL_BASE + write_index * 3 + 2] = 0;
}

void despawn_minipet() {
    ram[MINIPET_ANIM_STRUCT_ADDR] = 0xFFFF; // -1, matches the real despawn state's own write
    ram[MINIPET_TRACKING_FLAG_ADDR] = 0;
    ram[MINIPET_LINK_AVAILABLE_ADDR] = 0; // "unplug" the simulated handheld
}

// Room/area transitions reposition the player to a new spawn point in a
// single frame - a large, deliberate jump, not organic walking - and must
// not be amplified the same way (doing so was flinging the player outside
// the new room's valid bounds, getting them stuck). Location ID is coarse
// (it tracks broad unlockable game regions - Downtown, Beach, etc, per the
// CT table's dropdown - not individual rooms/buildings, so it won't catch
// every building-entry transition), but the raw per-tick magnitude clamp
// below is the real backstop: any single-tick delta big enough to matter
// (a teleport-style reposition) is far larger than this threshold, so it
// gets zeroed regardless of whether a location-ID change also fired.
static const int MOVEMENT_TELEPORT_THRESHOLD = 32; // raw per-frame delta beyond this is treated as a non-organic jump
uint16_t g_last_location_id = 0;
bool g_have_last_location_id = false;
int g_location_change_grace_frames = 0;

// No-Clip: ported directly from the user's own Cheat Engine AOB-scan script
// (3 "collision check returns 0" instruction sites, each patched so the
// result is 1/free-space instead). Cheat Engine found these by scanning our
// own process's raw memory - i.e. this rom[] array - so the same patterns
// are searched here directly, word-addressed instead of byte-addressed.
// Our CPU core runs the plain interpreter (unsp.cpp's execute_run(), not
// the DRC recompiler in unspdrc.cpp), so there is no instruction cache to
// invalidate - a patched rom[] word takes effect on the very next fetch.
struct NoClipPatch { int32_t patch_word_addr = -1; uint16_t original_value = 0; };
NoClipPatch g_noclip_patches[3];
bool g_cheat_noclip = false;

// Disable Shadows: same rom[] live-patch technique as No-Clip above, but a
// single fixed, already-named address instead of an AOB scan - a tester
// found DrawShadow (ROM 0x01DEB5, independently confirmed as Ghidra's own
// name for this exact address) and patched its real "push bp,sp" prologue
// (0xDA88) to a bare "retf" (0x9A90), making it return before drawing
// anything. Same effect here: it's a real ROM function neutered in place,
// not a synthetic effect, so it also naturally removes shadows from the
// sprite extractor's output (DrawShadow never populates a shadow sprite
// table entry to begin with).
static const uint32_t SHADOW_PATCH_ADDR = 0x01DEB5;
static const uint16_t SHADOW_RETF_OPCODE = 0x9A90;
uint16_t g_shadow_patch_original = 0;
bool g_shadows_disabled = false;

int32_t find_word_pattern(const uint16_t* haystack, uint32_t haystack_len, const uint16_t* needle, uint32_t needle_len) {
    if (needle_len == 0 || haystack_len < needle_len) return -1;
    for (uint32_t i = 0; i + needle_len <= haystack_len; i++) {
        bool match = true;
        for (uint32_t j = 0; j < needle_len; j++) {
            if (haystack[i + j] != needle[j]) { match = false; break; }
        }
        if (match) return (int32_t)i;
    }
    return -1;
}

void find_noclip_patch_addresses() {
    // Each pattern is the Cheat Engine AOB bytes reinterpreted as little-
    // endian uint16_t words; the patch target is always +4 bytes (+2 words)
    // into the match, matching the CT script's "NoClipN+4: db 41 92".
    static const uint16_t pattern1[] = { 0x9F0F, 0xC1E5, 0x9240, 0x9888, 0x9A90, 0xDA88, 0x2049 };
    static const uint16_t pattern2[] = { 0x9F0F, 0xC253, 0x9240, 0x9F0F, 0xC254, 0x9241, 0x0049 };
    static const uint16_t pattern3[] = { 0x9F0F, 0xC2FC, 0x9240, 0x9F0F, 0xC305, 0x980A, 0x0841 };
    const uint16_t* patterns[3] = { pattern1, pattern2, pattern3 };
    for (int p = 0; p < 3; p++) {
        int32_t match = find_word_pattern(rom, 0x400000, patterns[p], 7);
        if (match >= 0) {
            g_noclip_patches[p].patch_word_addr = match + 2;
            g_noclip_patches[p].original_value = rom[match + 2];
        }
    }
    

}

void set_shadows_disabled(bool disabled) {
    rom[SHADOW_PATCH_ADDR] = disabled ? SHADOW_RETF_OPCODE : g_shadow_patch_original;
}

void set_noclip_enabled(bool enabled) {
    for (int p = 0; p < 3; p++) {
        if (g_noclip_patches[p].patch_word_addr < 0) continue;
        rom[g_noclip_patches[p].patch_word_addr] = enabled
            ? (uint16_t)(g_noclip_patches[p].original_value | 0x0001)
            : g_noclip_patches[p].original_value;
    }
}

// Mod menu layout: computed once per frame (from input handling, before
// rendering) into g_mod_menu_layout so both mouse hit-testing and drawing
// use identical row geometry. Two panels: the main list (Filter/Movement
// Speed/No-Clip/stats/inventory categories) and a docked Favorites panel
// showing just the pinned stats/items, so favorited cheats stay reachable
// without hunting through the full list. The main list can be far taller
// than fits on screen once inventory categories are expanded (up to 268
// items), so it's scrolled: logical_order holds every currently-visible
// logical row (respecting collapse state) in display order, and main_rows
// holds only the slice of that currently scrolled into view.
 // index into logical_order of the first visible row

// Both computed once (name/value tables are static, not every frame) and
// shared between layout and drawing - label_extra_w widens the gap before
// the chevrons for long labels (e.g. "King's Magnificent Statue"),
// value_extra_w widens the gap AFTER the value text for long values (e.g.
// minipet species names, "Zapped In"/"Zapped Out") so neither overlaps the
// chevrons. Both start at -1 (not yet computed).
int s_label_extra_w = -1;
int s_value_extra_w = -1;

bool row_is_category(int row_index) { return row_index >= ROW_INVENTORY_CAT_START && row_index < ROW_INVENTORY_ITEM_START; }
// Bounded at ROW_CUSTOM_CAT now that the Custom category's rows sit right
// after Inventory in the chain - this used to have no upper bound (safe
// back when Inventory was the last thing in the chain), which would have
// silently misclassified every Custom row as an inventory item once
// something was appended after it.
bool row_is_inv_item(int row_index) { return row_index >= ROW_INVENTORY_ITEM_START && row_index < ROW_CUSTOM_CAT; }
bool row_is_custom(int row_index) { return row_index >= ROW_CUSTOM_START && row_index < ROW_CUSTOM_START + g_custom_mod_count; }
// Any collapsible section header (toggled by Enter/click same as a value
// row's chevron, rather than opening an edit prompt).
bool row_is_expand_header(int row_index) {
    return row_is_category(row_index) || row_index == ROW_CONTROLS_CAT
        || row_index == ROW_EXTRACTOR_CAT || row_index == ROW_STATS_CAT || row_index == ROW_CUSTOM_CAT;
}
// Upper bound must be ROW_MOD_MENU_KEY specifically (== ROW_CONTROLS_START +
// GAME_BUTTON_COUNT, i.e. one past the last real keybind row), NOT
// ROW_FILTER - ROW_MOD_MENU_KEY sits between them, and using ROW_FILTER here
// used to work only because it happened to equal that boundary before
// ROW_MOD_MENU_KEY existed. Using it now would misclassify ROW_MOD_MENU_KEY
// itself as a real keybind row, indexing GAME_BUTTONS[GAME_BUTTON_COUNT] out
// of bounds and crashing on open.
bool row_is_keybind(int row_index) { return row_index >= ROW_CONTROLS_START && row_index < ROW_MOD_MENU_KEY; }
bool row_has_value(int row_index) { return (row_index >= ROW_STATS_START && row_index < ROW_INVENTORY_CAT_START) || row_is_inv_item(row_index) || row_is_custom(row_index); }

void compute_mod_menu_layout(int screen_w, int screen_h) {
    int row_h = 22;

    memset(g_mod_menu_layout.separator_after_row, 0, sizeof(g_mod_menu_layout.separator_after_row));

    // Pass 1: full logical row order, respecting current expand state.
    int lidx = 0;
    g_mod_menu_layout.logical_order[lidx++] = ROW_CONTROLS_CAT;
    if (g_controls_expanded && GAME_BUTTON_COUNT > 0) {
        for (int b = 0; b < GAME_BUTTON_COUNT; b++) g_mod_menu_layout.logical_order[lidx++] = ROW_CONTROLS_START + b;
        g_mod_menu_layout.logical_order[lidx++] = ROW_MOD_MENU_KEY;
        g_mod_menu_layout.separator_after_row[ROW_MOD_MENU_KEY] = true;
    }
    g_mod_menu_layout.logical_order[lidx++] = ROW_FILTER;
    g_mod_menu_layout.logical_order[lidx++] = ROW_MOVE_SPEED;
    g_mod_menu_layout.logical_order[lidx++] = ROW_NOCLIP;
    g_mod_menu_layout.logical_order[lidx++] = ROW_MINIPET;
    g_mod_menu_layout.logical_order[lidx++] = ROW_MINIPET_ACTION;
    g_mod_menu_layout.logical_order[lidx++] = ROW_FPS_LIMIT;
    g_mod_menu_layout.logical_order[lidx++] = ROW_INTERPOLATION;
    g_mod_menu_layout.logical_order[lidx++] = ROW_FPS_COUNTER;
    g_mod_menu_layout.logical_order[lidx++] = ROW_STAT_HUD;
    g_mod_menu_layout.logical_order[lidx++] = ROW_QUEST_ARROW;
    g_mod_menu_layout.logical_order[lidx++] = ROW_EXTRACTOR_CAT;
    if (g_extractor_expanded) {
        g_mod_menu_layout.logical_order[lidx++] = ROW_EXPORT_SCALE;
        g_mod_menu_layout.logical_order[lidx++] = ROW_EXTRACTOR_SKIP_PLAYER;
        g_mod_menu_layout.logical_order[lidx++] = ROW_DISABLE_SHADOWS;
        g_mod_menu_layout.separator_after_row[ROW_DISABLE_SHADOWS] = true;
    }
    // The debug/utility values (12+: Location ID, Player/Camera X/Y, Quests
    // Done) always visible; the actual pet care stats (1-11: Halos through
    // Tricks) collapsible under their own header, matching the split the
    // user asked for. Money moves down next to the rest of Inventory below,
    // not grouped with these.
    g_mod_menu_layout.logical_order[lidx++] = ROW_STATS_CAT;
    if (g_stats_expanded) {
        for (int i = 1; i <= 11; i++) g_mod_menu_layout.logical_order[lidx++] = ROW_STATS_START + i;
        g_mod_menu_layout.separator_after_row[ROW_STATS_START + 11] = true;
    }
    for (int i = 12; i < CHEAT_STAT_COUNT; i++) g_mod_menu_layout.logical_order[lidx++] = ROW_STATS_START + i;
    g_mod_menu_layout.logical_order[lidx++] = ROW_STATS_START + 0; // Money, next to Inventory
    for (int c = 0; c < INVENTORY_CATEGORY_COUNT; c++) {
        g_mod_menu_layout.logical_order[lidx++] = ROW_INVENTORY_CAT_START + c;
        if (g_category_expanded[c]) {
            const InventoryCategory& cat = INVENTORY_CATEGORIES[c];
            for (int j = 0; j < cat.count; j++) {
                g_mod_menu_layout.logical_order[lidx++] = ROW_INVENTORY_ITEM_START + cat.start + j;
            }
            if (cat.count > 0) {
                g_mod_menu_layout.separator_after_row[ROW_INVENTORY_ITEM_START + cat.start + cat.count - 1] = true;
            }
        }
    }
    g_mod_menu_layout.logical_order[lidx++] = ROW_CUSTOM_CAT;
    if (g_custom_expanded) {
        g_mod_menu_layout.logical_order[lidx++] = ROW_CUSTOM_ADD;
        for (int i = 0; i < g_custom_mod_count; i++) g_mod_menu_layout.logical_order[lidx++] = ROW_CUSTOM_START + i;
        g_mod_menu_layout.separator_after_row[g_custom_mod_count > 0 ? (ROW_CUSTOM_START + g_custom_mod_count - 1) : ROW_CUSTOM_ADD] = true;
    }
    g_mod_menu_layout.logical_count = lidx;

    int visible_rows = (screen_h - 90) / row_h;
    if (visible_rows < 8) visible_rows = 8;
    if (visible_rows > MOD_MENU_MAX_VISIBLE_ROWS) visible_rows = MOD_MENU_MAX_VISIBLE_ROWS;
    g_mod_menu_layout.has_scroll = g_mod_menu_layout.logical_count > visible_rows;

    int max_scroll = g_mod_menu_layout.logical_count - visible_rows;
    if (max_scroll < 0) max_scroll = 0;
    if (g_mod_menu_scroll > max_scroll) g_mod_menu_scroll = max_scroll;
    if (g_mod_menu_scroll < 0) g_mod_menu_scroll = 0;

    int shown = g_mod_menu_layout.logical_count - g_mod_menu_scroll;
    if (shown > visible_rows) shown = visible_rows;
    if (shown < 0) shown = 0;

    // Longest item/stat name (e.g. "King's Magnificent Statue") determines
    // how far the chevron/value columns need to shift right so nothing gets
    // truncated - computed once since the name tables are static, not every
    // frame.
    if (s_label_extra_w < 0) {
        int widest = 0;
        for (int i = 0; i < CHEAT_STAT_COUNT; i++) widest = std::max(widest, ui_measure_text(CHEAT_STATS[i].name, 15));
        for (int i = 0; i < INVENTORY_ITEM_COUNT; i++) widest = std::max(widest, ui_measure_text(INVENTORY_ITEMS[i].name, 15));
        const int available = 148 - 22; // label start (px+22) to arrow_ll (px+148) below
        s_label_extra_w = widest > available ? (widest - available + 4) : 0;
    }

    // Same idea for the VALUE column - unlike the label, value_str was never
    // clipped or measured against its 42px column at all, so anything wider
    // (minipet species names, "Zapped In"/"Zapped Out") just overflowed
    // straight into the ">"/">>" chevrons. Covers every value string that
    // can appear, not just the has_value (stat/item) rows - MINIPET_NAMES
    // and the action-toggle text are drawn via the same r.value/arrow_r
    // positions despite not being "stat" rows.
    if (s_value_extra_w < 0) {
        int widest = 0;
        for (int i = 0; i < 8; i++) widest = std::max(widest, ui_measure_text(MINIPET_NAMES[i], 15));
        widest = std::max(widest, ui_measure_text("Zapped In", 15));
        widest = std::max(widest, ui_measure_text("Zapped Out", 15));
        widest = std::max(widest, ui_measure_text("Unlimited", 15));
        widest = std::max(widest, ui_measure_text("65535", 15));
        for (int i = 0; i < (int)(sizeof(SICKNESS_PRESETS) / sizeof(SICKNESS_PRESETS[0])); i++)
            widest = std::max(widest, ui_measure_text(SICKNESS_PRESETS[i].label, 15));
        const int available = 224 - 180; // value start (px+180) to arrow_r (px+224) below
        s_value_extra_w = widest > available ? (widest - available + 4) : 0;
    }

    int mw = 300 + s_label_extra_w + s_value_extra_w, fmw = 290 + s_value_extra_w, gap = 10;
    // +14 over the base 44 reserves room for the Fav/Freeze checkbox legend
    // drawn once under the title (see the main-panel draw call) - the
    // fav_panel keeps the plain 44, it doesn't draw that legend itself.
    int mh = 58 + shown * row_h + (g_mod_menu_layout.has_scroll ? 14 : 0);

    int fav_count = 0;
    for (int i = 0; i < CHEAT_STAT_COUNT; i++) if (g_stat_favorite[i]) fav_count++;
    for (int i = 0; i < INVENTORY_ITEM_COUNT; i++) if (g_inv_favorite[i]) fav_count++;
    int fmh = 44 + (fav_count > 0 ? fav_count : 1) * row_h;

    int fzmh = 44 + row_h; // fixed size - explanatory only, never lists rows

    int combined_w = mw + gap + fmw;
    int mx = (screen_w - combined_w) / 2;
    if (mx < 10) mx = 10; // combined_w can exceed screen_w (long item names + a narrow window) - never let the main panel start off-screen
    int my = (screen_h - mh) / 2;
    if (my < 10) my = 10;
    int fmx = mx + mw + gap;
    int fmy = my;
    int fzmx = fmx;
    int fzmy = fmy + fmh + gap; // stacked below the Favorites panel, same column

    g_mod_menu_layout.main_panel = Rectangle{ (float)mx, (float)my, (float)mw, (float)mh };
    g_mod_menu_layout.fav_panel = Rectangle{ (float)fmx, (float)fmy, (float)fmw, (float)fmh };
    g_mod_menu_layout.frozen_panel = Rectangle{ (float)fzmx, (float)fzmy, (float)fmw, (float)fzmh };

    auto fill_row = [&](int px, int y, int row_index) {
        ModMenuRowRect r;
        r.full = Rectangle{ (float)(px + 6), (float)y, (float)(mw - 12), (float)row_h };
        r.fav = Rectangle{ (float)(px + 10), (float)(y + 3), 14, 14 };
        r.arrow_ll = Rectangle{ (float)(px + 148 + s_label_extra_w), (float)y, 14, (float)row_h };
        r.arrow_l = Rectangle{ (float)(px + 164 + s_label_extra_w), (float)y, 14, (float)row_h };
        r.value = Rectangle{ (float)(px + 180 + s_label_extra_w), (float)y, (float)(42 + s_value_extra_w), (float)row_h };
        r.arrow_r = Rectangle{ (float)(px + 224 + s_label_extra_w + s_value_extra_w), (float)y, 14, (float)row_h };
        r.arrow_rr = Rectangle{ (float)(px + 240 + s_label_extra_w + s_value_extra_w), (float)y, 14, (float)row_h };
        r.freeze = Rectangle{ (float)(px + 258 + s_label_extra_w + s_value_extra_w), (float)(y + 3), 14, 14 };
        r.preset_btn = Rectangle{ (float)(px + 276 + s_label_extra_w + s_value_extra_w), (float)(y + 3), 14, 14 };
        r.row_index = row_index;
        return r;
    };

    int y = my + 46, out_idx = 0;
    for (int i = 0; i < shown; i++) {
        g_mod_menu_layout.main_rows[out_idx++] = fill_row(mx, y, g_mod_menu_layout.logical_order[g_mod_menu_scroll + i]);
        y += row_h;
    }
    g_mod_menu_layout.main_row_count = out_idx;

    int fy = fmy + 32, fidx = 0;
    for (int i = 0; i < CHEAT_STAT_COUNT; i++) {
        if (!g_stat_favorite[i]) continue;
        g_mod_menu_layout.fav_rows[fidx++] = fill_row(fmx, fy, ROW_STATS_START + i);
        fy += row_h;
    }
    for (int i = 0; i < INVENTORY_ITEM_COUNT; i++) {
        if (!g_inv_favorite[i]) continue;
        g_mod_menu_layout.fav_rows[fidx++] = fill_row(fmx, fy, ROW_INVENTORY_ITEM_START + i);
        fy += row_h;
    }
    for (int i = 0; i < g_custom_mod_count; i++) {
        if (!g_custom_mods[i].favorite) continue;
        g_mod_menu_layout.fav_rows[fidx++] = fill_row(fmx, fy, ROW_CUSTOM_START + i);
        fy += row_h;
    }
    g_mod_menu_layout.fav_row_count = fidx;
}

// delta is the actual amount to add for value-bearing rows (+-1 from a
// single chevron, +-10 from a double chevron); for cycling rows (Filter/
// Move Speed/No-Clip/category expand) only its sign matters.
void adjust_row(int row_index, int delta) {
    int dir = (delta > 0) - (delta < 0);
    if (row_index == ROW_CONTROLS_CAT) {
        if (dir != 0) g_controls_expanded = !g_controls_expanded;
    } else if (row_index == ROW_EXTRACTOR_CAT) {
        if (dir != 0) g_extractor_expanded = !g_extractor_expanded;
    } else if (row_index == ROW_STATS_CAT) {
        if (dir != 0) g_stats_expanded = !g_stats_expanded;
    } else if (row_index == ROW_CUSTOM_CAT) {
        if (dir != 0) g_custom_expanded = !g_custom_expanded;
    } else if (row_is_custom(row_index)) {
        int i = row_index - ROW_CUSTOM_START;
        int32_t v = (int32_t)ram[g_custom_mods[i].addr] + delta;
        if (v < 0) v = 0;
        if (v > 65535) v = 65535;
        ram[g_custom_mods[i].addr] = (uint16_t)v;
        if (g_custom_mods[i].frozen) { g_custom_mods[i].frozen_value = (uint16_t)v; save_custom_mods(); }
    } else if (row_index == ROW_FILTER) {
        g_render_filter = (g_render_filter + dir + FILTER_COUNT) % FILTER_COUNT;
        // Sharp-bilinear's shader relies on hardware bilinear sampling too -
        // it computes a coordinate landing between texel centers and lets
        // the GPU do the actual blend, same as plain Smooth.
        bool wants_bilinear = g_render_filter == FILTER_SMOOTH || g_render_filter == FILTER_SHARP;
        SetTextureFilter(screen_texture, wants_bilinear ? TEXTURE_FILTER_BILINEAR : TEXTURE_FILTER_POINT);
    } else if (row_index == ROW_MOVE_SPEED) {
        g_movement_speed_idx = (g_movement_speed_idx + dir + MOVEMENT_SPEED_LEVEL_COUNT) % MOVEMENT_SPEED_LEVEL_COUNT;
    } else if (row_index == ROW_NOCLIP) {
        if (dir != 0) { g_cheat_noclip = !g_cheat_noclip; set_noclip_enabled(g_cheat_noclip); }
    } else if (row_index == ROW_DISABLE_SHADOWS) {
        if (dir != 0) { g_shadows_disabled = !g_shadows_disabled; set_shadows_disabled(g_shadows_disabled); }
    } else if (row_index == ROW_MINIPET) {
        // Browsing only - no side effect. Changing species while one is
        // already spawned does NOT re-spawn it; use the Action row for that.
        g_minipet_picker_idx = (g_minipet_picker_idx + dir + 8) % 8;
    } else if (row_index == ROW_MINIPET_ACTION) {
        if (dir != 0) {
            g_minipet_spawned = !g_minipet_spawned;
            if (g_minipet_spawned) spawn_minipet(g_minipet_picker_idx);
            else despawn_minipet();
        }
    } else if (row_index == ROW_FPS_LIMIT) {
        if (dir != 0) {
            g_fps_limit_idx = (g_fps_limit_idx + dir + FPS_LIMIT_OPTION_COUNT) % FPS_LIMIT_OPTION_COUNT;
            SetTargetFPS(FPS_LIMIT_OPTIONS[g_fps_limit_idx]);
            save_fps_limit();
        }
    } else if (row_index == ROW_INTERPOLATION) {
        if (dir != 0) g_interp_enabled = !g_interp_enabled;
    } else if (row_index == ROW_FPS_COUNTER) {
        if (dir != 0) g_show_fps_hud = !g_show_fps_hud;
    } else if (row_index == ROW_STAT_HUD) {
        if (dir != 0) g_show_stat_hud = !g_show_stat_hud;
    } else if (row_index == ROW_QUEST_ARROW) {
        if (dir != 0) g_quest_arrow_enabled = !g_quest_arrow_enabled;
    } else if (row_index == ROW_EXPORT_SCALE) {
        g_export_scale_idx = (g_export_scale_idx + dir + EXPORT_SCALE_OPTION_COUNT) % EXPORT_SCALE_OPTION_COUNT;
    } else if (row_index == ROW_EXTRACTOR_SKIP_PLAYER) {
        if (dir != 0) g_extractor_skip_player = !g_extractor_skip_player;
    } else if (row_is_inv_item(row_index)) {
        int i = row_index - ROW_INVENTORY_ITEM_START;
        set_inv_value(i, (int32_t)ram[INVENTORY_ITEMS[i].addr] + delta);
    } else if (row_is_category(row_index)) {
        if (dir != 0) {
            int c = row_index - ROW_INVENTORY_CAT_START;
            g_category_expanded[c] = !g_category_expanded[c];
        }
    } else if (row_is_keybind(row_index)) {
        // no-op: rebinding goes through the awaiting-capture flow below,
        // not +/- delta adjustment.
    } else if (row_index >= ROW_STATS_START) {
        int i = row_index - ROW_STATS_START;
        set_stat_value(i, (int32_t)ram[CHEAT_STATS[i].addr] + delta);
    }
}

// Favorite/freeze/edit operate on a row_index directly (dispatching to
// either CHEAT_STATS or INVENTORY_ITEMS based on which range it falls in),
// so the same mouse/keyboard handling code works for both without the
// caller needing to know which kind of row it clicked.
void toggle_favorite_row(int row_index) {
    if (row_is_custom(row_index)) {
        g_custom_mods[row_index - ROW_CUSTOM_START].favorite = !g_custom_mods[row_index - ROW_CUSTOM_START].favorite;
        save_custom_mods();
        return;
    }
    if (row_is_inv_item(row_index)) {
        int i = row_index - ROW_INVENTORY_ITEM_START;
        g_inv_favorite[i] = !g_inv_favorite[i];
    } else {
        int i = row_index - ROW_STATS_START;
        g_stat_favorite[i] = !g_stat_favorite[i];
    }
    save_cheat_state();
}

void toggle_freeze_row(int row_index) {
    if (row_is_custom(row_index)) {
        int i = row_index - ROW_CUSTOM_START;
        g_custom_mods[i].frozen = !g_custom_mods[i].frozen;
        if (g_custom_mods[i].frozen) g_custom_mods[i].frozen_value = ram[g_custom_mods[i].addr];
        save_custom_mods();
        return;
    }
    if (row_is_inv_item(row_index)) {
        int i = row_index - ROW_INVENTORY_ITEM_START;
        g_inv_frozen[i] = !g_inv_frozen[i];
        if (g_inv_frozen[i]) g_inv_frozen_value[i] = ram[INVENTORY_ITEMS[i].addr];
    } else {
        int i = row_index - ROW_STATS_START;
        g_stat_frozen[i] = !g_stat_frozen[i];
        if (g_stat_frozen[i]) g_stat_frozen_value[i] = ram[CHEAT_STATS[i].addr];
    }
    save_cheat_state();
}

void begin_edit_row(int row_index) {
    g_editing_row = row_index;
    uint16_t addr = row_is_custom(row_index) ? g_custom_mods[row_index - ROW_CUSTOM_START].addr
        : row_is_inv_item(row_index)
        ? INVENTORY_ITEMS[row_index - ROW_INVENTORY_ITEM_START].addr
        : CHEAT_STATS[row_index - ROW_STATS_START].addr;
    sprintf(g_edit_buffer, "%u", ram[addr]);
    g_edit_buffer_len = (int)strlen(g_edit_buffer);
}

void commit_edit_row() {
    if (g_editing_row < 0) return;
    int32_t val = g_edit_buffer_len > 0 ? atoi(g_edit_buffer) : 0;
    if (val < 0) val = 0;
    if (val > 65535) val = 65535;
    if (row_is_custom(g_editing_row)) {
        int i = g_editing_row - ROW_CUSTOM_START;
        ram[g_custom_mods[i].addr] = (uint16_t)val;
        if (g_custom_mods[i].frozen) { g_custom_mods[i].frozen_value = (uint16_t)val; save_custom_mods(); }
    }
    else if (row_is_inv_item(g_editing_row)) set_inv_value(g_editing_row - ROW_INVENTORY_ITEM_START, val);
    else set_stat_value(g_editing_row - ROW_STATS_START, val);
    g_editing_row = -1;
}

void cancel_edit_row() { g_editing_row = -1; }

// Truncates with an ellipsis so long labels (e.g. "King's Magnificent
// Statue") can't overlap the chevrons/value column to their right - row
// width is fixed but item/stat name length isn't.
void draw_text_clipped(const char* text, int x, int y, int font_size, Color color, int max_w) {
    if (ui_measure_text(text, font_size) <= max_w) {
        ui_draw_text(text, x, y, font_size, color);
        return;
    }
    char buf[64];
    int len = (int)strlen(text);
    if (len > 63) len = 63;
    strncpy(buf, text, len); buf[len] = 0;
    while (len > 0 && ui_measure_text(TextFormat("%s...", buf), font_size) > max_w) {
        buf[--len] = 0;
    }
    ui_draw_text(TextFormat("%s...", buf), x, y, font_size, color);
}

void draw_mod_menu_row(const ModMenuRowRect& r) {
    bool selected = r.row_index == g_mod_menu_selection;
    Color text_color = selected ? YELLOW : WHITE;

    if (r.row_index == ROW_CONTROLS_CAT) {
        char label[32];
        sprintf(label, "%s Controls (%d)", g_controls_expanded ? "-" : "+", GAME_BUTTON_COUNT);
        ui_draw_text(label, (int)r.full.x + 4, (int)r.full.y, 15, selected ? YELLOW : SKYBLUE);
        // Column header for the per-row keyboard/gamepad values below,
        // right-aligned over the same x-offsets they're drawn at - without
        // this, "Pad"/"A"/"B" on their own don't read as gamepad-specific.
        if (g_controls_expanded) {
            ui_draw_text("Keyboard", (int)r.full.x + 155, (int)r.full.y, 12, GRAY);
            ui_draw_text("Gamepad", (int)r.full.x + 215, (int)r.full.y, 12, GRAY);
        }
        return;
    }

    if (row_is_category(r.row_index)) {
        int c = r.row_index - ROW_INVENTORY_CAT_START;
        const InventoryCategory& cat = INVENTORY_CATEGORIES[c];
        char label[64];
        sprintf(label, "%s %s (%d)", g_category_expanded[c] ? "-" : "+", cat.name, cat.count);
        draw_text_clipped(label, (int)r.full.x + 4, (int)r.full.y, 15, selected ? YELLOW : SKYBLUE, (int)r.full.width - 8);
        return;
    }

    if (r.row_index == ROW_EXTRACTOR_CAT) {
        char label[32];
        sprintf(label, "%s Sprite Extractor (3)", g_extractor_expanded ? "-" : "+");
        ui_draw_text(label, (int)r.full.x + 4, (int)r.full.y, 15, selected ? YELLOW : SKYBLUE);
        return;
    }

    if (r.row_index == ROW_STATS_CAT) {
        char label[32];
        sprintf(label, "%s Stats (11)", g_stats_expanded ? "-" : "+");
        ui_draw_text(label, (int)r.full.x + 4, (int)r.full.y, 15, selected ? YELLOW : SKYBLUE);
        return;
    }

    if (r.row_index == ROW_CUSTOM_CAT) {
        char label[40];
        sprintf(label, "%s Custom (%d)", g_custom_expanded ? "-" : "+", g_custom_mod_count);
        ui_draw_text(label, (int)r.full.x + 4, (int)r.full.y, 15, selected ? YELLOW : SKYBLUE);
        return;
    }

    if (r.row_index == ROW_CUSTOM_ADD) {
        ui_draw_text("+ Add Custom Address...", (int)r.full.x + 4, (int)r.full.y, 15, selected ? YELLOW : SKYBLUE);
        if (g_custom_add_phase != CUSTOM_ADD_NONE) {
            const char* prompt = g_custom_add_phase == CUSTOM_ADD_ADDRESS ? "Addr (hex):" : "Name:";
            char display[64];
            sprintf(display, "%s %s_", prompt, g_custom_add_buffer);
            DrawRectangle((int)r.full.x, (int)r.full.y + 20, (int)r.full.width, 20, Color{40, 40, 40, 255});
            DrawRectangleLines((int)r.full.x, (int)r.full.y + 20, (int)r.full.width, 20, YELLOW);
            ui_draw_text(display, (int)r.full.x + 4, (int)r.full.y + 22, 14, YELLOW);
        }
        return;
    }

    if (row_is_keybind(r.row_index)) {
        int b = r.row_index - ROW_CONTROLS_START;
        bool awaiting_kb = g_awaiting_keybind_for == b && !g_awaiting_gamepad_rebind;
        bool awaiting_gp = g_awaiting_keybind_for == b && g_awaiting_gamepad_rebind;
        ui_draw_text(GAME_BUTTONS[b].name, (int)r.full.x + 4, (int)r.full.y, 15, text_color);
        // Two columns aligned under the "Keyboard"/"Gamepad" header drawn on
        // the Controls category row above - each independently rebindable
        // (click either column, or select the row and press Enter for
        // keyboard / a gamepad button for gamepad) without touching the
        // other side.
        if (awaiting_kb) {
            ui_draw_text("Press a key...", (int)r.full.x + 155, (int)r.full.y, 13, ORANGE);
        } else {
            ui_draw_text(get_key_display_name(g_key_binding[b]), (int)r.full.x + 155, (int)r.full.y, 13, SKYBLUE);
        }
        if (awaiting_gp) {
            ui_draw_text("Press...", (int)r.full.x + 215, (int)r.full.y, 13, ORANGE);
        } else {
            ui_draw_text(get_gamepad_button_name(g_gamepad_binding[b]), (int)r.full.x + 215, (int)r.full.y, 13, SKYBLUE);
        }
        return;
    }

    if (r.row_index == ROW_MOD_MENU_KEY) {
        // Host-side UI toggle, not a real GigaPets button - same two-column
        // rebind display as the Controls rows above, just standalone (see
        // g_mod_menu_key's comment for why it isn't part of that array).
        bool awaiting_kb = g_awaiting_keybind_for == GAME_BUTTON_COUNT && !g_awaiting_gamepad_rebind;
        bool awaiting_gp = g_awaiting_keybind_for == GAME_BUTTON_COUNT && g_awaiting_gamepad_rebind;
        ui_draw_text("Mod Menu Key", (int)r.full.x + 4, (int)r.full.y, 15, text_color);
        if (awaiting_kb) {
            ui_draw_text("Press a key...", (int)r.full.x + 155, (int)r.full.y, 13, ORANGE);
        } else {
            ui_draw_text(get_key_display_name(g_mod_menu_key), (int)r.full.x + 155, (int)r.full.y, 13, SKYBLUE);
        }
        if (awaiting_gp) {
            ui_draw_text("Press...", (int)r.full.x + 215, (int)r.full.y, 13, ORANGE);
        } else {
            ui_draw_text(get_gamepad_button_name(g_mod_menu_gamepad), (int)r.full.x + 215, (int)r.full.y, 13, SKYBLUE);
        }
        return;
    }

    bool has_value = row_has_value(r.row_index);
    bool is_item = row_is_inv_item(r.row_index);
    char label[48] = {0};
    char value_str[16] = {0};
    bool fav = false, frozen = false;
    int preset_count = 0;
    const CheatPreset* presets = nullptr;

    if (r.row_index == ROW_FILTER) {
        const char* names[FILTER_COUNT] = { "Crisp", "Smooth", "Sharp", "CRT" };
        strcpy(label, "Filter");
        strcpy(value_str, names[g_render_filter]);
    } else if (r.row_index == ROW_MOVE_SPEED) {
        strcpy(label, "Move Speed");
        sprintf(value_str, "%dx", MOVEMENT_SPEED_LEVELS[g_movement_speed_idx]);
    } else if (r.row_index == ROW_NOCLIP) {
        strcpy(label, "No-Clip");
        strcpy(value_str, g_cheat_noclip ? "On" : "Off");
    } else if (r.row_index == ROW_DISABLE_SHADOWS) {
        strcpy(label, "Disable Shadows");
        strcpy(value_str, g_shadows_disabled ? "On" : "Off");
    } else if (r.row_index == ROW_MINIPET) {
        strcpy(label, "Minipet");
        strcpy(value_str, MINIPET_NAMES[g_minipet_picker_idx]);
    } else if (r.row_index == ROW_MINIPET_ACTION) {
        strcpy(label, "Minipet Action");
        strcpy(value_str, g_minipet_spawned ? "Zapped In" : "Zapped Out");
    } else if (r.row_index == ROW_FPS_LIMIT) {
        strcpy(label, "FPS Limit");
        int fps = FPS_LIMIT_OPTIONS[g_fps_limit_idx];
        if (fps == 0) strcpy(value_str, "Unlimited");
        else sprintf(value_str, "%d", fps);
    } else if (r.row_index == ROW_INTERPOLATION) {
        strcpy(label, "Interpolation");
        strcpy(value_str, g_interp_enabled ? "On" : "Off");
    } else if (r.row_index == ROW_FPS_COUNTER) {
        strcpy(label, "FPS Counter");
        strcpy(value_str, g_show_fps_hud ? "On" : "Off");
    } else if (r.row_index == ROW_STAT_HUD) {
        strcpy(label, "Stat HUD");
        strcpy(value_str, g_show_stat_hud ? "On" : "Off");
    } else if (r.row_index == ROW_QUEST_ARROW) {
        strcpy(label, "Quest Arrow");
        strcpy(value_str, g_quest_arrow_enabled ? "On" : "Off");
    } else if (r.row_index == ROW_EXPORT_SCALE) {
        strcpy(label, "Sprite Export Scale");
        sprintf(value_str, "%dx", EXPORT_SCALE_OPTIONS[g_export_scale_idx]);
    } else if (r.row_index == ROW_EXTRACTOR_SKIP_PLAYER) {
        strcpy(label, "Extractor Skip Player");
        strcpy(value_str, g_extractor_skip_player ? "On" : "Off");
    } else if (row_is_custom(r.row_index)) {
        int i = r.row_index - ROW_CUSTOM_START;
        strncpy(label, g_custom_mods[i].name, 47); label[47] = 0;
        sprintf(value_str, "%u", ram[g_custom_mods[i].addr]);
        fav = g_custom_mods[i].favorite; frozen = g_custom_mods[i].frozen;
    } else if (is_item) {
        int i = r.row_index - ROW_INVENTORY_ITEM_START;
        strncpy(label, INVENTORY_ITEMS[i].name, 47); label[47] = 0;
        sprintf(value_str, "%u", ram[INVENTORY_ITEMS[i].addr]);
        fav = g_inv_favorite[i]; frozen = g_inv_frozen[i];
    } else {
        int i = r.row_index - ROW_STATS_START;
        strcpy(label, CHEAT_STATS[i].name);
        uint16_t stat_val = ram[CHEAT_STATS[i].addr];
        if (CHEAT_STATS[i].addr == 0x218D && stat_val < sizeof(SICKNESS_PRESETS) / sizeof(SICKNESS_PRESETS[0])) {
            strcpy(value_str, SICKNESS_PRESETS[stat_val].label);
        } else {
            sprintf(value_str, "%u", stat_val);
        }
        fav = g_stat_favorite[i]; frozen = g_stat_frozen[i];
        preset_count = CHEAT_STATS[i].preset_count;
        presets = CHEAT_STATS[i].presets;
    }

    int label_x = (int)r.full.x + (has_value ? 22 : 4);
    int label_max_w = has_value ? (int)(r.arrow_ll.x - label_x - 4) : (int)(r.full.width - (label_x - r.full.x));
    draw_text_clipped(label, label_x, (int)r.full.y, 15, text_color, label_max_w);
    ui_draw_text("<<", (int)r.arrow_ll.x, (int)r.arrow_ll.y, 14, text_color);
    ui_draw_text("<", (int)r.arrow_l.x, (int)r.arrow_l.y, 16, text_color);
    ui_draw_text(">", (int)r.arrow_r.x, (int)r.arrow_r.y, 16, text_color);
    ui_draw_text(">>", (int)r.arrow_rr.x, (int)r.arrow_rr.y, 14, text_color);

    if (has_value) {
        DrawRectangleLines((int)r.fav.x, (int)r.fav.y, (int)r.fav.width, (int)r.fav.height, YELLOW);
        if (fav) DrawRectangle((int)r.fav.x + 3, (int)r.fav.y + 3, (int)r.fav.width - 6, (int)r.fav.height - 6, YELLOW);
        DrawRectangleLines((int)r.freeze.x, (int)r.freeze.y, (int)r.freeze.width, (int)r.freeze.height, SKYBLUE);
        if (frozen) DrawRectangle((int)r.freeze.x + 3, (int)r.freeze.y + 3, (int)r.freeze.width - 6, (int)r.freeze.height - 6, SKYBLUE);
        (void)presets;
        if (preset_count > 0) {
            Color c = (g_open_dropdown_stat == (r.row_index - ROW_STATS_START)) ? YELLOW : GRAY;
            DrawRectangleLines((int)r.preset_btn.x, (int)r.preset_btn.y, (int)r.preset_btn.width, (int)r.preset_btn.height, c);
            ui_draw_text("v", (int)r.preset_btn.x + 4, (int)r.preset_btn.y + 1, 12, c);
        }

        if (r.row_index == g_editing_row) {
            DrawRectangle((int)r.value.x, (int)r.value.y, (int)r.value.width, (int)r.value.height, Color{40, 40, 40, 255});
            DrawRectangleLines((int)r.value.x, (int)r.value.y, (int)r.value.width, (int)r.value.height, YELLOW);
            char edit_display[10];
            sprintf(edit_display, "%s_", g_edit_buffer);
            ui_draw_text(edit_display, (int)r.value.x + 2, (int)r.value.y, 15, YELLOW);
            return;
        }
    }
    ui_draw_text(value_str, (int)r.value.x, (int)r.value.y, 15, text_color);
}

void check_video_irq() {
    if (video_regs[0x63] & video_regs[0x62]) cpu_ptr->execute_set_input(UNSP_IRQ0_LINE, 1);
    else cpu_ptr->execute_set_input(UNSP_IRQ0_LINE, 0);
}

void audio_reset() {
    memset(audio_regs, 0, sizeof(audio_regs));
    memset(audio_phase_regs, 0, sizeof(audio_phase_regs));
    memset(audio_ctrl_regs, 0, sizeof(audio_ctrl_regs));
    memset(audio_channel_rate, 0, sizeof(audio_channel_rate));
    memset(audio_channel_rate_accum, 0, sizeof(audio_channel_rate_accum));
    memset(audio_sample_shift, 0, sizeof(audio_sample_shift));
    memset(audio_sample_count, 0, sizeof(audio_sample_count));
    memset(audio_rampdown_frame, 0, sizeof(audio_rampdown_frame));
    memset(audio_envclk_frame, 0, sizeof(audio_envclk_frame));
    memset(audio_envelope_addr_rt, 0, sizeof(audio_envelope_addr_rt));
    memset(audio_ima_signal, 0, sizeof(audio_ima_signal));
    memset(audio_ima_step, 0, sizeof(audio_ima_step));
    memset(audio_adpcm36_remaining, 0, sizeof(audio_adpcm36_remaining));
    memset(audio_adpcm36_header, 0, sizeof(audio_adpcm36_header));
    memset(audio_adpcm36_prevsamp, 0, sizeof(audio_adpcm36_prevsamp));
    audio_curr_beat_base_count = 0;
}

// Ported from spg2xx_audio_device::audio_ctrl_w (mame/src/devices/machine/spg2xx_audio.cpp):
// channel-enable writes actually start/stop channels, and BEAT_COUNT is a
// write-1-to-clear IRQ-status register - a plain passthrough write to
// audio_ctrl_regs (like the other, simpler registers) would silently break
// both of those. Per-register masking (main volume, FIQ enable) kept too.
void audio_ctrl_w(uint32_t offset, uint16_t data) {
    switch (offset) {
        case AUDIO_CHANNEL_ENABLE: {
            uint16_t old = audio_ctrl_regs[offset];
            audio_ctrl_regs[offset] = data;
            uint16_t changed = old ^ data;
            for (int ch = 0; ch < 16; ch++) {
                uint16_t mask = 1 << ch;
                if (!(changed & mask)) continue;
                if (data & mask) {
                    if (audio_ctrl_regs[AUDIO_CHANNEL_STOP] & mask) continue;
                    if (!(audio_ctrl_regs[AUDIO_CHANNEL_STATUS] & mask)) audio_start_channel(ch);
                } else {
                    if (audio_ctrl_regs[AUDIO_CHANNEL_STATUS] & mask) audio_stop_channel(ch);
                }
            }
            break;
        }
        case AUDIO_MAIN_VOLUME:
            audio_ctrl_regs[offset] = data & AUDIO_MAIN_VOLUME_MASK;
            break;
        case AUDIO_CHANNEL_FIQ_STATUS:
            audio_ctrl_regs[offset] &= ~(data & AUDIO_CHANNEL_FIQ_STATUS_MASK);
            break;
        case AUDIO_BEAT_BASE_COUNT:
            audio_ctrl_regs[offset] = data & AUDIO_BEAT_BASE_COUNT_MASK;
            audio_curr_beat_base_count = audio_ctrl_regs[offset];
            break;
        case AUDIO_BEAT_COUNT: {
            uint16_t old_bis = audio_ctrl_regs[offset] & AUDIO_BIS_MASK;
            audio_ctrl_regs[offset] &= ~(data & AUDIO_BIS_MASK); // write-1-to-clear BIS
            audio_ctrl_regs[offset] = (audio_ctrl_regs[offset] & AUDIO_BIS_MASK) | (data & ~AUDIO_BIS_MASK);
            (void)old_bis;
            check_audio_irq();
            break;
        }
        case AUDIO_CHANNEL_STOP: {
            // ROOT CAUSE of total silence: this register is write-1-to-CLEAR
            // (real hardware/MAME's audio_ctrl_w), not a plain overwrite.
            // Several places in our own code latch a channel's stop bit when
            // its one-shot sample finishes (audio_fetch_sample et al); with
            // no case here, this fell into the generic `default:` plain
            // overwrite below, so a stop bit could never actually be
            // cleared once set - CHANNEL_ENABLE's own handler explicitly
            // skips restarting a channel while its stop bit is set, so
            // every channel permanently died the first time it ever
            // finished a sample, until nothing could play at all.
            uint16_t old = audio_ctrl_regs[offset];
            audio_ctrl_regs[offset] &= ~data;
            uint16_t changed = old ^ audio_ctrl_regs[offset];
            for (int ch = 0; ch < 16; ch++) {
                uint16_t mask = 1 << ch;
                if (!(changed & mask)) continue;
                if (!(audio_ctrl_regs[AUDIO_CHANNEL_ENABLE] & mask)) continue;
                if (!(audio_ctrl_regs[AUDIO_CHANNEL_STATUS] & mask)) audio_start_channel(ch);
            }
            break;
        }
        default:
            audio_ctrl_regs[offset] = data;
            break;
    }
}

// Producer/consumer ring buffer between generate_audio_frame() (called once
// per simulation tick, in whatever batch size the tick's elapsed time
// demands) and raylib's AudioStream (which wants fixed-size chunks, only
// when it has actually finished the previous one).
static const int AUDIO_QUEUE_CAPACITY_FRAMES = 1 << 16; // stereo frames
static int16_t g_audio_queue[AUDIO_QUEUE_CAPACITY_FRAMES * 2];
static int g_audio_queue_head = 0; // next write position (frames)
static int g_audio_queue_tail = 0; // next read position (frames)
static int g_audio_queue_count = 0; // frames currently buffered
// Real hardware's audio DAC clock is derived from the same 27MHz master
// clock as the CPU, 1 sample per 384 cycles (27000000/70312.5 = 384 exactly)
// - and critically, ticks INTERLEAVED with CPU execution, cycle by cycle,
// not in a single lump sum after a whole frame's worth of instructions have
// already run (see the cpu.step() loop, where this actually drives
// generation - a prior wall-clock-paced batch approach here was the real
// root cause of a ~10% slow-tempo bug).
double audio_cycle_debt = 0.0;
AudioStream audio_stream;

void audio_queue_push(const int16_t* buf, int num_frames) {
    for (int i = 0; i < num_frames; i++) {
        if (g_audio_queue_count >= AUDIO_QUEUE_CAPACITY_FRAMES) break; // drop on overflow
        g_audio_queue[g_audio_queue_head * 2 + 0] = buf[i * 2 + 0];
        g_audio_queue[g_audio_queue_head * 2 + 1] = buf[i * 2 + 1];
        g_audio_queue_head = (g_audio_queue_head + 1) % AUDIO_QUEUE_CAPACITY_FRAMES;
        g_audio_queue_count++;
    }
}

void audio_queue_feed_stream(AudioStream stream) {
    // ROOT CAUSE of choppy audio: raylib's internal stream buffer is sized
    // via SetAudioStreamBufferSizeDefault(AUDIO_STREAM_CHUNK=4096), and
    // IsAudioStreamProcessed() only reports true once a full buffer of that
    // size has finished playing. Handing over only 1024 frames per "ready"
    // signal was refilling a quarter of what raylib actually needed each
    // time, so the real output device kept running dry between top-ups even
    // though our own software queue was sitting permanently full (confirmed
    // via trace: queue_count pinned at max capacity the whole session).
    // Matching this to AUDIO_STREAM_CHUNK fixes the mismatch.
    const int CHUNK = AUDIO_STREAM_CHUNK; // frames per UpdateAudioStream call
    while (g_audio_queue_count >= CHUNK && IsAudioStreamProcessed(stream)) {
        static int16_t chunk[CHUNK * 2];
        for (int i = 0; i < CHUNK; i++) {
            chunk[i * 2 + 0] = g_audio_queue[g_audio_queue_tail * 2 + 0];
            chunk[i * 2 + 1] = g_audio_queue[g_audio_queue_tail * 2 + 1];
            g_audio_queue_tail = (g_audio_queue_tail + 1) % AUDIO_QUEUE_CAPACITY_FRAMES;
        }
        g_audio_queue_count -= CHUNK;
        UpdateAudioStream(stream, chunk, CHUNK);
    }
}

uint16_t memory_read16(uint32_t addr) {
    if (addr < 0x2800) return ram[addr];
    if (addr <= 0x28FF) return video_regs[addr - 0x2800];
    if (addr <= 0x2FFF) return ram[addr];
    if (addr <= 0x31FF) return audio_r(addr - 0x3000);
    if (addr <= 0x33FF) return audio_phase_r(addr - 0x3200);
    if (addr <= 0x341F) return audio_ctrl_regs[addr - 0x3400];
    if (addr <= 0x3FFF) {
        // Hardware PRNG
        if (addr == 0x3D2C || addr == 0x3D2D) return (uint16_t)rand();

        // ADC Data (Random Pet Colors) - ready bit + random low bits
        if (addr == 0x3D27) return (uint16_t)((rand() & 0x0FFF) | 0x8000);

        // REG_DATA_SEGMENT passthrough - see the DS-register bug note above.
        if (addr == 0x3D2F) return cpu_ptr->get_ds();

        // GPIO Port A Data (Inputs) - REG_IOA_DATA. Buttons are wired to
        // Port A; bit index matches GameButton enum order by construction.
        if (addr == 0x3D01) {
            uint16_t val = io[addr - 0x3000];
            uint16_t low = 0x0000; // active-high; default = nothing pressed
            if (!g_mod_menu_open) { // don't let mod-menu navigation leak into the game
                for (int b = 0; b < GAME_BUTTON_COUNT; b++) {
                    if (IsKeyDown(g_key_binding[b])) low |= (1 << b);
                }
                // Native controller support: gamepad 0's D-pad and left
                // stick both drive movement (whichever the player uses),
                // face/start buttons map to Select/Back/Menu via each
                // platform's own "confirm/cancel/start" convention - raylib
                // normalizes Xbox-style (A/B/Start) and PlayStation-style
                // (Cross/Circle/Start) pads to the same enum values, so this
                // covers both without per-platform cases. Remappable via
                // g_gamepad_binding (Controls menu, Gamepad column),
                // persisted the same way as the keyboard bindings above.
                if (IsGamepadAvailable(0)) {
                    const float deadzone = 0.35f;
                    float ax = GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_X);
                    float ay = GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_Y);
                    for (int b = 0; b < GAME_BUTTON_COUNT; b++) {
                        if (IsGamepadButtonDown(0, g_gamepad_binding[b])) low |= (1 << b);
                    }
                    if (ax < -deadzone) low |= (1 << BTN_LEFT);
                    if (ax > deadzone) low |= (1 << BTN_RIGHT);
                    if (ay < -deadzone) low |= (1 << BTN_UP);
                    if (ay > deadzone) low |= (1 << BTN_DOWN);
                }
            }
            // Opt-in auto test-menu unlock (F9) - synthesize the real
            // button sequence instead of real input while active, timed off
            // g_test_menu_seq_frame (advanced once per sim frame below).
            if (g_test_menu_seq_active) {
                int f = g_test_menu_seq_frame;
                low = 0;
                if (g_test_menu_chime_frame < 0) {
                    // Chime hasn't fired yet - keep holding no matter how
                    // long it takes (confirmed real threshold is ~182
                    // frames, releasing early resets the test-mode flag).
                    low = (1 << BTN_LEFT) | (1 << BTN_SELECT);
                } else {
                    int rel = f - g_test_menu_chime_frame;
                    if (rel < 10) low = (1 << BTN_LEFT) | (1 << BTN_SELECT);
                    else if (rel >= 16 && rel < 22) low = (1 << BTN_UP);
                    else if (rel >= 28 && rel < 34) low = (1 << BTN_DOWN);
                    else if (rel >= 40 && rel < 46) low = (1 << BTN_MENU);
                    else if (rel >= 52 && rel < 58) low = (1 << BTN_BACK);
                    else if (rel >= 64) g_test_menu_seq_active = false;
                }
            }
            return (val & ~0x007F) | low;
        }

        // GPIO Port B (EEPROM DO on bit 3)
        if (addr == 0x3D06) {
            uint16_t val = io[addr - 0x3000];
            return (val & ~0x0008) | (eeprom_do_read() ? 0x0008 : 0);
        }

        return io[addr - 0x3000];
    }
    if (addr < 0x400000) return rom[addr];
    return 0;
}

void memory_write16(uint32_t addr, uint16_t data) {
    if (addr < 0x2800) {
        ram[addr] = data;
    } else if (addr <= 0x28FF) {
        if (addr == 0x2863) { video_regs[0x63] &= ~data; check_video_irq(); }
        else if (addr == 0x2862) { video_regs[0x62] = data; check_video_irq(); }
        else if (addr == 0x2872) {
            video_regs[0x72] = data & 0x03FF;
            uint16_t len = video_regs[0x72] ? video_regs[0x72] : 0x400;
            uint32_t src = video_regs[0x70] & 0x3FFF;
            uint32_t dst = video_regs[0x71] & 0x03FF;
            for (uint32_t j = 0; j < len; j++) if (dst + j < 0x400) ram[0x2C00 + dst + j] = memory_read16(src + j);
            video_regs[0x72] = 0;
            if (video_regs[0x62] & 4) { video_regs[0x63] |= 4; check_video_irq(); }
        } else {
            video_regs[addr - 0x2800] = data;
        }
    } else if (addr <= 0x2FFF) {
        ram[addr] = data;
    } else if (addr <= 0x31FF) {
        audio_w(addr - 0x3000, data);
    } else if (addr <= 0x33FF) {
        audio_phase_w(addr - 0x3200, data);
    } else if (addr <= 0x341F) {
        audio_ctrl_w(addr - 0x3400, data);
    } else if (addr <= 0x3FFF) {
        io[addr - 0x3000] = data;

        // REG_DATA_SEGMENT passthrough - see the get_ds() read-side note
        // above. Without wiring the write side too, ds:-prefixed extended
        // addressing (room/tile/sprite bank select) never actually changes
        // banks - caused invisible-wall collision bugs, pet-selector sprite
        // snapping, and personality-screen icon misplacement, all from the
        // same stale-DS root cause.
        if (addr == 0x3D2F) cpu_ptr->set_ds(data & 0x3f);

        // GPIO Port B: bit0=EEPROM CS, bit1=CLK, bit2=DI (bit3=DO is read-only)
        if (addr == 0x3D06) {
            eeprom_cs_write((data & 0x0001) != 0);
            eeprom_clk_write((data & 0x0002) != 0);
            eeprom_di_write((data & 0x0004) != 0);
        }

        // REG_SYSTEM_CTRL (0x3D20) bit15 / REG_WATCHDOG_CLEAR (0x3D24),
        // ported from spg2xx_io_device's real watchdog: bit15 arms a
        // 750ms countdown (750ms @ 60fps = 45 frames, matching this
        // port's own SIM_DT granularity), writing 0x55AA to the clear
        // register while armed reloads it, and letting it expire
        // unfed asserts then clears INPUT_LINE_RESET - a genuine CPU
        // reset. Ground-truthed live via a MAME watchpoint: real
        // hardware's Quit flow arms the watchdog and deliberately stops
        // feeding it, so after 750ms idle the CPU resets itself clean
        // (landing at ResetVector, ROM 0x00fa72, which restores default
        // RAM including GameState=0 and returns to the main menu) -
        // this port never implemented these two registers at all, so
        // the countdown never started and that reset never happened,
        // which is the actual root cause of the Quit-freeze investigated
        // this session. watchdog_enabled/watchdog_frames_left already
        // existed as dead scaffolding for exactly this (decrement-and-
        // reset logic already implemented in the main loop) - just never
        // wired to a real trigger before now.
        if (addr == 0x3D20) {
            bool want_enabled = (data & 0x8000) != 0;
            if (want_enabled && !watchdog_enabled) { watchdog_enabled = true; watchdog_frames_left = 45; }
            else if (!want_enabled) { watchdog_enabled = false; watchdog_frames_left = 0; }
        }
        if (addr == 0x3D24 && data == 0x55AA && watchdog_enabled) {
            watchdog_frames_left = 45;
        }

        // System DMA (ported from spg2xx_sysdma_device::do_cpu_dma, mame/src/
        // devices/machine/spg2xx_sysdma.cpp): writing the word count (+ control
        // bits in the top 2 bits) to 0x3E02 copies `len` words from a 22-bit
        // source address (0x3E00 low / 0x3E01 high 6 bits) to a 14-bit
        // destination window (0x3E03). Real hardware clears 0x3E02 AND advances
        // the source/dest registers past the copied region when the transfer
        // completes - a ROM doing several chained transfers (advancing src/dst
        // itself between calls, or relying on this auto-advance) would silently
        // re-read/re-write the same stale addresses every time without this,
        // corrupting whatever data those chained transfers were assembling
        // (plausible cause of the new-game spawn/stats corruption). The clear
        // alone was also needed already, since the ROM's wait-for-DMA-complete
        // poll loop spins forever on a 0x3E02 value that never resets.
        if (addr == 0x3E02) {
            uint32_t src = ((io[0x3E01 - 0x3000] & 0x3f) << 16) | io[0x3E00 - 0x3000];
            uint32_t dst = io[0x3E03 - 0x3000] & 0x3fff;
            uint32_t len = data & ~0xc000;
            if (!(data & 0xc000)) {
                for (uint32_t j = 0; j < len; j++) memory_write16((dst + j) & 0x3fff, memory_read16(src + j));
                src += len;
                io[0x3E00 - 0x3000] = (uint16_t)src;
                io[0x3E01 - 0x3000] = (src >> 16) & 0x3f;
                io[0x3E03 - 0x3000] = (dst + len) & 0x3fff;
            }
            io[0x3E02 - 0x3000] = 0;
        }
    }
}

Color decode_color(uint16_t rgb555) {
    Color c;
    c.r = (unsigned char)(((rgb555 >> 10) & 0x1F) * 255 / 31);
    c.g = (unsigned char)(((rgb555 >> 5) & 0x1F) * 255 / 31);
    c.b = (unsigned char)(((rgb555 >> 0) & 0x1F) * 255 / 31);
    c.a = 255;
    return c;
}

// Blend level control (video_regs[0x2A] & 3) - real hardware's alpha-blend
// mode for tiles/sprites (shadows, glass, etc): min level is 25% opaque, max
// is 100% opaque. Matches spg_renderer_device::mix_channel / s_blend_levels.
static const uint8_t BLEND_LEVELS[4] = { 0x08, 0x10, 0x18, 0x20 };
inline Color mix_color(Color bottom, Color top, uint8_t alpha) {
    Color c;
    c.r = (unsigned char)(((0x20 - alpha) * bottom.r + alpha * top.r) >> 5);
    c.g = (unsigned char)(((0x20 - alpha) * bottom.g + alpha * top.g) >> 5);
    c.b = (unsigned char)(((0x20 - alpha) * bottom.b + alpha * top.b) >> 5);
    c.a = 255;
    return c;
}

void draw_page(Color* framebuffer, int fb_w, int margin_l, int margin_r, int page_idx, uint16_t* tilemapregs, uint16_t* scrollregs, uint32_t tilegfxdata_addr, int target_priority) {
    uint32_t ctrl = tilemapregs[1];
    if (!(ctrl & 0x0008)) return;

    if (ctrl & 0x0001) {
        // Linemap mode: a linear per-scanline buffer, never widened - just
        // pillarboxed via margin_l, sampling only the original 320 columns.
        uint32_t tilemap = tilemapregs[2];
        uint32_t palette_map = tilemapregs[3];
        uint32_t yscroll = scrollregs[1];
        uint32_t attr = tilemapregs[0];
        if (((attr & 0x3000) >> 12) != target_priority) return;

        for (int y = 0; y < 240; y++) {
            int realline = (y + yscroll) & 0xff;
            uint32_t tile = memory_read16(tilemap + realline);
            uint16_t palette = memory_read16(palette_map + (realline / 2));
            if (y & 1) palette >>= 8; else palette &= 0x00ff;
            uint32_t sourcebase = tile | (palette << 16);

            uint8_t bpp = attr & 0x0003;
            uint32_t nc_bpp = ((bpp) + 1) << 1;
            uint32_t palette_offset = (attr & 0x0f00) >> 4;
            palette_offset >>= nc_bpp;
            palette_offset <<= nc_bpp;

            uint32_t bits = 0, nbits = 0;
            for (int x = 0; x < 320; x++) {
                bits <<= nc_bpp;
                if (nbits < nc_bpp) {
                    uint16_t b = memory_read16(sourcebase++);
                    b = (b << 8) | (b >> 8);
                    bits |= b << (nc_bpp - nbits);
                    nbits += 16;
                }
                nbits -= nc_bpp;
                uint32_t color_idx = bits >> 16;
                bits &= 0xffff;
                int screen_x_lm = x + margin_l;
                uint16_t rgb = ram[0x2B00 + palette_offset + color_idx];
                if (!(rgb & 0x8000)) {
                    framebuffer[y * fb_w + screen_x_lm] = decode_color(rgb);
                }
            }
        }
        return;
    }

    // Tilemap mode: a toroidal buffer wider than the visible window - the
    // column range is extended past the normal 320px viewport by however
    // many extra tiles cover margin_l/margin_r on each side.
    const uint32_t attr = tilemapregs[0];
    if (((attr & 0x3000) >> 12) != target_priority) return;
    const uint32_t tilemap_rambase = tilemapregs[2];
    const uint32_t exattributemap_rambase = tilemapregs[3];
    const int tile_width = (attr & 0x0030) >> 4;
    const uint32_t tile_h = 8 << ((attr & 0x00c0) >> 6);
    const uint32_t tile_w = 8 << (tile_width);
    const uint32_t tile_count_x = 512 / tile_w;
    const uint32_t xscroll = scrollregs[0];
    const uint32_t yscroll = scrollregs[1];

    for (int y = 0; y < 240; y++) {
        uint32_t bitmap_y = (y + yscroll) & 0xff;
        uint32_t y0 = bitmap_y / tile_h;
        uint32_t tile_scanline = bitmap_y % tile_h;

        uint32_t realxscroll = xscroll;
        if (ctrl & 0x0010) { // Row scroll: per-scanline X offset (0x2900-0x29FF)
            realxscroll += (int16_t)ram[0x2900 + ((y + yscroll) & 0xff)];
        }
        const int upperscrollbits = (realxscroll >> (tile_width + 3));
        const int endpos = (320 + tile_w) / tile_w;
        const int x0_start = -(int)((margin_l + tile_w - 1) / tile_w) - 1;
        const int x0_end = endpos + (int)((margin_r + tile_w - 1) / tile_w) + 1;

        for (int x0 = x0_start; x0 < x0_end; x0++) {
            const int realx0 = (x0 + upperscrollbits) & (tile_count_x - 1);
            uint32_t tile_address = realx0 + (tile_count_x * y0);

            uint32_t tile = (ctrl & 0x0004) ? memory_read16(tilemap_rambase) : memory_read16(tilemap_rambase + tile_address);
            if (!tile) continue;

            uint32_t tileattr = attr;
            uint32_t tilectrl = ctrl;

            if ((tilectrl & 2) == 0) {
                uint16_t exattribute = (tilectrl & 0x0004) ? memory_read16(exattributemap_rambase) : memory_read16(exattributemap_rambase + tile_address / 2);
                if (realx0 & 1) exattribute >>= 8; else exattribute &= 0x00ff;
                tileattr &= ~0x000c; tileattr |= (exattribute >> 2) & 0x000c;
                tileattr &= ~0x0f00; tileattr |= (exattribute << 8) & 0x0f00;
                tilectrl &= ~0x0100; tilectrl |= (exattribute << 2) & 0x0100;
            }

            bool blend = (tilectrl & 0x0100) ? true : false;
            bool flip_x = (tileattr & 0x0004) ? true : false;
            bool flip_y = (tileattr & 0x0008) ? true : false;
            uint8_t bpp = tileattr & 0x0003;
            uint32_t nc_bpp = ((bpp) + 1) << 1;
            uint32_t bits_per_row = nc_bpp * tile_w / 16;
            uint32_t words_per_tile = bits_per_row * tile_h;

            uint32_t palette_offset = (tileattr & 0x0f00) >> 4;
            palette_offset >>= nc_bpp;
            palette_offset <<= nc_bpp;

            const uint32_t yflipmask = flip_y ? tile_h - 1 : 0;
            uint32_t m = (tilegfxdata_addr * 0x40) + words_per_tile * tile + bits_per_row * (tile_scanline ^ yflipmask);

            uint32_t bits = 0, nbits = 0;
            int drawx = (x0 * (int)tile_w) - (int)(realxscroll & (tile_w - 1));

            for (int32_t px = flip_x ? (tile_w - 1) : 0; flip_x ? px >= 0 : px < (int32_t)tile_w; flip_x ? px-- : px++) {
                bits <<= nc_bpp;
                if (nbits < nc_bpp) {
                    uint16_t b = memory_read16(m++);
                    b = (b << 8) | (b >> 8);
                    bits |= b << (nc_bpp - nbits);
                    nbits += 16;
                }
                nbits -= nc_bpp;
                uint32_t color_idx = bits >> 16;
                bits &= 0xffff;
                {
                    int screen_x = drawx + px + margin_l;
                    if (screen_x >= 0 && screen_x < fb_w) {
                        uint16_t rgb = ram[0x2B00 + palette_offset + color_idx];
                        if (!(rgb & 0x8000)) {
                            Color top = decode_color(rgb);
                            int fbi = y * fb_w + screen_x;
                            framebuffer[fbi] = blend
                                ? mix_color(framebuffer[fbi], top, BLEND_LEVELS[video_regs[0x2A] & 3])
                                : top;
                        }
                    }
                }
            }
        }
    }
}

void draw_sprites(Color* framebuffer, int fb_w, int margin_l, int target_priority) {
    uint32_t sprite_addr = 0x2C00;
    uint32_t spritegfxdata_addr = 0x40 * video_regs[0x22];

    for (int i = 0; i < 256; i++) {
        uint16_t tile = ram[sprite_addr + i * 4 + 0];
        if (!tile) continue;
        int16_t raw_x = (int16_t)ram[sprite_addr + i * 4 + 1];
        int16_t raw_y = (int16_t)ram[sprite_addr + i * 4 + 2];
        uint16_t attr = ram[sprite_addr + i * 4 + 3];

        if (((attr & 0x3000) >> 12) != target_priority) continue;

        uint32_t tile_h = 8 << ((attr & 0x00c0) >> 6);
        uint32_t tile_w = 8 << ((attr & 0x0030) >> 4);
        int centered_x = raw_x, centered_y = raw_y;
        if (!(video_regs[0x42] & 0x0002)) {
            centered_x = (320 / 2) + raw_x - (int)tile_w / 2;
            centered_y = (256 / 2) - raw_y - (int)tile_h / 2;
        }
        // Real hardware masks the (possibly negative, off-the-top-left)
        // centered position to a 9-bit toroidal coordinate space (0-511,
        // wider than the visible screen) and then uses it AS-IS, unsigned -
        // ported from spg_renderer_device::draw_sprite. It is NOT sign-
        // extended back to negative: values above 255 are legitimate large
        // on-screen X coordinates (very much so on this port's widened
        // 426px canvas), and the plain bounds check below already discards
        // anything that lands off-screen either way. Re-interpreting them
        // as negative (an earlier, wrong attempt at this same fix) made
        // every sprite past X~256 vanish instead of draw - the "right side
        // doesn't load" bug.
        int x = centered_x & 0x1ff;
        int y = centered_y & 0x1ff;

        bool blend = (attr & 0x4000) != 0;
        bool flip_x = (attr & 0x0004) != 0;
        bool flip_y = (attr & 0x0008) != 0;
        uint8_t bpp = attr & 0x0003;
        uint32_t nc_bpp = ((bpp) + 1) << 1;
        uint32_t bits_per_row = nc_bpp * tile_w / 16;
        uint32_t words_per_tile = bits_per_row * tile_h;

        uint32_t palette_offset = (attr & 0x0f00) >> 4;
        palette_offset >>= nc_bpp;
        palette_offset <<= nc_bpp;

        // x/y are 9-bit toroidal coordinates (0-511) on real hardware, not
        // plain screen offsets - a sprite near the top/left edge can have a
        // "small negative" position that masks to a large value near 511,
        // and its rows/columns must wrap back around through 0 to appear on
        // screen (see spg_renderer_device::draw_sprite's firstline/lastline
        // wraparound). Re-masking each computed row/col with & 0x1ff before
        // the bounds check reproduces that wrap; without it, sprites whose
        // position wraps this way (a tall tree/house anchored near the top
        // of the viewport) vanish entirely instead of wrapping into view.
        for (uint32_t py = 0; py < tile_h; py++) {
            int draw_y = (y + py) & 0x1ff;
            if (draw_y >= 240) continue;
            uint32_t ty = flip_y ? (tile_h - 1 - py) : py;
            uint32_t m = spritegfxdata_addr + words_per_tile * tile + bits_per_row * ty;

            uint32_t bits = 0, nbits = 0;
            for (int32_t px = flip_x ? (tile_w - 1) : 0; flip_x ? px >= 0 : px < (int32_t)tile_w; flip_x ? px-- : px++) {
                bits <<= nc_bpp;
                if (nbits < nc_bpp) {
                    uint16_t b = memory_read16(m++);
                    b = (b << 8) | (b >> 8);
                    bits |= b << (nc_bpp - nbits);
                    nbits += 16;
                }
                nbits -= nc_bpp;
                uint32_t color_idx = bits >> 16;
                bits &= 0xffff;
                {
                    int draw_x = ((x + px) & 0x1ff) + margin_l;
                    if (draw_x >= 0 && draw_x < fb_w) {
                        uint16_t rgb = ram[0x2B00 + palette_offset + color_idx];
                        if (!(rgb & 0x8000)) {
                            Color top = decode_color(rgb);
                            int fbi = draw_y * fb_w + draw_x;
                            framebuffer[fbi] = blend
                                ? mix_color(framebuffer[fbi], top, BLEND_LEVELS[video_regs[0x2A] & 3])
                                : top;
                        }
                    }
                }
            }
        }
    }
}

// Sprite PNG extractor: MAME's generic sprite viewer can't decode this
// driver's tiles (variable bpp/width/height read from each sprite's own
// attr word at runtime, not a static gfx_layout MAME's tool expects) -
// but this port already has a byte-for-byte-correct decode of that same
// variable format (draw_sprites above, ported from spg_renderer_device).
// A single on-screen object (a tree, a character) is usually built from
// several adjacent hardware sprite tiles, not one big tile - exporting
// each tile independently scatters one object across many small files.
// This groups active sprites into clusters by bounding-box adjacency
// (touching or within a few pixels), then composites each cluster onto
// one canvas at the tiles' correct relative offsets, honoring flip -
// i.e. reconstructs the actual on-screen object as a single image.
// Shared by the one-shot F8 export and the continuous F7 capture mode,
// each with their own output folder and dedup set so the two don't
// interfere with each other.
void export_visible_sprite_clusters(const std::string& out_dir, std::set<std::string>& exported_clusters) {
    struct ExtractSprite { uint16_t tile, attr; int x, y; uint32_t w, h; int priority; int slot; };
    std::vector<ExtractSprite> active;
    uint32_t sprite_addr2 = 0x2C00;
    for (int i = 0; i < 256; i++) {
        uint16_t tile = ram[sprite_addr2 + i * 4 + 0];
        if (!tile) continue;
        int16_t raw_x = (int16_t)ram[sprite_addr2 + i * 4 + 1];
        int16_t raw_y = (int16_t)ram[sprite_addr2 + i * 4 + 2];
        uint16_t attr = ram[sprite_addr2 + i * 4 + 3];
        uint32_t tw = 8 << ((attr & 0x0030) >> 4);
        uint32_t th = 8 << ((attr & 0x00c0) >> 6);
        int cx = raw_x, cy = raw_y;
        if (!(video_regs[0x42] & 0x0002)) {
            cx = (320 / 2) + raw_x - (int)tw / 2;
            cy = (256 / 2) - raw_y - (int)th / 2;
        }
        int x = cx & 0x1ff, y = cy & 0x1ff;
        if (x > 400 || y > 300) continue; // drop wrapped/off-canvas junk, not a real on-screen object
        active.push_back({ tile, attr, x, y, tw, th, (attr & 0x3000) >> 12, i });
    }

    // Union-find clustering by bbox proximity (expand each box by 3px
    // before testing overlap, so touching-but-not-overlapping tiles of
    // the same object still merge).
    std::vector<int> parent(active.size());
    for (size_t i = 0; i < active.size(); i++) parent[i] = (int)i;
    std::function<int(int)> find = [&](int a) { while (parent[a] != a) { parent[a] = parent[parent[a]]; a = parent[a]; } return a; };
    const int MARGIN = 3;
    for (size_t i = 0; i < active.size(); i++) {
        for (size_t j = i + 1; j < active.size(); j++) {
            auto& A = active[i]; auto& B = active[j];
            bool overlap = A.x - MARGIN < (int)(B.x + B.w) && B.x - MARGIN < (int)(A.x + A.w)
                        && A.y - MARGIN < (int)(B.y + B.h) && B.y - MARGIN < (int)(A.y + A.h);
            if (overlap) { int ra = find((int)i), rb = find((int)j); if (ra != rb) parent[ra] = rb; }
        }
    }
    std::map<int, std::vector<int>> clusters;
    for (size_t i = 0; i < active.size(); i++) clusters[find((int)i)].push_back((int)i);

    for (auto& [root, members] : clusters) {
        std::sort(members.begin(), members.end(), [&](int a, int b) {
            if (active[a].priority != active[b].priority) return active[a].priority < active[b].priority;
            return a < b;
        });
        int minX = 100000, minY = 100000, maxX = -100000, maxY = -100000;
        for (int idx : members) {
            auto& s = active[idx];
            minX = std::min(minX, s.x); minY = std::min(minY, s.y);
            maxX = std::max(maxX, (int)(s.x + s.w)); maxY = std::max(maxY, (int)(s.y + s.h));
        }
        int cw = maxX - minX, ch = maxY - minY;
        if (cw <= 0 || ch <= 0 || cw > 512 || ch > 512) continue;

        // Player-centered camera (g_cameraScrollX/Y IS the player's own
        // world position) means the player's sprite cluster lands at/near
        // screen center (160,128) every frame, unlike any other object -
        // used to identify and skip it when the toggle is on.
        if (g_extractor_skip_player) {
            int centerX = (minX + maxX) / 2, centerY = (minY + maxY) / 2;
            const int PLAYER_CENTER_TOL = 20;
            if (std::abs(centerX - 160) <= PLAYER_CENTER_TOL && std::abs(centerY - 128) <= PLAYER_CENTER_TOL) continue;
        }

        std::vector<Color> canvas(cw * ch, Color{0, 0, 0, 0});
        for (int idx : members) {
            auto& s = active[idx];
            bool flip_x = (s.attr & 0x0004) != 0, flip_y = (s.attr & 0x0008) != 0;
            uint8_t bpp = s.attr & 0x0003;
            uint32_t nc_bpp = (bpp + 1) << 1;
            uint32_t bits_per_row = nc_bpp * s.w / 16;
            uint32_t words_per_tile = bits_per_row * s.h;
            uint32_t palette_offset = (s.attr & 0x0f00) >> 4;
            palette_offset >>= nc_bpp; palette_offset <<= nc_bpp;
            uint32_t gfx_base = 0x40 * video_regs[0x22];
            for (uint32_t py = 0; py < s.h; py++) {
                uint32_t ty = flip_y ? (s.h - 1 - py) : py;
                uint32_t m = gfx_base + words_per_tile * s.tile + bits_per_row * ty;
                uint32_t bits = 0, nbits = 0;
                for (int32_t px = flip_x ? (int32_t)(s.w - 1) : 0; flip_x ? px >= 0 : px < (int32_t)s.w; flip_x ? px-- : px++) {
                    bits <<= nc_bpp;
                    if (nbits < nc_bpp) {
                        uint16_t b = memory_read16(m++);
                        b = (b << 8) | (b >> 8);
                        bits |= b << (nc_bpp - nbits);
                        nbits += 16;
                    }
                    nbits -= nc_bpp;
                    uint32_t color_idx = bits >> 16;
                    bits &= 0xffff;
                    int dx = s.x + px - minX, dy = s.y + (int)py - minY;
                    if (dx < 0 || dx >= cw || dy < 0 || dy >= ch) continue;
                    uint16_t rgb = ram[0x2B00 + palette_offset + color_idx];
                    if (!(rgb & 0x8000)) canvas[dy * cw + dx] = decode_color(rgb);
                }
            }
        }

        // Dedup on the actual composited pixels, not tile/attr/position -
        // some idle animations (e.g. a palette-cycling color effect) keep
        // the same tile/attr/x/y every frame and only change which colors
        // the palette RAM at this offset currently holds, which the old
        // pre-composite key couldn't see at all (captured exactly one
        // frame then silently skipped every later one as a "duplicate").
        // FNV-1a over the canvas bytes catches any visually distinct frame
        // regardless of what changed under the hood to produce it.
        uint64_t hash = 1469598103934665603ULL;
        for (auto& c : canvas) {
            hash = (hash ^ c.r) * 1099511628211ULL;
            hash = (hash ^ c.g) * 1099511628211ULL;
            hash = (hash ^ c.b) * 1099511628211ULL;
            hash = (hash ^ c.a) * 1099511628211ULL;
        }
        char keybuf[48];
        sprintf(keybuf, "%dx%d_%016llX", cw, ch, (unsigned long long)hash);
        if (!exported_clusters.insert(keybuf).second) continue;

        // Nearest-neighbor pixel replication, not smooth scaling - this is
        // flat-color pixel art, so any interpolation would blur the clean
        // edges. Each source pixel becomes an exact scale x scale block, so
        // the upscaled image is still lossless relative to the source data.
        // Adjustable live via the mod menu's "Sprite Export Scale" row.
        int scale = EXPORT_SCALE_OPTIONS[g_export_scale_idx];
        std::vector<Color> upscaled((size_t)cw * scale * ch * scale);
        int scw = cw * scale, sch = ch * scale;
        for (int sy = 0; sy < sch; sy++)
            for (int sx = 0; sx < scw; sx++)
                upscaled[sy * scw + sx] = canvas[(sy / scale) * cw + (sx / scale)];

        // Organize by lowest hardware sprite SLOT in the cluster, not tile
        // ID - a persistent on-screen actor (the minipet, the player) keeps
        // the same slot(s) assigned every frame while it's on screen, even
        // though its tile ID deliberately changes every frame for
        // animation (and an idle bob also shifts its x/y). Grouping by tile
        // ID put every animation frame in its own folder; slot is the
        // stable identity that actually groups them together.
        int anchor_slot = 256;
        for (int idx : members) anchor_slot = std::min(anchor_slot, active[idx].slot);
        std::string obj_dir = out_dir + "/obj_slot" + std::to_string(anchor_slot);
        std::filesystem::create_directories(obj_dir);
        char path[320];
        sprintf(path, "%s/tile0x%04X_%dtiles_%dx%d_f%llu.png", obj_dir.c_str(), active[members[0]].tile,
            (int)members.size(), cw, ch, (unsigned long long)exported_clusters.size());
        Image img{ (void*)upscaled.data(), scw, sch, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
        ExportImage(img, path);
    }
}

int main() {
    srand((unsigned)time(NULL));
    g_app_dir = GetApplicationDirectory();

    FILE* rom_f = fopen(app_path("resources/data/rom.u7").c_str(), "rb");
    if (!rom_f) {
        // Windowed-subsystem build has no console, so a printf here would
        // be invisible and the app would just silently vanish on first run.
        MessageBoxA(nullptr,
            "ROM not found.\n\nRename your Giga Pets Explorer ROM to \"rom.u7\" and place it in the\n\"resources\\data\" folder next to GigaPetsPC.exe, then run again.",
            "GigaPets PC Port", 0x10 /* MB_ICONERROR */);
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

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow((int)(WIDE_W * DEFAULT_WINDOW_SCALE), (int)(NATIVE_H * DEFAULT_WINDOW_SCALE), "GigaPets PC Port");
    SetTargetFPS(FPS_LIMIT_OPTIONS[g_fps_limit_idx]);

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

                // MYSTERY ISLAND ACCESS - TABLED, not fixed. Two theories
                // for the real "let the player travel" gate were both
                // disproven via live ground-truth tracing (not just static
                // analysis) this session:
                //   1. g_miniPetTrackingFlag/IsMiniPetTrackingEnabled
                //      (0x1980/0x04698d) - traced every distinct real caller
                //      during both a blocked and a successful attempt; all
                //      were already-known, unrelated call sites (dialogue-
                //      denial bookkeeping in Game_ProcessTick etc.), none
                //      newly triggered by the travel attempt itself.
                //   2. TickMiniPetCartridgeCheckSequence's hardware GPIO
                //      check (0x3d01 & 0x10) at ROM 0x015fc0-0x015fc4 - a
                //      real, verified code path (byte-for-byte confirmed
                //      against raw disassembly), but a PC tracer on its
                //      entry/gate addresses never fired during either
                //      attempt - this code simply isn't reached for this
                //      interaction, despite plausible-sounding surrounding
                //      analysis (from an external agent) claiming it's
                //      reached via Pet_RunCurrentAction's activity dispatch;
                //      re-checking that function directly found no such
                //      dispatch logic where claimed.
                // The real gate is still unknown. Confirmed workaround:
                // spawn any minipet via the mod menu before traveling - it
                // satisfies whatever the actual (unfound) check is.
                //
                // TEMP DEBUG: log every real per-tick HandlePetStageEvents
                // call (ROM 0x01308A - confirmed via decompile to be the
                // actual per-tick handler, not a one-time room-load call)
                // while investigating the Mystery Island gate. Captures the
                // raw tracking flag, current area, and player position each
                // call so a "spawn mid-visit, blocked" run can be diffed
                // against a "reload, works" run. Safe to remove once the
                // gate is found.
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
        EndDrawing();
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
