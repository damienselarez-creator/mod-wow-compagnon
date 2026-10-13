/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */
#ifndef COMPANION_GUILD_BANK_H
#define COMPANION_GUILD_BANK_H

#include "ObjectGuid.h"
class PlayerbotAI;
class Item;

bool CompanionGuildBankEnabled();
bool CompanionGuildBankVisitDue(ObjectGuid character);
bool QueueCompanionGuildBank(PlayerbotAI* ai, ObjectGuid bank, bool depositOnly = false);
bool IsCompanionCraftingStock(PlayerbotAI* ai, Item* item);
bool HasCompanionGuildDeposit(PlayerbotAI* ai);
bool CompanionGuildBankHasProduct(uint32 guild, uint32 item);
void AddCompanionGuildBankScripts();
#endif
