// Mod Menu: row layout, input handling helpers, and row drawing.

#include "common.h"

ModMenuLayout g_mod_menu_layout;
int g_mod_menu_scroll = 0;
bool g_mod_menu_open = false;

bool g_category_expanded[INVENTORY_CATEGORY_COUNT] = {false};

int g_mod_menu_selection = 0;
bool g_controls_expanded = false;
bool g_extractor_expanded = false;
bool g_stats_expanded = false;
bool g_custom_expanded = false;
CustomAddPhase g_custom_add_phase = CUSTOM_ADD_NONE;

char g_custom_add_buffer[24] = {};

int g_custom_add_buffer_len = 0;
uint16_t g_custom_add_pending_addr = 0;
int g_editing_row = -1;

char g_edit_buffer[8] = {};

int g_edit_buffer_len = 0;
int g_open_dropdown_stat = -1; // stat index whose preset dropdown is open, -1 = none

Rectangle g_dropdown_anchor = {};

int g_dropdown_gp_index = 0; // gamepad-highlighted option row inside the open dropdown
int g_awaiting_keybind_for = -1; // GameButton index waiting on the next keypress to rebind, -1 = none

// Mod menu layout: computed once per frame (from input handling, before
// rendering) into g_mod_menu_layout so both mouse hit-testing and drawing
// use identical row geometry. Two panels: the main list (Filter/Movement
// Speed/No-Clip/stats/inventory categories) and a docked Favorites panel
// showing just the pinned stats/items, so favorited cheats stay reachable
// without hunting through the full list. The main list can be far taller
// than fits on screen once inventory categories are expanded (up to 268
// items), so it's scrolled: logical_order holds every currently-visible
// logical row (respecting collapse state) in display order, and main_rows
// holds only the slice of that currently scrolled into view.
 // index into logical_order of the first visible row

// Both computed once (name/value tables are static, not every frame) and
// shared between layout and drawing - label_extra_w widens the gap before
// the chevrons for long labels (e.g. "King's Magnificent Statue"),
// value_extra_w widens the gap AFTER the value text for long values (e.g.
// minipet species names, "Zapped In"/"Zapped Out") so neither overlaps the
// chevrons. Both start at -1 (not yet computed).
int s_label_extra_w = -1;
int s_value_extra_w = -1;

bool row_is_category(int row_index) { return row_index >= ROW_INVENTORY_CAT_START && row_index < ROW_INVENTORY_ITEM_START; }

// Bounded at ROW_CUSTOM_CAT now that the Custom category's rows sit right
// after Inventory in the chain - this used to have no upper bound (safe
// back when Inventory was the last thing in the chain), which would have
// silently misclassified every Custom row as an inventory item once
// something was appended after it.
bool row_is_inv_item(int row_index) { return row_index >= ROW_INVENTORY_ITEM_START && row_index < ROW_CUSTOM_CAT; }

bool row_is_custom(int row_index) { return row_index >= ROW_CUSTOM_START && row_index < ROW_CUSTOM_START + g_custom_mod_count; }

// Any collapsible section header (toggled by Enter/click same as a value
// row's chevron, rather than opening an edit prompt).
bool row_is_expand_header(int row_index) {
    return row_is_category(row_index) || row_index == ROW_CONTROLS_CAT
        || row_index == ROW_EXTRACTOR_CAT || row_index == ROW_STATS_CAT || row_index == ROW_CUSTOM_CAT;
}

// Upper bound must be ROW_MOD_MENU_KEY specifically (== ROW_CONTROLS_START +
// GAME_BUTTON_COUNT, i.e. one past the last real keybind row), NOT
// ROW_FILTER - ROW_MOD_MENU_KEY sits between them, and using ROW_FILTER here
// used to work only because it happened to equal that boundary before
// ROW_MOD_MENU_KEY existed. Using it now would misclassify ROW_MOD_MENU_KEY
// itself as a real keybind row, indexing GAME_BUTTONS[GAME_BUTTON_COUNT] out
// of bounds and crashing on open.
bool row_is_keybind(int row_index) { return row_index >= ROW_CONTROLS_START && row_index < ROW_MOD_MENU_KEY; }

bool row_has_value(int row_index) { return (row_index >= ROW_STATS_START && row_index < ROW_INVENTORY_CAT_START) || row_is_inv_item(row_index) || row_is_custom(row_index); }

void compute_mod_menu_layout(int screen_w, int screen_h) {
    int row_h = 22;

    memset(g_mod_menu_layout.separator_after_row, 0, sizeof(g_mod_menu_layout.separator_after_row));

    // Pass 1: full logical row order, respecting current expand state.
    int lidx = 0;
    g_mod_menu_layout.logical_order[lidx++] = ROW_CONTROLS_CAT;
    if (g_controls_expanded && GAME_BUTTON_COUNT > 0) {
        for (int b = 0; b < GAME_BUTTON_COUNT; b++) g_mod_menu_layout.logical_order[lidx++] = ROW_CONTROLS_START + b;
        g_mod_menu_layout.logical_order[lidx++] = ROW_MOD_MENU_KEY;
        g_mod_menu_layout.separator_after_row[ROW_MOD_MENU_KEY] = true;
    }
    g_mod_menu_layout.logical_order[lidx++] = ROW_FILTER;
    g_mod_menu_layout.logical_order[lidx++] = ROW_MOVE_SPEED;
    g_mod_menu_layout.logical_order[lidx++] = ROW_NOCLIP;
    g_mod_menu_layout.logical_order[lidx++] = ROW_MINIPET;
    g_mod_menu_layout.logical_order[lidx++] = ROW_MINIPET_ACTION;
    g_mod_menu_layout.logical_order[lidx++] = ROW_FPS_LIMIT;
    g_mod_menu_layout.logical_order[lidx++] = ROW_INTERPOLATION;
    g_mod_menu_layout.logical_order[lidx++] = ROW_FPS_COUNTER;
    g_mod_menu_layout.logical_order[lidx++] = ROW_STAT_HUD;
    g_mod_menu_layout.logical_order[lidx++] = ROW_QUEST_ARROW;
    g_mod_menu_layout.logical_order[lidx++] = ROW_EXTRACTOR_CAT;
    if (g_extractor_expanded) {
        g_mod_menu_layout.logical_order[lidx++] = ROW_EXPORT_SCALE;
        g_mod_menu_layout.logical_order[lidx++] = ROW_EXTRACTOR_SKIP_PLAYER;
        g_mod_menu_layout.logical_order[lidx++] = ROW_DISABLE_SHADOWS;
        g_mod_menu_layout.separator_after_row[ROW_DISABLE_SHADOWS] = true;
    }
    // The debug/utility values (12+: Location ID, Player/Camera X/Y, Quests
    // Done) always visible; the actual pet care stats (1-11: Halos through
    // Tricks) collapsible under their own header, matching the split the
    // user asked for. Money moves down next to the rest of Inventory below,
    // not grouped with these.
    g_mod_menu_layout.logical_order[lidx++] = ROW_STATS_CAT;
    if (g_stats_expanded) {
        for (int i = 1; i <= 11; i++) g_mod_menu_layout.logical_order[lidx++] = ROW_STATS_START + i;
        g_mod_menu_layout.separator_after_row[ROW_STATS_START + 11] = true;
    }
    for (int i = 12; i < CHEAT_STAT_COUNT; i++) g_mod_menu_layout.logical_order[lidx++] = ROW_STATS_START + i;
    g_mod_menu_layout.logical_order[lidx++] = ROW_STATS_START + 0; // Money, next to Inventory
    for (int c = 0; c < INVENTORY_CATEGORY_COUNT; c++) {
        g_mod_menu_layout.logical_order[lidx++] = ROW_INVENTORY_CAT_START + c;
        if (g_category_expanded[c]) {
            const InventoryCategory& cat = INVENTORY_CATEGORIES[c];
            for (int j = 0; j < cat.count; j++) {
                g_mod_menu_layout.logical_order[lidx++] = ROW_INVENTORY_ITEM_START + cat.start + j;
            }
            if (cat.count > 0) {
                g_mod_menu_layout.separator_after_row[ROW_INVENTORY_ITEM_START + cat.start + cat.count - 1] = true;
            }
        }
    }
    g_mod_menu_layout.logical_order[lidx++] = ROW_CUSTOM_CAT;
    if (g_custom_expanded) {
        g_mod_menu_layout.logical_order[lidx++] = ROW_CUSTOM_ADD;
        for (int i = 0; i < g_custom_mod_count; i++) g_mod_menu_layout.logical_order[lidx++] = ROW_CUSTOM_START + i;
        g_mod_menu_layout.separator_after_row[g_custom_mod_count > 0 ? (ROW_CUSTOM_START + g_custom_mod_count - 1) : ROW_CUSTOM_ADD] = true;
    }
    g_mod_menu_layout.logical_count = lidx;

    int visible_rows = (screen_h - 90) / row_h;
    if (visible_rows < 8) visible_rows = 8;
    if (visible_rows > MOD_MENU_MAX_VISIBLE_ROWS) visible_rows = MOD_MENU_MAX_VISIBLE_ROWS;
    g_mod_menu_layout.has_scroll = g_mod_menu_layout.logical_count > visible_rows;

    int max_scroll = g_mod_menu_layout.logical_count - visible_rows;
    if (max_scroll < 0) max_scroll = 0;
    if (g_mod_menu_scroll > max_scroll) g_mod_menu_scroll = max_scroll;
    if (g_mod_menu_scroll < 0) g_mod_menu_scroll = 0;

    int shown = g_mod_menu_layout.logical_count - g_mod_menu_scroll;
    if (shown > visible_rows) shown = visible_rows;
    if (shown < 0) shown = 0;

    // Longest item/stat name (e.g. "King's Magnificent Statue") determines
    // how far the chevron/value columns need to shift right so nothing gets
    // truncated - computed once since the name tables are static, not every
    // frame.
    if (s_label_extra_w < 0) {
        int widest = 0;
        for (int i = 0; i < CHEAT_STAT_COUNT; i++) widest = std::max(widest, ui_measure_text(CHEAT_STATS[i].name, 15));
        for (int i = 0; i < INVENTORY_ITEM_COUNT; i++) widest = std::max(widest, ui_measure_text(INVENTORY_ITEMS[i].name, 15));
        const int available = 148 - 22; // label start (px+22) to arrow_ll (px+148) below
        s_label_extra_w = widest > available ? (widest - available + 4) : 0;
    }

    // Same idea for the VALUE column - unlike the label, value_str was never
    // clipped or measured against its 42px column at all, so anything wider
    // (minipet species names, "Zapped In"/"Zapped Out") just overflowed
    // straight into the ">"/">>" chevrons. Covers every value string that
    // can appear, not just the has_value (stat/item) rows - MINIPET_NAMES
    // and the action-toggle text are drawn via the same r.value/arrow_r
    // positions despite not being "stat" rows.
    if (s_value_extra_w < 0) {
        int widest = 0;
        for (int i = 0; i < 8; i++) widest = std::max(widest, ui_measure_text(MINIPET_NAMES[i], 15));
        widest = std::max(widest, ui_measure_text("Zapped In", 15));
        widest = std::max(widest, ui_measure_text("Zapped Out", 15));
        widest = std::max(widest, ui_measure_text("Unlimited", 15));
        widest = std::max(widest, ui_measure_text("65535", 15));
        for (int i = 0; i < (int)(sizeof(SICKNESS_PRESETS) / sizeof(SICKNESS_PRESETS[0])); i++)
            widest = std::max(widest, ui_measure_text(SICKNESS_PRESETS[i].label, 15));
        const int available = 224 - 180; // value start (px+180) to arrow_r (px+224) below
        s_value_extra_w = widest > available ? (widest - available + 4) : 0;
    }

    int mw = 300 + s_label_extra_w + s_value_extra_w, fmw = 290 + s_value_extra_w, gap = 10;
    // +14 over the base 44 reserves room for the Fav/Freeze checkbox legend
    // drawn once under the title (see the main-panel draw call) - the
    // fav_panel keeps the plain 44, it doesn't draw that legend itself.
    int mh = 58 + shown * row_h + (g_mod_menu_layout.has_scroll ? 14 : 0);

    int fav_count = 0;
    for (int i = 0; i < CHEAT_STAT_COUNT; i++) if (g_stat_favorite[i]) fav_count++;
    for (int i = 0; i < INVENTORY_ITEM_COUNT; i++) if (g_inv_favorite[i]) fav_count++;
    int fmh = 44 + (fav_count > 0 ? fav_count : 1) * row_h;

    int fzmh = 44 + row_h; // fixed size - explanatory only, never lists rows

    int combined_w = mw + gap + fmw;
    int mx = (screen_w - combined_w) / 2;
    if (mx < 10) mx = 10; // combined_w can exceed screen_w (long item names + a narrow window) - never let the main panel start off-screen
    int my = (screen_h - mh) / 2;
    if (my < 10) my = 10;
    int fmx = mx + mw + gap;
    int fmy = my;
    int fzmx = fmx;
    int fzmy = fmy + fmh + gap; // stacked below the Favorites panel, same column

    g_mod_menu_layout.main_panel = Rectangle{ (float)mx, (float)my, (float)mw, (float)mh };
    g_mod_menu_layout.fav_panel = Rectangle{ (float)fmx, (float)fmy, (float)fmw, (float)fmh };
    g_mod_menu_layout.frozen_panel = Rectangle{ (float)fzmx, (float)fzmy, (float)fmw, (float)fzmh };

    auto fill_row = [&](int px, int y, int row_index) {
        ModMenuRowRect r;
        r.full = Rectangle{ (float)(px + 6), (float)y, (float)(mw - 12), (float)row_h };
        r.fav = Rectangle{ (float)(px + 10), (float)(y + 3), 14, 14 };
        r.arrow_ll = Rectangle{ (float)(px + 148 + s_label_extra_w), (float)y, 14, (float)row_h };
        r.arrow_l = Rectangle{ (float)(px + 164 + s_label_extra_w), (float)y, 14, (float)row_h };
        r.value = Rectangle{ (float)(px + 180 + s_label_extra_w), (float)y, (float)(42 + s_value_extra_w), (float)row_h };
        r.arrow_r = Rectangle{ (float)(px + 224 + s_label_extra_w + s_value_extra_w), (float)y, 14, (float)row_h };
        r.arrow_rr = Rectangle{ (float)(px + 240 + s_label_extra_w + s_value_extra_w), (float)y, 14, (float)row_h };
        r.freeze = Rectangle{ (float)(px + 258 + s_label_extra_w + s_value_extra_w), (float)(y + 3), 14, 14 };
        r.preset_btn = Rectangle{ (float)(px + 276 + s_label_extra_w + s_value_extra_w), (float)(y + 3), 14, 14 };
        r.row_index = row_index;
        return r;
    };

    int y = my + 46, out_idx = 0;
    for (int i = 0; i < shown; i++) {
        g_mod_menu_layout.main_rows[out_idx++] = fill_row(mx, y, g_mod_menu_layout.logical_order[g_mod_menu_scroll + i]);
        y += row_h;
    }
    g_mod_menu_layout.main_row_count = out_idx;

    int fy = fmy + 32, fidx = 0;
    for (int i = 0; i < CHEAT_STAT_COUNT; i++) {
        if (!g_stat_favorite[i]) continue;
        g_mod_menu_layout.fav_rows[fidx++] = fill_row(fmx, fy, ROW_STATS_START + i);
        fy += row_h;
    }
    for (int i = 0; i < INVENTORY_ITEM_COUNT; i++) {
        if (!g_inv_favorite[i]) continue;
        g_mod_menu_layout.fav_rows[fidx++] = fill_row(fmx, fy, ROW_INVENTORY_ITEM_START + i);
        fy += row_h;
    }
    for (int i = 0; i < g_custom_mod_count; i++) {
        if (!g_custom_mods[i].favorite) continue;
        g_mod_menu_layout.fav_rows[fidx++] = fill_row(fmx, fy, ROW_CUSTOM_START + i);
        fy += row_h;
    }
    g_mod_menu_layout.fav_row_count = fidx;
}

// delta is the actual amount to add for value-bearing rows (+-1 from a
// single chevron, +-10 from a double chevron); for cycling rows (Filter/
// Move Speed/No-Clip/category expand) only its sign matters.
void adjust_row(int row_index, int delta) {
    int dir = (delta > 0) - (delta < 0);
    if (row_index == ROW_CONTROLS_CAT) {
        if (dir != 0) g_controls_expanded = !g_controls_expanded;
    } else if (row_index == ROW_EXTRACTOR_CAT) {
        if (dir != 0) g_extractor_expanded = !g_extractor_expanded;
    } else if (row_index == ROW_STATS_CAT) {
        if (dir != 0) g_stats_expanded = !g_stats_expanded;
    } else if (row_index == ROW_CUSTOM_CAT) {
        if (dir != 0) g_custom_expanded = !g_custom_expanded;
    } else if (row_is_custom(row_index)) {
        int i = row_index - ROW_CUSTOM_START;
        int32_t v = (int32_t)ram[g_custom_mods[i].addr] + delta;
        if (v < 0) v = 0;
        if (v > 65535) v = 65535;
        ram[g_custom_mods[i].addr] = (uint16_t)v;
        if (g_custom_mods[i].frozen) { g_custom_mods[i].frozen_value = (uint16_t)v; save_custom_mods(); }
    } else if (row_index == ROW_FILTER) {
        g_render_filter = (g_render_filter + dir + FILTER_COUNT) % FILTER_COUNT;
        // Sharp-bilinear's shader relies on hardware bilinear sampling too -
        // it computes a coordinate landing between texel centers and lets
        // the GPU do the actual blend, same as plain Smooth.
        bool wants_bilinear = g_render_filter == FILTER_SMOOTH || g_render_filter == FILTER_SHARP;
        SetTextureFilter(screen_texture, wants_bilinear ? TEXTURE_FILTER_BILINEAR : TEXTURE_FILTER_POINT);
    } else if (row_index == ROW_MOVE_SPEED) {
        g_movement_speed_idx = (g_movement_speed_idx + dir + MOVEMENT_SPEED_LEVEL_COUNT) % MOVEMENT_SPEED_LEVEL_COUNT;
    } else if (row_index == ROW_NOCLIP) {
        if (dir != 0) { g_cheat_noclip = !g_cheat_noclip; set_noclip_enabled(g_cheat_noclip); }
    } else if (row_index == ROW_DISABLE_SHADOWS) {
        if (dir != 0) { g_shadows_disabled = !g_shadows_disabled; set_shadows_disabled(g_shadows_disabled); }
    } else if (row_index == ROW_MINIPET) {
        // Browsing only - no side effect. Changing species while one is
        // already spawned does NOT re-spawn it; use the Action row for that.
        g_minipet_picker_idx = (g_minipet_picker_idx + dir + 8) % 8;
    } else if (row_index == ROW_MINIPET_ACTION) {
        if (dir != 0) {
            g_minipet_spawned = !g_minipet_spawned;
            if (g_minipet_spawned) spawn_minipet(g_minipet_picker_idx);
            else despawn_minipet();
        }
    } else if (row_index == ROW_FPS_LIMIT) {
        if (dir != 0) {
            g_fps_limit_idx = (g_fps_limit_idx + dir + FPS_LIMIT_OPTION_COUNT) % FPS_LIMIT_OPTION_COUNT;
            SetTargetFPS(FPS_LIMIT_OPTIONS[g_fps_limit_idx]);
            save_fps_limit();
        }
    } else if (row_index == ROW_INTERPOLATION) {
        if (dir != 0) g_interp_enabled = !g_interp_enabled;
    } else if (row_index == ROW_FPS_COUNTER) {
        if (dir != 0) g_show_fps_hud = !g_show_fps_hud;
    } else if (row_index == ROW_STAT_HUD) {
        if (dir != 0) g_show_stat_hud = !g_show_stat_hud;
    } else if (row_index == ROW_QUEST_ARROW) {
        if (dir != 0) g_quest_arrow_enabled = !g_quest_arrow_enabled;
    } else if (row_index == ROW_EXPORT_SCALE) {
        g_export_scale_idx = (g_export_scale_idx + dir + EXPORT_SCALE_OPTION_COUNT) % EXPORT_SCALE_OPTION_COUNT;
    } else if (row_index == ROW_EXTRACTOR_SKIP_PLAYER) {
        if (dir != 0) g_extractor_skip_player = !g_extractor_skip_player;
    } else if (row_is_inv_item(row_index)) {
        int i = row_index - ROW_INVENTORY_ITEM_START;
        set_inv_value(i, (int32_t)ram[INVENTORY_ITEMS[i].addr] + delta);
    } else if (row_is_category(row_index)) {
        if (dir != 0) {
            int c = row_index - ROW_INVENTORY_CAT_START;
            g_category_expanded[c] = !g_category_expanded[c];
        }
    } else if (row_is_keybind(row_index)) {
        // no-op: rebinding goes through the awaiting-capture flow below,
        // not +/- delta adjustment.
    } else if (row_index >= ROW_STATS_START) {
        int i = row_index - ROW_STATS_START;
        set_stat_value(i, (int32_t)ram[CHEAT_STATS[i].addr] + delta);
    }
}

// Favorite/freeze/edit operate on a row_index directly (dispatching to
// either CHEAT_STATS or INVENTORY_ITEMS based on which range it falls in),
// so the same mouse/keyboard handling code works for both without the
// caller needing to know which kind of row it clicked.
void toggle_favorite_row(int row_index) {
    if (row_is_custom(row_index)) {
        g_custom_mods[row_index - ROW_CUSTOM_START].favorite = !g_custom_mods[row_index - ROW_CUSTOM_START].favorite;
        save_custom_mods();
        return;
    }
    if (row_is_inv_item(row_index)) {
        int i = row_index - ROW_INVENTORY_ITEM_START;
        g_inv_favorite[i] = !g_inv_favorite[i];
    } else {
        int i = row_index - ROW_STATS_START;
        g_stat_favorite[i] = !g_stat_favorite[i];
    }
    save_cheat_state();
}

void toggle_freeze_row(int row_index) {
    if (row_is_custom(row_index)) {
        int i = row_index - ROW_CUSTOM_START;
        g_custom_mods[i].frozen = !g_custom_mods[i].frozen;
        if (g_custom_mods[i].frozen) g_custom_mods[i].frozen_value = ram[g_custom_mods[i].addr];
        save_custom_mods();
        return;
    }
    if (row_is_inv_item(row_index)) {
        int i = row_index - ROW_INVENTORY_ITEM_START;
        g_inv_frozen[i] = !g_inv_frozen[i];
        if (g_inv_frozen[i]) g_inv_frozen_value[i] = ram[INVENTORY_ITEMS[i].addr];
    } else {
        int i = row_index - ROW_STATS_START;
        g_stat_frozen[i] = !g_stat_frozen[i];
        if (g_stat_frozen[i]) g_stat_frozen_value[i] = ram[CHEAT_STATS[i].addr];
    }
    save_cheat_state();
}

void begin_edit_row(int row_index) {
    g_editing_row = row_index;
    uint16_t addr = row_is_custom(row_index) ? g_custom_mods[row_index - ROW_CUSTOM_START].addr
        : row_is_inv_item(row_index)
        ? INVENTORY_ITEMS[row_index - ROW_INVENTORY_ITEM_START].addr
        : CHEAT_STATS[row_index - ROW_STATS_START].addr;
    sprintf(g_edit_buffer, "%u", ram[addr]);
    g_edit_buffer_len = (int)strlen(g_edit_buffer);
}

void commit_edit_row() {
    if (g_editing_row < 0) return;
    int32_t val = g_edit_buffer_len > 0 ? atoi(g_edit_buffer) : 0;
    if (val < 0) val = 0;
    if (val > 65535) val = 65535;
    if (row_is_custom(g_editing_row)) {
        int i = g_editing_row - ROW_CUSTOM_START;
        ram[g_custom_mods[i].addr] = (uint16_t)val;
        if (g_custom_mods[i].frozen) { g_custom_mods[i].frozen_value = (uint16_t)val; save_custom_mods(); }
    }
    else if (row_is_inv_item(g_editing_row)) set_inv_value(g_editing_row - ROW_INVENTORY_ITEM_START, val);
    else set_stat_value(g_editing_row - ROW_STATS_START, val);
    g_editing_row = -1;
}

void cancel_edit_row() { g_editing_row = -1; }

// Truncates with an ellipsis so long labels (e.g. "King's Magnificent
// Statue") can't overlap the chevrons/value column to their right - row
// width is fixed but item/stat name length isn't.
void draw_text_clipped(const char* text, int x, int y, int font_size, Color color, int max_w) {
    if (ui_measure_text(text, font_size) <= max_w) {
        ui_draw_text(text, x, y, font_size, color);
        return;
    }
    char buf[64];
    int len = (int)strlen(text);
    if (len > 63) len = 63;
    strncpy(buf, text, len); buf[len] = 0;
    while (len > 0 && ui_measure_text(TextFormat("%s...", buf), font_size) > max_w) {
        buf[--len] = 0;
    }
    ui_draw_text(TextFormat("%s...", buf), x, y, font_size, color);
}

void draw_mod_menu_row(const ModMenuRowRect& r) {
    bool selected = r.row_index == g_mod_menu_selection;
    Color text_color = selected ? YELLOW : WHITE;

    if (r.row_index == ROW_CONTROLS_CAT) {
        char label[32];
        sprintf(label, "%s Controls (%d)", g_controls_expanded ? "-" : "+", GAME_BUTTON_COUNT);
        ui_draw_text(label, (int)r.full.x + 4, (int)r.full.y, 15, selected ? YELLOW : SKYBLUE);
        // Column header for the per-row keyboard/gamepad values below,
        // right-aligned over the same x-offsets they're drawn at - without
        // this, "Pad"/"A"/"B" on their own don't read as gamepad-specific.
        if (g_controls_expanded) {
            ui_draw_text("Keyboard", (int)r.full.x + 155, (int)r.full.y, 12, GRAY);
            ui_draw_text("Gamepad", (int)r.full.x + 215, (int)r.full.y, 12, GRAY);
        }
        return;
    }

    if (row_is_category(r.row_index)) {
        int c = r.row_index - ROW_INVENTORY_CAT_START;
        const InventoryCategory& cat = INVENTORY_CATEGORIES[c];
        char label[64];
        sprintf(label, "%s %s (%d)", g_category_expanded[c] ? "-" : "+", cat.name, cat.count);
        draw_text_clipped(label, (int)r.full.x + 4, (int)r.full.y, 15, selected ? YELLOW : SKYBLUE, (int)r.full.width - 8);
        return;
    }

    if (r.row_index == ROW_EXTRACTOR_CAT) {
        char label[32];
        sprintf(label, "%s Sprite Extractor (3)", g_extractor_expanded ? "-" : "+");
        ui_draw_text(label, (int)r.full.x + 4, (int)r.full.y, 15, selected ? YELLOW : SKYBLUE);
        return;
    }

    if (r.row_index == ROW_STATS_CAT) {
        char label[32];
        sprintf(label, "%s Stats (11)", g_stats_expanded ? "-" : "+");
        ui_draw_text(label, (int)r.full.x + 4, (int)r.full.y, 15, selected ? YELLOW : SKYBLUE);
        return;
    }

    if (r.row_index == ROW_CUSTOM_CAT) {
        char label[40];
        sprintf(label, "%s Custom (%d)", g_custom_expanded ? "-" : "+", g_custom_mod_count);
        ui_draw_text(label, (int)r.full.x + 4, (int)r.full.y, 15, selected ? YELLOW : SKYBLUE);
        return;
    }

    if (r.row_index == ROW_CUSTOM_ADD) {
        ui_draw_text("+ Add Custom Address...", (int)r.full.x + 4, (int)r.full.y, 15, selected ? YELLOW : SKYBLUE);
        if (g_custom_add_phase != CUSTOM_ADD_NONE) {
            const char* prompt = g_custom_add_phase == CUSTOM_ADD_ADDRESS ? "Addr (hex):" : "Name:";
            char display[64];
            sprintf(display, "%s %s_", prompt, g_custom_add_buffer);
            DrawRectangle((int)r.full.x, (int)r.full.y + 20, (int)r.full.width, 20, Color{40, 40, 40, 255});
            DrawRectangleLines((int)r.full.x, (int)r.full.y + 20, (int)r.full.width, 20, YELLOW);
            ui_draw_text(display, (int)r.full.x + 4, (int)r.full.y + 22, 14, YELLOW);
        }
        return;
    }

    if (row_is_keybind(r.row_index)) {
        int b = r.row_index - ROW_CONTROLS_START;
        bool awaiting_kb = g_awaiting_keybind_for == b && !g_awaiting_gamepad_rebind;
        bool awaiting_gp = g_awaiting_keybind_for == b && g_awaiting_gamepad_rebind;
        ui_draw_text(GAME_BUTTONS[b].name, (int)r.full.x + 4, (int)r.full.y, 15, text_color);
        // Two columns aligned under the "Keyboard"/"Gamepad" header drawn on
        // the Controls category row above - each independently rebindable
        // (click either column, or select the row and press Enter for
        // keyboard / a gamepad button for gamepad) without touching the
        // other side.
        if (awaiting_kb) {
            ui_draw_text("Press a key...", (int)r.full.x + 155, (int)r.full.y, 13, ORANGE);
        } else {
            ui_draw_text(get_key_display_name(g_key_binding[b]), (int)r.full.x + 155, (int)r.full.y, 13, SKYBLUE);
        }
        if (awaiting_gp) {
            ui_draw_text("Press...", (int)r.full.x + 215, (int)r.full.y, 13, ORANGE);
        } else {
            ui_draw_text(get_gamepad_button_name(g_gamepad_binding[b]), (int)r.full.x + 215, (int)r.full.y, 13, SKYBLUE);
        }
        return;
    }

    if (r.row_index == ROW_MOD_MENU_KEY) {
        // Host-side UI toggle, not a real GigaPets button - same two-column
        // rebind display as the Controls rows above, just standalone (see
        // g_mod_menu_key's comment for why it isn't part of that array).
        bool awaiting_kb = g_awaiting_keybind_for == GAME_BUTTON_COUNT && !g_awaiting_gamepad_rebind;
        bool awaiting_gp = g_awaiting_keybind_for == GAME_BUTTON_COUNT && g_awaiting_gamepad_rebind;
        ui_draw_text("Mod Menu Key", (int)r.full.x + 4, (int)r.full.y, 15, text_color);
        if (awaiting_kb) {
            ui_draw_text("Press a key...", (int)r.full.x + 155, (int)r.full.y, 13, ORANGE);
        } else {
            ui_draw_text(get_key_display_name(g_mod_menu_key), (int)r.full.x + 155, (int)r.full.y, 13, SKYBLUE);
        }
        if (awaiting_gp) {
            ui_draw_text("Press...", (int)r.full.x + 215, (int)r.full.y, 13, ORANGE);
        } else {
            ui_draw_text(get_gamepad_button_name(g_mod_menu_gamepad), (int)r.full.x + 215, (int)r.full.y, 13, SKYBLUE);
        }
        return;
    }

    bool has_value = row_has_value(r.row_index);
    bool is_item = row_is_inv_item(r.row_index);
    char label[48] = {0};
    char value_str[16] = {0};
    bool fav = false, frozen = false;
    int preset_count = 0;
    const CheatPreset* presets = nullptr;

    if (r.row_index == ROW_FILTER) {
        const char* names[FILTER_COUNT] = { "Crisp", "Smooth", "Sharp", "CRT" };
        strcpy(label, "Filter");
        strcpy(value_str, names[g_render_filter]);
    } else if (r.row_index == ROW_MOVE_SPEED) {
        strcpy(label, "Move Speed");
        sprintf(value_str, "%dx", MOVEMENT_SPEED_LEVELS[g_movement_speed_idx]);
    } else if (r.row_index == ROW_NOCLIP) {
        strcpy(label, "No-Clip");
        strcpy(value_str, g_cheat_noclip ? "On" : "Off");
    } else if (r.row_index == ROW_DISABLE_SHADOWS) {
        strcpy(label, "Disable Shadows");
        strcpy(value_str, g_shadows_disabled ? "On" : "Off");
    } else if (r.row_index == ROW_MINIPET) {
        strcpy(label, "Minipet");
        strcpy(value_str, MINIPET_NAMES[g_minipet_picker_idx]);
    } else if (r.row_index == ROW_MINIPET_ACTION) {
        strcpy(label, "Minipet Action");
        strcpy(value_str, g_minipet_spawned ? "Zapped In" : "Zapped Out");
    } else if (r.row_index == ROW_FPS_LIMIT) {
        strcpy(label, "FPS Limit");
        int fps = FPS_LIMIT_OPTIONS[g_fps_limit_idx];
        if (fps == 0) strcpy(value_str, "Unlimited");
        else sprintf(value_str, "%d", fps);
    } else if (r.row_index == ROW_INTERPOLATION) {
        strcpy(label, "Interpolation");
        strcpy(value_str, g_interp_enabled ? "On" : "Off");
    } else if (r.row_index == ROW_FPS_COUNTER) {
        strcpy(label, "FPS Counter");
        strcpy(value_str, g_show_fps_hud ? "On" : "Off");
    } else if (r.row_index == ROW_STAT_HUD) {
        strcpy(label, "Stat HUD");
        strcpy(value_str, g_show_stat_hud ? "On" : "Off");
    } else if (r.row_index == ROW_QUEST_ARROW) {
        strcpy(label, "Quest Arrow");
        strcpy(value_str, g_quest_arrow_enabled ? "On" : "Off");
    } else if (r.row_index == ROW_EXPORT_SCALE) {
        strcpy(label, "Sprite Export Scale");
        sprintf(value_str, "%dx", EXPORT_SCALE_OPTIONS[g_export_scale_idx]);
    } else if (r.row_index == ROW_EXTRACTOR_SKIP_PLAYER) {
        strcpy(label, "Extractor Skip Player");
        strcpy(value_str, g_extractor_skip_player ? "On" : "Off");
    } else if (row_is_custom(r.row_index)) {
        int i = r.row_index - ROW_CUSTOM_START;
        strncpy(label, g_custom_mods[i].name, 47); label[47] = 0;
        sprintf(value_str, "%u", ram[g_custom_mods[i].addr]);
        fav = g_custom_mods[i].favorite; frozen = g_custom_mods[i].frozen;
    } else if (is_item) {
        int i = r.row_index - ROW_INVENTORY_ITEM_START;
        strncpy(label, INVENTORY_ITEMS[i].name, 47); label[47] = 0;
        sprintf(value_str, "%u", ram[INVENTORY_ITEMS[i].addr]);
        fav = g_inv_favorite[i]; frozen = g_inv_frozen[i];
    } else {
        int i = r.row_index - ROW_STATS_START;
        strcpy(label, CHEAT_STATS[i].name);
        uint16_t stat_val = ram[CHEAT_STATS[i].addr];
        if (CHEAT_STATS[i].addr == 0x218D && stat_val < sizeof(SICKNESS_PRESETS) / sizeof(SICKNESS_PRESETS[0])) {
            strcpy(value_str, SICKNESS_PRESETS[stat_val].label);
        } else {
            sprintf(value_str, "%u", stat_val);
        }
        fav = g_stat_favorite[i]; frozen = g_stat_frozen[i];
        preset_count = CHEAT_STATS[i].preset_count;
        presets = CHEAT_STATS[i].presets;
    }

    int label_x = (int)r.full.x + (has_value ? 22 : 4);
    int label_max_w = has_value ? (int)(r.arrow_ll.x - label_x - 4) : (int)(r.full.width - (label_x - r.full.x));
    draw_text_clipped(label, label_x, (int)r.full.y, 15, text_color, label_max_w);
    ui_draw_text("<<", (int)r.arrow_ll.x, (int)r.arrow_ll.y, 14, text_color);
    ui_draw_text("<", (int)r.arrow_l.x, (int)r.arrow_l.y, 16, text_color);
    ui_draw_text(">", (int)r.arrow_r.x, (int)r.arrow_r.y, 16, text_color);
    ui_draw_text(">>", (int)r.arrow_rr.x, (int)r.arrow_rr.y, 14, text_color);

    if (has_value) {
        DrawRectangleLines((int)r.fav.x, (int)r.fav.y, (int)r.fav.width, (int)r.fav.height, YELLOW);
        if (fav) DrawRectangle((int)r.fav.x + 3, (int)r.fav.y + 3, (int)r.fav.width - 6, (int)r.fav.height - 6, YELLOW);
        DrawRectangleLines((int)r.freeze.x, (int)r.freeze.y, (int)r.freeze.width, (int)r.freeze.height, SKYBLUE);
        if (frozen) DrawRectangle((int)r.freeze.x + 3, (int)r.freeze.y + 3, (int)r.freeze.width - 6, (int)r.freeze.height - 6, SKYBLUE);
        (void)presets;
        if (preset_count > 0) {
            Color c = (g_open_dropdown_stat == (r.row_index - ROW_STATS_START)) ? YELLOW : GRAY;
            DrawRectangleLines((int)r.preset_btn.x, (int)r.preset_btn.y, (int)r.preset_btn.width, (int)r.preset_btn.height, c);
            ui_draw_text("v", (int)r.preset_btn.x + 4, (int)r.preset_btn.y + 1, 12, c);
        }

        if (r.row_index == g_editing_row) {
            DrawRectangle((int)r.value.x, (int)r.value.y, (int)r.value.width, (int)r.value.height, Color{40, 40, 40, 255});
            DrawRectangleLines((int)r.value.x, (int)r.value.y, (int)r.value.width, (int)r.value.height, YELLOW);
            char edit_display[10];
            sprintf(edit_display, "%s_", g_edit_buffer);
            ui_draw_text(edit_display, (int)r.value.x + 2, (int)r.value.y, 15, YELLOW);
            return;
        }
    }
    ui_draw_text(value_str, (int)r.value.x, (int)r.value.y, 15, text_color);
}

// Handles all Mod Menu input for this frame: open/close, text entry, dropdowns, key rebinding,
// keyboard/gamepad navigation, and mouse clicks.
void mod_menu_update() {
    // Tab always works as a fallback even if the user rebinds the
    // primary key/gamepad button to something else, so a bad rebind
    // can never lock them out of the menu that would let them fix it.
    bool mod_menu_toggle_kb = IsKeyPressed(KEY_TAB) || (g_mod_menu_key != KEY_TAB && IsKeyPressed(g_mod_menu_key));
    bool mod_menu_toggle_gp = IsGamepadAvailable(0) && IsGamepadButtonPressed(0, g_mod_menu_gamepad);
    if ((mod_menu_toggle_kb || mod_menu_toggle_gp) && g_editing_row < 0 && g_awaiting_keybind_for < 0) g_mod_menu_open = !g_mod_menu_open;
    if (g_mod_menu_open) {
        // Same fixed virtual UI space the stat HUD/FPS counter already
        // use (see g_hud_cam, computed once at the top of this loop),
        // NOT the real window size - everything drawn under
        // BeginMode2D(g_hud_cam) is scaled from this space into
        // whatever the actual window/fullscreen size
        // is, so feeding this the real size double-scales/mispositions
        // it once that differs from the default (e.g. fullscreen).
        compute_mod_menu_layout((int)(WIDE_W * DEFAULT_WINDOW_SCALE), (int)(NATIVE_H * DEFAULT_WINDOW_SCALE));

        if (g_editing_row >= 0) {
            int ch;
            while ((ch = GetCharPressed()) != 0) {
                if (ch >= '0' && ch <= '9' && g_edit_buffer_len < (int)sizeof(g_edit_buffer) - 1) {
                    g_edit_buffer[g_edit_buffer_len++] = (char)ch;
                    g_edit_buffer[g_edit_buffer_len] = '\0';
                }
            }
            if (IsKeyPressed(KEY_BACKSPACE) && g_edit_buffer_len > 0) {
                g_edit_buffer_len--;
                g_edit_buffer[g_edit_buffer_len] = '\0';
            }
            bool gp_ok_edit = IsGamepadAvailable(0);
            if (IsKeyPressed(KEY_ENTER) || (gp_ok_edit && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN))) commit_edit_row();
            // Typing a custom number still needs a real keyboard (no
            // on-screen keypad), but a controller-only player must be
            // able to back out of this prompt without one - same fixed
            // "Right Face = cancel/back" convention as the dropdown and
            // rebind-capture cancel below.
            if (IsKeyPressed(KEY_ESCAPE) || (gp_ok_edit && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT))) cancel_edit_row();
        } else if (g_custom_add_phase != CUSTOM_ADD_NONE) {
            // Two-step add flow: type a hex address, Enter, then type a
            // name, Enter. Same GetCharPressed() text-capture pattern as
            // the numeric edit box above, just with a different allowed
            // character set per phase (hex digits vs. any printable
            // character for the name).
            int ch;
            while ((ch = GetCharPressed()) != 0) {
                bool allowed = (g_custom_add_phase == CUSTOM_ADD_ADDRESS)
                    ? ((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F'))
                    : (ch >= 32 && ch <= 125);
                if (allowed && g_custom_add_buffer_len < (int)sizeof(g_custom_add_buffer) - 1) {
                    g_custom_add_buffer[g_custom_add_buffer_len++] = (char)ch;
                    g_custom_add_buffer[g_custom_add_buffer_len] = '\0';
                }
            }
            if (IsKeyPressed(KEY_BACKSPACE) && g_custom_add_buffer_len > 0) {
                g_custom_add_buffer_len--;
                g_custom_add_buffer[g_custom_add_buffer_len] = '\0';
            }
            if (IsKeyPressed(KEY_ENTER) && g_custom_add_buffer_len > 0) {
                if (g_custom_add_phase == CUSTOM_ADD_ADDRESS) {
                    unsigned long parsed = strtoul(g_custom_add_buffer, nullptr, 16);
                    if (parsed >= sizeof(ram) / sizeof(ram[0])) parsed = sizeof(ram) / sizeof(ram[0]) - 1;
                    g_custom_add_pending_addr = (uint16_t)parsed;
                    g_custom_add_phase = CUSTOM_ADD_NAME;
                    g_custom_add_buffer[0] = 0; g_custom_add_buffer_len = 0;
                } else if (g_custom_mod_count < MAX_CUSTOM_MODS) {
                    CustomModEntry& e = g_custom_mods[g_custom_mod_count++];
                    strncpy(e.name, g_custom_add_buffer, sizeof(e.name) - 1); e.name[sizeof(e.name) - 1] = 0;
                    e.addr = g_custom_add_pending_addr;
                    e.favorite = false; e.frozen = false; e.frozen_value = 0;
                    save_custom_mods();
                    g_custom_add_phase = CUSTOM_ADD_NONE;
                } else {
                    g_custom_add_phase = CUSTOM_ADD_NONE; // list full, silently drop
                }
            }
            if (IsKeyPressed(KEY_ESCAPE)) g_custom_add_phase = CUSTOM_ADD_NONE;
        } else if (row_is_custom(g_mod_menu_selection) && IsKeyPressed(KEY_DELETE)) {
            // Remove the selected custom entry - shift the rest down to
            // keep the array/rows contiguous, then persist.
            int i = g_mod_menu_selection - ROW_CUSTOM_START;
            for (int j = i; j < g_custom_mod_count - 1; j++) g_custom_mods[j] = g_custom_mods[j + 1];
            g_custom_mod_count--;
            save_custom_mods();
        } else if (g_open_dropdown_stat >= 0) {
            // Preset dropdown open: only its own option list is live -
            // any click (hit or miss) closes it, matching normal
            // dropdown UX.
            Vector2 mouse = GetScreenToWorld2D(GetMousePosition(), g_hud_cam);
            const CheatStatRow& row = CHEAT_STATS[g_open_dropdown_stat];
            int opt_h = 20;
            Rectangle list_rect{ g_dropdown_anchor.x, g_dropdown_anchor.y + g_dropdown_anchor.height,
                                  240, (float)(row.preset_count * opt_h) };
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                if (CheckCollisionPointRec(mouse, list_rect)) {
                    int option = (int)((mouse.y - list_rect.y) / opt_h);
                    if (option >= 0 && option < row.preset_count) {
                        set_stat_value(g_open_dropdown_stat, row.presets[option].value);
                    }
                }
                g_open_dropdown_stat = -1;
            }
            bool gp_ok_dd = IsGamepadAvailable(0);
            if (IsKeyPressed(KEY_DOWN) || (gp_ok_dd && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_DOWN))) {
                g_dropdown_gp_index = (g_dropdown_gp_index + 1) % row.preset_count;
            }
            if (IsKeyPressed(KEY_UP) || (gp_ok_dd && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_UP))) {
                g_dropdown_gp_index = (g_dropdown_gp_index - 1 + row.preset_count) % row.preset_count;
            }
            if (IsKeyPressed(KEY_ENTER) || (gp_ok_dd && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN))) {
                set_stat_value(g_open_dropdown_stat, row.presets[g_dropdown_gp_index].value);
                g_open_dropdown_stat = -1;
            }
            if (IsKeyPressed(KEY_ESCAPE) || (gp_ok_dd && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT))) g_open_dropdown_stat = -1;
        } else if (g_awaiting_keybind_for >= 0 && g_awaiting_gamepad_rebind) {
            // Gamepad capture: only keyboard Escape cancels - any
            // gamepad button, including whatever's bound to Back, gets
            // bound (rebinding IS the point of this capture mode).
            if (IsKeyPressed(KEY_ESCAPE)) {
                g_awaiting_keybind_for = -1;
            } else {
                int btn = get_gamepad_button_pressed(0);
                if (btn >= 0) {
                    if (g_awaiting_keybind_for == GAME_BUTTON_COUNT) {
                        g_mod_menu_gamepad = btn;
                        save_gamepad_binds();
                    } else {
                        g_gamepad_binding[g_awaiting_keybind_for] = btn;
                        save_gamepad_binds();
                    }
                    g_awaiting_keybind_for = -1;
                }
            }
        } else if (g_awaiting_keybind_for >= 0) {
            // Capture the next key pressed and bind it, whatever it is -
            // except Escape/Tab, which are reserved (Tab already toggles
            // this menu by default - still true even after rebinding,
            // since Tab stays reserved as a safety net) and instead
            // cancel the rebind.
            int key = GetKeyPressed();
            if (key == KEY_ESCAPE || key == KEY_TAB) {
                g_awaiting_keybind_for = -1;
            } else if (key != 0) {
                if (g_awaiting_keybind_for == GAME_BUTTON_COUNT) {
                    g_mod_menu_key = key;
                    save_keybinds();
                } else {
                    g_key_binding[g_awaiting_keybind_for] = key;
                    save_keybinds();
                }
                g_awaiting_keybind_for = -1;
            }
        } else {
            // Full controller navigation uses a fixed D-Pad convention
            // (like the existing confirm button below), not the
            // player's own remapped in-game bindings - the mod menu is
            // host-side UI, so it should navigate the same way
            // regardless of how they've customized actual gameplay
            // controls.
            bool gp_ok = IsGamepadAvailable(0);
            bool nav_down = IsKeyPressed(KEY_DOWN) || (gp_ok && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_DOWN));
            bool nav_up = IsKeyPressed(KEY_UP) || (gp_ok && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_UP));
            if (nav_down || nav_up) {
                int dir = nav_down ? 1 : -1;
                int pos = 0;
                for (int i = 0; i < g_mod_menu_layout.logical_count; i++) {
                    if (g_mod_menu_layout.logical_order[i] == g_mod_menu_selection) { pos = i; break; }
                }
                pos = (pos + dir + g_mod_menu_layout.logical_count) % g_mod_menu_layout.logical_count;
                g_mod_menu_selection = g_mod_menu_layout.logical_order[pos];
                // Keep the new selection inside the visible scroll window
                // (takes effect next frame's compute_mod_menu_layout).
                if (pos < g_mod_menu_scroll) g_mod_menu_scroll = pos;
                else if (pos >= g_mod_menu_scroll + g_mod_menu_layout.main_row_count) {
                    g_mod_menu_scroll = pos - g_mod_menu_layout.main_row_count + 1;
                }
            }
            if (IsKeyPressed(KEY_PAGE_DOWN) || (gp_ok && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_TRIGGER_1))) g_mod_menu_scroll += 10;
            if (IsKeyPressed(KEY_PAGE_UP) || (gp_ok && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_TRIGGER_1))) g_mod_menu_scroll -= 10;
            if (IsKeyPressed(KEY_LEFT) || (gp_ok && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_LEFT))) adjust_row(g_mod_menu_selection, -1);
            if (IsKeyPressed(KEY_RIGHT) || (gp_ok && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_FACE_RIGHT))) adjust_row(g_mod_menu_selection, 1);
            // Menu confirm accepts either Enter (keyboard) or the
            // gamepad's bottom face button (a fixed, universal menu-
            // navigation convention - independent of the player's own
            // remapped in-game Select binding), so a controller-only
            // player can also open a Controls row's capture prompt.
            bool confirm_kb = IsKeyPressed(KEY_ENTER);
            bool confirm_gp = IsGamepadAvailable(0) && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN);
            if (confirm_kb || confirm_gp) {
                if (row_has_value(g_mod_menu_selection)) begin_edit_row(g_mod_menu_selection);
                else if (g_mod_menu_selection == ROW_CUSTOM_ADD) {
                    g_custom_add_phase = CUSTOM_ADD_ADDRESS;
                    g_custom_add_buffer[0] = 0; g_custom_add_buffer_len = 0;
                }
                else if (row_is_expand_header(g_mod_menu_selection)) adjust_row(g_mod_menu_selection, 1);
                else if (row_is_keybind(g_mod_menu_selection)) {
                    g_awaiting_keybind_for = g_mod_menu_selection - ROW_CONTROLS_START;
                    g_awaiting_gamepad_rebind = confirm_gp && !confirm_kb;
                }
                else if (g_mod_menu_selection == ROW_MOD_MENU_KEY) {
                    g_awaiting_keybind_for = GAME_BUTTON_COUNT;
                    g_awaiting_gamepad_rebind = confirm_gp && !confirm_kb;
                }
            }

            Vector2 mouse = GetScreenToWorld2D(GetMousePosition(), g_hud_cam);
            // Mouse wheel always scrolls the list (never adjusts a row's
            // value - use the chevrons or type a value for that)
            // whenever hovering anywhere over either panel.
            float wheel = GetMouseWheelMove();
            if (wheel != 0.0f && (CheckCollisionPointRec(mouse, g_mod_menu_layout.main_panel)
                                   || CheckCollisionPointRec(mouse, g_mod_menu_layout.fav_panel)
                                   || CheckCollisionPointRec(mouse, g_mod_menu_layout.frozen_panel))) {
                g_mod_menu_scroll -= (int)(wheel * 3);
            }

            ModMenuRowRect* row_sets[2] = { g_mod_menu_layout.main_rows, g_mod_menu_layout.fav_rows };
            int row_counts[2] = { g_mod_menu_layout.main_row_count, g_mod_menu_layout.fav_row_count };
            for (int set = 0; set < 2; set++) {
                for (int i = 0; i < row_counts[set]; i++) {
                    ModMenuRowRect& r = row_sets[set][i];
                    if (!CheckCollisionPointRec(mouse, r.full)) continue;
                    g_mod_menu_selection = r.row_index;
                    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                        if (r.row_index == ROW_CUSTOM_ADD) {
                            g_custom_add_phase = CUSTOM_ADD_ADDRESS;
                            g_custom_add_buffer[0] = 0; g_custom_add_buffer_len = 0;
                        } else if (row_is_expand_header(r.row_index)) {
                            adjust_row(r.row_index, 1);
                        } else if (row_is_keybind(r.row_index)) {
                            g_awaiting_keybind_for = r.row_index - ROW_CONTROLS_START;
                            // Column detection: keyboard name draws at
                            // x+155, gamepad at x+215 (draw_mod_menu_row) -
                            // clicking past the gamepad column's start
                            // rebinds that side instead of keyboard.
                            g_awaiting_gamepad_rebind = mouse.x >= r.full.x + 215;
                        } else if (r.row_index == ROW_MOD_MENU_KEY) {
                            g_awaiting_keybind_for = GAME_BUTTON_COUNT;
                            g_awaiting_gamepad_rebind = mouse.x >= r.full.x + 215;
                        } else {
                            bool has_value = row_has_value(r.row_index);
                            bool has_preset = !row_is_inv_item(r.row_index) && r.row_index >= ROW_STATS_START
                                && CHEAT_STATS[r.row_index - ROW_STATS_START].preset_count > 0;
                            if (has_value && CheckCollisionPointRec(mouse, r.fav)) toggle_favorite_row(r.row_index);
                            else if (has_value && CheckCollisionPointRec(mouse, r.freeze)) toggle_freeze_row(r.row_index);
                            else if (has_preset && CheckCollisionPointRec(mouse, r.preset_btn)) {
                                g_open_dropdown_stat = r.row_index - ROW_STATS_START;
                                g_dropdown_anchor = r.preset_btn;
                                g_dropdown_gp_index = 0;
                            }
                            else if (CheckCollisionPointRec(mouse, r.arrow_ll)) adjust_row(r.row_index, -10);
                            else if (CheckCollisionPointRec(mouse, r.arrow_l)) adjust_row(r.row_index, -1);
                            else if (CheckCollisionPointRec(mouse, r.arrow_r)) adjust_row(r.row_index, 1);
                            else if (CheckCollisionPointRec(mouse, r.arrow_rr)) adjust_row(r.row_index, 10);
                            else if (has_value && CheckCollisionPointRec(mouse, r.value)) begin_edit_row(r.row_index);
                        }
                    }
                }
            }
        }
    }
}

// Draws the Mod Menu panels. Must be called between BeginMode2D(g_hud_cam) and EndMode2D().
void mod_menu_draw_overlay() {
    if (g_mod_menu_open) {
        Rectangle mp = g_mod_menu_layout.main_panel;
        DrawRectangle((int)mp.x, (int)mp.y, (int)mp.width, (int)mp.height, Color{0, 0, 0, 220});
        DrawRectangleLines((int)mp.x, (int)mp.y, (int)mp.width, (int)mp.height, GREEN);
        ui_draw_text(g_awaiting_keybind_for >= 0 ? "MOD MENU (Esc cancels rebind)" : "MOD MENU (Tab to close)",
                 (int)mp.x + 10, (int)mp.y + 8, 15, GREEN);
        // Fav/Freeze checkbox legend - space for this was reserved
        // (mh's +14) since whenever this was added, but the actual text
        // was never drawn, leaving the yellow box explained only in the
        // Favorites panel's empty-state hint and the blue Freeze box
        // never explained anywhere at all.
        ui_draw_text("Fav", (int)mp.x + 10, (int)mp.y + 26, 11, YELLOW);
        ui_draw_text("Freeze", (int)mp.x + 258 + s_label_extra_w + s_value_extra_w - 10, (int)mp.y + 26, 11, SKYBLUE);
        for (int i = 0; i < g_mod_menu_layout.main_row_count; i++) {
            const ModMenuRowRect& r = g_mod_menu_layout.main_rows[i];
            draw_mod_menu_row(r);
            if (g_mod_menu_layout.separator_after_row[r.row_index]) {
                int ly = (int)(r.full.y + r.full.height - 2);
                DrawRectangle((int)r.full.x, ly, (int)r.full.width, 2, Color{180, 180, 180, 255});
            }
        }
        if (g_mod_menu_layout.has_scroll) {
            char scroll_buf[32];
            sprintf(scroll_buf, "%d-%d / %d (PgUp/PgDn)", g_mod_menu_scroll + 1,
                    g_mod_menu_scroll + g_mod_menu_layout.main_row_count, g_mod_menu_layout.logical_count);
            ui_draw_text(scroll_buf, (int)mp.x + 10, (int)(mp.y + mp.height - 16), 11, GRAY);
        }

        Rectangle fp = g_mod_menu_layout.fav_panel;
        DrawRectangle((int)fp.x, (int)fp.y, (int)fp.width, (int)fp.height, Color{0, 0, 0, 220});
        DrawRectangleLines((int)fp.x, (int)fp.y, (int)fp.width, (int)fp.height, YELLOW);
        ui_draw_text("FAVORITES", (int)fp.x + 10, (int)fp.y + 8, 15, YELLOW);
        if (g_mod_menu_layout.fav_row_count == 0) {
            ui_draw_text("Click the yellow box\nnext to a stat to pin it here", (int)fp.x + 10, (int)fp.y + 32, 13, GRAY);
        } else {
            for (int i = 0; i < g_mod_menu_layout.fav_row_count; i++) {
                draw_mod_menu_row(g_mod_menu_layout.fav_rows[i]);
            }
        }

        // Frozen panel - explanatory only, unlike Favorites it never
        // lists rows (freezing already shows on the item's own row via
        // the filled blue checkbox - this is just the "what does that
        // blue box mean" explanation Favorites gets from its own
        // empty-state hint).
        Rectangle zp = g_mod_menu_layout.frozen_panel;
        DrawRectangle((int)zp.x, (int)zp.y, (int)zp.width, (int)zp.height, Color{0, 0, 0, 220});
        DrawRectangleLines((int)zp.x, (int)zp.y, (int)zp.width, (int)zp.height, SKYBLUE);
        ui_draw_text("FROZEN", (int)zp.x + 10, (int)zp.y + 8, 15, SKYBLUE);
        ui_draw_text("Click the blue box\nnext to a stat to freeze it", (int)zp.x + 10, (int)zp.y + 32, 13, GRAY);

        if (g_open_dropdown_stat >= 0) {
            const CheatStatRow& row = CHEAT_STATS[g_open_dropdown_stat];
            int opt_h = 20;
            int lx = (int)g_dropdown_anchor.x, ly = (int)(g_dropdown_anchor.y + g_dropdown_anchor.height);
            int lw = 240, lh = row.preset_count * opt_h;
            DrawRectangle(lx, ly, lw, lh, Color{20, 20, 20, 240});
            DrawRectangleLines(lx, ly, lw, lh, YELLOW);
            for (int i = 0; i < row.preset_count; i++) {
                if (i == g_dropdown_gp_index) {
                    DrawRectangle(lx, ly + i * opt_h, lw, opt_h, Color{80, 80, 0, 255});
                }
                char opt_buf[48];
                sprintf(opt_buf, "%d: %s", row.presets[i].value, row.presets[i].label);
                ui_draw_text(opt_buf, lx + 4, ly + i * opt_h + 2, 13, WHITE);
            }
        }
    }
}
