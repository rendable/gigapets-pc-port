// SPG2xx hardware register addresses and video register offsets, named after MAME's
// spg2xx_io.cpp / spg2xx_video.cpp / spg2xx_sysdma.cpp so they can be cross-referenced.
//
// CPU-visible memory map (word addresses), see memory_map.cpp:
//   0x0000-0x27FF  RAM
//   0x2800-0x28FF  video registers (video_regs[], indexed by offset from 0x2800)
//   0x2900-0x2FFF  more RAM: row-scroll table, palette, sprite table
//   0x3000-0x31FF  audio channel registers
//   0x3200-0x33FF  audio phase registers
//   0x3400-0x341F  audio control registers
//   0x3420-0x3FFF  IO registers (GPIO, system control, timers, DMA, ...), io[] indexed from 0x3000
//   0x4000-...     ROM (banked through the data segment / code bank bits)
#pragma once

#include <stdint.h>

static const uint32_t VIDEO_REGS_BASE = 0x2800;
static const uint32_t IO_REGS_BASE = 0x3000;

// Video RAM regions (inside RAM).
static const uint32_t ROW_SCROLL_RAM_ADDR = 0x2900; // per-scanline X scroll offsets (0x2900-0x29FF)
static const uint32_t PALETTE_RAM_ADDR = 0x2B00;    // 256 RGB555 palette entries

// Video register offsets (video_regs[VREG_*], i.e. CPU address 0x2800 + offset).
static const int VREG_PAGE1_X_SCROLL = 0x10;  // X then Y scroll (0x11)
static const int VREG_PAGE1_ATTR = 0x12;      // attributes, control, tile address, attribute address
static const int VREG_PAGE2_X_SCROLL = 0x16;
static const int VREG_PAGE2_ATTR = 0x18;
static const int VREG_PAGE1_SEGMENT = 0x20;   // tile graphics segment address
static const int VREG_PAGE2_SEGMENT = 0x21;
static const int VREG_SPRITE_SEGMENT = 0x22;  // sprite graphics segment address
static const int VREG_BLEND_LEVEL = 0x2A;
static const int VREG_SPRITE_CONTROL = 0x42;
static const int VREG_IRQ_ENABLE = 0x62;      // bit0 = vblank
static const int VREG_IRQ_STATUS = 0x63;      // write-1-to-clear
static const int VREG_SPRITE_DMA_SRC = 0x70;
static const int VREG_SPRITE_DMA_DST = 0x71;
static const int VREG_SPRITE_DMA_LEN = 0x72;

// IO registers (CPU addresses).
static const uint32_t REG_IOA_DATA = 0x3D01;        // GPIO port A: the game buttons
static const uint32_t REG_IOB_DATA = 0x3D06;        // GPIO port B: EEPROM CS/CLK/DI/DO
static const uint32_t REG_SYSTEM_CTRL = 0x3D20;     // bit15 arms the watchdog
static const uint32_t REG_WATCHDOG_CLEAR = 0x3D24;  // write 0x55AA to feed the watchdog
static const uint32_t REG_ADC_DATA = 0x3D27;
static const uint32_t REG_PRNG1 = 0x3D2C;
static const uint32_t REG_PRNG2 = 0x3D2D;
static const uint32_t REG_DATA_SEGMENT = 0x3D2F;    // ds: bank select for extended addressing

// System DMA registers.
static const uint32_t SYSDMA_SRC_LO = 0x3E00;
static const uint32_t SYSDMA_SRC_HI = 0x3E01;
static const uint32_t SYSDMA_LENGTH = 0x3E02;
static const uint32_t SYSDMA_DST = 0x3E03;
