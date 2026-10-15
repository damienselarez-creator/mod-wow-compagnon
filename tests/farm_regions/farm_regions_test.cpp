#include "CompanionFarmRegionPolicy.h"
#include <cassert>
#include <iostream>

int main()
{
    using namespace CompanionFarm;
    assert(GatheringTier(false, 7, 75, 1) == 2); // Copper for an apprentice, regardless of character level.
    assert(GatheringTier(true, 7, 75, 65) == 0); // Tin is still inaccessible.
    assert(GatheringTier(false, 101, 150, 1) == 0); // Grey copper gives no gathering progression.
    assert(GatheringTier(true, 101, 150, 1) == 3); // Still useful for a real copper-based recipe.
    assert(GatheringTier(false, 75, 75, 65) == 0); // No fictitious progress at a rank cap.
    assert(GatheringTier(false, 75, 150, 65) == 2);

    std::vector<Site> sites = {{1, 130, 186, 2, 10, 10, 0},
        {2, 85, 186, 2, 300, 0, 0}, {3, 85, 186, 2, 400, 0, 0},
        {4, 85, 186, 2, 500, 0, 0}, {5, 85, 186, 2, 600, 0, 0}};
    auto region = ChooseRegion(sites, 0, 0, 0);
    assert(region.zone == 85 && region.coverage == 4); // Productive region beats one nearby spawn.
    for (uint32_t index = 0; index < 500; ++index)
        sites.push_back({1000 + index, 130, 186, 2, 10, 10, 0});
    assert(ChooseRegion(sites, 0, 0, 0).zone == 85); // Pooled duplicates cannot inflate regional density.
    sites.push_back({6, 215, 186, 3, 10000, 0, 0});
    assert(ChooseRegion(sites, 0, 0, 0).zone == 215); // A verified material need has priority over proximity.
    sites.back().tier = 0;
    assert(ChooseRegion(sites, 0, 0, 0).zone == 85);
    std::set<uint64_t> visited;
    size_t first = NextSurvey(sites, region, visited, 0, 0);
    assert(first == 1);
    visited.insert(SurveyCell(sites[first].x, sites[first].y));
    assert(NextSurvey(sites, region, visited, 0, 0) == 2);
    for (auto const& site : sites)
        visited.insert(SurveyCell(site.x, site.y));
    assert(NextSurvey(sites, region, visited, 0, 0) == sites.size());
    assert(ChooseRegion({}, 0, 0, 0).skill == 0);
    assert(SurveyCell(-1, -1) != SurveyCell(1, 1));

    assert(!DefensiveAttack(true, false, false)); // No proactive attacks for experience or materials.
    assert(DefensiveAttack(true, true, false));
    assert(DefensiveAttack(true, false, true));
    assert(!DefensiveAttack(false, true, true));
    assert(WorkBudget(11, 59, 2, true));
    assert(!WorkBudget(12, 59, 0, true));
    assert(!WorkBudget(1, 60, 0, true));
    assert(!WorkBudget(1, 1, 2, false));
    std::cout << "PASS: profession thresholds, useful regions, real survey coverage, bounded work and defence\n";
}
