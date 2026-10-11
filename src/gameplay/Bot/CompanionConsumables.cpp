/*
 * This file is part of the mod-wow-compagnon module.
 * Released under GNU GPL v2 or later; see AUTHORS.
 */
#include "CompanionConsumables.h"
#include "Action.h"
#include "AiFactory.h"
#include "CompanionErrands.h"
#include "CompanionFlight.h"
#include "CompanionVocation.h"
#include "NamedObjectContext.h"
#include "Playerbots.h"
#include "Spell.h"
#include "SpellAuraEffects.h"

#include <algorithm>

using ConsumableKind = CompanionConsumables::Kind;
using ConsumableStat = CompanionConsumables::Stat;

namespace
{
    CompanionConsumables::Role ConsumableRole(PlayerbotAI* ai)
    {
        Player* bot = ai->GetBot();
        auto const vocation = CompanionVocation::Get(bot);
        auto tabs = AiFactory::GetPlayerSpecTabs(bot);
        int const tab = tabs[0] + tabs[1] + tabs[2] == 0 && vocation.managed && vocation.tab >= 0 ?
            vocation.tab : AiFactory::GetPlayerSpecTab(bot);
        using Role = CompanionConsumables::Role;
        switch (bot->getClass())
        {
            case CLASS_PRIEST:
            case CLASS_MAGE:
            case CLASS_WARLOCK:
                return Role::Caster;
            case CLASS_DRUID:
                return tab == DRUID_TAB_FERAL ? (ai->IsTank(bot) ? Role::Tank : Role::Agile) : Role::Caster;
            case CLASS_SHAMAN:
                return tab == SHAMAN_TAB_ENHANCEMENT ? Role::Agile : Role::Caster;
            case CLASS_PALADIN:
                return tab == PALADIN_TAB_HOLY ? Role::Caster :
                    (tab == PALADIN_TAB_PROTECTION ? Role::Tank : Role::Physical);
            case CLASS_HUNTER:
                return Role::Ranged;
            case CLASS_ROGUE:
                return Role::Agile;
            case CLASS_WARRIOR:
                return tab == WARRIOR_TAB_PROTECTION ? Role::Tank : Role::Physical;
            case CLASS_DEATH_KNIGHT:
                return tab == DEATH_KNIGHT_TAB_BLOOD ? Role::Tank : Role::Physical;
            default:
                return Role::Physical;
        }
    }

    ConsumableStat ScrollStat(SpellEffectInfo const& effect)
    {
        if (effect.ApplyAuraName == SPELL_AURA_MOD_RESISTANCE &&
            effect.MiscValue == SPELL_SCHOOL_MASK_NORMAL)
            return ConsumableStat::Armor;
        if (effect.ApplyAuraName != SPELL_AURA_MOD_STAT)
            return ConsumableStat::Unknown;
        switch (effect.MiscValue)
        {
            case STAT_STRENGTH: return ConsumableStat::Strength;
            case STAT_AGILITY: return ConsumableStat::Agility;
            case STAT_STAMINA: return ConsumableStat::Stamina;
            case STAT_INTELLECT: return ConsumableStat::Intellect;
            case STAT_SPIRIT: return ConsumableStat::Spirit;
            default: return ConsumableStat::Unknown;
        }
    }

    int ScrollPriority(PlayerbotAI* ai, CompanionConsumable const& profile)
    {
        return CompanionConsumables::ScrollPriority(profile.stat, ConsumableRole(ai),
            ai->GetBot()->GetMaxPower(POWER_MANA) > 0);
    }

    bool ScrollAlreadyCovered(PlayerbotAI* ai, CompanionConsumable const& profile)
    {
        Player* bot = ai->GetBot();
        auto const* spell = sSpellMgr->GetSpellInfo(profile.spell);
        if (!spell)
            return true;
        // Native SPELL_SPECIFIC_SCROLL makes different stat scrolls mutually exclusive.
        // Keep the preferred active family rather than cycling through every useful stat.
        if (spell->GetSpellSpecific() == SPELL_SPECIFIC_SCROLL)
            for (AuraType type : {SPELL_AURA_MOD_STAT, SPELL_AURA_MOD_RESISTANCE})
                for (auto const* aura : bot->GetAuraEffectsByType(type))
                {
                    auto const* active = aura->GetSpellInfo();
                    if (active->GetSpellSpecific() != SPELL_SPECIFIC_SCROLL)
                        continue;
                    CompanionConsumable current{ConsumableKind::Scroll,
                        ScrollStat(active->Effects[aura->GetEffIndex()]), active->Id, float(aura->GetAmount())};
                    int const priority = ScrollPriority(ai, profile);
                    int const activePriority = ScrollPriority(ai, current);
                    if (activePriority > priority || (activePriority == priority && current.amount >= profile.amount))
                        return true;
                }
        for (auto const& effect : spell->GetEffects())
        {
            if (!effect.IsAura())
                continue;
            for (auto const* aura : bot->GetAuraEffectsByType(effect.ApplyAuraName))
            {
                auto const* active = aura->GetSpellInfo();
                bool const exclusive = active->Id == spell->Id || active->IsRankOf(spell) ||
                    active->IsAuraExclusiveBySpecificWith(spell) ||
                    sSpellMgr->CheckSpellGroupStackRules(active, spell) != SPELL_GROUP_STACK_RULE_DEFAULT;
                bool const sameStat = aura->GetMiscValue() == effect.MiscValue ||
                    (effect.ApplyAuraName == SPELL_AURA_MOD_STAT && aura->GetMiscValue() == -1);
                if (exclusive && sameStat && aura->GetAmount() >= effect.CalcValue(bot))
                    return true;
            }
        }
        return false;
    }

    bool CanUseConsumable(PlayerbotAI* ai, Item* item, CompanionConsumable const& profile)
    {
        Player* bot = ai->GetBot();
        auto const* spell = sSpellMgr->GetSpellInfo(profile.spell);
        if (!spell || item->IsInTrade() || item->IsWrapped() || item->GetTemplate()->StartQuest ||
            bot->CanUseItem(item) != EQUIP_ERR_OK || bot->IsNonMeleeSpellCast(false))
            return false;
        Spell check(bot, spell, TRIGGERED_NONE);
        check.m_CastItem = item;
        check.m_targets.SetUnitTarget(bot);
        return check.CheckCast(true) == SPELL_CAST_OK;
    }

    bool SafeConsumableUser(PlayerbotAI* ai)
    {
        if (!IsCompanionInventoryManaged(ai))
            return false;
        Player* bot = ai->GetBot();
        Player* master = ai->GetMaster();
        return bot->IsAlive() && bot->IsInWorld() && bot->GetSession() &&
            !bot->GetSession()->IsLoggingOut() && master->IsInWorld() &&
            master->GetSession() && !master->GetSession()->IsLoggingOut() &&
            !bot->IsBeingTeleported() && !bot->HasUnitState(UNIT_STATE_LOST_CONTROL) &&
            !bot->IsMounted() && !bot->IsInFlight() && !bot->IsFalling() &&
            !bot->GetTransport() && !bot->GetVehicle() && !bot->GetTradeData() &&
            !bot->IsNonMeleeSpellCast(false) && !HasPendingCompanionFlight(ai) &&
            !IsCompanionErrandActive(ai) && !ai->HasStrategy("passive", BOT_STATE_NON_COMBAT);
    }

    bool SafeRest(PlayerbotAI* ai)
    {
        Player* bot = ai->GetBot();
        Player* master = ai->GetMaster();
        if (!master->IsAlive() || bot->IsInCombat() || master->IsInCombat() || bot->isMoving() || master->isMoving() ||
            master->IsMounted() || master->IsInFlight() || master->IsBeingTeleported() ||
            bot->GetMap() != master->GetMap() || !(bot->GetPhaseMask() & master->GetPhaseMask()) ||
            bot->GetDistance(master) > 30 || !bot->GetGroup() || bot->GetGroup() != master->GetGroup() ||
            !bot->getAttackers().empty() || bot->HasAuraType(SPELL_AURA_PERIODIC_DAMAGE) ||
            bot->HasAuraType(SPELL_AURA_PERIODIC_DAMAGE_PERCENT) ||
            bot->HasAuraType(SPELL_AURA_PERIODIC_LEECH))
            return false;
        for (GroupReference* ref = bot->GetGroup()->GetFirstMember(); ref; ref = ref->next())
            if (Player* member = ref->GetSource())
                if (member->IsInCombat())
                    return false;
        return true;
    }

    Item* SelectConsumable(PlayerbotAI* ai, ConsumableKind requested = ConsumableKind::Unknown)
    {
        if (!SafeConsumableUser(ai))
            return nullptr;
        Player* bot = ai->GetBot();
        bool const manaUser = bot->getPowerType() == POWER_MANA && bot->GetMaxPower(POWER_MANA) > 0;
        float const mana = manaUser ? bot->GetPowerPct(POWER_MANA) : 100;
        bool const safeRest = !bot->IsInCombat() && SafeRest(ai);
        Item* best = nullptr;
        CompanionConsumable selected;
        for (Item* item : ai->GetInventoryItems())
        {
            auto const profile = DescribeCompanionConsumable(ai, item->GetTemplate());
            if ((requested != ConsumableKind::Unknown && profile.kind != requested) ||
                !CompanionConsumables::NeededNow(profile.kind, bot->IsInCombat(), bot->GetHealthPct(),
                    mana, manaUser, safeRest, ai->IsHeal(bot)) ||
                (profile.kind == ConsumableKind::Scroll &&
                    (!IsUsefulCompanionScroll(ai, profile) || ScrollAlreadyCovered(ai, profile))) ||
                !CanUseConsumable(ai, item, profile))
                continue;
            // Healing comes before mana in danger; bandaging comes before preparations at rest.
            if (best && profile.kind > selected.kind)
                continue;
            float const missing = profile.kind == ConsumableKind::Mana ?
                bot->GetMaxPower(POWER_MANA) - bot->GetPower(POWER_MANA) : bot->GetMaxHealth() - bot->GetHealth();
            bool const better = !best || profile.kind < selected.kind ||
                (profile.kind == ConsumableKind::Scroll ?
                    (ScrollPriority(ai, profile) > ScrollPriority(ai, selected) ||
                        (ScrollPriority(ai, profile) == ScrollPriority(ai, selected) && profile.amount > selected.amount)) :
                    CompanionConsumables::BetterRestoration(profile.amount, selected.amount, missing));
            if (better)
            {
                best = item;
                selected = profile;
            }
        }
        return best;
    }

    bool UseConsumable(PlayerbotAI* ai, Item* item)
    {
        if (!item)
            return false;
        Player* bot = ai->GetBot();
        auto const profile = DescribeCompanionConsumable(ai, item->GetTemplate());
        if (!CanUseConsumable(ai, item, profile))
            return false;
        uint32 const entry = item->GetEntry();
        uint32 const before = bot->GetItemCount(entry, false);
        auto const* spell = sSpellMgr->GetSpellInfo(profile.spell);
        uint32 const duration = spell->IsChanneled() ? uint32(std::max(0, spell->GetDuration())) :
            spell->CalcCastTime(bot);
        WorldPacket packet(CMSG_USE_ITEM);
        packet << uint8(item->GetBagSlot()) << uint8(item->GetSlot()) << uint8(1) << profile.spell <<
            item->GetGUID() << uint32(0) << uint8(0) << uint32(TARGET_FLAG_UNIT) << bot->GetGUID().WriteAsPacked();
        bot->GetSession()->HandleUseItemOpcode(packet);
        // The handler may consume/delete the item. Never read that pointer afterwards.
        Spell* current = bot->GetCurrentSpell(CURRENT_CHANNELED_SPELL);
        if (!current)
            current = bot->GetCurrentSpell(CURRENT_GENERIC_SPELL);
        bool const started = bot->GetItemCount(entry, false) < before ||
            (current && current->GetSpellInfo()->Id == profile.spell);
        if (started)
        {
            ai->SetNextCheckDelay(duration + sPlayerbotAIConfig.reactDelay);
            LOG_INFO("playerbots.companion", "[Consumable] {} uses item={} spell={} kind={}",
                bot->GetName(), entry, profile.spell, uint32(profile.kind));
        }
        return started;
    }

    class CompanionConsumableAction : public Action
    {
    public:
        explicit CompanionConsumableAction(PlayerbotAI* ai) : Action(ai, "companion consumable") { }
        bool isUseful() override { return SelectConsumable(botAI) != nullptr; }
        bool Execute(Event /*event*/) override { return UseConsumable(botAI, SelectConsumable(botAI)); }
    };

    class CompanionConsumableContext : public NamedObjectContext<Action>
    {
    public:
        CompanionConsumableContext() { creators["companion consumable"] = &Create; }
    private:
        static Action* Create(PlayerbotAI* ai) { return new CompanionConsumableAction(ai); }
    };
}

CompanionConsumable DescribeCompanionConsumable(PlayerbotAI* ai, ItemTemplate const* item)
{
    if (!ai || !ai->GetBot() || !item || item->Class != ITEM_CLASS_CONSUMABLE)
        return {};
    for (auto const& use : item->Spells)
    {
        if (use.SpellId <= 0 || use.SpellTrigger != ITEM_SPELLTRIGGER_ON_USE)
            continue;
        auto const* spell = sSpellMgr->GetSpellInfo(uint32(use.SpellId));
        if (!spell)
            continue;
        CompanionConsumable result;
        result.spell = spell->Id;
        bool unsupported = false;
        for (auto const& effect : spell->GetEffects())
        {
            if (!effect.Effect)
                continue;
            float const amount = effect.CalcValue(ai->GetBot());
            if (item->SubClass == ITEM_SUBCLASS_SCROLL)
            {
                auto const stat = ScrollStat(effect);
                if (!effect.IsAura() || stat == ConsumableStat::Unknown || amount <= 0 ||
                    (result.stat != ConsumableStat::Unknown && result.stat != stat))
                    unsupported = true;
                else
                {
                    result.kind = ConsumableKind::Scroll;
                    result.stat = stat;
                    result.amount += amount;
                }
            }
            else if (item->SubClass == ITEM_SUBCLASS_BANDAGE &&
                effect.ApplyAuraName == SPELL_AURA_PERIODIC_HEAL && amount > 0 && effect.Amplitude)
            {
                result.kind = ConsumableKind::Bandage;
                result.amount += amount * std::max(1, spell->GetDuration() / int32(effect.Amplitude));
            }
            else if (item->SubClass == ITEM_SUBCLASS_POTION)
            {
                if (effect.IsEffect(SPELL_EFFECT_HEAL) && amount > 0)
                {
                    if (result.kind == ConsumableKind::Mana)
                        result.amount = 0;
                    result.kind = ConsumableKind::Healing;
                    result.amount += amount;
                }
                else if (effect.IsEffect(SPELL_EFFECT_ENERGIZE) && effect.MiscValue == POWER_MANA && amount > 0 &&
                    result.kind != ConsumableKind::Healing)
                {
                    result.kind = ConsumableKind::Mana;
                    result.amount += amount;
                }
                else if (!effect.IsEffect(SPELL_EFFECT_ENERGIZE) || effect.MiscValue != POWER_MANA)
                    unsupported = true;
            }
        }
        // Recall/teleport/enchant scrolls are not stat scrolls and must never be auto-used or sold.
        if (!unsupported && result.kind != ConsumableKind::Unknown && result.amount > 0)
            return result;
    }
    return {};
}

bool IsUsefulCompanionScroll(PlayerbotAI* ai, CompanionConsumable const& scroll)
{
    return ai && ai->GetBot() && scroll.kind == ConsumableKind::Scroll &&
        CompanionConsumables::UsefulScroll(scroll.stat,
        ConsumableRole(ai), ai->GetBot()->GetMaxPower(POWER_MANA) > 0);
}

bool UseCompanionPotion(PlayerbotAI* ai, bool healing)
{
    return UseConsumable(ai, SelectConsumable(ai, healing ? ConsumableKind::Healing : ConsumableKind::Mana));
}

NamedObjectContext<Action>* CreateCompanionConsumableActionContext()
{
    return new CompanionConsumableContext();
}
