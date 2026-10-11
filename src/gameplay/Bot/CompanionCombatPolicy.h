#ifndef COMPANION_COMBAT_POLICY_H
#define COMPANION_COMBAT_POLICY_H

#include <algorithm>

namespace CompanionCombat
{
    inline float HealingScore(float health, float incoming, float distance, bool pressuredTank, float critical)
    {
        if (health >= 95.0f)
            return 200.0f;
        // A heal still in flight is not a reason to abandon a critically injured ally.
        float projected = health < critical ? health : std::min(100.0f, health + incoming);
        return projected + distance / 10.0f - (pressuredTank && projected < 80.0f ? 8.0f : 0.0f);
    }

    inline bool RedundantHeal(float health, float incoming, float critical)
    {
        return health >= 95.0f || (health >= critical && health + incoming >= 95.0f);
    }

    inline float RescueUrgency(float health, bool healer, bool tank)
    {
        return tank ? 0.0f : (healer ? 100.0f : 50.0f) + (100.0f - health);
    }
}
#endif
