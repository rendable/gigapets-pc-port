// Sprite extractor: exports sprites visible on screen as PNGs (F7/F8).

#include "common.h"

// Sprite PNG extractor: MAME's generic sprite viewer can't decode this
// driver's tiles (variable bpp/width/height read from each sprite's own
// attr word at runtime, not a static gfx_layout MAME's tool expects) -
// but this port already has a byte-for-byte-correct decode of that same
// variable format (draw_sprites above, ported from spg_renderer_device).
// A single on-screen object (a tree, a character) is usually built from
// several adjacent hardware sprite tiles, not one big tile - exporting
// each tile independently scatters one object across many small files.
// This groups active sprites into clusters by bounding-box adjacency
// (touching or within a few pixels), then composites each cluster onto
// one canvas at the tiles' correct relative offsets, honoring flip -
// i.e. reconstructs the actual on-screen object as a single image.
// Shared by the one-shot F8 export and the continuous F7 capture mode,
// each with their own output folder and dedup set so the two don't
// interfere with each other.
void export_visible_sprite_clusters(const std::string& out_dir, std::set<std::string>& exported_clusters) {
    struct ExtractSprite { uint16_t tile, attr; int x, y; uint32_t w, h; int priority; int slot; };
    std::vector<ExtractSprite> active;
    uint32_t sprite_addr2 = 0x2C00;
    for (int i = 0; i < 256; i++) {
        uint16_t tile = ram[sprite_addr2 + i * 4 + 0];
        if (!tile) continue;
        int16_t raw_x = (int16_t)ram[sprite_addr2 + i * 4 + 1];
        int16_t raw_y = (int16_t)ram[sprite_addr2 + i * 4 + 2];
        uint16_t attr = ram[sprite_addr2 + i * 4 + 3];
        uint32_t tw = 8 << ((attr & 0x0030) >> 4);
        uint32_t th = 8 << ((attr & 0x00c0) >> 6);
        int cx = raw_x, cy = raw_y;
        if (!(video_regs[0x42] & 0x0002)) {
            cx = (320 / 2) + raw_x - (int)tw / 2;
            cy = (256 / 2) - raw_y - (int)th / 2;
        }
        int x = cx & 0x1ff, y = cy & 0x1ff;
        if (x > 400 || y > 300) continue; // drop wrapped/off-canvas junk, not a real on-screen object
        active.push_back({ tile, attr, x, y, tw, th, (attr & 0x3000) >> 12, i });
    }

    // Union-find clustering by bbox proximity (expand each box by 3px
    // before testing overlap, so touching-but-not-overlapping tiles of
    // the same object still merge).
    std::vector<int> parent(active.size());
    for (size_t i = 0; i < active.size(); i++) parent[i] = (int)i;
    std::function<int(int)> find = [&](int a) { while (parent[a] != a) { parent[a] = parent[parent[a]]; a = parent[a]; } return a; };
    const int MARGIN = 3;
    for (size_t i = 0; i < active.size(); i++) {
        for (size_t j = i + 1; j < active.size(); j++) {
            auto& A = active[i]; auto& B = active[j];
            bool overlap = A.x - MARGIN < (int)(B.x + B.w) && B.x - MARGIN < (int)(A.x + A.w)
                        && A.y - MARGIN < (int)(B.y + B.h) && B.y - MARGIN < (int)(A.y + A.h);
            if (overlap) { int ra = find((int)i), rb = find((int)j); if (ra != rb) parent[ra] = rb; }
        }
    }
    std::map<int, std::vector<int>> clusters;
    for (size_t i = 0; i < active.size(); i++) clusters[find((int)i)].push_back((int)i);

    for (auto& [root, members] : clusters) {
        std::sort(members.begin(), members.end(), [&](int a, int b) {
            if (active[a].priority != active[b].priority) return active[a].priority < active[b].priority;
            return a < b;
        });
        int minX = 100000, minY = 100000, maxX = -100000, maxY = -100000;
        for (int idx : members) {
            auto& s = active[idx];
            minX = std::min(minX, s.x); minY = std::min(minY, s.y);
            maxX = std::max(maxX, (int)(s.x + s.w)); maxY = std::max(maxY, (int)(s.y + s.h));
        }
        int cw = maxX - minX, ch = maxY - minY;
        if (cw <= 0 || ch <= 0 || cw > 512 || ch > 512) continue;

        // Player-centered camera (g_cameraScrollX/Y IS the player's own
        // world position) means the player's sprite cluster lands at/near
        // screen center (160,128) every frame, unlike any other object -
        // used to identify and skip it when the toggle is on.
        if (g_extractor_skip_player) {
            int centerX = (minX + maxX) / 2, centerY = (minY + maxY) / 2;
            const int PLAYER_CENTER_TOL = 20;
            if (std::abs(centerX - 160) <= PLAYER_CENTER_TOL && std::abs(centerY - 128) <= PLAYER_CENTER_TOL) continue;
        }

        std::vector<Color> canvas(cw * ch, Color{0, 0, 0, 0});
        for (int idx : members) {
            auto& s = active[idx];
            bool flip_x = (s.attr & 0x0004) != 0, flip_y = (s.attr & 0x0008) != 0;
            uint8_t bpp = s.attr & 0x0003;
            uint32_t nc_bpp = (bpp + 1) << 1;
            uint32_t bits_per_row = nc_bpp * s.w / 16;
            uint32_t words_per_tile = bits_per_row * s.h;
            uint32_t palette_offset = (s.attr & 0x0f00) >> 4;
            palette_offset >>= nc_bpp; palette_offset <<= nc_bpp;
            uint32_t gfx_base = 0x40 * video_regs[0x22];
            for (uint32_t py = 0; py < s.h; py++) {
                uint32_t ty = flip_y ? (s.h - 1 - py) : py;
                uint32_t m = gfx_base + words_per_tile * s.tile + bits_per_row * ty;
                uint32_t bits = 0, nbits = 0;
                for (int32_t px = flip_x ? (int32_t)(s.w - 1) : 0; flip_x ? px >= 0 : px < (int32_t)s.w; flip_x ? px-- : px++) {
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
                    int dx = s.x + px - minX, dy = s.y + (int)py - minY;
                    if (dx < 0 || dx >= cw || dy < 0 || dy >= ch) continue;
                    uint16_t rgb = ram[0x2B00 + palette_offset + color_idx];
                    if (!(rgb & 0x8000)) canvas[dy * cw + dx] = decode_color(rgb);
                }
            }
        }

        // Dedup on the actual composited pixels, not tile/attr/position -
        // some idle animations (e.g. a palette-cycling color effect) keep
        // the same tile/attr/x/y every frame and only change which colors
        // the palette RAM at this offset currently holds, which the old
        // pre-composite key couldn't see at all (captured exactly one
        // frame then silently skipped every later one as a "duplicate").
        // FNV-1a over the canvas bytes catches any visually distinct frame
        // regardless of what changed under the hood to produce it.
        uint64_t hash = 1469598103934665603ULL;
        for (auto& c : canvas) {
            hash = (hash ^ c.r) * 1099511628211ULL;
            hash = (hash ^ c.g) * 1099511628211ULL;
            hash = (hash ^ c.b) * 1099511628211ULL;
            hash = (hash ^ c.a) * 1099511628211ULL;
        }
        char keybuf[48];
        sprintf(keybuf, "%dx%d_%016llX", cw, ch, (unsigned long long)hash);
        if (!exported_clusters.insert(keybuf).second) continue;

        // Nearest-neighbor pixel replication, not smooth scaling - this is
        // flat-color pixel art, so any interpolation would blur the clean
        // edges. Each source pixel becomes an exact scale x scale block, so
        // the upscaled image is still lossless relative to the source data.
        // Adjustable live via the mod menu's "Sprite Export Scale" row.
        int scale = EXPORT_SCALE_OPTIONS[g_export_scale_idx];
        std::vector<Color> upscaled((size_t)cw * scale * ch * scale);
        int scw = cw * scale, sch = ch * scale;
        for (int sy = 0; sy < sch; sy++)
            for (int sx = 0; sx < scw; sx++)
                upscaled[sy * scw + sx] = canvas[(sy / scale) * cw + (sx / scale)];

        // Organize by lowest hardware sprite SLOT in the cluster, not tile
        // ID - a persistent on-screen actor (the minipet, the player) keeps
        // the same slot(s) assigned every frame while it's on screen, even
        // though its tile ID deliberately changes every frame for
        // animation (and an idle bob also shifts its x/y). Grouping by tile
        // ID put every animation frame in its own folder; slot is the
        // stable identity that actually groups them together.
        int anchor_slot = 256;
        for (int idx : members) anchor_slot = std::min(anchor_slot, active[idx].slot);
        std::string obj_dir = out_dir + "/obj_slot" + std::to_string(anchor_slot);
        std::filesystem::create_directories(obj_dir);
        char path[320];
        sprintf(path, "%s/tile0x%04X_%dtiles_%dx%d_f%llu.png", obj_dir.c_str(), active[members[0]].tile,
            (int)members.size(), cw, ch, (unsigned long long)exported_clusters.size());
        Image img{ (void*)upscaled.data(), scw, sch, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
        ExportImage(img, path);
    }
}
