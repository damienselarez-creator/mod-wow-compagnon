#ifndef COMPANION_ACTIVITY_POLICY_H
#define COMPANION_ACTIVITY_POLICY_H

#include "CompanionGoalPolicy.h"

namespace CompanionActivity
{
    inline double Score(CompanionGoals::Domain domain, double base, uint32_t requestPriority,
        float distance, bool damaged, bool bagsCrowded, bool mountedMaster)
    {
        using Domain = CompanionGoals::Domain;
        if (domain == Domain::Maintenance && damaged)
            base = -100000.0;
        else if (domain == Domain::Maintenance && bagsCrowded)
            base = -80000.0;
        else if (domain == Domain::Bags && bagsCrowded)
            base = -60000.0;
        else if (domain == Domain::Mount && mountedMaster)
            base -= 20000.0;
        return base + distance - requestPriority;
    }

    inline bool ReturnToAdventure(bool masterMoving, float distance, bool requested, bool crafting)
    {
        return masterMoving && (crafting || distance > (requested ? 120.0f : 60.0f));
    }
}
#endif
