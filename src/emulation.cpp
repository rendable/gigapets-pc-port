// One emulated 1/60s tick: runs the CPU for a frame's worth of cycles with the ROM hooks and
// cycle-interleaved audio generation, then per-tick bookkeeping.

#include "common.h"

// Runs the CPU for one frame (27 MHz / 60 cycles), firing the video vblank IRQ first.
static void emulate_frame() {
    unsp_20_device& cpu = *cpu_ptr;

    if (video_regs[VREG_IRQ_ENABLE] & 1) { video_regs[VREG_IRQ_STATUS] |= 1; check_video_irq(); }
    // Real unSP instructions cost variable cycles (2-12+), not a flat 1 -
    // budget against actual cycles consumed so this matches real
    // hardware throughput per frame instead of running far more
    // instructions than real silicon would in 1/60s.
    long cycle_budget = 27000000 / 60;
    while (cycle_budget > 0) {
        if (rom_hooks_before_step(cpu)) break;

        cpu.step(1);
        long cycles_this_instr = 1 - cpu.icount();
        cycle_budget -= cycles_this_instr;

        audio_run_cycles(cycles_this_instr);

        rom_hooks_after_step(cpu);
    }
}

void sim_tick() {
    watchdog_tick();
    test_menu_tick();
    emulate_frame();
    player_mods_tick();
    video_snapshot_sprites();
    g_frame++;
}
