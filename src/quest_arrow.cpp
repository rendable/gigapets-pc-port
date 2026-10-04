// Quest arrow: door-link table and routing used to point at the next door toward the quest target.

#include "common.h"

// Door-to-destination links, gathered empirically by walking through each door and recording
// (fromArea, door object id, toArea) at the moment of the transition. (x, y) is the door object's
// world position from the ROM's interactable-object table. Only direct connections are listed;
// find_route_door chains them for multi-hop routes.
struct DoorLink { uint16_t fromArea, toArea; int16_t x, y; };

static const DoorLink DOOR_LINKS[] = {
    { 0, 4, 351, 1323 },
    { 0, 7, 841, 1561 },
    { 0, 6, 1396, 440 }, // the only entrance that actually leads to Gigalympia's NPCs/quests -
    { 0, 1, 2475, 751 }, // the OTHER area0<->6 door pair (objectId 6/43) goes to a disconnected
    { 1, 9, 445, 571 },  // part of the area and is deliberately left out, not an oversight.
    { 1, 5, 2216, 292 },
    { 1, 0, 96, 726 },
    { 1, 3, 2175, 1568 },
    { 2, 3, 463, 1100 },
    { 3, 0, 1561, 429 },
    { 3, 1, 1261, 119 },
    { 3, 2, 349, 926 },
    { 4, 0, 226, 368 },
    { 5, 1, 1572, 1570 },
    { 6, 0, 1394, 1215 },
    { 7, 0, 876, 120 },
    { 9, 1, 960, 1568 },
};

static const int DOOR_LINK_COUNT = sizeof(DOOR_LINKS) / sizeof(DOOR_LINKS[0]);

// Returns the world position of the nearest known door in fromArea that
// leads toward toArea, or false if no direct connection is known (v1 -
// multi-hop routing through intermediate areas isn't built yet).
bool find_door_to_area(uint16_t fromArea, uint16_t toArea, int16_t playerX, int16_t playerY, int16_t* outX, int16_t* outY) {
    bool found = false;
    int32_t best_dist_sq = 0;
    for (int i = 0; i < DOOR_LINK_COUNT; i++) {
        if (DOOR_LINKS[i].fromArea != fromArea || DOOR_LINKS[i].toArea != toArea) continue;
        int32_t dx = DOOR_LINKS[i].x - playerX, dy = DOOR_LINKS[i].y - playerY;
        int32_t dist_sq = dx * dx + dy * dy;
        if (!found || dist_sq < best_dist_sq) {
            found = true;
            best_dist_sq = dist_sq;
            *outX = DOOR_LINKS[i].x;
            *outY = DOOR_LINKS[i].y;
        }
    }
    return found;
}

// Multi-hop routing: BFS over DOOR_LINKS to find a path from fromArea to
// toArea through intermediate areas when there's no direct connection,
// then points at the door for the FIRST hop of that path (walking through
// it re-triggers this same logic from the new area, giving the next hop).
// Reduces to a plain find_door_to_area lookup when the connection is
// direct, so this can just replace that call everywhere.
static const int MAX_ROUTABLE_AREAS = 64;

bool find_route_door(uint16_t fromArea, uint16_t toArea, int16_t playerX, int16_t playerY, int16_t* outX, int16_t* outY) {
    if (fromArea == toArea || fromArea >= MAX_ROUTABLE_AREAS || toArea >= MAX_ROUTABLE_AREAS) return false;
    int16_t prev[MAX_ROUTABLE_AREAS];
    bool visited[MAX_ROUTABLE_AREAS];
    for (int i = 0; i < MAX_ROUTABLE_AREAS; i++) { prev[i] = -1; visited[i] = false; }
    uint16_t queue[MAX_ROUTABLE_AREAS];
    int qh = 0, qt = 0;
    queue[qt++] = fromArea;
    visited[fromArea] = true;
    while (qh < qt) {
        uint16_t cur = queue[qh++];
        if (cur == toArea) break;
        for (int i = 0; i < DOOR_LINK_COUNT; i++) {
            if (DOOR_LINKS[i].fromArea != cur) continue;
            uint16_t nxt = DOOR_LINKS[i].toArea;
            if (nxt >= MAX_ROUTABLE_AREAS || visited[nxt]) continue;
            visited[nxt] = true;
            prev[nxt] = (int16_t)cur;
            queue[qt++] = nxt;
        }
    }
    if (!visited[toArea]) return false; // no known route at all, even multi-hop
    // Walk backward from toArea until the step right after fromArea -
    // that's the first hop's destination area.
    uint16_t step = toArea;
    while (prev[step] != -1 && prev[step] != (int16_t)fromArea) step = (uint16_t)prev[step];
    return find_door_to_area(fromArea, step, playerX, playerY, outX, outY);
}

void quest_arrow_draw() {
    // Quest arrow (work in progress): points at the door leading toward the current quest's delivery
    // target. The target is the NPC at QUEST_TARGET_POOL_IDX_ADDR (set once when the quest is
    // rolled, so it correctly distinguishes "bring to me" from "bring to someone else"), and a quest
    // is active when QUEST_OBJECTIVE_FLAG_ADDR != 0xFFFF (the same check the game uses for its own
    // turn-in summary). No arrow if the target area is unreachable via DOOR_LINKS or we're already there.
    if (g_quest_arrow_enabled) {
        bool gate = ram[GAME_STATE_ADDR] == GAME_STATE_IN_ROOM && ram[QUEST_OBJECTIVE_FLAG_ADDR] != 0xFFFF;
        uint16_t pool_idx = ram[QUEST_TARGET_POOL_IDX_ADDR];
        uint16_t target_area = rom[ROM_NPC_AREA_TABLE + pool_idx * ROM_NPC_AREA_TABLE_STRIDE]; // real per-NPC area, lives in ROM not RAM
        uint16_t cur_area = ram[LOCATION_ID_ADDR];
        bool door_found = false;
        int16_t door_x = 0, door_y = 0;
        if (gate && cur_area != target_area) {
            int16_t px = (int16_t)ram[PLAYER_WORLD_X], py = (int16_t)ram[PLAYER_WORLD_Y];
            door_found = find_route_door(cur_area, target_area, px, py, &door_x, &door_y);
            if (door_found) {
                float angle = atan2f((float)(door_y - py), (float)(door_x - px));
                float acx = WIDE_W / 2.0f, acy = 36.0f;
                float c = cosf(angle), s = sinf(angle);
                // Drawn as a "staircase" of rotated rectangles narrowing toward the tip, all using the
                // same rotation and placed along one direction line. (DrawTriangle fills and angled
                // head pieces rendered disconnected or not at all here, cause not pinned down.)
                float angle_deg = angle * RAD2DEG;
                float shaft_len = 14.0f, shaft_thick = 6.0f;
                DrawRectanglePro(Rectangle{ acx, acy, shaft_len, shaft_thick }, Vector2{ 0, shaft_thick / 2 }, angle_deg, WHITE);
                static const float SEG_W[5] = { 18, 13.5f, 9.5f, 6, 2.5f };
                float seg_len = 3.0f, dist = shaft_len;
                for (int i = 0; i < 5; i++) {
                    float sx = acx + c * dist, sy = acy + s * dist;
                    DrawRectanglePro(Rectangle{ sx, sy, seg_len, SEG_W[i] }, Vector2{ 0, SEG_W[i] / 2 }, angle_deg, WHITE);
                    dist += seg_len;
                }
            }
        }
    }
}
