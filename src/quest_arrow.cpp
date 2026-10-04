// Quest arrow: door-link table and routing used to point at the next door toward the quest target.

#include "common.h"

// Real door-to-destination links, gathered empirically (the mechanism
// connecting a warp-trigger object to which new area actually loads was
// never resolved via static analysis - traced live instead: walked
// through each door while logging (fromArea, lastActiveStoryObjectId,
// toArea) at the moment of transition). Position (x,y) is that door
// object's own world position, pulled from the interactable-object table
// (ROM 0x6BFD-area-indexed, confirmed reliable via 6 separate live
// cross-checks during development, including this exact door set). Direct
// connections only, v1 scope - if the target area isn't directly reachable
// from the player's current area, no entry exists here.
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
