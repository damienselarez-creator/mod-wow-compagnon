/*
 * WoW Compagnon, GNU GPL v2 or later. See AUTHORS.
 */
#include "CompanionErrands.h"
#include "EventMap.h"
#include "MotionMaster.h"
#include "Playerbots.h"
#include "PlayerScript.h"
#include "ServerScript.h"
#include "SessionScript.h"

#include <chrono>
#include <map>
#include <mutex>
#include <optional>
#include <set>

namespace
{
    using Clock = std::chrono::steady_clock;
    struct Journey
    {
        std::optional<WorldLocation> destination;
        std::optional<WorldLocation> home;
        uint32 area = 0;
        bool confirmed = false;
        std::set<ObjectGuid> arrived;
        EventMap events;
        Clock::time_point expires;
        uint64 revision = 0;
    };
    std::mutex journeysMutex;
    std::map<ObjectGuid, Journey> journeys;
    uint64 nextRevision = 0;

    bool Human(Player* player)
    {
        return player && player->GetSession() && !player->GetSession()->IsHeadless() && IsRealPlayer(player);
    }

    bool Ready(Player* player)
    {
        return player && player->IsInWorld() && !player->IsBeingTeleported() &&
            !player->IsDuringRemoveFromWorld() && player->GetSession() && !player->GetSession()->IsLoggingOut();
    }

    bool SafeTravel(Player* player)
    {
        return Ready(player) && player->IsAlive() && !player->IsInCombat() && !player->InBattleground() &&
            !player->GetMap()->IsBattlegroundOrArena() && !player->IsInFlight() && !player->IsFlying() &&
            !player->IsFalling() && !player->GetTransport() && !player->GetVehicle();
    }

    bool AtDestination(Player* player, WorldLocation const& destination)
    {
        return Ready(player) && player->GetMapId() == destination.GetMapId() &&
            player->GetExactDist(destination.GetPositionX(), destination.GetPositionY(),
                destination.GetPositionZ()) <= 30.0f;
    }

    bool SameHome(Player* player, WorldLocation const& home, uint32 area)
    {
        return player->m_homebindMapId == home.GetMapId() && player->m_homebindAreaId == area &&
            player->m_homebindX == home.GetPositionX() && player->m_homebindY == home.GetPositionY() &&
            player->m_homebindZ == home.GetPositionZ();
    }

    void Arm(Journey& journey)
    {
        journey.revision = ++nextRevision;
        journey.expires = Clock::now() + std::chrono::seconds(60);
        journey.events.RescheduleEvent(1, Milliseconds(500));
    }

    void Process(Player* master, Journey& journey)
    {
        if (!Ready(master))
            return;

        // The master's native bind has already succeeded; ignore a superseded packet.
        if (journey.home && !SameHome(master, *journey.home, journey.area))
            journey.home.reset();

        if (journey.destination && AtDestination(master, *journey.destination))
            journey.confirmed = true;
        bool const travel = journey.destination && journey.confirmed && SafeTravel(master);
        bool pending = false;
        Group* group = master->GetGroup();
        if (!group)
        {
            journey.destination.reset();
            journey.home.reset();
            return;
        }

        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* bot = ref->GetSource();
            PlayerbotAI* ai = bot ? GET_PLAYERBOT_AI(bot) : nullptr;
            if (!ai || ai->GetMaster() != master || !IsCompanionInventoryManaged(ai))
                continue;

            if (journey.home && Ready(bot) && !SameHome(bot, *journey.home, journey.area))
                bot->SetHomebind(*journey.home, journey.area);

            if (!travel || journey.arrived.count(bot->GetGUID()))
                continue;
            if (!ai->HasStrategy("follow", BOT_STATE_NON_COMBAT) ||
                ai->HasStrategy("stay", BOT_STATE_NON_COMBAT))
                continue;

            if (Ready(bot) && bot->GetMap() == master->GetMap() && bot->IsWithinDistInMap(master, 10.0f))
            {
                journey.arrived.insert(bot->GetGUID());
                continue;
            }
            if (!SafeTravel(bot))
            {
                pending = true;
                continue;
            }

            CancelCompanionErrands(ai);
            bot->InterruptNonMeleeSpells(false);
            bot->GetMotionMaster()->Clear();
            bot->StopMoving();
            // Native TeleportTo retains access, difficulty and instance restrictions.
            if (bot->TeleportTo(master->GetWorldLocation()))
                journey.arrived.insert(bot->GetGUID());
            else
                pending = true;
        }
        // Retry unavailable companions until expiry, but do not chase later ordinary movement.
        if (travel && !pending)
            journey.destination.reset();

        bool homePending = false;
        if (journey.home)
            for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
            {
                Player* bot = ref->GetSource();
                PlayerbotAI* ai = bot ? GET_PLAYERBOT_AI(bot) : nullptr;
                if (ai && ai->GetMaster() == master && IsCompanionInventoryManaged(ai) &&
                    !SameHome(bot, *journey.home, journey.area))
                    homePending = true;
            }
        if (!homePending)
            journey.home.reset();
    }
}

class CompanionJourneyPlayerScript : public PlayerScript
{
public:
    CompanionJourneyPlayerScript() : PlayerScript("CompanionJourneyPlayerScript",
        {PLAYERHOOK_ON_BEFORE_TELEPORT, PLAYERHOOK_ON_BEFORE_LOGOUT}) { }

    bool OnPlayerBeforeTeleport(Player* player, uint32 map, float x, float y, float z,
        float orientation, uint32 /*options*/, Unit* /*target*/) override
    {
        if (!Human(player) || !player->GetGroup() || !player->IsAlive() || player->InBattleground())
            return true;
        // Small local moves (e.g. blink) are handled by ordinary following.
        if (player->GetMapId() == map && player->GetExactDist(x, y, z) < 40.0f)
            return true;
        std::lock_guard<std::mutex> lock(journeysMutex);
        Journey& journey = journeys[player->GetGUID()];
        journey.destination = WorldLocation(map, x, y, z, orientation);
        journey.confirmed = false;
        journey.arrived.clear();
        Arm(journey);
        return true;
    }

    void OnPlayerBeforeLogout(Player* player) override
    {
        std::lock_guard<std::mutex> lock(journeysMutex);
        journeys.erase(player->GetGUID());
    }
};

class CompanionJourneyServerScript : public ServerScript
{
public:
    CompanionJourneyServerScript() : ServerScript("CompanionJourneyServerScript", {SERVERHOOK_ON_PACKET_SENT}) { }

    void OnPacketSent(WorldSession* session, WorldPacket const& packet) override
    {
        if (packet.GetOpcode() != SMSG_PLAYERBOUND)
            return;
        Player* master = session ? session->GetPlayer() : nullptr;
        if (!Human(master) || !master->GetGroup())
            return;
        // PLAYERBOUND confirms an actual bind, unlike the bind-point packet also sent at login.
        MapEntry const* entry = sMapStore.LookupEntry(master->m_homebindMapId);
        if (!entry || entry->Instanceable())
            return;
        std::lock_guard<std::mutex> lock(journeysMutex);
        Journey& journey = journeys[master->GetGUID()];
        journey.home = WorldLocation(master->m_homebindMapId, master->m_homebindX,
            master->m_homebindY, master->m_homebindZ);
        journey.area = master->m_homebindAreaId;
        Arm(journey);
    }
};

class CompanionJourneySessionScript : public SessionScript
{
public:
    CompanionJourneySessionScript() : SessionScript("CompanionJourneySessionScript", {SESSIONHOOK_ON_UPDATE}) { }

    void OnSessionUpdate(WorldSession* session, uint32 diff) override
    {
        Player* master = session ? session->GetPlayer() : nullptr;
        if (!Human(master))
            return;
        Journey snapshot;
        {
            std::lock_guard<std::mutex> lock(journeysMutex);
            auto it = journeys.find(master->GetGUID());
            if (it == journeys.end())
                return;
            if (Clock::now() >= it->second.expires)
            {
                journeys.erase(it);
                return;
            }
            it->second.events.Update(diff);
            if (!it->second.events.ExecuteEvent())
                return;
            it->second.events.ScheduleEvent(1, Milliseconds(500));
            snapshot = it->second;
        }
        // Session updates run on the world thread; no cross-map mutation from a map callback.
        Process(master, snapshot);
        std::lock_guard<std::mutex> lock(journeysMutex);
        auto it = journeys.find(master->GetGUID());
        if (it == journeys.end() || it->second.revision != snapshot.revision)
            return;
        if (!snapshot.destination && !snapshot.home)
            journeys.erase(it);
        else
        {
            it->second.destination = snapshot.destination;
            it->second.home = snapshot.home;
            it->second.confirmed = snapshot.confirmed;
            it->second.arrived = std::move(snapshot.arrived);
        }
    }
};

void AddCompanionJourneyScripts()
{
    new CompanionJourneyPlayerScript();
    new CompanionJourneyServerScript();
    new CompanionJourneySessionScript();
}
