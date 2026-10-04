#include "Spells/Scripts/SpellScript.h"

enum LeechSpells
{
    SPELL_HEAL = 18984,

    // Encounter-scoped blessing for outdoor bosses (no zone aura): 30 s copies of the 40-man blessing/regen,
    // refreshed by the boss's pulse while in combat, so they fall off on their own after the fight.
    SPELL_ZONE_BLESSING_40      = 34126,
    SPELL_ENCOUNTER_BLESSING    = 34146,
    SPELL_ENCOUNTER_REGEN       = 34147,
};

// One script instance is bound per tier aura (spell_scripts rows 34127-34130), and every bound
// UnitScript runs on every damage event, so each instance must only react to its own aura.
// Otherwise a single aura would trigger the heal once per bound row.
template <uint32 AuraId>
struct InstanceLeechOnDamageHealing : public UnitScript {
    void OnDealDamage(Unit* attacker, Unit* victim, uint32 damage) const override {
        if (attacker == nullptr) return; // attacker should not be null?
        if (attacker == victim) return; // no leech from self-damage (Hellfire etc.)

        bool isPet = attacker->GetOwner() && attacker->GetOwner()->GetTypeId() == TYPEID_PLAYER;
        if (!isPet && attacker->GetTypeId() != TYPEID_PLAYER) return;

        Unit* player = isPet ? attacker->GetOwner() : attacker;
        if (!player->HasAura(AuraId)) return;
        auto leech_heal = static_cast<int32>(0.05f * float(damage));
        if (leech_heal <= 0) return; // hits under 20 damage would heal 0 (combat log spam)
        if (!player->IsAlive()) return; // DoTs keep ticking after death; DealHeal would revive
        // Heal directly like Spell's health leech: no SMSG_SPELL_GO, so no cast visual/sound on every hit.
        // DealHeal still sends SMSG_SPELLHEALLOG under SPELL_HEAL for the combat text.
        if (SpellEntry const* healInfo = sSpellTemplate.LookupEntry<SpellEntry>(SPELL_HEAL))
            player->DealHeal(player, uint32(leech_heal), healInfo);
    }
};

// 34145 - Encounter Blessing (pulse cast by the boss on every enemy within 100 yd)
struct EncounterBlessingPulse : public SpellScript {
    void OnEffectExecute(Spell* spell, SpellEffectIndex effIdx) const override {
        if (effIdx != EFFECT_INDEX_0) return;
        Unit* target = spell->GetUnitTarget();
        if (!target || target->GetTypeId() != TYPEID_PLAYER || !target->IsAlive()) return;
        if (target->HasAura(SPELL_ZONE_BLESSING_40)) return; // never stack with a zone blessing
        target->CastSpell(target, SPELL_ENCOUNTER_BLESSING, TRIGGERED_OLD_TRIGGERED);
        target->CastSpell(target, SPELL_ENCOUNTER_REGEN, TRIGGERED_OLD_TRIGGERED);
    }
};

void LoadInstanceScripts() {
    RegisterSpellScript<InstanceLeechOnDamageHealing<34127>>("spell_instance_heal");
    RegisterSpellScript<InstanceLeechOnDamageHealing<34128>>("spell_instance_heal_10");
    RegisterSpellScript<InstanceLeechOnDamageHealing<34129>>("spell_instance_heal_20");
    RegisterSpellScript<InstanceLeechOnDamageHealing<34130>>("spell_instance_heal_40");
    RegisterSpellScript<InstanceLeechOnDamageHealing<SPELL_ENCOUNTER_REGEN>>("spell_instance_heal_40_encounter");
    RegisterSpellScript<EncounterBlessingPulse>("spell_encounter_blessing");
}
