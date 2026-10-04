// Emulated machine state: address spaces (RAM/IO/ROM/video regs), the CPU pointer, and
// helpers for calling ROM functions from host code.
#pragma once

#include "base.h"

extern std::string g_app_dir;
extern uint16_t ram[0x3000];
extern uint16_t io[0x1000];
extern uint16_t rom[0x400000];
extern uint16_t video_regs[0x100];
extern unsp_20_device* cpu_ptr;
extern long g_frame;
extern bool watchdog_enabled;
extern int watchdog_frames_left;

inline uint32_t full_pc() { return ((cpu_ptr->get_r(6) & 0x3f) << 16) | cpu_ptr->get_r(7); }

std::string app_path(const char* rel);
void call_rom_function(uint32_t target_full_addr, const std::vector<uint16_t>& args_in_push_order);
void check_video_irq();
void watchdog_tick();
