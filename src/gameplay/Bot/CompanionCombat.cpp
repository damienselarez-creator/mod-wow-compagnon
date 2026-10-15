/*
 * This file is part of the mod-wow-compagnon module.
 * Released under GNU GPL v2 or later; see AUTHORS.
 */
#include "CompanionCombat.h"
#include "CompanionCombatPolicy.h"
#include "CompanionErrands.h"
#include "CompanionFarming.h"
#include "Playerbots.h"
#include "Spell.h"

#include <algorithm>

namespace
{
    bool ActiveCast(Spell* spell)
    {
        return spell && (spell->getState() == SPELL_STATE_CASTING ||
            (spell->getState() == SPELL_STATE_PREPARING && spell->GetCastTime() > 0));
    }

    bool InterruptEffect(SpellInfo const* info)
    {
        for (auto const& effect : info->GetEffects())
            if (effect.Effect == SPELL_EFFECT_INTERRUPT_CAST ||
                (effect.Effect == SPELL_EFFECT_APPLY_AURA && effect.ApplyAuraName == SPELL_AURA_MOD_SILENCE))
                return true;
        return false;
    }

    bool SingleTargetHeal(SpellInfo const* info)
    {
        uint32 heals = 0;
        for (auto const& effect : info->GetEffects())
            if (effect.Effect == SPELL_EFFECT_HEAL)
            {
                if (effect.TargetA.IsArea() || effect.TargetB.IsArea() || effect.ChainTarget > 1 ||
                    effect.TargetA.GetTarget() == TARGET_UNIT_CASTER || effect.TargetB.GetTarget() == TARGET_UNIT_CASTER)
                    return false;
                ++heals;
            }
        return heals == 1;
    }

    bool DamageEffect(SpellEffectInfo const& effect)
    {
        return effect.Effect == SPELL_EFFECT_SCHOOL_DAMAGE || effect.Effect == SPELL_EFFECT_WEAPON_DAMAGE ||
            effect.Effect == SPELL_EFFECT_WEAPON_DAMAGE_NOSCHOOL ||
            effect.Effect == SPELL_EFFECT_NORMALIZED_WEAPON_DMG || effect.Effect == SPELL_EFFECT_WEAPON_PERCENT_DAMAGE ||
            effect.Effect == SPELL_EFFECT_HEALTH_LEECH ||
            (effect.Effect == SPELL_EFFECT_APPLY_AURA &&
                (effect.ApplyAuraName == SPELL_AURA_PERIODIC_DAMAGE ||
                    effect.ApplyAuraName == SPELL_AURA_PERIODIC_DAMAGE_PERCENT ||
                    effect.ApplyAuraName == SPELL_AURA_PERIODIC_LEECH));
    }

    bool SameGroup(Player* bot, Player* member)
    {
        return member && member->IsInWorld() && member->IsAlive() && member->GetMap() == bot->GetMap() &&
            (member->GetPhaseMask() & bot->GetPhaseMask()) && member->IsInSameGroupWith(bot);
    }

    bool InterruptibleNow(Unit* target, SpellInfo const* interrupt);

    bool PendingInterrupt(PlayerbotAI* ai, Unit* target)
    {
        Player* bot = ai->GetBot();
        if (!bot->GetGroup())
            return false;
        int32 remaining = 1000;
        for (auto type : {CURRENT_GENERIC_SPELL, CURRENT_CHANNELED_SPELL})
            if (Spell* cast = target->GetCurrentSpell(type); ActiveCast(cast) && cast->GetCastTimeRemaining() > 0)
                remaining = std::min(remaining, cast->GetCastTimeRemaining());
        for (GroupReference* ref = bot->GetGroup()->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (member == bot || !SameGroup(bot, member))
                continue;
            Spell* cast = member->GetCurrentSpell(CURRENT_GENERIC_SPELL);
            if (ActiveCast(cast) && cast->GetCastTimeRemaining() <= remaining &&
                cast->m_targets.GetUnitTargetGUID() == target->GetGUID() && InterruptEffect(cast->m_spellInfo) &&
                InterruptibleNow(target, cast->m_spellInfo))
                return true;
            if (Pet* pet = member->GetPet())
            {
                cast = pet->GetCurrentSpell(CURRENT_GENERIC_SPELL);
                if (ActiveCast(cast) && cast->GetCastTimeRemaining() <= remaining &&
                    cast->m_targets.GetUnitTargetGUID() == target->GetGUID() && InterruptEffect(cast->m_spellInfo) &&
                    InterruptibleNow(target, cast->m_spellInfo))
                    return true;
            }
        }
        return false;
    }

    bool InterruptibleNow(Unit* target, SpellInfo const* interrupt)
    {
        for (uint8 effect = EFFECT_0; effect <= EFFECT_2; ++effect)
        {
            auto const& data = interrupt->Effects[effect];
            bool silence = data.Effect == SPELL_EFFECT_APPLY_AURA && data.ApplyAuraName == SPELL_AURA_MOD_SILENCE;
            if ((data.Effect != SPELL_EFFECT_INTERRUPT_CAST && !silence) ||
                target->IsImmunedToSpellEffect(interrupt, effect))
                continue;
            for (auto type : {CURRENT_GENERIC_SPELL, CURRENT_CHANNELED_SPELL})
                if (Spell* cast = target->GetCurrentSpell(type); ActiveCast(cast))
                {
                    auto const* info = cast->m_spellInfo;
                    if (info->PreventionType == SPELL_PREVENTION_TYPE_SILENCE &&
                        (silence || (type == CURRENT_GENERIC_SPELL ?
                            (info->InterruptFlags & SPELL_INTERRUPT_FLAG_INTERRUPT) != 0 :
                            (info->ChannelInterruptFlags & CHANNEL_INTERRUPT_FLAG_INTERRUPT) != 0)))
                        return true;
                }
        }
        return false;
    }
}

bool RefinedCompanionCombat(PlayerbotAI* ai)
{
    return ai && sPlayerbotAIConfig.companionCombatRefinement && IsCompanionInventoryManaged(ai);
}

uint32 CompanionIncomingHealing(PlayerbotAI* ai, Unit* target)
{
    Player* bot = ai->GetBot();
    if (!target || !target->IsAlive() || !bot->GetGroup() || !target->GetMaxHealth())
        return 0;
    uint64 total = 0;
    for (GroupReference* ref = bot->GetGroup()->GetFirstMember(); ref; ref = ref->next())
    {
        Player* member = ref->GetSource();
        if (member == bot || !SameGroup(bot, member) || !member->IsWithinLOSInMap(target))
            continue;
        Spell* cast = member->GetCurrentSpell(CURRENT_GENERIC_SPELL);
        if (!ActiveCast(cast) || cast->GetCastTimeRemaining() > 2000 ||
            cast->m_targets.GetUnitTargetGUID() != target->GetGUID())
            continue;
        for (uint8 effect = EFFECT_0; effect <= EFFECT_2; ++effect)
            if (cast->m_spellInfo->Effects[effect].Effect == SPELL_EFFECT_HEAL)
            {
                int32 amount = cast->CalculateSpellDamage(effect, target);
                if (amount > 0)
                {
                    uint32 healing = member->SpellHealingBonusDone(target, cast->m_spellInfo,
                        uint32(amount), HEAL, effect);
                    total += target->SpellHealingBonusTaken(member, cast->m_spellInfo, healing, HEAL) * uint64(4) / 5;
                }
            }
    }
    return uint32(std::min<uint64>(total, target->GetMaxHealth()));
}

float CompanionHealingScore(PlayerbotAI* ai, Unit* target)
{
    if (!target || !target->GetMaxHealth())
        return 200.0f;
    Player* player = target->ToPlayer();
    float incoming = 100.0f * CompanionIncomingHealing(ai, target) / target->GetMaxHealth();
    return CompanionCombat::HealingScore(target->GetHealthPct(), incoming, ai->GetBot()->GetDistance2d(target),
        player && ai->IsTank(player) && player->IsInCombat(), sPlayerbotAIConfig.lowHealth);
}

float CompanionRescueUrgency(PlayerbotAI* ai, Unit* enemy)
{
    Unit* victim = enemy ? enemy->GetVictim() : nullptr;
    Player* player = victim ? victim->ToPlayer() : nullptr;
    if (!SameGroup(ai->GetBot(), player) || player == ai->GetBot())
        return 0.0f;
    return CompanionCombat::RescueUrgency(player->GetHealthPct(), ai->IsHeal(player), ai->IsTank(player));
}

bool CompanionCombatSpellAllowed(PlayerbotAI* ai, Unit* target, SpellInfo const* info)
{
    if (IsCompanionFarming(ai) && info && !ai->GetBot()->IsInCombat())
        for (auto const& effect : info->GetEffects())
            if (DamageEffect(effect))
                return false;
    if (target && IsCompanionFarming(ai) && target != ai->GetBot())
    {
        if (ai->GetBot()->IsFriendlyTo(target))
        {
            if (target->GetCharmerOrOwnerPlayerOrPlayerItself() != ai->GetBot())
                return false;
        }
        else if (!CompanionFarmAttackAllowed(ai, target))
            return false;
    }
    if (!RefinedCompanionCombat(ai) || !target || !info)
        return true;
    Player* bot = ai->GetBot();
    if (!target->IsInWorld() || target->GetMap() != bot->GetMap() ||
        !(target->GetPhaseMask() & bot->GetPhaseMask()))
        return false;
    bool friendly = bot->IsFriendlyTo(target);
    if (!friendly && InterruptEffect(info) && (!InterruptibleNow(target, info) || PendingInterrupt(ai, target)))
        return false;
    if (friendly && target->IsAlive() && SingleTargetHeal(info) && target->GetMaxHealth())
        if (CompanionCombat::RedundantHeal(target->GetHealthPct(),
            100.0f * CompanionIncomingHealing(ai, target) / target->GetMaxHealth(), sPlayerbotAIConfig.lowHealth))
            return false;
    if (!friendly && (info->HasEffect(SPELL_EFFECT_ATTACK_ME) || info->HasAura(SPELL_AURA_MOD_TAUNT)) &&
        bot->GetGroup() && !bot->GetGroup()->isRaidGroup())
        if (Player* victim = target->GetVictim() ? target->GetVictim()->ToPlayer() : nullptr)
            if (victim != bot && SameGroup(bot, victim) && ai->IsTank(victim))
                return false;
    bool damage = false;
    float radius = 0.0f;
    bool aroundCaster = false;
    for (auto const& effect : info->GetEffects())
        if (DamageEffect(effect))
        {
            damage = true;
            if (effect.TargetA.IsArea() || effect.TargetB.IsArea())
            {
                radius = std::max(radius, effect.CalcRadius(bot));
                aroundCaster = aroundCaster || effect.TargetA.GetTarget() == TARGET_UNIT_SRC_AREA_ENEMY ||
                    effect.TargetB.GetTarget() == TARGET_UNIT_SRC_AREA_ENEMY;
            }
        }
    if (!damage)
        return true;
    if (!friendly && target->HasBreakableByDamageCrowdControlAura())
        return false;
    if (radius > 0.0f)
    {
        Unit* center = aroundCaster ? bot : target;
        GuidVector const nearby = ai->GetAiObjectContext()->GetValue<GuidVector>("nearest hostile npcs")->Get();
        for (ObjectGuid const guid : nearby)
            if (Unit* enemy = ai->GetUnit(guid); enemy && enemy->IsAlive() && enemy->GetMap() == bot->GetMap() &&
                (enemy->GetPhaseMask() & bot->GetPhaseMask()) && enemy->HasBreakableByDamageCrowdControlAura() &&
                center->GetDistance(enemy) <= radius + enemy->GetCombatReach())
                return false;
    }
    // Healers keep their spell action available for a nearby emergency instead of starting an offensive cast.
    if (!IsCompanionFarming(ai) && !(friendly && info->HasEffect(SPELL_EFFECT_HEAL)) && ai->IsHeal(bot) &&
        bot->getPowerType() == POWER_MANA && bot->GetPowerPct(POWER_MANA) > 10.0f && bot->GetGroup())
        for (GroupReference* ref = bot->GetGroup()->GetFirstMember(); ref; ref = ref->next())
            if (Player* member = ref->GetSource(); SameGroup(bot, member) &&
                member->GetHealthPct() < sPlayerbotAIConfig.lowHealth &&
                bot->GetDistance(member) < sPlayerbotAIConfig.healDistance && bot->IsWithinLOSInMap(member))
                return false;
    return true;
}

void UpdateCompanionCombat(PlayerbotAI* ai)
{
    bool farming = IsCompanionFarming(ai);
    if (!RefinedCompanionCombat(ai) && !farming)
        return;
    Player* bot = ai->GetBot();
    if (Unit* victim = bot->GetVictim(); victim && (victim->HasBreakableByDamageCrowdControlAura() ||
        (farming && !CompanionFarmAttackAllowed(ai, victim))))
        bot->AttackStop();
    if (Spell* shot = bot->GetCurrentSpell(CURRENT_AUTOREPEAT_SPELL))
        if (Unit* target = ai->GetUnit(shot->m_targets.GetUnitTargetGUID());
            target && (target->HasBreakableByDamageCrowdControlAura() ||
                (farming && !CompanionFarmAttackAllowed(ai, target))))
            bot->InterruptSpell(CURRENT_AUTOREPEAT_SPELL);
    if (Pet* pet = bot->GetPet())
    {
        if (farming)
            pet->SetReactState(REACT_DEFENSIVE);
        if (Unit* victim = pet->GetVictim(); victim && (victim->HasBreakableByDamageCrowdControlAura() ||
            (farming && !CompanionFarmAttackAllowed(ai, victim))))
            pet->AttackStop();
    }
}
