/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */
#ifndef COMPANION_GUILD_BANK_POLICY_H
#define COMPANION_GUILD_BANK_POLICY_H

#include <algorithm>
#include <cstdint>
#include <map>
#include <string>

namespace CompanionSharing
{
    enum class Kind : uint8_t { Bags, Gems, Armor, Weapons, Recipes, Consumables, Tools, Materials, Enhancements };
    constexpr uint64_t Week = 7 * 24 * 60 * 60;
    constexpr uint32_t MaterialLimit = 5;
    constexpr uint32_t RetainedReagents = 20;

    inline uint32_t TakeCount(Kind kind, uint32_t stock, uint32_t missing, uint32_t stackLimit = 1)
    {
        if (!stock || !missing)
            return 0;
        if (kind == Kind::Consumables || kind == Kind::Enhancements)
            return std::min({stock, missing, stackLimit});
        if (kind != Kind::Materials)
            return 1;
        // A small part of the shared stock, at most enough for one craft.
        uint32_t share = stock / 3 + (stock % 3 != 0);
        return std::min({MaterialLimit, share, missing});
    }

    inline uint32_t Surplus(uint32_t held, uint32_t retained)
    {
        return held > retained ? held - retained : 0;
    }

    inline std::string Key(uint32_t character, Kind kind)
    {
        // Neither changing guild/tab nor choosing another item resets a family quota.
        return std::to_string(character) + ":" + std::to_string(uint32_t(kind));
    }

    class Quotas
    {
    public:
        std::map<std::string, uint64_t> Next;

        bool Available(std::string const& key, uint64_t now) const
        {
            auto found = Next.find(key);
            return found == Next.end() || now >= found->second;
        }

        bool Reserve(std::string const& key, uint64_t now)
        {
            if (!Available(key, now) || now > UINT64_MAX - Week)
                return false;
            Next[key] = now + Week;
            return true;
        }
    };
}
#endif
