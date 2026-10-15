/*
 * WoW Compagnon, GNU GPL v2 or later. See the preserved component author notices.
 */
#ifndef COMPANION_FARMING_H
#define COMPANION_FARMING_H

#include "CompanionFarmPolicy.h"
#include "ObjectGuid.h"
#include <memory>

class PlayerbotAI;
class Unit;
struct CompanionFarmingState;
CompanionFarm::Assignment GetCompanionFarmAssignment(PlayerbotAI* ai);
bool SetCompanionFarmAssignment(PlayerbotAI* ai, CompanionFarm::Assignment const& assignment);
void ObserveCompanionFarming(PlayerbotAI* ai, uint64_t revision, std::string const& status,
    uint32_t harvested = 0, uint32_t destinationZone = 0);
bool IsCompanionFarming(PlayerbotAI* ai);
bool IsCompanionReturningFromFarm(PlayerbotAI* ai);
bool CompanionFarmAttackAllowed(PlayerbotAI* ai, Unit* target);
void UpdateCompanionFarming(PlayerbotAI* ai, uint32_t elapsed);
bool ExecuteCompanionFarming(PlayerbotAI* ai);
bool IsCompanionFarmLoot(PlayerbotAI* ai, ObjectGuid guid);
bool FinishCompanionFarmLoot(PlayerbotAI* ai, ObjectGuid guid);
void CancelCompanionFarmRuntime(PlayerbotAI* ai);
std::shared_ptr<CompanionFarmingState>& CompanionFarmRuntime(PlayerbotAI* ai);

#endif
