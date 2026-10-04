// Quest arrow: door-link table and routing used to point at the next door toward the quest target.
#pragma once

#include "base.h"

bool find_door_to_area(uint16_t fromArea, uint16_t toArea, int16_t playerX, int16_t playerY, int16_t* outX, int16_t* outY);
bool find_route_door(uint16_t fromArea, uint16_t toArea, int16_t playerX, int16_t playerY, int16_t* outX, int16_t* outY);
void quest_arrow_draw();
