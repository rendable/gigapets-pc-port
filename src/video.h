// Software rendering of the SPG2xx tilemaps and sprites into an RGBA framebuffer, plus the
// sprite-table snapshots used for render interpolation. Based on MAME's spg2xx_video.cpp
// (BSD-3-Clause, Ryan Holtz).
#pragma once

#include "base.h"
#include "constants.h"

struct InterpSnapshot {
    uint16_t tile[SPRITE_SLOT_COUNT];
    int16_t x[SPRITE_SLOT_COUNT];
    int16_t y[SPRITE_SLOT_COUNT];
    uint16_t attr[SPRITE_SLOT_COUNT];
};

extern InterpSnapshot g_interp_curr, g_interp_prev;
extern bool g_have_interp_snapshot;

Color decode_color(uint16_t rgb555);
void draw_page(Color* framebuffer, int fb_w, int margin_l, int margin_r, int page_idx, uint16_t* tilemapregs, uint16_t* scrollregs, uint32_t tilegfxdata_addr, int target_priority);
void draw_sprites(Color* framebuffer, int fb_w, int margin_l, int target_priority);
