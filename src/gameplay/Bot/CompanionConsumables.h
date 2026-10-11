/*
 * This file is part of the mod-wow-compagnon module.
 * Released under GNU GPL v2 or later; see AUTHORS.
 */
#ifndef COMPANION_CONSUMABLES_H
#define COMPANION_CONSUMABLES_H

#include "CompanionConsumablePolicy.h"
#include "Define.h"

class Action;
class Item;
class PlayerbotAI;
struct ItemTemplate;
template<class T> class NamedObjectContext;

struct CompanionConsumable
{
    CompanionConsumables::Kind kind = CompanionConsumables::Kind::Unknown;
    CompanionConsumables::Stat stat = CompanionConsumables::Stat::Unknown;
    uint32 spell = 0;
    float amount = 0;
    float manaAmount = 0;
};

CompanionConsumable DescribeCompanionConsumable(PlayerbotAI* ai, ItemTemplate const* item);
bool IsUsefulCompanionScroll(PlayerbotAI* ai, CompanionConsumable const& scroll);
bool UseCompanionPotion(PlayerbotAI* ai, bool healing);
NamedObjectContext<Action>* CreateCompanionConsumableActionContext();

#endif
