#include "Spells/Scripts/SpellScript.h"

enum LeechSpells
{
    SPELL_HEAL = 18984
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

void LoadInstanceScripts() {
    RegisterSpellScript<InstanceLeechOnDamageHealing<34127>>("spell_instance_heal");
    RegisterSpellScript<InstanceLeechOnDamageHealing<34128>>("spell_instance_heal_10");
    RegisterSpellScript<InstanceLeechOnDamageHealing<34129>>("spell_instance_heal_20");
    RegisterSpellScript<InstanceLeechOnDamageHealing<34130>>("spell_instance_heal_40");
}
