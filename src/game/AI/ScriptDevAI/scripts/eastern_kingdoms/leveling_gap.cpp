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
SDComment: Custom leveling-gap content (claude-projects/leveling-gaps). Captive rescues for quests 60602 and 60605 (Searing Gorge, hub 12).
           Texts are broadcast_text 600600-600605.
SDCategory: Searing Gorge
EndScriptData
*/

#include "AI/ScriptDevAI/include/sc_common.h"

/*######
## npc_lg_captive
##
## A chained captive who gives a rescue quest. On accept he breaks free and the Dark Irons rush him in one or two
## waves; he fights where he stands. When the last wave is dead he thanks the player, the quest completes (event
## happened) and he runs off and despawns. If he dies first, the quest fails. No walking route, so no pathing to go wrong.
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
};

struct LgWave
{
    uint32 captive;
    uint32 sayOnSpawn;                                      // 0 = none
    uint32 attackers[3];                                    // 0 = unused slot
};

static const LgWave lgWaves[] =
{
    {NPC_BRANNOCK, SAY_BRANNOCK_AMBUSH, {NPC_DARK_IRON_SLAVER, NPC_DARK_IRON_SLAVER, 0}},
    {NPC_HARGUN,   0,                   {NPC_DARK_IRON_SLAVER, NPC_DARK_IRON_SLAVER, 0}},
    {NPC_HARGUN,   SAY_HARGUN_AMBUSH,   {NPC_DARK_IRON_TASKMASTER, NPC_DARK_IRON_SLAVER, 0}},
};

enum LgCaptivePhase
{
    PHASE_IDLE      = 0,
    PHASE_FIGHT     = 1,                                    // a wave is coming or alive
    PHASE_FREED     = 2,                                    // thanked the player, about to run off
};

static const uint32 LG_WAVE_DELAY       = 3000;             // breaking the chain / catching breath between waves
static const uint32 LG_EVENT_TIMEOUT    = 5 * MINUTE * IN_MILLISECONDS;
static const float  LG_PLAYER_RANGE     = 60.0f;            // the rescuer must still be around for the credit

struct npc_lg_captiveAI : public ScriptedAI
{
    npc_lg_captiveAI(Creature* creature) : ScriptedAI(creature), m_phase(PHASE_IDLE), m_questId(0), m_wave(0), m_timer(0), m_eventTimer(0) { Reset(); }

    LgCaptivePhase m_phase;
    ObjectGuid m_playerGuid;
    uint32 m_questId;
    uint32 m_wave;                                          // index into this captive's waves of the next wave to send
    uint32 m_timer;
    uint32 m_eventTimer;
    GuidList m_attackers;

    // Reset runs on every evade, including after each wave; the rescue state lives on until it ends or the captive respawns
    void Reset() override {}

    bool IsBrannock() const { return m_creature->GetEntry() == NPC_BRANNOCK; }

    LgWave const* GetWave(uint32 index) const
    {
        uint32 n = 0;
        for (LgWave const& wave : lgWaves)
            if (wave.captive == m_creature->GetEntry() && n++ == index)
                return &wave;
        return nullptr;
    }

    void StartRescue(Player* player, uint32 questId)
    {
        if (m_phase != PHASE_IDLE)
            return;

        m_phase = PHASE_FIGHT;
        m_playerGuid = player->GetObjectGuid();
        m_questId = questId;
        m_wave = 0;
        m_timer = LG_WAVE_DELAY;
        m_eventTimer = LG_EVENT_TIMEOUT;
        m_creature->SetFactionTemporary(FACTION_ESCORT_A_NEUTRAL_ACTIVE, TEMPFACTION_RESTORE_RESPAWN | TEMPFACTION_TOGGLE_IMMUNE_TO_NPC);
        m_creature->RemoveFlag(UNIT_NPC_FLAGS, UNIT_NPC_FLAG_QUESTGIVER);
        DoBroadcastText(IsBrannock() ? SAY_BRANNOCK_START : SAY_HARGUN_START, m_creature, player);
    }

    void SendWave(LgWave const& wave)
    {
        if (wave.sayOnSpawn)
            DoBroadcastText(wave.sayOnSpawn, m_creature);

        for (uint32 attacker : wave.attackers)
        {
            if (!attacker)
                continue;
            float x, y, z;
            m_creature->GetNearPoint(m_creature, x, y, z, 0.0f, 12.0f, frand(0.0f, 2 * M_PI_F));
            if (Creature* add = m_creature->SummonCreature(attacker, x, y, z, 0.0f, TEMPSPAWN_TIMED_OOC_OR_DEAD_DESPAWN, 60000))
                add->AI()->AttackStart(m_creature);
        }
    }

    void JustSummoned(Creature* summoned) override
    {
        m_attackers.push_back(summoned->GetObjectGuid());
    }

    void SummonedCreatureJustDied(Creature* summoned) override { RemoveAttacker(summoned); }
    void SummonedCreatureDespawn(Creature* summoned) override { RemoveAttacker(summoned); }

    void RemoveAttacker(Creature* summoned)
    {
        m_attackers.remove(summoned->GetObjectGuid());
        if (m_phase == PHASE_FIGHT && m_attackers.empty() && !m_timer)
            m_timer = LG_WAVE_DELAY;                        // next wave, or the end, after a breather
    }

    void JustDied(Unit* /*killer*/) override
    {
        if (m_phase == PHASE_FIGHT)
            if (Player* player = m_creature->GetMap()->GetPlayer(m_playerGuid))
                player->FailQuest(m_questId);
        m_phase = PHASE_IDLE;
    }

    // The rescuer left or the fight stalled: call it off and chain him up again
    void AbortRescue()
    {
        for (ObjectGuid const& guid : m_attackers)
            if (Creature* add = m_creature->GetMap()->GetCreature(guid))
                add->ForcedDespawn();
        m_attackers.clear();
        if (Player* player = m_creature->GetMap()->GetPlayer(m_playerGuid))
            player->FailQuest(m_questId);
        m_creature->ForcedDespawn(1000);                    // respawns chained at his post
        m_phase = PHASE_IDLE;
    }

    void Finish()
    {
        Player* player = m_creature->GetMap()->GetPlayer(m_playerGuid);
        if (!player || !player->IsAlive() || !player->IsWithinDistInMap(m_creature, LG_PLAYER_RANGE))
        {
            AbortRescue();
            return;
        }

        m_phase = PHASE_FREED;
        DoBroadcastText(IsBrannock() ? SAY_BRANNOCK_END : SAY_HARGUN_END, m_creature, player);
        player->RewardPlayerAndGroupAtEventExplored(m_questId, m_creature);
        m_timer = 4000;                                     // let the line be read, then run off
    }

    void UpdateAI(const uint32 diff) override
    {
        if (m_phase == PHASE_FIGHT)
        {
            if (m_eventTimer <= diff)
            {
                AbortRescue();
                return;
            }
            m_eventTimer -= diff;

            if (m_timer && m_attackers.empty())
            {
                if (m_timer <= diff)
                {
                    m_timer = 0;
                    if (LgWave const* wave = GetWave(m_wave++))
                        SendWave(*wave);
                    else
                        Finish();
                }
                else
                    m_timer -= diff;
            }
        }
        else if (m_phase == PHASE_FREED && m_timer)
        {
            if (m_timer <= diff)
            {
                m_timer = 0;
                float x, y, z;
                m_creature->GetNearPoint(m_creature, x, y, z, 0.0f, 20.0f, frand(0.0f, 2 * M_PI_F));
                m_creature->SetWalk(false);
                m_creature->GetMotionMaster()->MovePoint(1, x, y, z);
                m_creature->ForcedDespawn(4000);            // gone; respawns chained after his spawn's respawn time
            }
            else
                m_timer -= diff;
        }

        if (!m_creature->SelectHostileTarget() || !m_creature->GetVictim())
            return;

        DoMeleeAttackIfReady();
    }
};

UnitAI* GetAI_npc_lg_captive(Creature* creature)
{
    return new npc_lg_captiveAI(creature);
}

bool QuestAccept_npc_lg_captive(Player* player, Creature* creature, const Quest* quest)
{
    if (quest->HasSpecialFlag(QUEST_SPECIAL_FLAG_EXPLORATION_OR_EVENT))
    {
        if (npc_lg_captiveAI* ai = dynamic_cast<npc_lg_captiveAI*>(creature->AI()))
            ai->StartRescue(player, quest->GetQuestId());
        return true;
    }
    return false;
}

void AddSC_leveling_gap()
{
    Script* newScript = new Script;
    newScript->Name = "npc_lg_captive";
    newScript->GetAI = &GetAI_npc_lg_captive;
    newScript->pQuestAcceptNPC = &QuestAccept_npc_lg_captive;
    newScript->RegisterSelf();
}
