// Mod Menu: row layout, input handling helpers, and row drawing.
#pragma once

#include "base.h"
#include "cheat_tables.h"
#include "cheats.h"
#include "input.h"
#include "settings.h"

// Row indices: a fixed numbering of every possible Mod Menu row. Controls comes first as a
// collapsible category, then the option rows (filter, speed, no-clip, ...), then CHEAT_STATS, then
// one header row per inventory category followed by the items, then custom mods. The item range
// always reserves a slot for every item whether or not its category is expanded, so a row index
// identifies the same item regardless of what is currently shown (favorites and persistence rely on
// that).
static const int ROW_CONTROLS_CAT = 0;
static const int ROW_CONTROLS_START = 1;
static const int ROW_MOD_MENU_KEY = ROW_CONTROLS_START + GAME_BUTTON_COUNT;
static const int ROW_FILTER = ROW_MOD_MENU_KEY + 1;
static const int ROW_MOVE_SPEED = ROW_FILTER + 1;
static const int ROW_NOCLIP = ROW_MOVE_SPEED + 1;
static const int ROW_MINIPET = ROW_NOCLIP + 1;
static const int ROW_MINIPET_ACTION = ROW_MINIPET + 1;
static const int ROW_FPS_LIMIT = ROW_MINIPET_ACTION + 1;
static const int ROW_INTERPOLATION = ROW_FPS_LIMIT + 1;
static const int ROW_FPS_COUNTER = ROW_INTERPOLATION + 1;
static const int ROW_STAT_HUD = ROW_FPS_COUNTER + 1;
static const int ROW_QUEST_ARROW = ROW_STAT_HUD + 1;

// Collapsible "Extractor" category - sprite-capture-related toggles that
// only matter while actively using F7/F8, grouped out of the way like the
// Controls/Inventory categories below.
static const int ROW_EXTRACTOR_CAT = ROW_QUEST_ARROW + 1;
static const int ROW_EXPORT_SCALE = ROW_EXTRACTOR_CAT + 1;
static const int ROW_EXTRACTOR_SKIP_PLAYER = ROW_EXPORT_SCALE + 1;
static const int ROW_DISABLE_SHADOWS = ROW_EXTRACTOR_SKIP_PLAYER + 1;

// Collapsible "Stats" category - just the pet care stats (Halos through
// Tricks); Money and the debug/utility values (Location ID, Player/Camera
// X/Y, Quests Done) stay always-visible at the top level.
static const int ROW_STATS_CAT = ROW_DISABLE_SHADOWS + 1;
static const int ROW_STATS_START = ROW_STATS_CAT + 1;
static const int ROW_INVENTORY_CAT_START = ROW_STATS_START + CHEAT_STAT_COUNT;
static const int ROW_INVENTORY_ITEM_START = ROW_INVENTORY_CAT_START + INVENTORY_CATEGORY_COUNT;
static const int ROW_CUSTOM_CAT = ROW_INVENTORY_ITEM_START + INVENTORY_ITEM_COUNT;
static const int ROW_CUSTOM_ADD = ROW_CUSTOM_CAT + 1;
static const int ROW_CUSTOM_START = ROW_CUSTOM_ADD + 1;

// Upper bound on distinct row_index values (used to size the logical-order
// scratch array) - NOT the number of rows visible in any given frame, which
// depends on which categories are currently expanded.
static const int MOD_MENU_TOTAL_ROW_SLOTS = ROW_CUSTOM_START + MAX_CUSTOM_MODS;
static const int MOD_MENU_MAX_VISIBLE_ROWS = 22; // main-panel scroll window cap

struct ModMenuRowRect {
    // arrow_ll/rr = double chevron (step 10), arrow_l/r = single chevron (step 1).
    Rectangle full, fav, arrow_ll, arrow_l, value, arrow_r, arrow_rr, freeze, preset_btn;
    int row_index; // ROW_FILTER/ROW_MOVE_SPEED/ROW_NOCLIP, ROW_STATS_START+i,
                    // ROW_INVENTORY_CAT_START+c, or ROW_INVENTORY_ITEM_START+i
};

// Mod Menu layout, computed once per frame (mod_menu_update) so mouse hit-testing and drawing use
// identical row geometry. Two panels: the main list, and a docked Favorites panel with just the
// pinned rows. The main list can be far taller than the screen (up to 268 inventory items), so it
// scrolls: logical_order holds every currently visible logical row in display order (respecting
// which categories are expanded) and main_rows holds only the slice scrolled into view.
struct ModMenuLayout {
    Rectangle main_panel;
    ModMenuRowRect main_rows[MOD_MENU_MAX_VISIBLE_ROWS];
    int main_row_count;
    Rectangle fav_panel;
    ModMenuRowRect fav_rows[CHEAT_STAT_COUNT + INVENTORY_ITEM_COUNT + MAX_CUSTOM_MODS];
    int fav_row_count;
    // Frozen panel is explanatory only (no row listing, unlike Favorites) -
    // just a fixed-size box, no per-item rects needed.
    Rectangle frozen_panel;
    int logical_order[MOD_MENU_TOTAL_ROW_SLOTS];
    int logical_count;
    bool has_scroll;
    // Indexed by row_index - true for the last row of a currently-expanded
    // collapsible section, so a thin line can be drawn under it to
    // visually separate it from whatever (unrelated) row follows.
    bool separator_after_row[MOD_MENU_TOTAL_ROW_SLOTS];
};

enum CustomAddPhase { CUSTOM_ADD_NONE, CUSTOM_ADD_ADDRESS, CUSTOM_ADD_NAME };

extern ModMenuLayout g_mod_menu_layout;
extern int g_mod_menu_scroll;  // index into logical_order of the first visible row
extern bool g_mod_menu_open;
extern bool g_category_expanded[INVENTORY_CATEGORY_COUNT];
extern int g_mod_menu_selection;
extern bool g_controls_expanded;
extern bool g_extractor_expanded;
extern bool g_stats_expanded;
extern bool g_custom_expanded;
extern CustomAddPhase g_custom_add_phase;
extern char g_custom_add_buffer[24];
extern int g_custom_add_buffer_len;
extern uint16_t g_custom_add_pending_addr;
extern int g_editing_row;
extern char g_edit_buffer[8];
extern int g_edit_buffer_len;
extern int g_open_dropdown_stat;
extern Rectangle g_dropdown_anchor;
extern int g_dropdown_gp_index;
extern int g_awaiting_keybind_for;
extern int s_label_extra_w;
extern int s_value_extra_w;

bool row_is_category(int row_index);
bool row_is_inv_item(int row_index);
bool row_is_custom(int row_index);
bool row_is_expand_header(int row_index);
bool row_is_keybind(int row_index);
bool row_has_value(int row_index);
void compute_mod_menu_layout(int screen_w, int screen_h);
void adjust_row(int row_index, int delta);
void toggle_favorite_row(int row_index);
void toggle_freeze_row(int row_index);
void begin_edit_row(int row_index);
void commit_edit_row();
void cancel_edit_row();
void draw_text_clipped(const char* text, int x, int y, int font_size, Color color, int max_w);
void draw_mod_menu_row(const ModMenuRowRect& r);
void mod_menu_update();
void mod_menu_draw_overlay();
