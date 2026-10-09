/*
 * WoW Compagnon, GNU GPL v2 or later. See the preserved component author notices.
 */
#ifndef COMPANION_LOOT_SOURCES_H
#define COMPANION_LOOT_SOURCES_H

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

class CompanionLootSourceIndex
{
public:
    enum class Kind : uint8_t { Creature, Skinning, Gameobject, Reference };
    struct Row
    {
        uint32_t item;
        uint32_t reference;
        float chance;
        bool questRequired;
        uint16_t lootMode;
        uint8_t group;
        uint32_t maxCount;
    };

    void Add(Kind kind, uint32_t entry, Row row)
    {
        _tables[static_cast<size_t>(kind)][entry].push_back(row);
    }

    bool Has(Kind kind, uint32_t entry, uint32_t item, uint16_t lootMode = 1,
        uint8_t group = 0, uint8_t depth = 0) const
    {
        if (!item || depth > MaxDepth)
            return false;

        auto const& table = _tables[static_cast<size_t>(kind)];
        auto found = table.find(entry);
        if (found == table.end())
            return false;

        for (auto const& row : found->second)
        {
            if (row.questRequired || !(row.lootMode & lootMode) || !row.maxCount ||
                (group && row.group != group))
                continue;

            bool equalChance = row.chance == 0.0f && row.group && !row.reference;
            if (row.chance <= 0.0f && !equalChance)
                continue;

            if (row.reference)
            {
                if (depth < MaxDepth && Has(Kind::Reference, row.reference, item, lootMode, row.group, depth + 1))
                    return true;
            }
            else if (row.item == item)
                return true;
        }
        return false;
    }

private:
    static constexpr uint8_t MaxDepth = 16;
    std::array<std::unordered_map<uint32_t, std::vector<Row>>, 4> _tables;
};

class CompanionLootSources
{
public:
    static bool Load();
    static bool Has(CompanionLootSourceIndex::Kind kind, uint32_t entry, uint32_t item);
};
#endif
