/*
 * WoW Compagnon, GNU GPL v2 or later. See the preserved component author notices.
 */
#ifndef COMPANION_FARM_REGION_POLICY_H
#define COMPANION_FARM_REGION_POLICY_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <vector>

namespace CompanionFarm
{
    struct Site
    {
        uint64_t key = 0;
        uint32_t zone = 0, skill = 0, tier = 0;
        float x = 0, y = 0, z = 0;
    };

    struct Region
    {
        uint32_t zone = 0, skill = 0, tier = 0, coverage = 0;
        float cost = std::numeric_limits<float>::max();
    };

    inline uint64_t SurveyCell(float x, float y)
    {
        return (uint64_t(uint32_t(int32_t(std::floor(x / 96.0f)))) << 32) |
            uint32_t(int32_t(std::floor(y / 96.0f)));
    }

    // Native gathering thresholds: red, yellow +25, green +50, grey +100.
    inline uint32_t GatheringTier(bool needed, uint32_t value, uint32_t maximum, uint32_t required)
    {
        if (required > value)
            return 0;
        if (needed)
            return 3;
        return value < maximum && value < required + 100 ? 2 : 0;
    }

    inline Region ChooseRegion(std::vector<Site> const& sites, float x, float y, uint32_t current)
    {
        struct Coverage
        {
            uint32_t tier = 0;
            float nearest = std::numeric_limits<float>::max();
            std::set<uint64_t> cells;
        };
        std::map<std::pair<uint32_t, uint32_t>, Coverage> regions;
        for (auto const& site : sites)
        {
            if (!site.zone || !site.skill || !site.tier)
                continue;
            auto& region = regions[{site.zone, site.skill}];
            if (site.tier > region.tier)
                region = Coverage{site.tier};
            if (site.tier != region.tier)
                continue;
            region.cells.insert(SurveyCell(site.x, site.y));
            region.nearest = std::min(region.nearest, std::hypot(site.x - x, site.y - y));
        }
        Region best;
        for (auto const& [key, region] : regions)
        {
            // Distinct survey areas matter; hundreds of mutually exclusive pooled spawns do not.
            float density = std::sqrt(float(std::min<size_t>(region.cells.size(), 32)));
            float cost = (region.nearest + 1200.0f) / density;
            if (key.first == current)
                cost *= 0.8f;
            if (region.tier > best.tier || (region.tier == best.tier && cost < best.cost))
                best = {key.first, key.second, region.tier, uint32_t(region.cells.size()), cost};
        }
        return best;
    }

    inline size_t NextSurvey(std::vector<Site> const& sites, Region const& region,
        std::set<uint64_t> const& visited, float x, float y)
    {
        size_t best = sites.size();
        float distance = std::numeric_limits<float>::max();
        for (size_t index = 0; index < sites.size(); ++index)
        {
            auto const& site = sites[index];
            if (site.zone != region.zone || site.skill != region.skill || site.tier != region.tier ||
                visited.count(SurveyCell(site.x, site.y)))
                continue;
            float candidate = std::hypot(site.x - x, site.y - y);
            if (candidate < distance)
            {
                best = index;
                distance = candidate;
            }
        }
        return best;
    }

    inline bool WorkBudget(uint32_t products, uint32_t actions, uint32_t secondary, bool primary)
    {
        return products < 12 && actions < 60 && (primary || secondary < 2);
    }

    inline bool DefensiveAttack(bool hostile, bool combatWithCompanion, bool attackingCompanionOrPet)
    {
        return hostile && (combatWithCompanion || attackingCompanionOrPet);
    }
}
#endif
