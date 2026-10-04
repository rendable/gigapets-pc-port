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

// On-screen frame counter, shown in the corner each frame.
long g_frame = 0;

// Watchdog: real hardware resets the CPU if REG_WATCHDOG_CLEAR (0x3D24) isn't
// petted with 0x55AA within 750ms while enabled via REG_SYSTEM_CTRL bit 15.
// The ROM relies on this to bounce back to the boot vector (e.g. on quit),
// so it must be emulated or those flows hang forever.
bool watchdog_enabled = false;
int watchdog_frames_left = 0;

void check_video_irq() {
    if (video_regs[0x63] & video_regs[0x62]) cpu_ptr->execute_set_input(UNSP_IRQ0_LINE, 1);
    else cpu_ptr->execute_set_input(UNSP_IRQ0_LINE, 0);
}
