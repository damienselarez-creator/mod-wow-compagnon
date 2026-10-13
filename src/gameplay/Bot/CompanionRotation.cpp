/*
 * This file is part of the mod-wow-compagnon module.
 * Released under GNU GPL v2 or later; see AUTHORS.
 */
#include "CompanionRotation.h"
#include "CompanionCombat.h"
#include "AiFactory.h"
#include "GenericSpellActions.h"
#include "GenericBuffUtils.h"
#include "Playerbots.h"
#include "SpellAuraEffects.h"

namespace
{
    bool CombatUnit(Player* bot, Unit* unit)
    {
        return unit && unit->IsAlive() && unit->IsInWorld() && unit->GetMap() == bot->GetMap() &&
            (unit->GetPhaseMask() & bot->GetPhaseMask());
    }

    float LifeTime(PlayerbotAI* ai, Unit* unit)
    {
        float dps = ai->GetAiObjectContext()->GetValue<float>("estimated group dps")->Get();
        return unit && std::isfinite(dps) && dps > 0 ? float(unit->GetHealth()) / dps : 0;
    }

}

CompanionRotation::Situation BuildCompanionRotationSituation(PlayerbotAI* ai)
{
    CompanionRotation::Situation s;
    s.enabled = RefinedCompanionCombat(ai) && ai->GetState() == BOT_STATE_COMBAT;
    if (!s.enabled)
        return s;
    Player* bot = ai->GetBot();
    s.characterClass = bot->getClass();
    s.spec = AiFactory::GetPlayerSpecTab(bot);
    s.tank = ai->IsTank(bot);
    s.healer = ai->IsHeal(bot);
    s.health = bot->GetHealthPct();
    s.mana = bot->GetMaxPower(POWER_MANA) ? bot->GetPowerPct(POWER_MANA) : 100;
    s.rage = bot->GetPower(POWER_RAGE);
    s.energy = bot->GetPower(POWER_ENERGY);
    Unit* target = ai->GetAiObjectContext()->GetValue<Unit*>("current target")->Get();
    if (CombatUnit(bot, target) && bot->IsHostileTo(target))
    {
        s.combo = bot->GetComboPoints(target);
        s.targetLife = LifeTime(ai, target);
        s.targetHealth = target->GetHealthPct();
        if (Creature* creature = target->ToCreature())
            s.boss = creature->IsDungeonBoss();
    }
    GuidVector const attackers = ai->GetAiObjectContext()->GetValue<GuidVector>("attackers")->Get();
    for (ObjectGuid const guid : attackers)
        if (Unit* enemy = ai->GetUnit(guid); CombatUnit(bot, enemy) && bot->IsHostileTo(enemy))
        {
            ++s.targets;
            s.threatened = s.threatened || enemy->GetVictim() == bot;
        }
    auto proc = [ai, bot, &s](std::string const& name, uint32 flag)
    {
        if (ai->GetAura(name, bot, false, true))
            s.procs |= flag;
    };
    using namespace CompanionRotation;
    switch (s.characterClass)
    {
        case CLASS_WARRIOR:
            proc("sword and board", SwordBoard);
            proc("slam!", Bloodsurge);
            break;
        case CLASS_ROGUE:
            s.finisherKnown = ai->HasSpell("eviscerate") || ai->HasSpell("envenom");
            break;
        case CLASS_HUNTER:
        {
            uint32 explosive = ai->GetAiObjectContext()->GetValue<uint32>("spell id", "explosive shot")->Get();
            s.explosiveReady = explosive && bot->HasSpell(explosive) &&
                !ai::spell::HasSpellOrCategoryCooldown(bot, explosive);
            break;
        }
        case CLASS_MAGE:
            proc("hot streak", HotStreak);
            proc("missile barrage", MissileBarrage);
            proc("fingers of frost", Fingers);
            proc("fireball!", BrainFreeze);
            if (Aura* aura = ai->GetAura("arcane blast", bot, false, true))
                s.arcaneStacks = aura->GetStackAmount();
            break;
        case CLASS_DRUID:
            proc("eclipse (solar)", Solar);
            proc("eclipse (lunar)", Lunar);
            s.finisherKnown = ai->HasSpell("ferocious bite") || ai->HasSpell("rip");
            break;
        case CLASS_SHAMAN:
            if (Aura* aura = ai->GetAura("maelstrom weapon", bot, false, true))
                s.maelstrom = aura->GetStackAmount();
            break;
        case CLASS_WARLOCK:
            proc("molten core", MoltenCore);
            proc("decimation", Decimation);
            break;
        case CLASS_DEATH_KNIGHT:
            proc("killing machine", KillingMachine);
            proc("freezing fog", Rime);
            break;
    }
    return s;
}

float CompanionRotationPriority(PlayerbotAI* ai, Action* action, float relevance,
    CompanionRotation::Situation const& situation)
{
    if (!situation.enabled || !action)
        return relevance;
    auto* cast = dynamic_cast<CastSpellAction*>(action);
    if (!cast)
        return relevance;
    Player* bot = ai->GetBot();
    Unit* target = action->GetTarget();
    if (!CombatUnit(bot, target))
        return relevance;
    std::string const name = cast->getSpell();
    uint32 id = ai->GetAiObjectContext()->GetValue<uint32>("spell id", name)->Get();
    SpellInfo const* info = sSpellMgr->GetSpellInfo(id);
    if (!info)
        return relevance;
    CompanionRotation::Ability a;
    a.targetHealth = target->GetHealthPct();
    a.life = LifeTime(ai, target);
    a.hostile = bot->IsHostileTo(target);
    a.onCurrentTarget = target == ai->GetAiObjectContext()->GetValue<Unit*>("current target")->Get();
    a.castTime = info->CalcCastTime(bot);
    a.effectDelay = a.castTime;
    if (info->Speed > 0 && target != bot)
        a.effectDelay += uint32(1000.0f * std::max(5.0f, bot->GetDistance(target)) / info->Speed);
    a.duration = uint32(std::max(0, info->GetDuration()));
    a.cooldown = info->GetRecoveryTime();
    a.cooldownRemaining = bot->GetSpellCooldownDelay(id);
    for (auto const& pair : bot->GetSpellCooldownMap())
        if (info->GetCategory() && pair.second.category == info->GetCategory())
            a.cooldownRemaining = std::max(a.cooldownRemaining, bot->GetSpellCooldownDelay(pair.first));
    a.rageCost = info->PowerType == POWER_RAGE;
    a.cost = uint32(std::max(0, info->CalcPowerCost(bot, info->GetSchoolMask())));
    uint32 directHeals = 0;
    uint32 firstTick = 0;
    bool simple = true, aroundCaster = false;
    float radius = 0;
    for (auto const& effect : info->GetEffects())
    {
        if (effect.Amplitude && (effect.ApplyAuraName == SPELL_AURA_PERIODIC_DAMAGE ||
            effect.ApplyAuraName == SPELL_AURA_PERIODIC_HEAL || effect.ApplyAuraName == SPELL_AURA_PERIODIC_LEECH ||
            effect.ApplyAuraName == SPELL_AURA_PERIODIC_DAMAGE_PERCENT))
            firstTick = firstTick ? std::min(firstTick, effect.Amplitude) : effect.Amplitude;
        a.directDamage = a.directDamage || effect.Effect == SPELL_EFFECT_SCHOOL_DAMAGE ||
            effect.Effect == SPELL_EFFECT_WEAPON_DAMAGE || effect.Effect == SPELL_EFFECT_WEAPON_DAMAGE_NOSCHOOL ||
            effect.Effect == SPELL_EFFECT_NORMALIZED_WEAPON_DMG || effect.Effect == SPELL_EFFECT_WEAPON_PERCENT_DAMAGE;
        a.periodicDamage = a.periodicDamage || (effect.Effect == SPELL_EFFECT_APPLY_AURA &&
            (effect.ApplyAuraName == SPELL_AURA_PERIODIC_DAMAGE || effect.ApplyAuraName == SPELL_AURA_PERIODIC_LEECH ||
                effect.ApplyAuraName == SPELL_AURA_PERIODIC_DAMAGE_PERCENT));
        a.periodicHeal = a.periodicHeal || (effect.Effect == SPELL_EFFECT_APPLY_AURA &&
            effect.ApplyAuraName == SPELL_AURA_PERIODIC_HEAL);
        if (effect.Effect == SPELL_EFFECT_HEAL)
        {
            ++directHeals;
            simple = simple && !effect.TargetA.IsArea() && !effect.TargetB.IsArea() && effect.ChainTarget <= 1 &&
                effect.TargetA.GetTarget() != TARGET_UNIT_CASTER && effect.TargetB.GetTarget() != TARGET_UNIT_CASTER;
        }
        if (effect.TargetA.IsArea() || effect.TargetB.IsArea())
        {
            radius = std::max(radius, effect.CalcRadius(bot));
            aroundCaster = aroundCaster || effect.TargetA.GetTarget() == TARGET_UNIT_SRC_AREA_ENEMY ||
                effect.TargetB.GetTarget() == TARGET_UNIT_SRC_AREA_ENEMY;
        }
    }
    a.directHeal = directHeals > 0;
    if (firstTick && !a.directDamage && !a.directHeal && !info->HasAttribute(SPELL_ATTR5_EXTRA_INITIAL_PERIOD))
        a.effectDelay += firstTick;
    a.simpleHeal = directHeals == 1 && simple;
    a.projectedHealth = a.targetHealth;
    if (a.simpleHeal && target->GetMaxHealth())
        a.projectedHealth += 100.0f * CompanionIncomingHealing(ai, target) / target->GetMaxHealth();
    a.area = radius > 0 && (a.directDamage || a.periodicDamage);
    if (a.area)
    {
        a.hostile = true;
        Unit* center = aroundCaster ? bot : target;
        GuidVector const attackers = ai->GetAiObjectContext()->GetValue<GuidVector>("attackers")->Get();
        for (ObjectGuid const guid : attackers)
            if (Unit* enemy = ai->GetUnit(guid); CombatUnit(bot, enemy) && bot->IsHostileTo(enemy) &&
                !enemy->HasBreakableByDamageCrowdControlAura() &&
                center->GetDistance(enemy) <= radius + enemy->GetCombatReach())
                ++a.nearbyEnemies;
    }
    if (name == "icy touch" || name == "plague strike" || name == "lava burst" ||
        name == "thunder clap" || name == "demoralizing shout" || name == "lacerate" ||
        name == "slice and dice" || name == "savage roar")
    {
        std::string auraName = name == "icy touch" ? "frost fever" : name == "plague strike" ? "blood plague" :
            name == "lava burst" ? "flame shock" : name;
        bool owner = name != "thunder clap" && name != "demoralizing shout";
        Unit* auraTarget = name == "slice and dice" || name == "savage roar" ? bot : target;
        if (Aura* aura = ai->GetAura(auraName, auraTarget, owner))
        {
            a.auraRemaining = uint32(std::max(0, aura->GetDuration()));
            a.auraStacks = aura->GetStackAmount();
        }
    }
    return CompanionRotation::Priority(situation, a, name, relevance);
}

std::vector<NextAction> CompanionRotationDefaults(PlayerbotAI* ai)
{
    if (!RefinedCompanionCombat(ai) || ai->GetState() != BOT_STATE_COMBAT)
        return {};
    Player* bot = ai->GetBot();
    if (ai->IsTank(bot))
    {
        if (bot->getClass() == CLASS_WARRIOR)
            return {NextAction("shield slam", 5.8f), NextAction("revenge", 5.7f), NextAction("devastate", 5.6f)};
        if (bot->getClass() == CLASS_PALADIN)
            return {NextAction("holy shield", 5.8f)};
    }
    if (bot->getPowerType() == POWER_ENERGY)
    {
        if (bot->getClass() == CLASS_ROGUE)
        {
            uint8 spec = AiFactory::GetPlayerSpecTab(bot);
            return {NextAction(spec == 0 ? "mutilate" : spec == 1 ? "sinister strike" : "hemorrhage", 5.7f),
                NextAction("slice and dice", 5.6f), NextAction(spec == 0 ? "envenom" : "eviscerate", 5.5f),
                NextAction("rupture", 5.4f)};
        }
        if (bot->getClass() == CLASS_DRUID)
            return {NextAction("shred", 5.7f), NextAction("savage roar", 5.6f),
                NextAction("rip", 5.5f), NextAction("ferocious bite", 5.4f)};
    }
    return {};
}
