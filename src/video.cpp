// Software rendering of the SPG2xx tilemaps and sprites into an RGBA framebuffer, plus the
// sprite-table snapshots used for render interpolation. Based on MAME's spg2xx_video.cpp
// (BSD-3-Clause, Ryan Holtz).

#include "common.h"

InterpSnapshot g_interp_curr, g_interp_prev;
bool g_have_interp_snapshot = false;

Color decode_color(uint16_t rgb555) {
    Color c;
    c.r = (unsigned char)(((rgb555 >> 10) & 0x1F) * 255 / 31);
    c.g = (unsigned char)(((rgb555 >> 5) & 0x1F) * 255 / 31);
    c.b = (unsigned char)(((rgb555 >> 0) & 0x1F) * 255 / 31);
    c.a = 255;
    return c;
}

// Blend level control (video_regs[0x2A] & 3) - real hardware's alpha-blend
// mode for tiles/sprites (shadows, glass, etc): min level is 25% opaque, max
// is 100% opaque. Matches spg_renderer_device::mix_channel / s_blend_levels.
static const uint8_t BLEND_LEVELS[4] = { 0x08, 0x10, 0x18, 0x20 };

inline Color mix_color(Color bottom, Color top, uint8_t alpha) {
    Color c;
    c.r = (unsigned char)(((0x20 - alpha) * bottom.r + alpha * top.r) >> 5);
    c.g = (unsigned char)(((0x20 - alpha) * bottom.g + alpha * top.g) >> 5);
    c.b = (unsigned char)(((0x20 - alpha) * bottom.b + alpha * top.b) >> 5);
    c.a = 255;
    return c;
}

void draw_page(Color* framebuffer, int fb_w, int margin_l, int margin_r, int page_idx, uint16_t* tilemapregs, uint16_t* scrollregs, uint32_t tilegfxdata_addr, int target_priority) {
    uint32_t ctrl = tilemapregs[1];
    if (!(ctrl & 0x0008)) return;

    if (ctrl & 0x0001) {
        // Linemap mode: a linear per-scanline buffer, never widened - just
        // pillarboxed via margin_l, sampling only the original 320 columns.
        uint32_t tilemap = tilemapregs[2];
        uint32_t palette_map = tilemapregs[3];
        uint32_t yscroll = scrollregs[1];
        uint32_t attr = tilemapregs[0];
        if (((attr & 0x3000) >> 12) != target_priority) return;

        for (int y = 0; y < 240; y++) {
            int realline = (y + yscroll) & 0xff;
            uint32_t tile = memory_read16(tilemap + realline);
            uint16_t palette = memory_read16(palette_map + (realline / 2));
            if (y & 1) palette >>= 8; else palette &= 0x00ff;
            uint32_t sourcebase = tile | (palette << 16);

            uint8_t bpp = attr & 0x0003;
            uint32_t nc_bpp = ((bpp) + 1) << 1;
            uint32_t palette_offset = (attr & 0x0f00) >> 4;
            palette_offset >>= nc_bpp;
            palette_offset <<= nc_bpp;

            uint32_t bits = 0, nbits = 0;
            for (int x = 0; x < 320; x++) {
                bits <<= nc_bpp;
                if (nbits < nc_bpp) {
                    uint16_t b = memory_read16(sourcebase++);
                    b = (b << 8) | (b >> 8);
                    bits |= b << (nc_bpp - nbits);
                    nbits += 16;
                }
                nbits -= nc_bpp;
                uint32_t color_idx = bits >> 16;
                bits &= 0xffff;
                int screen_x_lm = x + margin_l;
                uint16_t rgb = ram[0x2B00 + palette_offset + color_idx];
                if (!(rgb & 0x8000)) {
                    framebuffer[y * fb_w + screen_x_lm] = decode_color(rgb);
                }
            }
        }
        return;
    }

    // Tilemap mode: a toroidal buffer wider than the visible window - the
    // column range is extended past the normal 320px viewport by however
    // many extra tiles cover margin_l/margin_r on each side.
    const uint32_t attr = tilemapregs[0];
    if (((attr & 0x3000) >> 12) != target_priority) return;
    const uint32_t tilemap_rambase = tilemapregs[2];
    const uint32_t exattributemap_rambase = tilemapregs[3];
    const int tile_width = (attr & 0x0030) >> 4;
    const uint32_t tile_h = 8 << ((attr & 0x00c0) >> 6);
    const uint32_t tile_w = 8 << (tile_width);
    const uint32_t tile_count_x = 512 / tile_w;
    const uint32_t xscroll = scrollregs[0];
    const uint32_t yscroll = scrollregs[1];

    for (int y = 0; y < 240; y++) {
        uint32_t bitmap_y = (y + yscroll) & 0xff;
        uint32_t y0 = bitmap_y / tile_h;
        uint32_t tile_scanline = bitmap_y % tile_h;

        uint32_t realxscroll = xscroll;
        if (ctrl & 0x0010) { // Row scroll: per-scanline X offset (0x2900-0x29FF)
            realxscroll += (int16_t)ram[0x2900 + ((y + yscroll) & 0xff)];
        }
        const int upperscrollbits = (realxscroll >> (tile_width + 3));
        const int endpos = (320 + tile_w) / tile_w;
        const int x0_start = -(int)((margin_l + tile_w - 1) / tile_w) - 1;
        const int x0_end = endpos + (int)((margin_r + tile_w - 1) / tile_w) + 1;

        for (int x0 = x0_start; x0 < x0_end; x0++) {
            const int realx0 = (x0 + upperscrollbits) & (tile_count_x - 1);
            uint32_t tile_address = realx0 + (tile_count_x * y0);

            uint32_t tile = (ctrl & 0x0004) ? memory_read16(tilemap_rambase) : memory_read16(tilemap_rambase + tile_address);
            if (!tile) continue;

            uint32_t tileattr = attr;
            uint32_t tilectrl = ctrl;

            if ((tilectrl & 2) == 0) {
                uint16_t exattribute = (tilectrl & 0x0004) ? memory_read16(exattributemap_rambase) : memory_read16(exattributemap_rambase + tile_address / 2);
                if (realx0 & 1) exattribute >>= 8; else exattribute &= 0x00ff;
                tileattr &= ~0x000c; tileattr |= (exattribute >> 2) & 0x000c;
                tileattr &= ~0x0f00; tileattr |= (exattribute << 8) & 0x0f00;
                tilectrl &= ~0x0100; tilectrl |= (exattribute << 2) & 0x0100;
            }

            bool blend = (tilectrl & 0x0100) ? true : false;
            bool flip_x = (tileattr & 0x0004) ? true : false;
            bool flip_y = (tileattr & 0x0008) ? true : false;
            uint8_t bpp = tileattr & 0x0003;
            uint32_t nc_bpp = ((bpp) + 1) << 1;
            uint32_t bits_per_row = nc_bpp * tile_w / 16;
            uint32_t words_per_tile = bits_per_row * tile_h;

            uint32_t palette_offset = (tileattr & 0x0f00) >> 4;
            palette_offset >>= nc_bpp;
            palette_offset <<= nc_bpp;

            const uint32_t yflipmask = flip_y ? tile_h - 1 : 0;
            uint32_t m = (tilegfxdata_addr * 0x40) + words_per_tile * tile + bits_per_row * (tile_scanline ^ yflipmask);

            uint32_t bits = 0, nbits = 0;
            int drawx = (x0 * (int)tile_w) - (int)(realxscroll & (tile_w - 1));

            for (int32_t px = flip_x ? (tile_w - 1) : 0; flip_x ? px >= 0 : px < (int32_t)tile_w; flip_x ? px-- : px++) {
                bits <<= nc_bpp;
                if (nbits < nc_bpp) {
                    uint16_t b = memory_read16(m++);
                    b = (b << 8) | (b >> 8);
                    bits |= b << (nc_bpp - nbits);
                    nbits += 16;
                }
                nbits -= nc_bpp;
                uint32_t color_idx = bits >> 16;
                bits &= 0xffff;
                {
                    int screen_x = drawx + px + margin_l;
                    if (screen_x >= 0 && screen_x < fb_w) {
                        uint16_t rgb = ram[0x2B00 + palette_offset + color_idx];
                        if (!(rgb & 0x8000)) {
                            Color top = decode_color(rgb);
                            int fbi = y * fb_w + screen_x;
                            framebuffer[fbi] = blend
                                ? mix_color(framebuffer[fbi], top, BLEND_LEVELS[video_regs[0x2A] & 3])
                                : top;
                        }
                    }
                }
            }
        }
    }
}

void draw_sprites(Color* framebuffer, int fb_w, int margin_l, int target_priority) {
    uint32_t sprite_addr = 0x2C00;
    uint32_t spritegfxdata_addr = 0x40 * video_regs[0x22];

    for (int i = 0; i < 256; i++) {
        uint16_t tile = ram[sprite_addr + i * 4 + 0];
        if (!tile) continue;
        int16_t raw_x = (int16_t)ram[sprite_addr + i * 4 + 1];
        int16_t raw_y = (int16_t)ram[sprite_addr + i * 4 + 2];
        uint16_t attr = ram[sprite_addr + i * 4 + 3];

        if (((attr & 0x3000) >> 12) != target_priority) continue;

        uint32_t tile_h = 8 << ((attr & 0x00c0) >> 6);
        uint32_t tile_w = 8 << ((attr & 0x0030) >> 4);
        int centered_x = raw_x, centered_y = raw_y;
        if (!(video_regs[0x42] & 0x0002)) {
            centered_x = (320 / 2) + raw_x - (int)tile_w / 2;
            centered_y = (256 / 2) - raw_y - (int)tile_h / 2;
        }
        // Real hardware masks the (possibly negative, off-the-top-left)
        // centered position to a 9-bit toroidal coordinate space (0-511,
        // wider than the visible screen) and then uses it AS-IS, unsigned -
        // ported from spg_renderer_device::draw_sprite. It is NOT sign-
        // extended back to negative: values above 255 are legitimate large
        // on-screen X coordinates (very much so on this port's widened
        // 426px canvas), and the plain bounds check below already discards
        // anything that lands off-screen either way. Re-interpreting them
        // as negative (an earlier, wrong attempt at this same fix) made
        // every sprite past X~256 vanish instead of draw - the "right side
        // doesn't load" bug.
        int x = centered_x & 0x1ff;
        int y = centered_y & 0x1ff;

        bool blend = (attr & 0x4000) != 0;
        bool flip_x = (attr & 0x0004) != 0;
        bool flip_y = (attr & 0x0008) != 0;
        uint8_t bpp = attr & 0x0003;
        uint32_t nc_bpp = ((bpp) + 1) << 1;
        uint32_t bits_per_row = nc_bpp * tile_w / 16;
        uint32_t words_per_tile = bits_per_row * tile_h;

        uint32_t palette_offset = (attr & 0x0f00) >> 4;
        palette_offset >>= nc_bpp;
        palette_offset <<= nc_bpp;

        // x/y are 9-bit toroidal coordinates (0-511) on real hardware, not
        // plain screen offsets - a sprite near the top/left edge can have a
        // "small negative" position that masks to a large value near 511,
        // and its rows/columns must wrap back around through 0 to appear on
        // screen (see spg_renderer_device::draw_sprite's firstline/lastline
        // wraparound). Re-masking each computed row/col with & 0x1ff before
        // the bounds check reproduces that wrap; without it, sprites whose
        // position wraps this way (a tall tree/house anchored near the top
        // of the viewport) vanish entirely instead of wrapping into view.
        for (uint32_t py = 0; py < tile_h; py++) {
            int draw_y = (y + py) & 0x1ff;
            if (draw_y >= 240) continue;
            uint32_t ty = flip_y ? (tile_h - 1 - py) : py;
            uint32_t m = spritegfxdata_addr + words_per_tile * tile + bits_per_row * ty;

            uint32_t bits = 0, nbits = 0;
            for (int32_t px = flip_x ? (tile_w - 1) : 0; flip_x ? px >= 0 : px < (int32_t)tile_w; flip_x ? px-- : px++) {
                bits <<= nc_bpp;
                if (nbits < nc_bpp) {
                    uint16_t b = memory_read16(m++);
                    b = (b << 8) | (b >> 8);
                    bits |= b << (nc_bpp - nbits);
                    nbits += 16;
                }
                nbits -= nc_bpp;
                uint32_t color_idx = bits >> 16;
                bits &= 0xffff;
                {
                    int draw_x = ((x + px) & 0x1ff) + margin_l;
                    if (draw_x >= 0 && draw_x < fb_w) {
                        uint16_t rgb = ram[0x2B00 + palette_offset + color_idx];
                        if (!(rgb & 0x8000)) {
                            Color top = decode_color(rgb);
                            int fbi = draw_y * fb_w + draw_x;
                            framebuffer[fbi] = blend
                                ? mix_color(framebuffer[fbi], top, BLEND_LEVELS[video_regs[0x2A] & 3])
                                : top;
                        }
                    }
                }
            }
        }
    }
}

void video_snapshot_sprites() {
// Snapshot the sprite table once per completed tick - the pair
// of snapshots either side of "now" is what render interpolation
// below blends between using the leftover sim_accumulator
// fraction.
g_interp_prev = g_interp_curr;
for (int i = 0; i < SPRITE_SLOT_COUNT; i++) {
    g_interp_curr.tile[i] = ram[SPRITE_TABLE_ADDR + i * 4 + 0];
    g_interp_curr.x[i] = (int16_t)ram[SPRITE_TABLE_ADDR + i * 4 + 1];
    g_interp_curr.y[i] = (int16_t)ram[SPRITE_TABLE_ADDR + i * 4 + 2];
    g_interp_curr.attr[i] = ram[SPRITE_TABLE_ADDR + i * 4 + 3];
}
g_have_interp_snapshot = true;
}

// Builds the RGBA framebuffer for this frame. sim_fraction is how far (0..1) we are between the last
// two simulation ticks, used to interpolate sprite positions when interpolation is enabled.
void render_game_frame(Color* framebuffer, double sim_fraction) {
// Render interpolation: temporarily substitute blended sprite
// positions in place of the real (current-tick) values, build the
// framebuffer with draw_page/draw_sprites completely unchanged, then
// restore the real values immediately after - same "substitute, use,
// restore" shape used elsewhere in this file (e.g. frozen stats),
// safe here since it's plain data, no code execution involved.
uint16_t saved_sprite_xy[SPRITE_SLOT_COUNT * 2];
bool did_interp_substitute = g_interp_enabled && g_have_interp_snapshot;
if (did_interp_substitute) {
    for (int i = 0; i < SPRITE_SLOT_COUNT; i++) {
        saved_sprite_xy[i * 2 + 0] = ram[SPRITE_TABLE_ADDR + i * 4 + 1];
        saved_sprite_xy[i * 2 + 1] = ram[SPRITE_TABLE_ADDR + i * 4 + 2];
    }

    float alpha = (float)sim_fraction;
    if (alpha < 0.0f) alpha = 0.0f;
    if (alpha > 1.0f) alpha = 1.0f;

    for (int i = 0; i < SPRITE_SLOT_COUNT; i++) {
        // Tile+attr matching, not a position-delta continuity check -
        // confirmed by testing: the latter caused broad jitter
        // (including on fully static objects) from an unconfirmed
        // walk-cycle-animation-snapping theory. Don't reintroduce
        // that without new evidence it's needed.
        bool same_object = g_interp_curr.tile[i] != 0
            && g_interp_curr.tile[i] == g_interp_prev.tile[i]
            && g_interp_curr.attr[i] == g_interp_prev.attr[i];
        int16_t ix = g_interp_curr.x[i], iy = g_interp_curr.y[i];
        if (same_object) {
            int16_t dx = (int16_t)(g_interp_curr.x[i] - g_interp_prev.x[i]);
            int16_t dy = (int16_t)(g_interp_curr.y[i] - g_interp_prev.y[i]);
            ix = (int16_t)((float)g_interp_prev.x[i] + (float)dx * alpha);
            iy = (int16_t)((float)g_interp_prev.y[i] + (float)dy * alpha);
        }
        ram[SPRITE_TABLE_ADDR + i * 4 + 1] = (uint16_t)ix;
        ram[SPRITE_TABLE_ADDR + i * 4 + 2] = (uint16_t)iy;
    }
    // Background scroll is deliberately NOT interpolated (confirmed
    // by testing - the floor visibly jittered while the sprite-
    // interpolated player looked correct). This game streams new
    // tile data into the edges of a toroidal tile buffer once per
    // real tick, keyed to that tick's exact scroll position - our
    // fractional in-between scroll values don't have correctly-
    // streamed tile content to go with them, so smoothing the
    // scroll register was showing mismatched edge tiles right as
    // new columns streamed in. Sprites have no such dependency.
}

for (int i = 0; i < WIDE_W * NATIVE_H; i++) framebuffer[i] = Color{0, 0, 0, 0};

for (int priority = 0; priority < 4; priority++) {
    draw_page(framebuffer, WIDE_W, WIDE_MARGIN_L, WIDE_MARGIN_R, 2, &video_regs[0x18], &video_regs[0x16], video_regs[0x21], priority);
    draw_page(framebuffer, WIDE_W, WIDE_MARGIN_L, WIDE_MARGIN_R, 1, &video_regs[0x12], &video_regs[0x10], video_regs[0x20], priority);

    draw_sprites(framebuffer, WIDE_W, WIDE_MARGIN_L, priority);
}

if (did_interp_substitute) {
    for (int i = 0; i < SPRITE_SLOT_COUNT; i++) {
        ram[SPRITE_TABLE_ADDR + i * 4 + 1] = saved_sprite_xy[i * 2 + 0];
        ram[SPRITE_TABLE_ADDR + i * 4 + 2] = saved_sprite_xy[i * 2 + 1];
    }
}
}

// Uploads the framebuffer, draws it to the window with the selected filter, and (for the Crisp
// filter) snaps the window to an integer scale. Leaves the frame open for overlay drawing.
void present_game_frame(const Color* framebuffer, double frame_time) {
UpdateTexture(screen_texture, framebuffer);
BeginDrawing();
ClearBackground(BLACK);

int sw = GetScreenWidth();
int sh = GetScreenHeight();
float scale = std::min((float)sw / WIDE_W, (float)sh / NATIVE_H);
// Crisp is point-sampled (no blending at all - that's what makes it
// "no filter"), so at a non-integer scale it's forced to make some
// source pixels cover one more/fewer screen pixel than their
// neighbors - not a filter artifact, just what zero interpolation
// means when the math doesn't divide evenly. Snapping to the
// largest integer multiple that still fits gives every native
// pixel a perfectly uniform square block, at the cost of a thin
// letterboxed border instead of an exact fill.
if (g_render_filter == FILTER_CRISP && scale > 1.0f) scale = (float)(int)scale;
float destW = WIDE_W * scale;
float destH = NATIVE_H * scale;
float offsetX = (sw - destW) / 2.0f;
float offsetY = (sh - destH) / 2.0f;

// Crisp already snaps the RENDERED content to an exact integer
// multiple so every pixel is a uniform block - but the window
// itself was left at whatever size it already was, so that
// leftover fractional space still showed up as a black border.
// Snapping the actual OS window to match removes it outright.
// IsWindowResized() fires on every intermediate frame of a live
// drag, not just once at the end - snapping immediately on every
// one of those fought the drag itself, making the window unable
// to grow past its current integer multiple (it kept getting
// floored back mid-drag before reaching the next size up).
// Debouncing: (re)start a short timer on every resize event, and
// only actually snap once it counts down without being reset
// again - i.e. once dragging has actually paused. Also fires
// once right when switching into Crisp, and never while fullscreen
// OR maximized (a fixed computed size makes no sense for either -
// maximize was getting treated as just another resize, so it
// un-maximized the window down to the snapped size, and since a
// resize doesn't reposition the window, it stayed pinned wherever
// maximize had left it instead of re-centering - looked like the
// window "snapped to the corner").
{
    static float resize_settle_timer = -1.0f;
    static int last_render_filter_for_resize = -1;
    bool switched_to_crisp = (g_render_filter == FILTER_CRISP && last_render_filter_for_resize != FILTER_CRISP);
    last_render_filter_for_resize = g_render_filter;
    if (g_render_filter == FILTER_CRISP && !IsWindowFullscreen() && !IsWindowMaximized()) {
        if (IsWindowResized() || switched_to_crisp) resize_settle_timer = 0.25f;
        if (resize_settle_timer > 0.0f) {
            resize_settle_timer -= (float)frame_time;
            if (resize_settle_timer <= 0.0f) {
                int snapW = (int)destW, snapH = (int)destH;
                if (snapW > 0 && snapH > 0 && (snapW != sw || snapH != sh)) SetWindowSize(snapW, snapH);
            }
        }
    } else {
        resize_settle_timer = -1.0f;
    }
}

bool use_crt = g_render_filter == FILTER_CRT;
bool use_sharp = g_render_filter == FILTER_SHARP;
if (use_crt) {
    float output_size[2] = { destW, destH };
    SetShaderValue(crt_shader, crt_output_size_loc, output_size, SHADER_UNIFORM_VEC2);
    BeginShaderMode(crt_shader);
} else if (use_sharp) {
    float source_size[2] = { (float)WIDE_W, (float)NATIVE_H };
    float output_scale[2] = { destW / WIDE_W, destH / NATIVE_H };
    SetShaderValue(sharp_shader, sharp_source_size_loc, source_size, SHADER_UNIFORM_VEC2);
    SetShaderValue(sharp_shader, sharp_output_scale_loc, output_scale, SHADER_UNIFORM_VEC2);
    BeginShaderMode(sharp_shader);
}

DrawTexturePro(screen_texture, Rectangle{0, 0, (float)WIDE_W, (float)NATIVE_H}, Rectangle{offsetX, offsetY, destW, destH}, Vector2{0, 0}, 0.0f, WHITE);

if (use_crt || use_sharp) EndShaderMode();
}
