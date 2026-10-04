// Window/render sizes, timing options and sprite-table constants.
#pragma once

#include "base.h"

const int FPS_LIMIT_OPTION_COUNT = 3;

const int FPS_LIMIT_OPTIONS[3] = { 60, 120, 0 };

const int EXPORT_SCALE_OPTION_COUNT = 4;

const int EXPORT_SCALE_OPTIONS[4] = { 1, 2, 4, 8 };

// Sprite table: 256 slots x 4 words (tile, x, y, attr) at 0x2C00 - see
// draw_sprites. Named here too since render interpolation needs to snapshot
// the whole table independently of the renderer.
static const uint32_t SPRITE_TABLE_ADDR = 0x2C00;
static const int SPRITE_SLOT_COUNT = 256;

// Bigger than the bare-minimum 2x - the mod menu/HUD text is a fixed pixel
// size in this same virtual space (see g_hud_cam), so raising this scales
// legibility up for everything uniformly without touching any individual
// font-size or row-layout constant.
static const float DEFAULT_WINDOW_SCALE = 2.5f;
static const int AUDIO_STREAM_CHUNK = 4096;
static const char* APP_VERSION = "0.1.0";
static const int NATIVE_W = 320;
static const int NATIVE_H = 240;
static const int WIDE_W = 320;
static const int WIDE_MARGIN_L = (WIDE_W - NATIVE_W) / 2;
static const int WIDE_MARGIN_R = (WIDE_W - NATIVE_W) - WIDE_MARGIN_L;
