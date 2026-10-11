#ifndef COMPANION_GOALS_H
#define COMPANION_GOALS_H

#include "CompanionGoalPolicy.h"
#include <string>
#include <vector>

class Player;
class PlayerbotAI;
bool CompanionGoalPermitted(PlayerbotAI* ai, CompanionGoals::Domain domain);
bool CompanionGoalAllowed(PlayerbotAI* ai, CompanionGoals::Domain domain);
bool CompanionGoalRequested(PlayerbotAI* ai, CompanionGoals::Domain domain);
uint32_t CompanionGoalPriority(PlayerbotAI* ai, CompanionGoals::Domain domain);
uint64_t CompanionGoalRevision(PlayerbotAI* ai);
void ObserveCompanionGoal(PlayerbotAI* ai, CompanionGoals::Domain domain, CompanionGoals::State state,
    std::string const& reason, uint32_t requiredMoney = 0, uint32_t confirmed = 0);
bool ReconsiderCompanionGoals(PlayerbotAI* ai);
uint32_t CompanionBagCapacity(Player* bot);
uint32_t CompanionGoalTarget(PlayerbotAI* ai, CompanionGoals::Domain domain);
std::string CompanionGoalsContext(Player* bot);
bool PBC_CompanionGoalChat(Player* player, Player* bot, std::string const& message);
bool PBC_CompanionGoalConversation(Player* player, std::vector<Player*> const& bots, std::string const& message);
void AddCompanionGoalScripts();
#endif
