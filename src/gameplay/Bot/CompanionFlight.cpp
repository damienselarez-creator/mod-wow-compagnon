#include "CompanionFlight.h"
#include "CompanionErrands.h"
#include "LastMovementValue.h"
#include "MovementActions.h"
#include "Playerbots.h"
#include "pbc_companion_language.h"
#include <chrono>
#include <cmath>
#include <map>
#include <mutex>

namespace
{
    using FlightClock = std::chrono::steady_clock;
    struct CompanionFlightPlan
    {
        ObjectGuid owner;
        ObjectGuid flightMaster;
        std::vector<uint32> nodes;
        WorldLocation departure;
        uint64 revision = 0;
        bool scheduled = false;
        bool confirmed = false;
        std::string stage = "awaiting_departure_confirmation";
        FlightClock::time_point expires;
        FlightClock::time_point confirmationExpires;
    };
    std::mutex flightsMutex;
    std::map<ObjectGuid, CompanionFlightPlan> flights;
    uint64 flightRevision = 0;

    bool FlightFollower(PlayerbotAI* ai)
    {
        return IsCompanionInventoryManaged(ai) && ai->GetBot()->GetSession() &&
            ai->GetMaster()->GetSession() && !ai->GetMaster()->GetSession()->IsLoggingOut() &&
            ai->GetBot()->GetSession()->GetAccountId() == ai->GetMaster()->GetSession()->GetAccountId() &&
            ai->GetBot()->GetGroup() && ai->GetBot()->GetGroup() == ai->GetMaster()->GetGroup() &&
            ai->HasStrategy("follow", BOT_STATE_NON_COMBAT) &&
            !ai->HasStrategy("stay", BOT_STATE_NON_COMBAT) &&
            !ai->HasStrategy("passive", BOT_STATE_NON_COMBAT);
    }

    void PublishFlight(ObjectGuid guid, CompanionFlightPlan const& plan)
    {
        std::lock_guard<std::mutex> lock(flightsMutex);
        auto found = flights.find(guid);
        if (found != flights.end() && found->second.revision == plan.revision)
            found->second = plan;
    }

    void EndFlight(PlayerbotAI* ai, CompanionFlightPlan const& plan, char const* reason)
    {
        {
            std::lock_guard<std::mutex> lock(flightsMutex);
            auto found = flights.find(ai->GetBot()->GetGUID());
            if (found == flights.end() || found->second.revision != plan.revision)
                return;
            flights.erase(found);
        }
        auto& movement = ai->GetAiObjectContext()->GetValue<LastMovement&>("last taxi")->Get();
        if (movement.taxiNodes == plan.nodes && movement.taxiMaster == plan.flightMaster)
        {
            movement.taxiNodes.clear();
            movement.taxiMaster.Clear();
        }
        LOG_INFO("playerbots.companion", "[Flight] {} state={} source={} destination={}",
            ai->GetBot()->GetName(), reason, plan.nodes.front(), plan.nodes.back());
    }

    void FlightNotice(PlayerbotAI* ai, char const* french, char const* english)
    {
        ai->TellMaster(PBC_IsFrenchClient(PBC_CompanionClientLocale(ai->GetBot(), ai->GetMaster())) ?
            french : english);
    }

    void ScheduleFlight(PlayerbotAI* ai, uint64 revision);

    class CompanionFlightAction : public MovementAction
    {
    public:
        explicit CompanionFlightAction(PlayerbotAI* ai) : MovementAction(ai, "companion flight") { }
        bool Execute(Event) override
        {
            CompanionFlightPlan plan;
            {
                std::lock_guard<std::mutex> lock(flightsMutex);
                auto found = flights.find(bot->GetGUID());
                if (found == flights.end())
                    return false;
                plan = found->second;
            }
            Player* master = botAI->GetMaster();
            auto const& requested = context->GetValue<LastMovement&>("last taxi")->Get();
            if (requested.taxiNodes != plan.nodes || requested.taxiMaster != plan.flightMaster)
            {
                EndFlight(botAI, plan, "superseded_or_cleared");
                return false;
            }
            if (!FlightFollower(botAI) || master->GetGUID() != plan.owner ||
                !master->IsInWorld() || !master->IsAlive() || !bot->IsAlive() ||
                bot->InBattleground() || bot->GetMap()->IsDungeon())
            {
                EndFlight(botAI, plan, "cancelled");
                return false;
            }
            if (FlightClock::now() >= plan.expires)
            {
                EndFlight(botAI, plan, "expired");
                FlightNotice(botAI, "Je n'ai pas pu rejoindre le depart a temps pour prendre ce vol.",
                    "I could not reach the departure point in time for this flight.");
                return false;
            }
            auto const& path = master->m_taxi.GetPath();
            if (master->IsInFlight() && !path.empty() && path.back() == plan.nodes.back())
                plan.confirmed = true;
            if (!plan.confirmed)
            {
                if (FlightClock::now() >= plan.confirmationExpires)
                {
                    EndFlight(botAI, plan, "master_departure_unconfirmed");
                    return false;
                }
                PublishFlight(bot->GetGUID(), plan);
                ScheduleFlight(botAI, plan.revision);
                return true;
            }
            if (bot->IsInFlight())
            {
                EndFlight(botAI, plan, "already_in_flight");
                return true;
            }
            if (bot->GetMapId() != plan.departure.GetMapId() || bot->IsBeingTeleported() ||
                master->IsBeingTeleported())
            {
                EndFlight(botAI, plan, "departure_map_changed");
                return false;
            }
            CancelCompanionErrands(botAI);
            if (bot->IsInCombat() || master->IsInCombat() || bot->GetTradeData() ||
                bot->GetTransport() || bot->GetVehicle() || bot->Unit::IsFalling() ||
                bot->IsNonMeleeSpellCast(false))
            {
                plan.stage = "waiting_for_safe_departure";
                PublishFlight(bot->GetGUID(), plan);
                ScheduleFlight(botAI, plan.revision);
                return true;
            }
            Creature* npc = ObjectAccessor::GetCreature(*bot, plan.flightMaster);
            if (!npc)
            {
                // The player's departure grid can unload while the companion is still inside the city.
                for (auto const& [spawn, data] : sObjectMgr->GetAllCreatureData())
                    if (data.id == plan.flightMaster.GetEntry() && data.mapid == plan.departure.GetMapId() &&
                        (data.phaseMask & bot->GetPhaseMask()) &&
                        plan.departure.GetExactDist(data.posX, data.posY, data.posZ) < 100.0f)
                    {
                        npc = ObjectAccessor::GetSpawnedCreatureByDBGUID(bot->GetMapId(), uint32(spawn));
                        plan.departure = WorldLocation(data.mapid, data.posX, data.posY, data.posZ, 0);
                        break;
                    }
            }
            if (!npc || !bot->GetNPCIfCanInteractWith(npc->GetGUID(), UNIT_NPC_FLAG_FLIGHTMASTER))
            {
                plan.stage = "going_to_flight_master";
                PublishFlight(bot->GetGUID(), plan);
                bool moved = MoveTo(plan.departure.GetMapId(), plan.departure.GetPositionX(),
                    plan.departure.GetPositionY(), plan.departure.GetPositionZ(), false, false, false,
                    true, MovementPriority::MOVEMENT_NORMAL, true);
                ScheduleFlight(botAI, plan.revision);
                return moved;
            }
            if (sObjectMgr->GetNearestTaxiNode(npc->GetPositionX(), npc->GetPositionY(), npc->GetPositionZ(),
                npc->GetMapId(), bot->GetTeamId()) != plan.nodes.front())
            {
                EndFlight(botAI, plan, "wrong_departure_node");
                return false;
            }
            uint64 total = 0;
            for (size_t index = 1; index < plan.nodes.size(); ++index)
            {
                uint32 route = 0, cost = 0;
                sObjectMgr->GetTaxiPath(plan.nodes[index - 1], plan.nodes[index], route, cost);
                if (!route)
                {
                    EndFlight(botAI, plan, "route_unavailable");
                    FlightNotice(botAI, "Ce maitre de vol ne propose pas ce trajet.",
                        "This flight master does not offer that route.");
                    return false;
                }
                total += cost;
            }
            uint64 price = uint64(std::ceil(total * bot->GetReputationPriceDiscount(npc)));
            if (price > bot->GetMoney())
            {
                EndFlight(botAI, plan, "insufficient_funds");
                bool french = PBC_IsFrenchClient(PBC_CompanionClientLocale(bot, master));
                botAI->TellMaster(std::string(french ? "Il me manque " : "I need another ") +
                    std::to_string(price - bot->GetMoney()) +
                    (french ? " pieces de cuivre pour ce vol. Je suis encore au depart." :
                        " copper for this flight. I am still at the departure point."));
                return false;
            }
            bot->StopMoving();
            botAI->RemoveShapeshift();
            if (bot->IsMounted())
                bot->RemoveAurasByType(SPELL_AURA_MOUNTED);
            bot->GetSession()->SendLearnNewTaxiNode(npc);
            bool started = bot->ActivateTaxiPathTo(plan.nodes, npc, 0);
            // Instant flights return false after native payment/teleport; do not call them a failed purchase.
            auto const* destination = sTaxiNodesStore.LookupEntry(plan.nodes.back());
            bool arrived = destination && bot->GetMapId() == destination->map_id &&
                bot->GetExactDist(destination->x, destination->y, destination->z) < 30.0f;
            EndFlight(botAI, plan, started || bot->IsInFlight() || arrived ? "flight_started" : "native_refusal");
            if (!started && !bot->IsInFlight() && !arrived)
                FlightNotice(botAI, "Le depart a ete refuse. Je suis encore pres du maitre de vol.",
                    "Take-off was refused. I am still near the flight master.");
            return started || bot->IsInFlight() || arrived;
        }
    };

    void ScheduleFlight(PlayerbotAI* ai, uint64 revision)
    {
        ObjectGuid guid = ai->GetBot()->GetGUID();
        {
            std::lock_guard<std::mutex> lock(flightsMutex);
            auto found = flights.find(guid);
            if (found == flights.end() || found->second.revision != revision || found->second.scheduled)
                return;
            found->second.scheduled = true;
        }
        // Resolve the actor at execution time; no Player*, AI*, context reference or NPC* survives this tick.
        ai->AddTimedEvent([guid, revision]()
        {
            {
                std::lock_guard<std::mutex> lock(flightsMutex);
                auto found = flights.find(guid);
                if (found == flights.end() || found->second.revision != revision)
                    return;
                found->second.scheduled = false;
            }
            Player* player = ObjectAccessor::FindPlayer(guid);
            PlayerbotAI* ai = player ? GET_PLAYERBOT_AI(player) : nullptr;
            if (ai && player->IsInWorld() && !player->IsBeingTeleported())
                CompanionFlightAction(ai).Execute(Event());
            else
            {
                std::lock_guard<std::mutex> lock(flightsMutex);
                auto found = flights.find(guid);
                if (found != flights.end() && found->second.revision == revision)
                    flights.erase(found);
            }
        }, 1000);
    }
}

bool RequestCompanionFlight(PlayerbotAI* ai)
{
    if (!FlightFollower(ai))
        return false;
    auto& movement = ai->GetAiObjectContext()->GetValue<LastMovement&>("last taxi")->Get();
    if (movement.taxiNodes.size() < 2 || movement.taxiNodes.size() > 32 || movement.taxiMaster.IsEmpty())
        return false;
    auto const* source = sTaxiNodesStore.LookupEntry(movement.taxiNodes.front());
    if (!source || ai->GetBot()->GetMapId() != source->map_id)
        return false;
    CompanionFlightPlan plan;
    plan.owner = ai->GetMaster()->GetGUID();
    plan.flightMaster = movement.taxiMaster;
    plan.nodes = movement.taxiNodes;
    plan.departure = WorldLocation(source->map_id, source->x, source->y, source->z, 0);
    if (Creature* npc = ObjectAccessor::GetCreature(*ai->GetBot(), plan.flightMaster))
        plan.departure = WorldLocation(npc->GetMapId(), npc->GetPositionX(), npc->GetPositionY(),
            npc->GetPositionZ(), npc->GetOrientation());
    plan.expires = FlightClock::now() + std::chrono::minutes(5);
    plan.confirmationExpires = FlightClock::now() + std::chrono::seconds(10);
    {
        std::lock_guard<std::mutex> lock(flightsMutex);
        for (auto it = flights.begin(); it != flights.end();)
            if (FlightClock::now() >= it->second.expires)
                it = flights.erase(it);
            else
                ++it;
        if (flights.size() >= 4096 && !flights.count(ai->GetBot()->GetGUID()))
            return false;
        plan.revision = ++flightRevision;
        flights[ai->GetBot()->GetGUID()] = plan;
    }
    CancelCompanionErrands(ai);
    ScheduleFlight(ai, plan.revision);
    return true;
}

bool HasPendingCompanionFlight(PlayerbotAI* ai)
{
    if (!ai || !ai->GetBot())
        return false;
    std::lock_guard<std::mutex> lock(flightsMutex);
    auto found = flights.find(ai->GetBot()->GetGUID());
    return found != flights.end() && FlightClock::now() < found->second.expires;
}

std::string CompanionFlightContext(Player* bot)
{
    if (!bot)
        return "";
    std::lock_guard<std::mutex> lock(flightsMutex);
    auto found = flights.find(bot->GetGUID());
    if (found == flights.end() || FlightClock::now() >= found->second.expires)
        return "";
    return "\n[OBSERVED COMPANION FLIGHT PLAN]\nstate=" + found->second.stage +
        "; master_departure_confirmed=" + (found->second.confirmed ? "true" : "false") +
        "\nThis is a saved procedural flight request, not a completed flight. "
        "Going to the flight master means reaching the departure point to take the player's route, "
        "not walking to the destination. Explain the concrete next step briefly without maxims.\n";
}
