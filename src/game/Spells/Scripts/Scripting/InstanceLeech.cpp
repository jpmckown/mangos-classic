#include <algorithm>
#include "Spells/Scripts/SpellScript.h"

enum LeechSpells
{
    SPELL_HEAL = 18984
};

std::array<uint32, 4> INSTANCE_HEALING_AURAS = { 34127, 34128, 34129, 34130 };

struct InstanceLeechOnDamageHealing : public UnitScript {
    void OnDealDamage(Unit* attacker, Unit* victim, uint32 damage) const override {
        if (attacker == nullptr) return; // attacker should not be null?

        bool isPet = attacker->GetOwner() && attacker->GetOwner()->GetTypeId() == TYPEID_PLAYER;
        if (!isPet && attacker->GetTypeId() != TYPEID_PLAYER) return;

        Unit* player = isPet ? attacker->GetOwner() : attacker;
        auto has_aura = std::any_of(INSTANCE_HEALING_AURAS.begin(), INSTANCE_HEALING_AURAS.end(), [player](uint32 id){
            return player->HasAura(id);
        });
        if (!has_aura) return;
        auto leech_heal = static_cast<int32>(0.05f * float(damage));
        player->CastCustomSpell(attacker, SPELL_HEAL, &leech_heal, nullptr, nullptr, TRIGGERED_OLD_TRIGGERED);
    }
};

void LoadInstanceScripts() {
    RegisterSpellScript<InstanceLeechOnDamageHealing>("spell_instance_heal");
    RegisterSpellScript<InstanceLeechOnDamageHealing>("spell_instance_heal_10");
    RegisterSpellScript<InstanceLeechOnDamageHealing>("spell_instance_heal_20");
    RegisterSpellScript<InstanceLeechOnDamageHealing>("spell_instance_heal_40");
}