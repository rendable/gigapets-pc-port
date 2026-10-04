// Player-affecting mods: movement speed, no-clip, and shadow removal.

#include "common.h"

int g_movement_speed_idx = 0;
int16_t g_last_player_x = 0;
int16_t g_last_player_y = 0;
bool g_have_last_player_pos = false;
uint16_t g_last_location_id = 0;
bool g_have_last_location_id = false;
int g_location_change_grace_frames = 0;
NoClipPatch g_noclip_patches[3];
bool g_cheat_noclip = false;
uint16_t g_shadow_patch_original = 0;
bool g_shadows_disabled = false;

int32_t find_word_pattern(const uint16_t* haystack, uint32_t haystack_len, const uint16_t* needle, uint32_t needle_len) {
    if (needle_len == 0 || haystack_len < needle_len) return -1;
    for (uint32_t i = 0; i + needle_len <= haystack_len; i++) {
        bool match = true;
        for (uint32_t j = 0; j < needle_len; j++) {
            if (haystack[i + j] != needle[j]) { match = false; break; }
        }
        if (match) return (int32_t)i;
    }
    return -1;
}

void find_noclip_patch_addresses() {
    // Each pattern is the Cheat Engine AOB bytes reinterpreted as little-
    // endian uint16_t words; the patch target is always +4 bytes (+2 words)
    // into the match, matching the CT script's "NoClipN+4: db 41 92".
    static const uint16_t pattern1[] = { 0x9F0F, 0xC1E5, 0x9240, 0x9888, 0x9A90, 0xDA88, 0x2049 };
    static const uint16_t pattern2[] = { 0x9F0F, 0xC253, 0x9240, 0x9F0F, 0xC254, 0x9241, 0x0049 };
    static const uint16_t pattern3[] = { 0x9F0F, 0xC2FC, 0x9240, 0x9F0F, 0xC305, 0x980A, 0x0841 };
    const uint16_t* patterns[3] = { pattern1, pattern2, pattern3 };
    for (int p = 0; p < 3; p++) {
        int32_t match = find_word_pattern(rom, 0x400000, patterns[p], 7);
        if (match >= 0) {
            g_noclip_patches[p].patch_word_addr = match + 2;
            g_noclip_patches[p].original_value = rom[match + 2];
        }
    }
    

}

void set_shadows_disabled(bool disabled) {
    rom[SHADOW_PATCH_ADDR] = disabled ? SHADOW_RETF_OPCODE : g_shadow_patch_original;
}

void set_noclip_enabled(bool enabled) {
    for (int p = 0; p < 3; p++) {
        if (g_noclip_patches[p].patch_word_addr < 0) continue;
        rom[g_noclip_patches[p].patch_word_addr] = enabled
            ? (uint16_t)(g_noclip_patches[p].original_value | 0x0001)
            : g_noclip_patches[p].original_value;
    }
}
