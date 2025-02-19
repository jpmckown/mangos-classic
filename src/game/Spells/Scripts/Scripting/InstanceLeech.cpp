#include <SpellScript.h>

enum LeechSpells
{
    SPELL_HEAL = 18984
};

struct InstanceLeechOnDamageHealing : public UnitScript {

};

void LoadInstanceScripts() {
    RegisterSpellScript<InstanceLeechOnDamageHealing>("spell_instance_heal");
    RegisterSpellScript<InstanceLeechOnDamageHealing>("spell_instance_heal_10");
    RegisterSpellScript<InstanceLeechOnDamageHealing>("spell_instance_heal_20");
    RegisterSpellScript<InstanceLeechOnDamageHealing>("spell_instance_heal_40");
}