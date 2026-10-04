// Emulated machine state: address spaces (RAM/IO/ROM/video regs), the CPU pointer, and
// helpers for calling ROM functions from host code.

#include "common.h"

std::string g_app_dir;

std::string app_path(const char* rel) { return g_app_dir + rel; }

uint16_t ram[0x3000];
uint16_t io[0x1000];
uint16_t rom[0x400000];
uint16_t video_regs[0x100];
unsp_20_device* cpu_ptr;

// Synchronously runs a real ROM function from host code, so the ROM's own proven-correct logic
// (e.g. its per-field EEPROM save routines) does the work instead of a C++ reimplementation.
//
// It mimics a far CALL as the unSP core implements it: push the arguments in the real caller's
// order, then a return frame of the CPU's current PC and SR (SR on top), then jump to the target.
// It steps until execution returns to that point and force-restores PC/SR/SP afterwards, so a
// misbehaving callee can't leave the CPU in a bad state. Args are 16-bit words, in push order.
void call_rom_function(uint32_t target_full_addr, const std::vector<uint16_t>& args_in_push_order) {
    uint32_t orig_pc = cpu_ptr->get_r(unsp_12_device::REG_PC);
    uint32_t orig_sr = cpu_ptr->get_r(unsp_12_device::REG_SR);
    uint32_t orig_sp = cpu_ptr->get_r(unsp_12_device::REG_SP);
    // Compare against the full banked PC (PC plus SR's low 6 bits), not the raw 16-bit PC register,
    // or the return check only works when the caller happens to be in bank 0.
    uint32_t orig_full_pc = full_pc();

    uint32_t sp = orig_sp;
    for (uint16_t v : args_in_push_order) { ram[sp] = v; sp = (uint16_t)(sp - 1); }
    ram[sp] = (uint16_t)orig_pc; sp = (uint16_t)(sp - 1);
    ram[sp] = (uint16_t)orig_sr; sp = (uint16_t)(sp - 1);
    cpu_ptr->set_r(unsp_12_device::REG_SP, sp);
    cpu_ptr->set_r(unsp_12_device::REG_PC, (uint16_t)(target_full_addr & 0xFFFF));
    cpu_ptr->set_r(unsp_12_device::REG_SR, (orig_sr & 0xFFC0) | ((target_full_addr >> 16) & 0x3F));

    // The emulated EEPROM drops writes while locked (a real 93C66 needs an unlock command first).
    // A normal call chain unlocks it earlier; this call jumps straight into a save routine and
    // skips that, so unlock for the duration of the call and restore it afterwards.
    bool orig_eeprom_locked = eeprom_locked;
    eeprom_locked = false;

    for (int guard = 0; guard < 200000; guard++) {
        // Some save paths wait on the vblank tick counter (ram 0x13/0x14). That normally advances once
        // per frame outside this call, so pump it here too or the callee would spin to the guard cap.
        if (guard % 7500 == 0 && (video_regs[VREG_IRQ_ENABLE] & 1)) { video_regs[VREG_IRQ_STATUS] |= 1; check_video_irq(); }
        cpu_ptr->step(1);
        if (full_pc() == orig_full_pc) break;
    }
    eeprom_locked = orig_eeprom_locked;
    cpu_ptr->set_r(unsp_12_device::REG_SP, orig_sp);
    cpu_ptr->set_r(unsp_12_device::REG_PC, orig_pc);
    cpu_ptr->set_r(unsp_12_device::REG_SR, orig_sr);
}

// Number of completed 1/60s simulation ticks.
long g_frame = 0;

// Watchdog: real hardware resets the CPU if REG_WATCHDOG_CLEAR isn't fed with 0x55AA within 750 ms
// while enabled via REG_SYSTEM_CTRL bit 15. The ROM relies on this to reboot (e.g. on Quit).
bool watchdog_enabled = false;
int watchdog_frames_left = 0;

void check_video_irq() {
    if (video_regs[VREG_IRQ_STATUS] & video_regs[VREG_IRQ_ENABLE]) cpu_ptr->execute_set_input(UNSP_IRQ0_LINE, 1);
    else cpu_ptr->execute_set_input(UNSP_IRQ0_LINE, 0);
}

// Counts down the emulated hardware watchdog once per 1/60s tick; if the ROM armed it and stopped
// feeding it, performs the CPU reset real hardware would (see memory_write16, 0x3D20/0x3D24).
void watchdog_tick() {
    if (watchdog_enabled) {
        watchdog_frames_left--;
        if (watchdog_frames_left <= 0) {
            memset(ram, 0, sizeof(ram));
            memset(io, 0, sizeof(io));
            memset(video_regs, 0, sizeof(video_regs));
            audio_reset();
            cpu_ptr->device_reset();
            watchdog_enabled = false;
            watchdog_frames_left = 0;
        }
    }
}
