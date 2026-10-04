// Per-instruction hooks keyed on the emulated PC, plus the hidden test-menu unlock state.
#pragma once

#include "base.h"

extern bool g_test_menu_seq_active;
extern int g_test_menu_seq_frame;
extern int g_test_menu_chime_frame;
extern uint16_t g_test_mode_eeprom_backup[256];
extern bool g_test_mode_backup_valid;
bool rom_hooks_before_step(unsp_20_device& cpu);
void rom_hooks_after_step(unsp_20_device& cpu);
void test_menu_tick();
void test_menu_unlock_begin();
