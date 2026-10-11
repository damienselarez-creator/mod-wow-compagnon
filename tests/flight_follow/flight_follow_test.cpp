#include <cassert>
#include <chrono>
#include <cmath>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

using uint32 = uint32_t;
using uint64 = uint64_t;
constexpr int BOT_STATE_NON_COMBAT = 0;
constexpr int UNIT_NPC_FLAG_FLIGHTMASTER = 1;
constexpr int SPELL_AURA_MOUNTED = 1;
#define LOG_INFO(...) ((void)0)
struct ObjectGuid
{
    uint32 id = 0;
    bool operator<(ObjectGuid const& other) const { return id < other.id; }
    bool operator==(ObjectGuid const&) const = default;
    void Clear() { id = 0; }
    bool IsEmpty() const { return id == 0; }
    uint32 GetEntry() const { return 99; }
};
struct WorldLocation
{
    uint32 map = 530;
    float x = 0, y = 0, z = 0;
    WorldLocation() = default;
    WorldLocation(uint32 map, float x, float y, float z, float) : map(map), x(x), y(y), z(z) { }
    uint32 GetMapId() const { return map; }
    float GetPositionX() const { return x; }
    float GetPositionY() const { return y; }
    float GetPositionZ() const { return z; }
    float GetOrientation() const { return 0; }
    float GetExactDist(float a, float b, float c) const
    {
        return std::sqrt((x - a) * (x - a) + (y - b) * (y - b) + (z - c) * (z - c));
    }
};
struct Creature : WorldLocation
{
    ObjectGuid GetGUID() const { return {99}; }
};
Creature npc;
struct TaxiNodesEntry { uint32 map_id = 530; float x = 0, y = 0, z = 0; };
struct TaxiStore
{
    TaxiNodesEntry source, destination{530, 1000, 0, 0};
    TaxiNodesEntry const* LookupEntry(uint32 id) const
    {
        return id == 1 ? &source : id == 2 ? &destination : nullptr;
    }
} sTaxiNodesStore;
struct Session
{
    bool loggingOut = false;
    uint32 account = 7;
    bool IsLoggingOut() const { return loggingOut; }
    uint32 GetAccountId() const { return account; }
    void SendLearnNewTaxiNode(Creature*) { }
};
struct Unit { bool IsFalling() const { return false; } };
struct Map { bool IsDungeon() const { return false; } };
class PlayerbotAI;
struct Player : Unit, WorldLocation
{
    ObjectGuid guid;
    Session session;
    Map mapObject;
    PlayerbotAI* ai = nullptr;
    uint32 money = 100, flights = 0;
    bool alive = true, inWorld = true, teleporting = false, combat = false;
    bool mounted = false, nearNpc = false, flying = false, nativeAccept = true, instant = false, instantTransfer = false;
    struct Taxi
    {
        std::deque<uint32> path;
        std::deque<uint32> const& GetPath() const { return path; }
    } m_taxi;
    ObjectGuid GetGUID() const { return guid; }
    Session* GetSession() { return &session; }
    int GetGroup() const { return 1; }
    Map* GetMap() { return &mapObject; }
    uint32 GetMoney() const { return money; }
    std::string GetName() const { return "Companion"; }
    bool IsAlive() const { return alive; }
    bool IsInWorld() const { return inWorld; }
    bool IsBeingTeleported() const { return teleporting; }
    bool IsInCombat() const { return combat; }
    bool InBattleground() const { return false; }
    bool IsInFlight() const { return flying; }
    bool IsMounted() const { return mounted; }
    bool IsNonMeleeSpellCast(bool) const { return false; }
    void* GetTradeData() const { return nullptr; }
    void* GetTransport() const { return nullptr; }
    void* GetVehicle() const { return nullptr; }
    uint32 GetPhaseMask() const { return 1; }
    int GetTeamId() const { return 1; }
    float GetReputationPriceDiscount(Creature*) const { return 0.8f; }
    Creature* GetNPCIfCanInteractWith(ObjectGuid, int) const { return nearNpc ? &npc : nullptr; }
    void StopMoving() { }
    void RemoveAurasByType(int) { mounted = false; }
    bool ActivateTaxiPathTo(std::vector<uint32> const&, Creature*, int)
    {
        if (!nativeAccept || mounted)
            return false;
        ++flights;
        money -= 40;
        flying = !instant;
        if (instantTransfer)
            teleporting = true;
        else if (instant)
            x = 1000;
        return !instant;
    }
};
struct LastMovement { std::vector<uint32> taxiNodes; ObjectGuid taxiMaster; };
struct Context
{
    LastMovement movement;
    struct Value { LastMovement& data; LastMovement& Get() { return data; } } value{movement};
    template<class T> Value* GetValue(char const*) { return &value; }
};
class PlayerbotAI
{
public:
    Player bot, master;
    Context context;
    bool stay = false, managed = true;
    uint32 moves = 0, cancelled = 0;
    std::vector<std::string> notices;
    std::vector<std::function<void()>> timers;
    Player* GetBot() { return &bot; }
    Player* GetMaster() { return &master; }
    Context* GetAiObjectContext() { return &context; }
    bool HasStrategy(char const* name, int) const
    {
        return std::string(name) == "follow" || (std::string(name) == "stay" && stay);
    }
    void TellMaster(std::string const& text) { notices.push_back(text); }
    void AddTimedEvent(std::function<void()> callback, uint32) { timers.push_back(std::move(callback)); }
    void RemoveShapeshift() { }
};
bool IsCompanionInventoryManaged(PlayerbotAI* ai) { return ai && ai->managed; }
void CancelCompanionErrands(PlayerbotAI* ai) { ++ai->cancelled; }
int PBC_CompanionClientLocale(Player*, Player*) { return 1; }
bool PBC_IsFrenchClient(int) { return true; }
#define GET_PLAYERBOT_AI(player) ((player)->ai)
std::map<ObjectGuid, Player*> actors;
bool npcLoaded = true;
namespace ObjectAccessor
{
    Player* FindPlayer(ObjectGuid guid)
    {
        auto found = actors.find(guid);
        return found == actors.end() ? nullptr : found->second;
    }
    Creature* GetCreature(Player&, ObjectGuid) { return npcLoaded ? &npc : nullptr; }
    Creature* GetSpawnedCreatureByDBGUID(uint32, uint32) { return npcLoaded ? &npc : nullptr; }
}
struct Spawn { uint32 id = 99, mapid = 530, phaseMask = 1; float posX = 0, posY = 0, posZ = 0; };
struct ObjectMgr
{
    std::map<uint32, Spawn> data{{99, {}}};
    auto const& GetAllCreatureData() const { return data; }
    uint32 GetNearestTaxiNode(float, float, float, uint32, int) const { return 1; }
    void GetTaxiPath(uint32 from, uint32 to, uint32& path, uint32& cost) const
    {
        path = from == 1 && to == 2 ? 1 : 0;
        cost = 50;
    }
} objectMgr;
ObjectMgr* sObjectMgr = &objectMgr;
constexpr int CMSG_ACTIVATETAXI = 1, CMSG_ACTIVATETAXIEXPRESS = 2;
struct WorldPacket
{
    int opcode = 0;
    std::vector<uint32> words;
    size_t position = 0;
    void rpos(size_t value) { position = value / sizeof(uint32); }
    size_t rpos() const { return position * sizeof(uint32); }
    size_t size() const { return words.size() * sizeof(uint32); }
    int GetOpcode() const { return opcode; }
    WorldPacket& operator>>(uint32& value) { value = words.at(position++); return *this; }
    WorldPacket& operator>>(ObjectGuid& value) { return *this >> value.id; }
};
struct Event
{
    WorldPacket packet;
    WorldPacket& getPacket() { return packet; }
};
enum class MovementPriority { MOVEMENT_NORMAL };
class MovementAction
{
protected:
    PlayerbotAI* botAI;
    Player* bot;
    Context* context;
    bool MoveTo(uint32 map, float x, float y, float z, bool, bool, bool, bool, MovementPriority, bool)
    {
        ++botAI->moves;
        assert(map == 530 && x == 0 && y == 0 && z == 0);
        return true;
    }
public:
    MovementAction(PlayerbotAI* ai, char const*) : botAI(ai), bot(ai->GetBot()), context(ai->GetAiObjectContext()) { }
    virtual bool Execute(Event) = 0;
};
#include "flight.inc"
struct RememberTaxiAction
{
    PlayerbotAI* botAI;
    Context* context;
    bool Execute(Event);
};
#include "remember.inc"

void Prepare(PlayerbotAI& ai, uint32 id)
{
    ai.bot.guid = {id};
    ai.master.guid = {id + 100};
    ai.bot.ai = &ai;
    ai.master.flying = true;
    ai.master.m_taxi.path = {1, 2};
    ai.context.movement.taxiNodes = {1, 2};
    ai.context.movement.taxiMaster = {99};
    actors[ai.bot.guid] = &ai.bot;
}
void Tick(PlayerbotAI& ai)
{
    assert(!ai.timers.empty());
    auto callback = std::move(ai.timers.front());
    ai.timers.erase(ai.timers.begin());
    callback();
}
int main()
{
    PlayerbotAI ai;
    Prepare(ai, 1);
    assert(RequestCompanionFlight(&ai));
    assert(HasPendingCompanionFlight(&ai));
    assert(ai.cancelled && ai.notices.empty());
    Tick(ai);
    assert(ai.moves == 1 && ai.bot.flights == 0 && HasPendingCompanionFlight(&ai));
    assert(CompanionFlightContext(&ai.bot).find("going_to_flight_master") != std::string::npos);
    // The human can already have landed while the companion is still reaching the departure NPC.
    ai.master.flying = false;
    ai.master.m_taxi.path.clear();
    ai.bot.nearNpc = true;
    ai.bot.mounted = true;
    Tick(ai);
    assert(ai.bot.flights == 1 && ai.bot.flying && ai.bot.money == 60);
    assert(!HasPendingCompanionFlight(&ai) && ai.context.movement.taxiNodes.empty());
    assert(ai.notices.empty());

    PlayerbotAI delayed;
    Prepare(delayed, 2);
    delayed.master.flying = false;
    assert(RequestCompanionFlight(&delayed));
    Tick(delayed);
    assert(delayed.moves == 0 && delayed.bot.flights == 0);
    delayed.master.flying = true;
    delayed.bot.combat = true;
    Tick(delayed);
    assert(delayed.moves == 0 && delayed.bot.flights == 0);
    delayed.bot.combat = false;
    npcLoaded = false;
    Tick(delayed);
    assert(delayed.moves == 1);
    npcLoaded = true;
    delayed.bot.nearNpc = true;
    Tick(delayed);
    assert(delayed.bot.flights == 1);

    PlayerbotAI poor;
    Prepare(poor, 3);
    poor.bot.money = 10;
    poor.bot.nearNpc = true;
    assert(RequestCompanionFlight(&poor));
    Tick(poor);
    assert(poor.bot.flights == 0 && poor.bot.money == 10 && poor.notices.size() == 1);
    assert(poor.notices.front().find("30 pieces de cuivre") != std::string::npos);
    assert(!HasPendingCompanionFlight(&poor));

    PlayerbotAI replaced;
    Prepare(replaced, 4);
    assert(RequestCompanionFlight(&replaced));
    assert(RequestCompanionFlight(&replaced));
    Tick(replaced); // Superseded callback cannot launch or erase the newer request.
    assert(replaced.moves == 0 && HasPendingCompanionFlight(&replaced));
    replaced.context.movement.taxiNodes.clear();
    Tick(replaced);
    assert(replaced.moves == 0 && !HasPendingCompanionFlight(&replaced));

    PlayerbotAI wait;
    Prepare(wait, 5);
    wait.stay = true;
    assert(!RequestCompanionFlight(&wait));
    wait.stay = false;
    assert(RequestCompanionFlight(&wait));
    wait.stay = true;
    Tick(wait);
    assert(!HasPendingCompanionFlight(&wait) && wait.bot.flights == 0);

    PlayerbotAI logout;
    Prepare(logout, 6);
    assert(RequestCompanionFlight(&logout));
    actors.erase(logout.bot.guid);
    Tick(logout);
    assert(!HasPendingCompanionFlight(&logout));

    PlayerbotAI refusal;
    Prepare(refusal, 7);
    refusal.bot.nearNpc = true;
    refusal.bot.nativeAccept = false;
    assert(RequestCompanionFlight(&refusal));
    Tick(refusal);
    assert(refusal.bot.flights == 0 && refusal.notices.size() == 1);
    assert(!HasPendingCompanionFlight(&refusal));

    PlayerbotAI instant;
    Prepare(instant, 8);
    instant.bot.nearNpc = true;
    instant.bot.instant = true;
    assert(RequestCompanionFlight(&instant));
    Tick(instant);
    assert(instant.bot.flights == 1 && instant.bot.x == 1000 && instant.notices.empty());

    PlayerbotAI transfer;
    Prepare(transfer, 10);
    transfer.bot.nearNpc = true;
    transfer.bot.instant = true;
    transfer.bot.instantTransfer = true;
    assert(RequestCompanionFlight(&transfer));
    Tick(transfer);
    assert(transfer.bot.flights == 1 && transfer.bot.teleporting && transfer.notices.empty());

    PlayerbotAI express;
    Prepare(express, 9);
    express.context.movement.taxiMaster.Clear();
    express.context.movement.taxiNodes.clear();
    RememberTaxiAction remember{&express, &express.context};
    Event packet;
    packet.packet.opcode = CMSG_ACTIVATETAXIEXPRESS;
    packet.packet.words = {99, 2, 1, 2};
    assert(remember.Execute(packet));
    assert(express.context.movement.taxiMaster == ObjectGuid{99});
    assert((express.context.movement.taxiNodes == std::vector<uint32>{1, 2}));
    assert(HasPendingCompanionFlight(&express));
    Tick(express);
    assert(express.moves == 1 && express.notices.empty());
    packet.packet.words = {99, 40};
    assert(!remember.Execute(packet));
    packet.packet.words = {99, 3, 1, 2};
    assert(!remember.Execute(packet));
}
