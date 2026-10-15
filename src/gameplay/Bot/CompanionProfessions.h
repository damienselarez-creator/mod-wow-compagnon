/*
 * WoW Compagnon, GNU GPL v2 or later. See the preserved component author notices.
 */
#ifndef COMPANION_PROFESSIONS_H
#define COMPANION_PROFESSIONS_H

#include <cstdint>
#include <map>
#include <set>

class PlayerbotAI;

struct CompanionProfessionPlan
{
    uint32_t root = 0;
    uint32_t next = 0;
    uint32_t skill = 0;
    uint32_t materialsFor = 0;
    bool primary = false;
    std::map<uint32_t, uint32_t> missing;
};

inline bool CompanionCraftBudgetAvailable(uint32_t products, uint32_t actions, uint32_t secondary, bool primary)
{
    return products < 5 && actions < 24 && (primary || secondary < 2);
}

void RestoreCompanionProfessionDefaults(PlayerbotAI* ai);
CompanionProfessionPlan PlanCompanionProfession(PlayerbotAI* ai, std::set<uint32_t> const& rejected);
CompanionProfessionPlan GetCompanionProfessionPlan(PlayerbotAI* ai);

#endif
