/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "RememberTaxiAction.h"
#include "AiObjectContext.h"
#include "Event.h"
#include "LastMovementValue.h"
#include "CompanionFlight.h"

bool RememberTaxiAction::Execute(Event event)
{
    WorldPacket p(event.getPacket());
    p.rpos(0);

    switch (p.GetOpcode())
    {
        case CMSG_ACTIVATETAXI:
        {
            LastMovement& movement = context->GetValue<LastMovement&>("last taxi")->Get();
            movement.taxiNodes.clear();
            movement.taxiNodes.resize(2);

            p >> movement.taxiMaster >> movement.taxiNodes[0] >> movement.taxiNodes[1];
            RequestCompanionFlight(botAI);
            return true;
        }
        case CMSG_ACTIVATETAXIEXPRESS:
        {
            ObjectGuid guid;
            uint32 node_count;
            p >> guid >> node_count;
            if (node_count < 2 || node_count > 32 || p.size() - p.rpos() < node_count * sizeof(uint32))
                return false;

            LastMovement& movement = context->GetValue<LastMovement&>("last taxi")->Get();
            movement.taxiNodes.clear();
            movement.taxiMaster = guid;
            for (uint32 i = 0; i < node_count; ++i)
            {
                uint32 node;
                p >> node;
                movement.taxiNodes.push_back(node);
            }
            RequestCompanionFlight(botAI);

            return true;
        }
    }

    return false;
}
