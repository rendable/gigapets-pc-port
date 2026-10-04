// Cheat engine state: favorites, frozen values, custom mods, their persistence, and the
// set_stat_value/set_inv_value entry points.

#include "common.h"

bool g_inv_favorite[INVENTORY_ITEM_COUNT] = {false};

bool g_stat_favorite[CHEAT_STAT_COUNT] = {false};

bool g_inv_frozen[INVENTORY_ITEM_COUNT] = {false};

uint16_t g_inv_frozen_value[INVENTORY_ITEM_COUNT] = {0};

bool g_stat_frozen[CHEAT_STAT_COUNT] = {false};

uint16_t g_stat_frozen_value[CHEAT_STAT_COUNT] = {0};

CustomModEntry g_custom_mods[MAX_CUSTOM_MODS];
int g_custom_mod_count = 0;

struct CheatStatePersistRecord { uint16_t addr; uint8_t favorite; uint8_t frozen; uint16_t frozen_value; };

void save_cheat_state() {
    FILE* f = fopen(app_path("resources/data/gigapets_cheat_state.dat").c_str(), "wb");
    if (!f) return;
    int32_t counts[2] = { CHEAT_STAT_COUNT, INVENTORY_ITEM_COUNT };
    fwrite(counts, sizeof(int32_t), 2, f);
    for (int i = 0; i < CHEAT_STAT_COUNT; i++) {
        CheatStatePersistRecord rec{ CHEAT_STATS[i].addr, (uint8_t)g_stat_favorite[i], (uint8_t)g_stat_frozen[i], g_stat_frozen_value[i] };
        fwrite(&rec, sizeof(rec), 1, f);
    }
    for (int i = 0; i < INVENTORY_ITEM_COUNT; i++) {
        CheatStatePersistRecord rec{ INVENTORY_ITEMS[i].addr, (uint8_t)g_inv_favorite[i], (uint8_t)g_inv_frozen[i], g_inv_frozen_value[i] };
        fwrite(&rec, sizeof(rec), 1, f);
    }
    fclose(f);
}

// Favorite/freeze records are keyed by each stat/item's RAM address, not its array position, so
// reordering, adding or removing table rows can never apply saved flags to the wrong row. Records
// for addresses that no longer exist are simply skipped.
void load_cheat_state() {
    FILE* f = fopen(app_path("resources/data/gigapets_cheat_state.dat").c_str(), "rb");
    if (!f) return;
    int32_t counts[2] = {0, 0};
    if (fread(counts, sizeof(int32_t), 2, f) != 2) { fclose(f); return; }
    for (int32_t r = 0; r < counts[0]; r++) {
        CheatStatePersistRecord rec;
        if (fread(&rec, sizeof(rec), 1, f) != 1) break;
        for (int i = 0; i < CHEAT_STAT_COUNT; i++) {
            if (CHEAT_STATS[i].addr == rec.addr) {
                g_stat_favorite[i] = rec.favorite != 0;
                g_stat_frozen[i] = rec.frozen != 0;
                g_stat_frozen_value[i] = rec.frozen_value;
                break;
            }
        }
    }
    for (int32_t r = 0; r < counts[1]; r++) {
        CheatStatePersistRecord rec;
        if (fread(&rec, sizeof(rec), 1, f) != 1) break;
        for (int i = 0; i < INVENTORY_ITEM_COUNT; i++) {
            if (INVENTORY_ITEMS[i].addr == rec.addr) {
                g_inv_favorite[i] = rec.favorite != 0;
                g_inv_frozen[i] = rec.frozen != 0;
                g_inv_frozen_value[i] = rec.frozen_value;
                break;
            }
        }
    }
    fclose(f);
}

void save_custom_mods() {
    FILE* f = fopen(app_path("resources/data/gigapets_custom_mods.dat").c_str(), "wb");
    if (!f) return;
    int32_t count = g_custom_mod_count;
    fwrite(&count, sizeof(count), 1, f);
    fwrite(g_custom_mods, sizeof(CustomModEntry), g_custom_mod_count, f);
    fclose(f);
}

void load_custom_mods() {
    FILE* f = fopen(app_path("resources/data/gigapets_custom_mods.dat").c_str(), "rb");
    if (!f) return;
    int32_t count = 0;
    if (fread(&count, sizeof(count), 1, f) == 1) {
        if (count < 0) count = 0;
        if (count > MAX_CUSTOM_MODS) count = MAX_CUSTOM_MODS;
        g_custom_mod_count = (int)fread(g_custom_mods, sizeof(CustomModEntry), count, f);
        // Drop entries from a corrupt/hand-edited file that would index
        // past ram[] or carry an unterminated name.
        int kept = 0;
        for (int i = 0; i < g_custom_mod_count; i++) {
            if (g_custom_mods[i].addr >= sizeof(ram) / sizeof(ram[0])) continue;
            g_custom_mods[i].name[sizeof(g_custom_mods[i].name) - 1] = 0;
            g_custom_mods[kept++] = g_custom_mods[i];
        }
        g_custom_mod_count = kept;
    }
    fclose(f);
}

void set_stat_value(int stat_idx, int32_t val) {
    if (val < CHEAT_STATS[stat_idx].min_val) val = CHEAT_STATS[stat_idx].min_val;
    if (val > CHEAT_STATS[stat_idx].max_val) val = CHEAT_STATS[stat_idx].max_val;
    ram[CHEAT_STATS[stat_idx].addr] = (uint16_t)val;
    if (CHEAT_STATS[stat_idx].addr == MONEY_STAT_ADDR) {
        // Real purchases save money immediately through the ROM's own routine; do the same so a
        // Mod Menu edit survives closing the game (see docs/ROM_NOTES.md, "Saving").
        call_rom_function(ROM_SAVE_MONEY_TO_SLOT, { 0, ram[CURRENT_SAVE_SLOT_ADDR] });
    }
    if (g_stat_frozen[stat_idx]) {
        g_stat_frozen_value[stat_idx] = (uint16_t)val;
        save_cheat_state();
    }
}

// King's Lost Items, Treasure Maps, and Rare Flowers are one-of-a-kind
// collectibles in-game (own it or don't) - capped at 1 rather than the
// usual uint16_t range. Matched by category name (not index) so this stays
// correct even if the generated INVENTORY_CATEGORIES table is ever
// regenerated in a different order.
bool inv_item_capped_at_one(int item_idx) {
    for (int c = 0; c < INVENTORY_CATEGORY_COUNT; c++) {
        const InventoryCategory& cat = INVENTORY_CATEGORIES[c];
        if (item_idx < cat.start || item_idx >= cat.start + cat.count) continue;
        return strcmp(cat.name, "King's Lost Items") == 0
            || strcmp(cat.name, "Treasure Maps") == 0
            || strcmp(cat.name, "Rare Flowers") == 0;
    }
    return false;
}

// Same as set_stat_value, for an inventory item index. There is no general per-item maximum, so
// this clamps to the full uint16_t range, except for the one-of-a-kind categories above.
void set_inv_value(int item_idx, int32_t new_val) {
    int32_t max_val = inv_item_capped_at_one(item_idx) ? 1 : 65535;
    if (new_val < 0) new_val = 0;
    if (new_val > max_val) new_val = max_val;
    ram[INVENTORY_ITEMS[item_idx].addr] = (uint16_t)new_val;
    // Persist like the ROM does after a real item gain (Item_IncrementOwnedAndSave ->
    // SaveItemFieldToSlot(itemIdx, field = 0, slot), args pushed in the real caller's order).
    // itemIdx is the ROM's item id, i.e. the item's RAM address minus ITEM_QUANTITY_BASE_ADDR -
    // NOT our INVENTORY_ITEMS array position, since the array is grouped by category.
    // See docs/ROM_NOTES.md, "Saving".
    call_rom_function(ROM_SAVE_ITEM_FIELD_TO_SLOT, { (uint16_t)(INVENTORY_ITEMS[item_idx].addr - ITEM_QUANTITY_BASE_ADDR), 0, ram[CURRENT_SAVE_SLOT_ADDR] });
    if (g_inv_frozen[item_idx]) {
        g_inv_frozen_value[item_idx] = (uint16_t)new_val;
        save_cheat_state();
    }
}

void cheats_reassert_frozen() {
    // Frozen stats/items: re-assert every tick so game logic (e.g.
    // Hunger ticking down over time) can't change them.
    for (int i = 0; i < CHEAT_STAT_COUNT; i++) {
        if (g_stat_frozen[i]) ram[CHEAT_STATS[i].addr] = g_stat_frozen_value[i];
    }
    for (int i = 0; i < INVENTORY_ITEM_COUNT; i++) {
        if (g_inv_frozen[i]) ram[INVENTORY_ITEMS[i].addr] = g_inv_frozen_value[i];
    }
    for (int i = 0; i < g_custom_mod_count; i++) {
        if (g_custom_mods[i].frozen) ram[g_custom_mods[i].addr] = g_custom_mods[i].frozen_value;
    }
}
