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

inline bool IsCompanionShieldReplacement(uint32 candidate, uint32 current)
{
    return (IsCompanionShieldMainHand(candidate) && !IsCompanionShieldMainHand(current)) ||
        (candidate == INVTYPE_SHIELD && current != INVTYPE_SHIELD);
}

#endif
