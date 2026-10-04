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
SDName: Boss_Ayamiss
SD%Complete: 95
SDComment: Swarmers need movement added for RP before they attack
SDCategory: Ruins of Ahn'Qiraj
EndScriptData

*/

#include "AI/ScriptDevAI/include/sc_common.h"
#include "ruins_of_ahnqiraj.h"
#include "AI/ScriptDevAI/base/CombatAI.h"
#include "AI/ScriptDevAI/include/sc_solo_scaling.h"
#include "Spells/Scripts/SpellScript.h"
#include "MotionGenerators/WaypointManager.h"
#include <G3D/Vector3.h>

enum
{
    EMOTE_GENERIC_FRENZY    = -1000002,

    SPELL_STINGER_SPRAY     = 25749,
    SPELL_POISON_STINGER    = 25748,                // only used in phase1
    SPELL_SUMMON_SWARMER    = 25708,
    SPELL_PARALYZE          = 25725,
    SPELL_LASH              = 25852,
    SPELL_FRENZY            = 8269,
    SPELL_THRASH            = 3391,

    SPELL_FEED              = 25721,                // cast by the Larva when reaches the player on the altar

    SPELL_SWARMER_TELEPORT_1 = 25709,
    SPELL_SWARMER_TELEPORT_2 = 25825,
    SPELL_SWARMER_TELEPORT_3 = 25826,
    SPELL_SWARMER_TELEPORT_4 = 25827,
    SPELL_SWARMER_TELEPORT_5 = 25828,

    SPELL_SUMMON_LARVA_1     = 26538,
    SPELL_SUMMON_LARVA_2     = 26539,

    NPC_LARVA               = 15555,
    NPC_SWARMER             = 15546,
    NPC_HORNET              = 15934,

    // Fork: solo scaling. Paralyze every 60 s solo down to 15 s at 20 players;
    // the larva's Feed deals 25% of max HP solo up to an instakill at 20 players.
    AYAMISS_RAID_SIZE       = 20,

    PHASE_AIR               = 0,
    PHASE_GROUND            = 1,

    POINT_AIR               = 2,
    POINT_GROUND            = 3,
};

struct SummonLocation
{
    float m_fX, m_fY, m_fZ;
};

// Spawn locations
static const SummonLocation aAyamissSpawnLocs[] =
{
    { -9647.352f, 1578.062f, 55.32f},           // anchor point for swarmers
    { -9717.18f, 1517.72f, 27.4677f},           // teleport location - need to be hardcoded because the player is teleported after the larva is summoned
};

enum AyamissActions
{
    AYAMISS_FLY_UP,
    AYAMISS_PHASE_2,
    AYAMISS_FRENZY,
    AYAMISS_PARALYZE,
    AYAMISS_STINGER_SPRAY,
    AYAMISS_POISON_STINGER,
    AYAMISS_SWARMER_ATTACK,
    AYAMISS_SUMMON_SWARMER,
    AYAMISS_LASH,
    AYAMISS_THRASH,
    AYAMISS_ACTION_MAX,
};

struct boss_ayamissAI : public CombatAI
{
    boss_ayamissAI(Creature* creature) :
        CombatAI(creature, AYAMISS_ACTION_MAX),
        m_phase(0)
    {
        AddTimerlessCombatAction(AYAMISS_FLY_UP, true);
        AddTimerlessCombatAction(AYAMISS_PHASE_2, true);
        AddTimerlessCombatAction(AYAMISS_FRENZY, true);
        AddCombatAction(AYAMISS_PARALYZE, 15000u);
        AddCombatAction(AYAMISS_STINGER_SPRAY, 20000, 30000);
        AddCombatAction(AYAMISS_POISON_STINGER, 5000u);
        AddCombatAction(AYAMISS_SUMMON_SWARMER, 5000u);
        AddCombatAction(AYAMISS_SWARMER_ATTACK, 60000u);
        AddCombatAction(AYAMISS_LASH, true);
        AddCombatAction(AYAMISS_THRASH, true);
        m_creature->SetWalk(false);
    }

    uint8 m_phase;

    ObjectGuid m_paralyzeTarget;
    GuidList m_swarmersGuidList;
    GuidVector m_spawns;

    void Reset() override
    {
        CombatAI::Reset();
        m_phase = PHASE_AIR;

        SetCombatMovement(false);
        SetMeleeEnabled(false);
        SetCombatScriptStatus(false);

        DespawnGuids(m_spawns);
    }

    void EnterEvadeMode() override
    {
        m_creature->SetImmobilizedState(false);
        CombatAI::EnterEvadeMode();
    }

    void JustSummoned(Creature* summoned) override
    {
        // store the swarmers for a future attack
        if (summoned->GetEntry() == NPC_SWARMER)
        {
            m_swarmersGuidList.push_back(summoned->GetObjectGuid());
            uint32 spellId = 0;
            switch (urand(0, 4))
            {
                case 0: spellId = SPELL_SWARMER_TELEPORT_1; break;
                case 1: spellId = SPELL_SWARMER_TELEPORT_2; break;
                case 2: spellId = SPELL_SWARMER_TELEPORT_3; break;
                case 3: spellId = SPELL_SWARMER_TELEPORT_4; break;
                case 4: spellId = SPELL_SWARMER_TELEPORT_5; break;
                default: break;
            }
            summoned->CastSpell(nullptr, spellId, TRIGGERED_OLD_TRIGGERED);
        }
        // move the larva to paralyze target position
        else if (summoned->GetEntry() == NPC_LARVA)
        {
            summoned->SetCorpseDelay(5);
            summoned->SetWalk(false);
            if (Unit* target = m_creature->GetMap()->GetUnit(m_paralyzeTarget))
            {
                summoned->AI()->AttackStart(target);
                summoned->FixateTarget(target);
            }
        }
        else if (summoned->GetEntry() == NPC_HORNET)
        {
            if (Unit* target = m_creature->SelectAttackingTarget(ATTACKING_TARGET_RANDOM, 0))
                summoned->AI()->AttackStart(target);
        }
        m_spawns.push_back(summoned->GetObjectGuid());
    }

    void MovementInform(uint32 motionType, uint32 pointId) override
    {
        if (motionType != POINT_MOTION_TYPE)
        {
            if (motionType == WAYPOINT_MOTION_TYPE)
            {
                if (pointId == POINT_GROUND)
                {
                    SetCombatScriptStatus(false);
                    SetCombatMovement(true, true);
                }
            }
            return;
        }

        if (pointId == POINT_AIR)
        {
            SetCombatScriptStatus(false);
            m_creature->SetImmobilizedState(true);
        }
    }

    void StartLanding()
    {
        m_creature->GetMotionMaster()->MovePath(1, PATH_FROM_ENTRY, FORCED_MOVEMENT_NONE, true);
    }

    void ExecuteAction(uint32 action) override
    {
        switch (action)
        {
            case AYAMISS_FLY_UP:
            {
                // Fork: no hover. Phase 1 stays on the ground at her spawn (still rooted, no melee of her own)
                // so melee classes can reach her; phase 2 starts from there without the landing path.
                SetActionReadyStatus(action, false);
                break;
            }
            case AYAMISS_PHASE_2:
            {
                if (m_creature->GetHealthPercent() <= 70.0f)
                {
                    m_phase = PHASE_GROUND;
                    m_creature->SetImmobilizedState(false);
                    SetMeleeEnabled(true);
                    m_creature->SetLevitate(false);
                    m_creature->SetHover(false);
                    DoResetThreat();
                    SetCombatMovement(true, true);      // fork: already on the ground, no landing path
                    SetActionReadyStatus(action, false);

                    DisableCombatAction(AYAMISS_POISON_STINGER);
                    ResetCombatAction(AYAMISS_LASH, urand(5000, 8000));
                    ResetCombatAction(AYAMISS_THRASH, urand(3000, 6000));
                }
                break;
            }
            case AYAMISS_FRENZY:
            {
                if (m_creature->GetHealthPercent() < 20.0f)
                {
                    if (DoCastSpellIfCan(nullptr, SPELL_FRENZY) == CAST_OK)
                    {
                        DoScriptText(EMOTE_GENERIC_FRENZY, m_creature);
                        SetActionReadyStatus(action, false);
                    }
                }
                break;
            }
            case AYAMISS_PARALYZE:
            {
                Unit* target = m_creature->SelectAttackingTarget(ATTACKING_TARGET_RANDOM, 1, SPELL_PARALYZE, SELECT_FLAG_PLAYER);
                if (!target)
                    target = m_creature->SelectAttackingTarget(ATTACKING_TARGET_RANDOM, 0, SPELL_PARALYZE, SELECT_FLAG_PLAYER);

                if (DoCastSpellIfCan(target, SPELL_PARALYZE) == CAST_OK)
                {
                    m_paralyzeTarget = target->GetObjectGuid();
                    ResetCombatAction(action, uint32(ScaleByPlayerCount(m_creature->GetMap(), AYAMISS_RAID_SIZE, 60000.0f, 15000.0f)));

                    // Summon a larva
                    uint32 spellId = urand(0, 1) ? SPELL_SUMMON_LARVA_1 : SPELL_SUMMON_LARVA_2;
                    m_creature->CastSpell(nullptr, spellId, TRIGGERED_OLD_TRIGGERED);
                }
                break;
            }
            case AYAMISS_STINGER_SPRAY:
            {
                if (DoCastSpellIfCan(nullptr, SPELL_STINGER_SPRAY) == CAST_OK)
                    ResetCombatAction(action, urand(15000, 20000));
                break;
            }
            case AYAMISS_POISON_STINGER:
            {
                if (Unit* target = m_creature->SelectAttackingTarget(ATTACKING_TARGET_TOPAGGRO, 0, SPELL_POISON_STINGER, SELECT_FLAG_PLAYER))
                    if (DoCastSpellIfCan(target, SPELL_POISON_STINGER) == CAST_OK)
                        ResetCombatAction(action, m_phase == PHASE_AIR ? urand(2000, 3000) : urand(6000, 12000));
                break;
            }
            case AYAMISS_SUMMON_SWARMER:
            {
                if (DoCastSpellIfCan(nullptr, SPELL_SUMMON_SWARMER) == CAST_OK)
                    ResetCombatAction(action, 5000);
                break;
            }
            case AYAMISS_SWARMER_ATTACK:
            {
                for (auto guid : m_swarmersGuidList)
                {
                    if (Creature* temp = m_creature->GetMap()->GetCreature(guid))
                    {
                        if (Unit* target = m_creature->SelectAttackingTarget(ATTACKING_TARGET_RANDOM, 0, nullptr, SELECT_FLAG_PLAYER))
                            temp->AI()->AttackStart(target);
                    }
                }
                m_swarmersGuidList.clear();
                ResetCombatAction(action, 60000);
                break;
            }
            case AYAMISS_LASH:
            {
                if (DoCastSpellIfCan(m_creature->GetVictim(), SPELL_LASH) == CAST_OK)
                    ResetCombatAction(action, urand(8000, 15000));
                break;
            }
            case AYAMISS_THRASH:
            {
                if (DoCastSpellIfCan(m_creature->GetVictim(), SPELL_THRASH) == CAST_OK)
                    ResetCombatAction(action, urand(5000, 7000));
                break;
            }
        }
    }
};

struct npc_hive_zara_larvaAI : public ScriptedAI
{
    npc_hive_zara_larvaAI(Creature* creature) : ScriptedAI(creature), m_instance(static_cast<instance_ruins_of_ahnqiraj*>(m_creature->GetInstanceData()))
    {
        if (m_instance)
            if (m_instance->GetData(TYPE_AYAMISS) == IN_PROGRESS)
                SetReactState(REACT_DEFENSIVE);
    }

    instance_ruins_of_ahnqiraj* m_instance;

    void Reset() override { }

    void MoveInLineOfSight(Unit* who) override
    {
        // don't attack anything during the Ayamiss encounter
        if (m_instance)
        {
            if (m_instance->GetData(TYPE_AYAMISS) == IN_PROGRESS)
            {
                if (m_creature->CanReachWithMeleeAttack(who) && who == m_creature->GetVictim())
                {
                    // Fork: below a full raid, Feed hits for a share of max HP instead of killing (see LarvaFeed)
                    if (GetEncounterPlayerCount(m_creature->GetMap()) < AYAMISS_RAID_SIZE)
                    {
                        float fraction = ScaleByPlayerCount(m_creature->GetMap(), AYAMISS_RAID_SIZE, 0.25f, 1.0f);
                        uint32 damage = std::min(uint32(who->GetMaxHealth() * fraction), who->GetHealth());
                        Unit::SendSpellNonMeleeDamageLog(m_creature, who, SPELL_FEED, damage, SPELL_SCHOOL_MASK_NORMAL, 0, 0, false, 0);
                        Unit::DealDamage(m_creature, who, damage, nullptr, DIRECT_DAMAGE, SPELL_SCHOOL_MASK_NORMAL, nullptr, false);
                    }
                    m_creature->CastSpell(who, SPELL_FEED, TRIGGERED_OLD_TRIGGERED, nullptr, nullptr, m_creature->GetObjectGuid());
                }
                return;
            }
        }

        ScriptedAI::MoveInLineOfSight(who);
    }

    void UpdateAI(const uint32 /*uiDiff*/) override
    {
        if (m_instance)
        {
            if (m_instance->GetData(TYPE_AYAMISS) == IN_PROGRESS)
                return;
        }

        if (!m_creature->SelectHostileTarget() || !m_creature->GetVictim())
            return;

        DoMeleeAttackIfReady();
    }
};

// 25721 - Feed
// Fork: effect 1 is an instakill on the fed player; drop the player from it below a full raid
// (the larva already dealt scaled damage). The hornet summon and the larva's own death still happen.
struct LarvaFeed : public SpellScript
{
    bool OnCheckTarget(const Spell* spell, Unit* target, SpellEffectIndex effIdx) const override
    {
        if (effIdx == EFFECT_INDEX_0 && target->GetTypeId() == TYPEID_PLAYER)
            return GetEncounterPlayerCount(spell->GetCaster()->GetMap()) >= AYAMISS_RAID_SIZE;
        return true;
    }
};

void AddSC_boss_ayamiss()
{
    Script* pNewScript = new Script;
    pNewScript->Name = "boss_ayamiss";
    pNewScript->GetAI = &GetNewAIInstance<boss_ayamissAI>;
    pNewScript->RegisterSelf();

    pNewScript = new Script;
    pNewScript->Name = "npc_hive_zara_larva";
    pNewScript->GetAI = &GetNewAIInstance<npc_hive_zara_larvaAI>;
    pNewScript->RegisterSelf();
    RegisterSpellScript<LarvaFeed>("spell_ayamiss_larva_feed");
}
