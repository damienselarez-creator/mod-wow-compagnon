#include <cassert>
#include <cmath>
#include <optional>
#include <set>
#include <string>

using uint32 = unsigned int;
using ObjectGuid = unsigned int;
constexpr int BOT_STATE_NON_COMBAT = 0;
struct WorldLocation
{
    uint32 map = 0;
    float x = 0, y = 0, z = 0;
    uint32 GetMapId() const { return map; }
    float GetPositionX() const { return x; }
    float GetPositionY() const { return y; }
    float GetPositionZ() const { return z; }
};
struct Session
{
    bool logout = false;
    bool IsLoggingOut() const { return logout; }
};
struct Map
{
    bool battleground = false;
    bool IsBattlegroundOrArena() const { return battleground; }
} maps[2];
struct Motion { void Clear() { } };
struct Group;
struct PlayerbotAI;
struct Player
{
    ObjectGuid guid = 1;
    WorldLocation pos;
    Session session;
    Motion motion;
    Group* group = nullptr;
    PlayerbotAI* ai = nullptr;
    bool world = true, teleporting = false, removing = false, alive = true, combat = false;
    bool taxi = false, flying = false, falling = false, transport = false, vehicle = false, reject = false;
    int teleports = 0, bindings = 0;
    uint32 m_homebindMapId = 0, m_homebindAreaId = 0;
    float m_homebindX = 0, m_homebindY = 0, m_homebindZ = 0;
    bool IsInWorld() const { return world; }
    bool IsBeingTeleported() const { return teleporting; }
    bool IsDuringRemoveFromWorld() const { return removing; }
    Session* GetSession() { return &session; }
    bool IsAlive() const { return alive; }
    bool IsInCombat() const { return combat; }
    bool InBattleground() const { return maps[pos.map].battleground; }
    Map* GetMap() { return &maps[pos.map]; }
    bool IsInFlight() const { return taxi; }
    bool IsFlying() const { return flying; }
    bool IsFalling() const { return falling; }
    bool GetTransport() const { return transport; }
    bool GetVehicle() const { return vehicle; }
    uint32 GetMapId() const { return pos.map; }
    float GetExactDist(float x, float y, float z) const
    {
        return std::sqrt((pos.x-x)*(pos.x-x) + (pos.y-y)*(pos.y-y) + (pos.z-z)*(pos.z-z));
    }
    bool IsWithinDistInMap(Player* other, float distance) const
    {
        return pos.map == other->pos.map && GetExactDist(other->pos.x, other->pos.y, other->pos.z) <= distance;
    }
    ObjectGuid GetGUID() const { return guid; }
    WorldLocation GetWorldLocation() const { return pos; }
    Group* GetGroup() { return group; }
    Motion* GetMotionMaster() { return &motion; }
    void StopMoving() { }
    void InterruptNonMeleeSpells(bool) { }
    bool TeleportTo(WorldLocation const& destination)
    {
        if (reject)
            return false;
        ++teleports;
        pos = destination;
        return true;
    }
    void SetHomebind(WorldLocation const& home, uint32 area)
    {
        ++bindings;
        m_homebindMapId = home.map;
        m_homebindAreaId = area;
        m_homebindX = home.x;
        m_homebindY = home.y;
        m_homebindZ = home.z;
    }
};
struct GroupReference
{
    Player* player;
    GroupReference* link = nullptr;
    Player* GetSource() { return player; }
    GroupReference* next() { return link; }
};
struct Group
{
    GroupReference* first;
    GroupReference* GetFirstMember() { return first; }
};
struct PlayerbotAI
{
    Player* master;
    bool managed = true, follow = true, stay = false;
    Player* GetMaster() { return master; }
    bool HasStrategy(std::string const& name, int) { return name == "follow" ? follow : stay; }
};
#define GET_PLAYERBOT_AI(player) ((player)->ai)
bool IsCompanionInventoryManaged(PlayerbotAI* ai) { return ai && ai->managed; }
bool detached = false;
bool IsCompanionFarming(PlayerbotAI*) { return detached; }
void CancelCompanionErrands(PlayerbotAI*) { }
struct Journey
{
    std::optional<WorldLocation> destination, home;
    uint32 area = 0;
    bool confirmed = false;
    bool started = false;
    std::set<ObjectGuid> arrived;
};
#include "production.inc"

int main()
{
    Player human, bot;
    bot.guid = 2;
    PlayerbotAI ai{&human};
    bot.ai = &ai;
    GroupReference member{&bot};
    Group group{&member};
    human.group = bot.group = &group;
    Journey journey;
    journey.destination = WorldLocation{1, 100, 0, 0};
    human.teleporting = true;
    Process(&human, journey);
    assert(bot.teleports == 0 && journey.destination);
    human.teleporting = false;
    Process(&human, journey); // Rejected or not yet completed master teleport.
    assert(bot.teleports == 0 && !journey.confirmed);
    human.pos = *journey.destination;
    Process(&human, journey); // Walking to a refused teleport destination must not trigger a recall.
    assert(bot.teleports == 0 && !journey.confirmed);
    journey.started = true;
    bot.combat = true;
    Process(&human, journey);
    assert(bot.teleports == 0 && journey.confirmed && journey.destination);
    human.pos.x += 100; // Human may move while companion finishes combat.
    bot.combat = false;
    Process(&human, journey);
    assert(bot.teleports == 1 && !journey.destination && bot.pos.x == human.pos.x);
    Process(&human, journey);
    assert(bot.teleports == 1);

    bot.pos = {};
    journey = {};
    journey.destination = human.pos;
    journey.started = true;
    ai.stay = true;
    Process(&human, journey);
    assert(bot.teleports == 1 && !journey.destination);
    ai.stay = false;
    journey.destination = human.pos;
    journey.confirmed = false;
    bot.alive = false;
    Process(&human, journey);
    assert(bot.teleports == 1 && journey.destination);
    bot.alive = true;
    bot.reject = true; // Native access restriction must never be bypassed.
    Process(&human, journey);
    assert(bot.teleports == 1 && journey.destination);
    bot.reject = false;
    bot.taxi = true;
    Process(&human, journey);
    assert(bot.teleports == 1);
    bot.taxi = false;
    maps[1].battleground = true;
    Process(&human, journey);
    assert(bot.teleports == 1);
    maps[1].battleground = false;

    journey = {};
    journey.home = WorldLocation{0, 5, 6, 7}; // Map 0 is a valid continent.
    journey.area = 42;
    human.SetHomebind(*journey.home, 42);
    bot.teleporting = true;
    Process(&human, journey);
    assert(bot.bindings == 0 && journey.home);
    bot.teleporting = false;
    Process(&human, journey);
    assert(bot.bindings == 1 && !journey.home);
    assert(bot.m_homebindMapId == 0 && bot.m_homebindX == 5 && bot.m_homebindAreaId == 42);
    journey.home = WorldLocation{0, 8, 9, 10}; // Superseded / mismatched packet.
    Process(&human, journey);
    assert(bot.bindings == 1 && !journey.home);

    detached = true;
    journey = {};
    journey.destination = human.pos;
    journey.started = true;
    journey.home = WorldLocation{0, 5, 6, 7};
    journey.area = 42;
    Process(&human, journey);
    assert(bot.teleports == 1 && bot.bindings == 1);
    detached = false;

    Player second, third, fourth, otherHuman;
    second.guid = 3;
    third.guid = 4;
    fourth.guid = 5;
    second.ai = third.ai = fourth.ai = &ai;
    GroupReference fifth{&otherHuman}, fourthRef{&fourth, &fifth};
    GroupReference thirdRef{&third, &fourthRef}, secondRef{&second, &thirdRef};
    member.link = &secondRef;
    bot.pos = {};
    journey = {};
    journey.destination = human.pos;
    journey.home = WorldLocation{0, 5, 6, 7};
    journey.started = true;
    journey.area = 42;
    Process(&human, journey);
    assert(bot.teleports == 2 && second.teleports == 1 && third.teleports == 1 && fourth.teleports == 1);
    assert(second.bindings == 1 && third.bindings == 1 && fourth.bindings == 1);
    assert(otherHuman.teleports == 0 && otherHuman.bindings == 0);
    assert(!journey.destination && !journey.home);
}
