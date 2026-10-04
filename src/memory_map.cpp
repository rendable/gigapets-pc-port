// The emulated memory map: every CPU read/write of RAM, IO registers, video regs, audio
// regs, GPIO and DMA lands in memory_read16/memory_write16.

#include "common.h"

uint16_t memory_read16(uint32_t addr) {
    if (addr < 0x2800) return ram[addr];
    if (addr <= 0x28FF) return video_regs[addr - VIDEO_REGS_BASE];
    if (addr <= 0x2FFF) return ram[addr];
    if (addr <= 0x31FF) return audio_r(addr - 0x3000);
    if (addr <= 0x33FF) return audio_phase_r(addr - 0x3200);
    if (addr <= 0x341F) return audio_ctrl_regs[addr - 0x3400];
    if (addr <= 0x3FFF) {
        // Hardware PRNG
        if (addr == REG_PRNG1 || addr == REG_PRNG2) return (uint16_t)rand();

        // ADC Data (Random Pet Colors) - ready bit + random low bits
        if (addr == REG_ADC_DATA) return (uint16_t)((rand() & 0x0FFF) | 0x8000);

        // REG_DATA_SEGMENT passthrough - see the DS-register bug note above.
        if (addr == REG_DATA_SEGMENT) return cpu_ptr->get_ds();

        // GPIO Port A Data (Inputs) - REG_IOA_DATA. Buttons are wired to
        // Port A; bit index matches GameButton enum order by construction.
        if (addr == REG_IOA_DATA) {
            uint16_t val = io[addr - IO_REGS_BASE];
            uint16_t low = 0x0000; // active-high; default = nothing pressed
            if (!g_mod_menu_open) { // don't let mod-menu navigation leak into the game
                for (int b = 0; b < GAME_BUTTON_COUNT; b++) {
                    if (IsKeyDown(g_key_binding[b])) low |= (1 << b);
                }
                // Gamepad 0: the D-pad and left stick both drive movement, and the face/start buttons
                // are bound through g_gamepad_binding (remappable in the Controls menu). raylib
                // normalizes Xbox- and PlayStation-style pads to the same button enum.
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
            if (g_selftest) low = selftest_buttons(g_frame);
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
        if (addr == REG_IOB_DATA) {
            uint16_t val = io[addr - IO_REGS_BASE];
            return (val & ~0x0008) | (eeprom_do_read() ? 0x0008 : 0);
        }

        return io[addr - IO_REGS_BASE];
    }
    if (addr < 0x400000) return rom[addr];
    return 0;
}

void memory_write16(uint32_t addr, uint16_t data) {
    if (addr < 0x2800) {
        ram[addr] = data;
    } else if (addr <= 0x28FF) {
        if (addr == VIDEO_REGS_BASE + VREG_IRQ_STATUS) { video_regs[VREG_IRQ_STATUS] &= ~data; check_video_irq(); }
        else if (addr == VIDEO_REGS_BASE + VREG_IRQ_ENABLE) { video_regs[VREG_IRQ_ENABLE] = data; check_video_irq(); }
        else if (addr == VIDEO_REGS_BASE + VREG_SPRITE_DMA_LEN) {
            video_regs[VREG_SPRITE_DMA_LEN] = data & 0x03FF;
            uint16_t len = video_regs[VREG_SPRITE_DMA_LEN] ? video_regs[VREG_SPRITE_DMA_LEN] : 0x400;
            uint32_t src = video_regs[VREG_SPRITE_DMA_SRC] & 0x3FFF;
            uint32_t dst = video_regs[VREG_SPRITE_DMA_DST] & 0x03FF;
            for (uint32_t j = 0; j < len; j++) if (dst + j < 0x400) ram[SPRITE_TABLE_ADDR + dst + j] = memory_read16(src + j);
            video_regs[VREG_SPRITE_DMA_LEN] = 0;
            if (video_regs[VREG_IRQ_ENABLE] & 4) { video_regs[VREG_IRQ_STATUS] |= 4; check_video_irq(); }
        } else {
            video_regs[addr - VIDEO_REGS_BASE] = data;
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
        io[addr - IO_REGS_BASE] = data;

        // REG_DATA_SEGMENT passthrough - see the get_ds() read-side note
        // above. Without wiring the write side too, ds:-prefixed extended
        // addressing (room/tile/sprite bank select) never actually changes
        // banks - caused invisible-wall collision bugs, pet-selector sprite
        // snapping, and personality-screen icon misplacement, all from the
        // same stale-DS root cause.
        if (addr == REG_DATA_SEGMENT) cpu_ptr->set_ds(data & 0x3f);

        // GPIO Port B: bit0=EEPROM CS, bit1=CLK, bit2=DI (bit3=DO is read-only)
        if (addr == REG_IOB_DATA) {
            eeprom_cs_write((data & 0x0001) != 0);
            eeprom_clk_write((data & 0x0002) != 0);
            eeprom_di_write((data & 0x0004) != 0);
        }

        // Watchdog, ported from spg2xx_io_device: bit 15 of REG_SYSTEM_CTRL arms a 750 ms countdown
        // (45 ticks at 60 fps), writing 0x55AA to REG_WATCHDOG_CLEAR while armed reloads it, and
        // letting it expire resets the CPU. The game's Quit flow relies on this: it arms the
        // watchdog and stops feeding it so the machine reboots to the main menu. Without it Quit
        // froze the screen. See watchdog_tick() and docs/ROM_NOTES.md, "Watchdog and Quit".
        if (addr == REG_SYSTEM_CTRL) {
            bool want_enabled = (data & 0x8000) != 0;
            if (want_enabled && !watchdog_enabled) { watchdog_enabled = true; watchdog_frames_left = 45; }
            else if (!want_enabled) { watchdog_enabled = false; watchdog_frames_left = 0; }
        }
        if (addr == REG_WATCHDOG_CLEAR && data == 0x55AA && watchdog_enabled) {
            watchdog_frames_left = 45;
        }

        // System DMA, ported from spg2xx_sysdma_device::do_cpu_dma: writing the word count (control
        // bits in the top 2) to SYSDMA_LENGTH copies that many words from a 22-bit source
        // (SYSDMA_SRC_LO/HI) to a 14-bit destination (SYSDMA_DST). On completion the hardware
        // clears the length register (the ROM polls it) AND advances source/destination past the
        // copied region, which chained transfers rely on.
        if (addr == SYSDMA_LENGTH) {
            uint32_t src = ((io[SYSDMA_SRC_HI - IO_REGS_BASE] & 0x3f) << 16) | io[SYSDMA_SRC_LO - IO_REGS_BASE];
            uint32_t dst = io[SYSDMA_DST - IO_REGS_BASE] & 0x3fff;
            uint32_t len = data & ~0xc000;
            if (!(data & 0xc000)) {
                for (uint32_t j = 0; j < len; j++) memory_write16((dst + j) & 0x3fff, memory_read16(src + j));
                src += len;
                io[SYSDMA_SRC_LO - IO_REGS_BASE] = (uint16_t)src;
                io[SYSDMA_SRC_HI - IO_REGS_BASE] = (src >> 16) & 0x3f;
                io[SYSDMA_DST - IO_REGS_BASE] = (dst + len) & 0x3fff;
            }
            io[SYSDMA_LENGTH - IO_REGS_BASE] = 0;
        }
    }
}
