/*
* This file is part of the CMaNGOS Project. See AUTHORS file for Copyright information
*
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

#include "Spells/Scripts/SpellScript.h"
#include "Spells/SpellAuras.h"
#include "Spells/SpellMgr.h"

struct SealOfTheCrusader : public AuraScript
{
    void OnApply(Aura* aura, bool apply) const override
    {
        if (aura->GetEffIndex() != EFFECT_INDEX_1)
            return;

        // Seal of the Crusader damage reduction
        // SotC increases attack speed but reduces damage to maintain the same DPS
        // custom: restored 2026-10-02 as the only penalty (the base weapon DPS one in Aura::HandleAuraDummy stays off; upstream runs both)
        float reduction = (-100.0f * aura->GetModifier()->m_amount) / (aura->GetModifier()->m_amount + 100.0f);
        aura->GetTarget()->HandleStatModifier(UNIT_MOD_DAMAGE_MAINHAND, TOTAL_PCT, reduction, apply);
    }

    // custom: burst seal (20 s, 1 min shared cooldown via Category 1200) - every landed melee hit (white swings and Crusader Strike,
    // not Reckoning or other extra attacks) deals rank Holy damage + 10% of Holy spell power (34143), halved against players.
    // The proc runs on the seal's judgement dummy (effect 3); the attack power and haste effects don't proc.
    bool OnCheckProc(Aura* aura, ProcExecutionData& /*data*/) const override
    {
        return aura->GetEffIndex() == EFFECT_INDEX_2;
    }

    SpellAuraProcResult OnProc(Aura* aura, ProcExecutionData& data) const override
    {
        Unit* caster = aura->GetTarget();
        Unit* victim = data.target;
        if (!victim || !victim->IsAlive() || victim == caster || caster->IsExecutingExtraAttacks())
            return SPELL_AURA_PROC_CANT_TRIGGER;

        int32 base;
        switch (aura->GetId())
        {
            case 21082: base = 48;  break; // Rank 1
            case 20162: base = 80;  break; // Rank 2
            case 20305: base = 147; break; // Rank 3
            case 20306: base = 227; break; // Rank 4
            case 20307: base = 346; break; // Rank 5
            case 20308: base = 477; break; // Rank 6
            default: return SPELL_AURA_PROC_CANT_TRIGGER;
        }
        int32 damage = base + int32(caster->SpellBaseDamageBonusDone(SPELL_SCHOOL_MASK_HOLY) * 0.10f);
        if (victim->GetTypeId() == TYPEID_PLAYER)
            damage /= 2;
        caster->CastCustomSpell(victim, 34143, &damage, nullptr, nullptr, TRIGGERED_OLD_TRIGGERED);
        return SPELL_AURA_PROC_CANT_TRIGGER; // not the generic seal dummy handler
    }
};

// 20164 - Seal of Justice (custom PvP rework): every landed melee hit slows the target by 50% for 5 s (34141); the 2 s stun (20170)
// keeps its 5 procs per minute and 3 s lockout, rolled here because procs-per-minute would otherwise gate the slow too
struct SealOfJustice : public AuraScript
{
    bool OnCheckProc(Aura* aura, ProcExecutionData& /*data*/) const override
    {
        return aura->GetEffIndex() == EFFECT_INDEX_0;
    }

    SpellAuraProcResult OnProc(Aura* aura, ProcExecutionData& data) const override
    {
        Unit* caster = aura->GetTarget();
        Unit* victim = data.target;
        if (!victim || !victim->IsAlive() || victim == caster)
            return SPELL_AURA_PROC_CANT_TRIGGER;

        caster->CastSpell(victim, 34141, TRIGGERED_OLD_TRIGGERED);

        SpellAuraHolder* holder = aura->GetHolder();
        TimePoint now = caster->GetMap()->GetCurrentClockTime();
        if (holder->IsProcReady(now) && roll_chance_f(caster->GetPPMProcChance(caster->GetAttackTime(data.attType), 5.0f)))
        {
            caster->CastSpell(victim, 20170, TRIGGERED_OLD_TRIGGERED);
            holder->SetProcCooldown(std::chrono::milliseconds(3000), now);
        }
        return SPELL_AURA_PROC_CANT_TRIGGER;
    }
};

// 5373 - Judgement of Light Intermediate
struct JudgementOfLightIntermediate : public SpellScript
{
    void OnEffectExecute(Spell* spell, SpellEffectIndex /*effIdx*/) const override
    {
        if (spell->GetTriggeredByAuraSpellInfo() == nullptr)
            return;

        uint32 triggerSpell = 0;
        switch (spell->GetTriggeredByAuraSpellInfo()->Id)
        {
            case 20185: triggerSpell = 20267; break; // Rank 1
            case 20344: triggerSpell = 20341; break; // Rank 2
            case 20345: triggerSpell = 20342; break; // Rank 3
            case 20346: triggerSpell = 20343; break; // Rank 4
        }
        if (triggerSpell)
            spell->GetUnitTarget()->CastSpell(nullptr, triggerSpell, TRIGGERED_IGNORE_GCD | TRIGGERED_IGNORE_CURRENT_CASTED_SPELL | TRIGGERED_HIDE_CAST_IN_COMBAT_LOG);
    }
};

// 1826 - Judgement of Wisdom Intermediate
struct JudgementOfWisdomIntermediate : public SpellScript
{
    void OnEffectExecute(Spell* spell, SpellEffectIndex /*effIdx*/) const override
    {
        if (spell->GetTriggeredByAuraSpellInfo() == nullptr)
            return;

        uint32 triggerSpell = 0;
        switch (spell->GetTriggeredByAuraSpellInfo()->Id)
        {
            case 20186: triggerSpell = 20268; break; // Rank 1
            case 20354: triggerSpell = 20352; break; // Rank 2
            case 20355: triggerSpell = 20353; break; // Rank 3
        }
        if (triggerSpell)
            spell->GetUnitTarget()->CastSpell(nullptr, triggerSpell, TRIGGERED_IGNORE_GCD | TRIGGERED_IGNORE_CURRENT_CASTED_SPELL | TRIGGERED_HIDE_CAST_IN_COMBAT_LOG);
    }
};

struct spell_judgement : public SpellScript
{
    void OnEffectExecute(Spell* spell, SpellEffectIndex effIdx) const override
    {
        Unit* unitTarget = spell->GetUnitTarget();
        if (!unitTarget || !unitTarget->IsAlive())
            return;

        Unit* caster = spell->GetCaster();

        if (!unitTarget || !unitTarget->IsAlive())
            return;

        uint32 spellId2 = 0;

        // all seals have aura dummy
        Unit::AuraList const& m_dummyAuras = caster->GetAurasByType(SPELL_AURA_DUMMY);
        for (Unit::AuraList::const_iterator itr = m_dummyAuras.begin(); itr != m_dummyAuras.end(); ++itr)
        {
            SpellEntry const* spellInfo = (*itr)->GetSpellProto();

            // search seal (all seals have judgement's aura dummy spell id in 2 effect
            if (!spellInfo || !IsSealSpell((*itr)->GetSpellProto()) || (*itr)->GetEffIndex() != 2)
                continue;

            // must be calculated base at raw base points in spell proto, GetModifier()->m_value for S.Righteousness modified by SPELLMOD_DAMAGE
            spellId2 = (*itr)->GetSpellProto()->CalculateSimpleValue(EFFECT_INDEX_2);

            if (spellId2 <= 1)
                continue;

            // found, remove seal
            // custom: the Seal of the Crusader burst window survives its judgement (as all judgements do in Wrath)
            switch ((*itr)->GetId())
            {
                case 21082: case 20162: case 20305: case 20306: case 20307: case 20308:
                    break;
                default:
                    caster->RemoveAurasDueToSpell((*itr)->GetId());
                    break;
            }

            break;
        }

        caster->CastSpell(unitTarget, spellId2, TRIGGERED_IGNORE_GCD | TRIGGERED_IGNORE_CURRENT_CASTED_SPELL | TRIGGERED_IGNORE_HIT_CALCULATION);

        // custom: Lasting Judgement reworked into Judgements of the Pure - judging grants 3/6/9% attack speed and spell haste for 1 min
        uint32 pure = 0;
        if (caster->HasAura(20361))      // Rank 3
            pure = 34138;
        else if (caster->HasAura(20360)) // Rank 2
            pure = 34137;
        else if (caster->HasAura(20359)) // Rank 1
            pure = 34136;
        if (pure && spellId2 > 1) // only when a seal was actually judged
            caster->CastSpell(caster, pure, TRIGGERED_OLD_TRIGGERED);

        // custom: PvP rework - Judgement of the Crusader also cuts healing received by 25% (34144, refreshed by melee hits like the judgement)
        // custom: Judgement of Justice's three effects are now its -20% attack/ranged/casting speed slows, so its Replenishment and
        // interrupt (formerly effects 2 and 3) are cast here
        switch (spellId2)
        {
            case 20184:
                caster->CastSpell(caster, 34000, TRIGGERED_OLD_TRIGGERED);
                caster->CastSpell(unitTarget, 34142, TRIGGERED_IGNORE_GCD | TRIGGERED_IGNORE_CURRENT_CASTED_SPELL | TRIGGERED_IGNORE_HIT_CALCULATION);
                break;
            case 21183: case 20188: case 20300: case 20301: case 20302: case 20303:
                caster->CastSpell(unitTarget, 34144, TRIGGERED_IGNORE_GCD | TRIGGERED_IGNORE_CURRENT_CASTED_SPELL | TRIGGERED_IGNORE_HIT_CALCULATION);
                break;
            default:
                break;
        }
    }
};

// 19977 - Blessing of Light
struct BlessingOfLight : public AuraScript
{
    void OnApply(Aura* aura, bool apply) const override
    {
        aura->GetTarget()->RegisterScriptedLocationAura(aura, SCRIPT_LOCATION_SPELL_HEALING_TAKEN, apply);
    }

    void OnDamageCalculate(Aura* aura, Unit* attacker, Unit* /*victim*/, int32& advertisedBenefit, float& totalMod) const override
    {
        advertisedBenefit += (aura->GetModifier()->m_amount);  // BoL is penalized since 2.3.0
        // Note: This forces the caster to keep libram equipped, but works regardless if the BOL is his or not
        if (Aura* improved = attacker->GetAura(38320, EFFECT_INDEX_0)) // improved Blessing of light
        {
            if (aura->GetEffIndex() == EFFECT_INDEX_0)
                advertisedBenefit += improved->GetModifier()->m_amount; // holy light gets full amount
            else
                advertisedBenefit += (improved->GetModifier()->m_amount / 2); // flash of light gets half
        }
    }
};

// 19752 - Divine Intervention
struct DivineIntervention : public SpellScript
{
    SpellCastResult OnCheckCast(Spell* spell, bool /*strict*/) const override
    {
        Unit* target = spell->m_targets.getUnitTarget();
        if (!target)
            return SPELL_FAILED_BAD_IMPLICIT_TARGETS;
        if (target->HasAura(23333) || target->HasAura(23335) || target->HasAura(34976)) // possibly SPELL_ATTR_EX_IMMUNITY_TO_HOSTILE_AND_FRIENDLY_EFFECTS
            return SPELL_FAILED_TARGET_AURASTATE;
        return SPELL_CAST_OK;
    }
};

// 20467, 20963, 20964, 20965, 20966, 27171 - Judgement of Command
struct JudgementOfCommand : public SpellScript
{
    void OnEffectExecute(Spell* spell, SpellEffectIndex /*effIdx*/) const override
    {
        if (!spell->GetUnitTarget()->IsStunned())
            spell->SetDamage(uint32(spell->GetDamage() / 2));
    }
};

// 20091, 20092 - Improved Retribution Aura (custom): every 2 sec in combat, the paladin's own
// Retribution Aura also deals its damage as Holy to all enemies within 8 yd (34131)
struct ImprovedRetributionAura : public AuraScript
{
    void OnPeriodicTrigger(Aura* aura, PeriodicTriggerData& data) const override
    {
        data.spellInfo = nullptr; // cast below with the aura's damage as basepoints

        Unit* target = aura->GetTarget();
        if (!target->IsAlive() || !target->IsInCombat())
            return;

        for (Aura* shield : target->GetAurasByType(SPELL_AURA_DAMAGE_SHIELD))
        {
            if (shield->GetCasterGuid() != target->GetObjectGuid() || !shield->GetSpellProto()->IsFitToFamily(SPELLFAMILY_PALADIN, uint64(0x0000000000000008)))
                continue;

            int32 damage = shield->GetModifier()->m_amount; // already includes this talent's +25/50%
            target->CastCustomSpell(nullptr, 34131, &damage, nullptr, nullptr, TRIGGERED_OLD_TRIGGERED);
            return;
        }
    }
};

// 25780 - Righteous Fury: Improved Righteous Fury (custom, TBC) fills effect 2 with 2/4/6% damage taken reduction
struct RighteousFury : public AuraScript
{
    int32 OnAuraValueCalculate(AuraCalcData& data, int32 value) const override
    {
        if (data.effIdx != EFFECT_INDEX_1 || !data.caster)
            return value;

        if (data.caster->HasAura(20470)) // Rank 3
            return -6;
        if (data.caster->HasAura(20469)) // Rank 2
            return -4;
        if (data.caster->HasAura(20468)) // Rank 1
            return -2;
        return 0;
    }
};

// 633, 2800, 10310 - Lay on Hands: Improved Lay on Hands (custom) refunds 50/100% of the drained mana
struct LayOnHands : public SpellScript
{
    void OnSuccessfulFinish(Spell* spell) const override
    {
        Unit* caster = spell->GetCaster();
        uint32 pct = 0;
        if (caster->HasAura(20235))      // Rank 2
            pct = 100;
        else if (caster->HasAura(20234)) // Rank 1
            pct = 50;

        if (uint32 refund = spell->GetPowerCost() * pct / 100)
            caster->EnergizeBySpell(caster, spell->m_spellInfo, refund, POWER_MANA);
    }
};

// 20224, 20225, 20330, 20331, 20332 - Improved Seal of Righteousness (custom): effect 2 procs one
// Seal of Righteousness hit when Crusader Strike, Exorcism, Holy Shock, Holy Wrath or Hammer of Wrath hits
struct ImprovedSealOfRighteousness : public AuraScript
{
    bool OnCheckProc(Aura* aura, ProcExecutionData& data) const override
    {
        if (aura->GetEffIndex() != EFFECT_INDEX_1 || !data.spellInfo)
            return false;

        switch (data.spellInfo->Id)
        {
            case 2537: case 8823: case 8824: case 10336: case 10337:        // Crusader Strike
            case 879: case 5614: case 5615: case 10312: case 10313: case 10314: // Exorcism
            case 25912: case 25911: case 25902:                            // Holy Shock (damage)
            case 2812: case 10318:                                         // Holy Wrath
            case 24275: case 24274: case 24239:                            // Hammer of Wrath
                return true;
            default:
                return false;
        }
    }

    SpellAuraProcResult OnProc(Aura* aura, ProcExecutionData& data) const override
    {
        Unit* caster = aura->GetTarget();
        for (Aura* seal : caster->GetAurasByType(SPELL_AURA_DUMMY))
        {
            if (seal->GetEffIndex() == EFFECT_INDEX_0 && seal->GetSpellProto()->IsFitToFamily(SPELLFAMILY_PALADIN, uint64(0x0000000008000000)))
            {
                caster->CastSealOfRighteousnessProc(seal, data.target);
                break;
            }
        }
        return SPELL_AURA_PROC_CANT_TRIGGER; // done here, skip the generic dummy handler
    }
};

// 34132, 34133 - Sheath of Light (custom trained passive): effect 1 is Holy spell damage equal to
// effect 2's percent (10/20) of attack power, recalculated on heartbeat as attack power changes
struct SheathOfLight : public AuraScript
{
    static int32 Calculate(Unit* target, SpellEntry const* spellProto)
    {
        if (spellProto->Id == 34132 && target->HasAura(34133)) // rank 2 supersedes rank 1
            return 0;
        int32 pct = spellProto->CalculateSimpleValue(EFFECT_INDEX_1);
        return int32(target->GetTotalAttackPowerValue(BASE_ATTACK) * pct / 100);
    }

    int32 OnAuraValueCalculate(AuraCalcData& data, int32 value) const override
    {
        if (data.effIdx != EFFECT_INDEX_0 || !data.target)
            return value;
        return Calculate(data.target, data.spellProto);
    }

    void OnHeartbeat(Aura* aura) const override
    {
        if (aura->GetEffIndex() != EFFECT_INDEX_0)
            return;
        int32 amount = Calculate(aura->GetTarget(), aura->GetSpellProto());
        if (amount == aura->GetModifier()->m_amount)
            return;
        aura->ApplyModifier(false, true);
        aura->GetModifier()->m_amount = amount;
        aura->ApplyModifier(true, true);
    }
};

// custom: Improved Devotion Aura - you keep 20/40/60/80/100% of your improved Devotion Aura's armor while running another aura
struct ImprovedDevotionAura : public AuraScript
{
    static int32 Calculate(Unit* target, SpellEntry const* spellProto)
    {
        static const uint32 devotionRanks[] = { 10293, 10292, 1032, 10291, 643, 10290, 465 }; // highest first
        if (target->GetTypeId() != TYPEID_PLAYER)
            return 0;
        for (uint32 id : devotionRanks)
            if (target->HasAura(id)) // Devotion Aura itself is up: no double dipping
                return 0;
        int32 pct = spellProto->CalculateSimpleValue(EFFECT_INDEX_0); // 5-25% Devotion Aura bonus
        for (uint32 id : devotionRanks)
        {
            if (!static_cast<Player*>(target)->HasSpell(id))
                continue;
            SpellEntry const* devotion = sSpellTemplate.LookupEntry<SpellEntry>(id);
            if (!devotion)
                return 0;
            int32 armor = devotion->CalculateSimpleValue(EFFECT_INDEX_0) * (100 + pct) / 100;
            return armor * (pct * 4) / 100; // rank 1-5 = 20-100%
        }
        return 0;
    }

    int32 OnAuraValueCalculate(AuraCalcData& data, int32 value) const override
    {
        if (data.effIdx != EFFECT_INDEX_1 || !data.target)
            return value;
        return Calculate(data.target, data.spellProto);
    }

    void OnHeartbeat(Aura* aura) const override
    {
        if (aura->GetEffIndex() != EFFECT_INDEX_1)
            return;
        int32 amount = Calculate(aura->GetTarget(), aura->GetSpellProto());
        if (amount == aura->GetModifier()->m_amount)
            return;
        aura->ApplyModifier(false, true);
        aura->GetModifier()->m_amount = amount;
        aura->ApplyModifier(true, true);
    }
};

// custom: Redoubt reworked into Ardent Defender - 6-30% less damage taken below 35% health (passive absorb, never used up).
// Rank 5 also survives a killing blow and ends at 30% health, once every 2 min (debuff 34140).
struct ArdentDefender : public AuraScript
{
    void OnAbsorb(Aura* aura, int32& currentAbsorb, int32& remainingDamage, uint32& /*reflectedSpellId*/, int32& /*reflectDamage*/, bool& preventedDeath, bool& /*dropCharge*/, DamageEffectType /*damageType*/) const override
    {
        Unit* target = aura->GetTarget();
        currentAbsorb = 0;
        if (target->GetHealth() * 100 < target->GetMaxHealth() * 35)
            currentAbsorb = remainingDamage * aura->GetModifier()->m_amount / 100;
        if (aura->GetId() == 20137 && !target->HasAura(34140))
            preventedDeath = true;
    }

    void OnAuraDeathPrevention(Aura* aura, int32& remainingDamage) const override
    {
        Unit* target = aura->GetTarget();
        uint32 health = target->GetHealth();
        uint32 floor = target->GetMaxHealth() * 30 / 100;
        remainingDamage = health > floor ? int32(health - floor) : 0;
        target->CastSpell(target, 34140, TRIGGERED_OLD_TRIGGERED);
        if (health < floor)
            target->DealHeal(target, floor - health, sSpellTemplate.LookupEntry<SpellEntry>(34140));
    }
};

void LoadPaladinScripts()
{
    RegisterSpellScript<JudgementOfLightIntermediate>("spell_judgement_of_light_intermediate");
    RegisterSpellScript<JudgementOfWisdomIntermediate>("spell_judgement_of_wisdom_intermediate");
    RegisterSpellScript<DivineIntervention>("spell_divine_intervention");
    RegisterSpellScript<spell_judgement>("spell_judgement");
    RegisterSpellScript<SealOfTheCrusader>("spell_seal_of_the_crusader");
    RegisterSpellScript<BlessingOfLight>("spell_blessing_of_light");
    RegisterSpellScript<JudgementOfCommand>("spell_judgement_of_command");
    RegisterSpellScript<ImprovedRetributionAura>("spell_paladin_improved_retribution_aura");
    RegisterSpellScript<RighteousFury>("spell_paladin_righteous_fury");
    RegisterSpellScript<LayOnHands>("spell_paladin_lay_on_hands");
    RegisterSpellScript<ImprovedSealOfRighteousness>("spell_paladin_improved_seal_of_righteousness");
    RegisterSpellScript<SheathOfLight>("spell_paladin_sheath_of_light");
    RegisterSpellScript<ImprovedDevotionAura>("spell_paladin_improved_devotion_aura");
    RegisterSpellScript<ArdentDefender>("spell_paladin_ardent_defender");
    RegisterSpellScript<SealOfJustice>("spell_paladin_seal_of_justice");
}