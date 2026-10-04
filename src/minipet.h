// Minipet spawn/despawn (replicates the RAM effects of the real zap-in animation).
#pragma once

#include "base.h"

// Labels only, not the species index itself - the array position is what
// gets written to GAME_CURRENT_ROOM_ID_ADDR and drives the real ROM sprite
// lookup (PlayMiniPetWalkAnim's table). The original alphabetical guess was
// never ground-truthed against the ROM's actual internal ordering; empirical
// in-game testing corrected indices 0/2/5/7, and all 8 (including 1/3/4/6 -
// Dragon Lizard/Pixie/Puffball/Scorpion) are now user-confirmed correct.
static const char* MINIPET_NAMES[8] = { "Hamster", "Dragon Lizard", "Tomcat", "Pixie", "Puffball", "Bunny", "Scorpion", "Pup" };

extern int g_minipet_picker_idx;
extern bool g_minipet_spawned;

void spawn_minipet(int species_idx);
void minipet_trail_record_step(int16_t x, int16_t y);
void despawn_minipet();
