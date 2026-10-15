/*
 * WoW Compagnon, GNU GPL v2 or later. See the preserved component author notices.
 */
#ifndef COMPANION_FARM_POLICY_H
#define COMPANION_FARM_POLICY_H

#include <cmath>
#include <cstdint>
#include <string>

namespace CompanionFarm
{
    enum class Mode : uint8_t { Follow, Working, Returning };
    enum class Intent : uint8_t { None, Start, Return, Status };
    struct Assignment
    {
        Mode mode = Mode::Follow;
        uint32_t map = 0;
        uint32_t zone = 0;
        float x = 0, y = 0, z = 0;
        std::string status = "following";
        uint32_t harvested = 0;
        uint64_t revision = 0;
        uint32_t destinationZone = 0;

        bool Valid() const
        {
            return mode <= Mode::Returning && std::isfinite(x) && std::isfinite(y) && std::isfinite(z) &&
                std::abs(x) <= 100000 && std::abs(y) <= 100000 && std::abs(z) <= 100000 && status.size() <= 80;
        }
    };

    inline float ResourceScore(float distance, bool needed, uint32_t skill, uint32_t required,
        uint32_t creatureLevel, uint32_t level)
    {
        float progress = skill && required <= skill && skill - required < 100 ?
            1500.0f * (1.0f - float(skill - required) / 100.0f) : 0.0f;
        if (creatureLevel && creatureLevel <= level && level - creatureLevel <= 4)
            progress = 1000.0f - 150.0f * (level - creatureLevel);
        return distance - (needed ? 5000.0f : 0.0f) - progress;
    }
}
#endif
