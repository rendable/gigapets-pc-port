// Cheat engine state: favorites, frozen values, custom mods, their persistence, and the
// set_stat_value/set_inv_value entry points.
#pragma once

#include "base.h"
#include "cheat_tables.h"

// Collapsible "Custom" category - user-added address/name pairs, typed in
// live via the Mod Menu itself (not hardcoded), persisted locally.
static const int MAX_CUSTOM_MODS = 24;

// User-added Mod Menu entries: address + name typed in live, not
// hardcoded. Own favorite/freeze state per entry (can't reuse
// CheatStatePersistRecord's fixed CHEAT_STATS/INVENTORY_ITEMS arrays since
// these addresses are only known at runtime).
struct CustomModEntry { char name[24]; uint16_t addr; bool favorite; bool frozen; uint16_t frozen_value; };

extern bool g_inv_favorite[INVENTORY_ITEM_COUNT];
extern bool g_stat_favorite[CHEAT_STAT_COUNT];
extern bool g_inv_frozen[INVENTORY_ITEM_COUNT];
extern uint16_t g_inv_frozen_value[INVENTORY_ITEM_COUNT];
extern bool g_stat_frozen[CHEAT_STAT_COUNT];
extern uint16_t g_stat_frozen_value[CHEAT_STAT_COUNT];
extern CustomModEntry g_custom_mods[MAX_CUSTOM_MODS];
extern int g_custom_mod_count;

void save_cheat_state();
void load_cheat_state();
void save_custom_mods();
void load_custom_mods();
void set_stat_value(int stat_idx, int32_t val);
bool inv_item_capped_at_one(int item_idx);
void set_inv_value(int item_idx, int32_t new_val);
