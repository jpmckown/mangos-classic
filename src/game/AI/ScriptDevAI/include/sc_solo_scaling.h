/* This file is part of the ScriptDev2 Project. See AUTHORS file for Copyright information
 * Fork addition: player-count scaling for raid mechanics that assume a full raid.
 */

#ifndef SC_SOLO_SCALING_H
#define SC_SOLO_SCALING_H

#include "Maps/Map.h"
#include "Util/Util.h"

#include <algorithm>

// Players in the map, GMs excluded, never less than 1.
inline uint32 GetEncounterPlayerCount(Map const* map)
{
    uint32 count = map ? map->GetPlayersCountExceptGMs() : 1;
    return std::max(count, 1u);
}

// Linear from `solo` with one player to `full` with `raidSize` or more players.
inline float ScaleByPlayerCount(Map const* map, uint32 raidSize, float solo, float full)
{
    if (raidSize <= 1)
        return full;
    uint32 count = std::min(GetEncounterPlayerCount(map), raidSize);
    return solo + (full - solo) * float(count - 1) / float(raidSize - 1);
}

// For mechanics on a fixed timer or spell list: roll whether this occurrence happens.
// `soloChance` with one player, always with a full raid, so the average frequency scales with the player count.
inline bool RollByPlayerCount(Map const* map, uint32 raidSize, float soloChance)
{
    return roll_chance_f(100.0f * ScaleByPlayerCount(map, raidSize, soloChance, 1.0f));
}

#endif
