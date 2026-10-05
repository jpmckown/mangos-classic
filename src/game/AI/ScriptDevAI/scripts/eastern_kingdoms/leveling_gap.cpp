/* This file is part of the ScriptDev2 Project. See AUTHORS file for Copyright information
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/* ScriptData
SDName: Leveling_Gap
SD%Complete: 100
SDComment: Custom leveling-gap content (tools/leveling-gaps). Escorts for quests 60602 and 60605 (Searing Gorge, hub 12).
           Waypoints are script_waypoint rows for the escortee's entry; texts are broadcast_text 600600-600605.
SDCategory: Searing Gorge
EndScriptData
*/

#include "AI/ScriptDevAI/include/sc_common.h"
#include "AI/ScriptDevAI/base/escort_ai.h"

/*######
## npc_lg_escort
######*/

enum
{
    NPC_BRANNOCK                = 60622,
    NPC_HARGUN                  = 60623,
    NPC_DARK_IRON_SLAVER        = 5844,
    NPC_DARK_IRON_TASKMASTER    = 5846,

    SAY_BRANNOCK_START          = 600600,
    SAY_BRANNOCK_AMBUSH         = 600601,
    SAY_BRANNOCK_END            = 600602,
    SAY_HARGUN_START            = 600603,
    SAY_HARGUN_AMBUSH           = 600604,
    SAY_HARGUN_END              = 600605,

    BRANNOCK_LAST_POINT         = 5,
    HARGUN_LAST_POINT           = 6,
};

struct LgAmbush
{
    uint32 escortEntry;
    uint32 pointId;
    uint32 attacker1;
    uint32 attacker2;
};

static const LgAmbush lgAmbushes[] =
{
    {NPC_BRANNOCK, 3, NPC_DARK_IRON_SLAVER,     NPC_DARK_IRON_SLAVER},
    {NPC_HARGUN,   2, NPC_DARK_IRON_SLAVER,     NPC_DARK_IRON_SLAVER},
    {NPC_HARGUN,   4, NPC_DARK_IRON_TASKMASTER, NPC_DARK_IRON_SLAVER},
};

struct npc_lg_escortAI : public npc_escortAI
{
    npc_lg_escortAI(Creature* creature) : npc_escortAI(creature), m_questId(0) { Reset(); }

    uint32 m_questId;

    void Reset() override {}

    bool IsBrannock() const { return m_creature->GetEntry() == NPC_BRANNOCK; }

    void ReceiveAIEvent(AIEventType eventType, Unit* /*sender*/, Unit* invoker, uint32 questId) override
    {
        if (eventType != AI_EVENT_START_ESCORT || !invoker || invoker->GetTypeId() != TYPEID_PLAYER)
            return;

        m_creature->SetFactionTemporary(FACTION_ESCORT_A_NEUTRAL_ACTIVE, TEMPFACTION_RESTORE_RESPAWN | TEMPFACTION_TOGGLE_IMMUNE_TO_NPC);
        m_questId = questId;
        DoBroadcastText(IsBrannock() ? SAY_BRANNOCK_START : SAY_HARGUN_START, m_creature, invoker);
        Start(false, static_cast<Player*>(invoker), GetQuestTemplateStore(questId), true);
    }

    void WaypointReached(uint32 pointId) override
    {
        for (LgAmbush const& ambush : lgAmbushes)
        {
            if (ambush.escortEntry != m_creature->GetEntry() || ambush.pointId != pointId)
                continue;

            DoBroadcastText(IsBrannock() ? SAY_BRANNOCK_AMBUSH : SAY_HARGUN_AMBUSH, m_creature);
            for (uint32 attacker : {ambush.attacker1, ambush.attacker2})
            {
                float x, y, z;
                m_creature->GetNearPoint(m_creature, x, y, z, 0.0f, 15.0f, frand(0.0f, 2 * M_PI_F));
                if (Creature* add = m_creature->SummonCreature(attacker, x, y, z, 0.0f, TEMPSPAWN_TIMED_OOC_OR_DEAD_DESPAWN, 60000))
                    add->AI()->AttackStart(m_creature);
            }
        }

        if (pointId == (IsBrannock() ? BRANNOCK_LAST_POINT : HARGUN_LAST_POINT))
        {
            if (Player* player = GetPlayerForEscort())
            {
                DoBroadcastText(IsBrannock() ? SAY_BRANNOCK_END : SAY_HARGUN_END, m_creature, player);
                if (m_questId)
                    player->RewardPlayerAndGroupAtEventExplored(m_questId, m_creature);
            }
        }
    }

    void UpdateEscortAI(const uint32 /*diff*/) override
    {
        if (!m_creature->SelectHostileTarget() || !m_creature->GetVictim())
            return;

        DoMeleeAttackIfReady();
    }
};

UnitAI* GetAI_npc_lg_escort(Creature* creature)
{
    return new npc_lg_escortAI(creature);
}

bool QuestAccept_npc_lg_escort(Player* player, Creature* creature, const Quest* quest)
{
    if (quest->HasSpecialFlag(QUEST_SPECIAL_FLAG_EXPLORATION_OR_EVENT))
    {
        creature->AI()->SendAIEvent(AI_EVENT_START_ESCORT, player, creature, quest->GetQuestId());
        return true;
    }
    return false;
}

void AddSC_leveling_gap()
{
    Script* newScript = new Script;
    newScript->Name = "npc_lg_escort";
    newScript->GetAI = &GetAI_npc_lg_escort;
    newScript->pQuestAcceptNPC = &QuestAccept_npc_lg_escort;
    newScript->RegisterSelf();
}
