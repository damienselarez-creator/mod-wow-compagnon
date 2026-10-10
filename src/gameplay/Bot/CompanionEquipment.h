/*
 * This file is part of the mod-wow-compagnon module.
 * Released under GNU GPL v2 or later; see AUTHORS.
 */
#ifndef COMPANION_EQUIPMENT_H
#define COMPANION_EQUIPMENT_H

#include "AiFactory.h"
#include "CompanionErrands.h"
#include "CompanionVocation.h"
#include "Playerbots.h"

inline bool PrefersCompanionShield(PlayerbotAI* ai)
{
    if (!IsCompanionInventoryManaged(ai))
        return false;

    Player* bot = ai->GetBot();
    auto const vocation = CompanionVocation::Get(bot);
    auto tabs = AiFactory::GetPlayerSpecTabs(bot);
    bool const hasTalents = tabs[0] + tabs[1] + tabs[2] > 0;
    int tab = !hasTalents && vocation.managed && vocation.tab >= 0 ?
        vocation.tab : AiFactory::GetPlayerSpecTab(bot);
    return (bot->getClass() == CLASS_WARRIOR && tab == WARRIOR_TAB_PROTECTION) ||
        (bot->getClass() == CLASS_PALADIN && tab == PALADIN_TAB_PROTECTION);
}

inline bool IsCompanionShieldMainHand(uint32 type)
{
    return type == INVTYPE_WEAPON || type == INVTYPE_WEAPONMAINHAND;
}

inline bool IsCompanionShieldRejected(uint32 type)
{
    return type == INVTYPE_2HWEAPON || type == INVTYPE_WEAPONOFFHAND || type == INVTYPE_HOLDABLE;
}

inline bool IsCompanionLayoutCandidate(ItemTemplate const* item)
{
    return (item->Class == ITEM_CLASS_ARMOR && item->InventoryType == INVTYPE_SHIELD) ||
        (item->Class == ITEM_CLASS_WEAPON && item->SubClass != ITEM_SUBCLASS_WEAPON_MISC &&
            item->SubClass != ITEM_SUBCLASS_WEAPON_FISHING_POLE && IsCompanionShieldMainHand(item->InventoryType));
}

inline bool IsCompanionShieldReplacement(uint32 candidate, uint32 current)
{
    return (IsCompanionShieldMainHand(candidate) && !IsCompanionShieldMainHand(current)) ||
        (candidate == INVTYPE_SHIELD && current != INVTYPE_SHIELD);
}

inline bool UsesCompanionArmorPriority(ItemTemplate const* item)
{
    if (!item || item->Class != ITEM_CLASS_ARMOR || !item->Armor)
        return false;
    switch (item->InventoryType)
    {
        case INVTYPE_HEAD:
        case INVTYPE_SHOULDERS:
        case INVTYPE_CHEST:
        case INVTYPE_ROBE:
        case INVTYPE_WAIST:
        case INVTYPE_LEGS:
        case INVTYPE_FEET:
        case INVTYPE_WRISTS:
        case INVTYPE_HANDS:
        case INVTYPE_CLOAK:
        case INVTYPE_SHIELD:
            return true;
        default:
            return false;
    }
}

inline int CompareCompanionArmor(ItemTemplate const* candidate, ItemTemplate const* current)
{
    if (candidate->Armor != current->Armor)
        return candidate->Armor > current->Armor ? 1 : -1;
    if (candidate->Quality != current->Quality)
        return candidate->Quality > current->Quality ? 1 : -1;
    if (candidate->ItemLevel != current->ItemLevel)
        return candidate->ItemLevel > current->ItemLevel ? 1 : -1;
    return 0;
}

#endif
