// Per-instruction hooks keyed on the emulated PC, plus the hidden test-menu unlock state.
//
// Most of these are "register fakes": when execution reaches a specific ROM address we overwrite
// one CPU register so the ROM's own next compare goes the way real hardware would have taken it.
// That is preferable to patching memory (which other ROM code also reads) or reimplementing ROM
// logic. Why each one exists is written up in docs/ROM_NOTES.md.

#include "common.h"

// F9 hidden test-menu unlock (off unless the player presses the hotkey): resets the machine, then
// drives the real Left+Select / Up, Down, Menu, Back button sequence during the fresh boot.
// See ROM_TEST_MENU_GATE below for the ROM-side half.
bool g_test_menu_seq_active = false;
int g_test_menu_seq_frame = 0;

// Frame at which the real chime (PlaySoundEffect 0x59) fired, ~182 frames in. The scripted button
// timing is relative to this, because releasing Left+Select before the chime cancels test mode.
int g_test_menu_chime_frame = -1;

// Test mode ends in PowerDownHardware (a genuine do-nothing loop) and TestRomChecksum erases the
// whole EEPROM, so the save is snapshotted on entry and restored on exit.
uint16_t g_test_mode_eeprom_backup[256];
bool g_test_mode_backup_valid = false;

// Hooks that run before each emulated instruction, keyed on the PC about to execute.
// Returns true if the machine was just reset and the caller should stop stepping this frame.
bool rom_hooks_before_step(unsp_20_device& cpu) {
    // Minipet "stay parked": FeedOrAdvanceMiniPet despawns the minipet unless the animation
    // struct's first word reads 0x7B, but the synthetic follow state needs a real walk-pose id
    // there. So instead of changing the struct, fake the one register the compare reads: after the
    // load at ROM_MINIPET_ANIM_LOAD executes, set r1 to 0x7B before the `cmp r1, 0x7B`.
    static bool force_r1_sentinel = false;
    if (force_r1_sentinel) {
        cpu.set_r(unsp_12_device::REG_R1, 0x7B);
        force_r1_sentinel = false;
    }
    if (g_minipet_spawned && full_pc() == ROM_MINIPET_ANIM_LOAD) {
        force_r1_sentinel = true;
    }

    // Known issue - Mystery Island travel: the dock trigger only works if a minipet was already
    // tracked when the room last loaded (ACTIVE_STORY_OBJECT_ID_ADDR must read 0x68 there).
    // Spawning from the Mod Menu mid-visit does not retroactively update it; leave and re-enter
    // the area first. A real fix needs LoadRoom's per-area arguments worked out (docs/ROM_NOTES.md).

    // Hidden hardware test menu: forcing this flag at the gate unlocks it. The real input combo is
    // still required on top (see g_test_menu_seq_active).
    if (full_pc() == ROM_TEST_MENU_GATE) {
        ram[TEST_MENU_UNLOCK_FLAG_ADDR] = 1;
    }
    // PlaySoundEffect just after its prologue: r2 is the sound id. 0x59 is the chime that times the
    // test-menu button release.
    if (full_pc() == ROM_PLAY_SOUND_EFFECT_ARG_READY) {
        uint16_t sound_id = cpu.get_r(unsp_12_device::REG_R2);
        if (sound_id == 0x59 && g_test_menu_seq_active && g_test_menu_chime_frame < 0) {
            g_test_menu_chime_frame = g_test_menu_seq_frame;
        }
    }
    // "Exit Test Mode" reaches PowerDownHardware, which on real hardware switches the device off.
    // Restore the pre-test EEPROM snapshot and reboot to the main menu instead.
    if (full_pc() == ROM_POWER_DOWN_HARDWARE && g_test_mode_backup_valid) {
        memcpy(eeprom_data, g_test_mode_eeprom_backup, sizeof(eeprom_data));
        eeprom_save();
        g_test_mode_backup_valid = false;
        memset(ram, 0, sizeof(ram));
        memset(io, 0, sizeof(io));
        memset(video_regs, 0, sizeof(video_regs));
        audio_reset();
        cpu.device_reset();
        return true;
    }
    return false;
}

// Hooks that run after each emulated instruction: they override a register right after the
// instruction that loaded it, so the very next compare sees the patched value.
void rom_hooks_after_step(unsp_20_device& cpu) {
    // Disable the 8-minute idle auto-power-off. WaitForNextTick compares the idle time (via
    // FloatCompare, result in r1) against the threshold and jumps to IdleTimeoutScreen, which spins
    // forever waiting for a physical power button. Forcing r1 to 0 makes that jump never fire.
    if (full_pc() == ROM_IDLE_TIMEOUT_COMPARE) {
        cpu.set_r(unsp_12_device::REG_R1, 0);
    }

    // Always report "no link-cable partner". If the link-detect GPIO pins on REG_IOA_DATA look
    // connected, the ROM runs Cart_WriteBytes, which disables video IRQs and never re-enables them,
    // so the next WaitForNextTick hangs and the game freezes. Real hardware with nothing plugged in
    // bails out here; zeroing r1 reproduces that. Button bits on the same register are untouched.
    if (full_pc() == ROM_LINK_PARTNER_GATE) {
        cpu.set_r(unsp_12_device::REG_R1, 0);
    }

    // GeneratePaletteBlendTable spins until the vblank tick counter (ram 0x13/0x14) changes. On real
    // hardware a vblank IRQ can interrupt that spin at any instruction; here the tick only advances
    // once per frame's cycle budget, so the spin could never end. Making the "did it change"
    // compare read as unequal lets the ROM's own completion logic proceed.
    if (full_pc() == ROM_PALETTE_BLEND_TICK_WAIT) {
        cpu.set_r(unsp_12_device::REG_R1, cpu.get_r(unsp_12_device::REG_R3) + 1);
    }
}

// Advances the F9 test-menu unlock sequence by one 1/60s tick.
void test_menu_tick() {
    if (g_test_menu_seq_active) {
        g_test_menu_seq_frame++;
        // Safety cap in case the chime never fires; the normal end of the sequence is handled in
        // the REG_IOA_DATA override in memory_map.cpp.
        if (g_test_menu_seq_frame >= 900) g_test_menu_seq_active = false;
    }
}

// F9: snapshot the save, reset the machine to a guaranteed-fresh boot, and start the scripted
// button sequence that opens the hidden test menu.
void test_menu_unlock_begin() {
    memcpy(g_test_mode_eeprom_backup, eeprom_data, sizeof(eeprom_data));
    g_test_mode_backup_valid = true;
    memset(ram, 0, sizeof(ram));
    memset(io, 0, sizeof(io));
    memset(video_regs, 0, sizeof(video_regs));
    audio_reset();
    cpu_ptr->device_reset();
    g_test_menu_seq_active = true;
    g_test_menu_seq_frame = 0;
    g_test_menu_chime_frame = -1;
}
