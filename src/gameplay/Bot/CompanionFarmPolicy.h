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

        bool Valid() const
        {
            return mode <= Mode::Returning && std::isfinite(x) && std::isfinite(y) && std::isfinite(z) &&
                std::abs(x) <= 100000 && std::abs(y) <= 100000 && std::abs(z) <= 100000 && status.size() <= 80;
        }
    };

}
#endif
