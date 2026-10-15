#include "CompanionFarmDialogue.h"
#include "CompanionGoalStore.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <random>
#include <set>
#include <vector>

using uint8 = uint8_t;
using uint32 = uint32_t;
using uint64 = uint64_t;
using Milliseconds = std::chrono::milliseconds;
constexpr int BOT_STATE_NON_COMBAT = 0, SKILL_SKINNING = 393;
struct ObjectGuid
{
    uint32 value = 0;
    bool operator==(ObjectGuid const&) const = default;
    bool IsEmpty() const { return !value; }
    uint32 GetCounter() const { return value; }
    void Clear() { value = 0; }
};
struct EventMap
{
    uint64 now = 0;
    std::map<uint32, uint64> events;
    void Update(uint32 elapsed) { now += elapsed; }
    void RescheduleEvent(uint32 event, Milliseconds delay) { events[event] = now + delay.count(); }
    void CancelEvent(uint32 event) { events.erase(event); }
    uint32 ExecuteEvent()
    {
        for (auto const& [id, when] : events)
            if (when <= now) { events.erase(id); return id; }
        return 0;
    }
};
struct Map { bool dungeon = false; bool IsDungeon() const { return dungeon; } } maps[2];
struct Session
{
    bool logout = false;
    uint32 account = 1;
    bool IsLoggingOut() const { return logout; }
    uint32 GetAccountId() const { return account; }
};
struct Motion { int clears = 0; void Clear() { ++clears; } };
struct Group { };
struct Player;
struct PlayerbotAI;
struct Unit
{
    ObjectGuid guid;
    Unit* victim = nullptr;
    Player* owner = nullptr;
    bool combatWithBot = false;
    ObjectGuid GetGUID() const { return guid; }
    Unit* GetVictim() { return victim; }
    Player* GetCharmerOrOwnerPlayerOrPlayerItself() { return owner; }
    bool IsInCombatWith(Player*) { return combatWithBot; }
};
struct Player : Unit
{
    PlayerbotAI* ai = nullptr;
    Session session;
    Group* group = nullptr;
    Motion motion;
    uint32 map = 0, zone = 85, phase = 1;
    float x = 50, y = 60, z = 10;
    bool world = true, alive = true, combat = false, teleport = false, taxi = false, skin = false;
    int stops = 0, interrupts = 0;
    bool IsInWorld() const { return world; }
    bool IsAlive() const { return alive; }
    bool IsInCombat() const { return combat; }
    bool IsBeingTeleported() const { return teleport; }
    bool IsInFlight() const { return taxi; }
    bool IsFlying() const { return false; }
    bool IsFalling() const { return false; }
    bool GetTransport() const { return false; }
    bool GetVehicle() const { return false; }
    bool InBattleground() const { return false; }
    Map* GetMap() const { return &maps[map]; }
    uint32 GetMapId() const { return map; }
    uint32 GetZoneId() const { return zone; }
    uint32 GetPhaseMask() const { return phase; }
    float GetPositionX() const { return x; }
    float GetPositionY() const { return y; }
    float GetPositionZ() const { return z; }
    float GetDistance(Player* other) const { return std::abs(x-other->x); }
    Session* GetSession() { return &session; }
    Group* GetGroup() { return group; }
    std::string GetName() const { return "any-companion"; }
    bool HasSkill(int skill) const { return skill == SKILL_SKINNING && skin; }
    void StopMoving() { ++stops; }
    void InterruptNonMeleeSpells(bool) { ++interrupts; }
    Motion* GetMotionMaster() { return &motion; }
};
struct Item { uint32 count = 0; uint32 GetCount() const { return count; } };
struct LootObject { ObjectGuid guid; };
struct LootObjectStack { std::vector<ObjectGuid> removed; void Remove(ObjectGuid guid) { removed.push_back(guid); } };
template<class T> struct Value { T value{}; T Get() { return value; } void Set(T const& next) { value = next; } };
struct Context
{
    Value<LootObject> target;
    LootObjectStack stack;
    Value<LootObjectStack*> available{&stack};
    template<class T> auto GetValue(std::string const&)
    {
        if constexpr (std::is_same_v<T, LootObject>) return &target;
        else return &available;
    }
};
struct CompanionFarmingState;
struct PlayerbotAI
{
    Player bot, master;
    Group group;
    bool managed = true;
    Context context;
    std::vector<Item*> items;
    std::shared_ptr<CompanionFarmingState> runtime;
    int cancelled = 0, changed = 0, training = 0;
    PlayerbotAI() { bot.ai = this; bot.group = master.group = &group; bot.guid.value = 2; bot.owner = &bot; }
    Player* GetBot() { return &bot; }
    Player* GetMaster() { return &master; }
    Context* GetAiObjectContext() { return &context; }
    auto const& GetInventoryItems() { return items; }
    void ChangeStrategy(char const*, int) { ++changed; }
};
bool IsCompanionInventoryManaged(PlayerbotAI* ai) { return ai && ai->managed; }
std::shared_ptr<CompanionFarmingState>& CompanionFarmRuntime(PlayerbotAI* ai) { return ai->runtime; }
void CancelCompanionErrands(PlayerbotAI* ai) { ++ai->cancelled; }
void RequestCompanionTraining(PlayerbotAI* ai) { ++ai->training; }
void SetCompanionNarrativeFocus(ObjectGuid, char const*) { }
std::mutex goalsMutex;
CompanionGoalStore history;
uint64 Now() { return 1000; }
CompanionGoals::Book* Find(Player*, bool create = false)
{
    if (!history.Ready) return nullptr;
    auto found = history.Books.find("owner:bot");
    if (found != history.Books.end()) return &found->second;
    return create ? &history.Books["owner:bot"] : nullptr;
}
#define LOG_INFO(...) ((void)0)
void ObserveCompanionFarming(PlayerbotAI*, uint64_t, std::string const&, uint32_t = 0, uint32_t = 0);
#include "assignment.inc"
namespace
{
#include "farm_owner.inc"
#include "carried.inc"
}
#include "state.inc"
#include "farm_helpers.inc"
#include "farm_update.inc"

struct PBC_EventItem { std::vector<uint32> respondingChars, silentCharGuids; };
enum class PBC_LogLevel { PBC_DEBUG };
#define PBC_Log(...) ((void)0)
#define GET_PLAYERBOT_AI(player) ((player)->ai)
std::vector<Player*> groupBots;
std::vector<Player*> PBC_FindGroupBots(Player*) { return groupBots; }
bool PBC_IsActiveSelfbot(Player*) { return false; }
std::mt19937 rng;
auto& PBC_GetRNG() { return rng; }
bool PBC_RollChance(uint32 chance) { return chance == 100; }
uint32 PBC_SnapshotCharacter(Player* player) { return player->GetGUID().GetCounter(); }
void PBC_RollBotsWithPenalty(PBC_EventItem& event, std::vector<Player*> const& bots, uint32 chance, char const*)
{
    for (Player* bot : bots)
        if (PBC_RollChance(chance)) event.respondingChars.push_back(bot->GetGUID().GetCounter());
}
#include "reactions.inc"

int main(int argc, char** argv)
{
    assert(argc == 2);
    using namespace CompanionFarm;
    for (auto text : {"Scrootch, va farmer", "va farmer de ton cote", "peux-tu aller farmer ?",
        "go farm on your own", "tu peux aller récolter seul ?"})
        assert(Parse(text) == Intent::Start);
    for (auto text : {"reviens avec moi", "rejoins-moi", "suis-moi", "on repart à l'aventure", "stop farming"})
        assert(Parse(text) == Intent::Return);
    for (auto text : {"ne va pas farmer", "ne reviens pas", "si tu reviens", "pourquoi farmer ?",
        "comment tu reviens ?", "il dit \"reviens\"", "tu pourrais peut-être aller farmer",
        "quand vas tu farmer ?", "tu vas farmer ?"})
        assert(Parse(text) == Intent::None);

    assert(Parse("how is the farming ?") == Intent::Status);
    assert(ResourceScore(500, false, 75, 75, 0, 15) < ResourceScore(100, false, 75, 1, 0, 15));
    assert(ResourceScore(1000, true, 75, 1, 0, 15) < ResourceScore(100, false, 75, 75, 0, 15));
    assert(ResourceScore(100, false, 0, 0, 15, 15) < ResourceScore(100, false, 0, 0, 5, 15));
    assert(ResourceScore(100, false, 75, 50, 0, 15) < ResourceScore(2000, false, 75, 50, 0, 15));

    auto path = std::filesystem::absolute(argv[1]);
    std::filesystem::remove(path);
    assert(history.Load(path));
    PlayerbotAI ai;
    Find(&ai.bot, true)->Apply(CompanionGoals::Domain::Gathering, CompanionGoals::Directive::Forbid, 900);
    auto order = GetCompanionFarmAssignment(&ai);
    order.mode = Mode::Working;
    order.status = "start_requested";
    assert(SetCompanionFarmAssignment(&ai, order));
    assert(Find(&ai.bot)->Permitted(CompanionGoals::Domain::Gathering, 1000));
    assert(!SetCompanionFarmAssignment(&ai, order)); // A stale order cannot overwrite a new one.
    CompanionGoalStore restart;
    assert(restart.Load(path));
    assert(restart.Books.at("owner:bot").farming.status == "start_requested");
    ai.bot.combat = true;
    UpdateCompanionFarming(&ai, 1000);
    assert(GetCompanionFarmAssignment(&ai).status == "awaiting_safe_start");
    ai.bot.combat = false;
    UpdateCompanionFarming(&ai, 1000);
    auto assigned = GetCompanionFarmAssignment(&ai);
    assert(assigned.map == 0 && assigned.zone == 85 && assigned.x == 50 && assigned.status == "searching");
    ObserveCompanionFarming(&ai, assigned.revision, "destination_selected", 0, 3433);
    assert(GetCompanionFarmAssignment(&ai).destinationZone == 3433);
    ai.master.map = 1;
    ai.master.x = 9000;
    ai.master.combat = true;
    UpdateCompanionFarming(&ai, 1000);
    assert(IsCompanionFarming(&ai)); // Owner distance, map and combat do not cancel the job.
    groupBots = {&ai.bot};
    PBC_EventItem quest;
    assert(!PBC_RollGroupBotsIntoEvent(quest, &ai.master, 100, "quest", true));
    assert(quest.respondingChars.empty()); // A detached worker does not comment on the owner's quests.
    ai.master.world = false;
    ai.master.teleport = true;
    UpdateCompanionFarming(&ai, 1000);
    assert(IsCompanionFarming(&ai)); // Loading a new map is not owner logout.
    ai.master.world = true;
    ai.master.teleport = false;
    ai.runtime->selected = true;
    ai.runtime->target.value = 21;
    ai.runtime->events.RescheduleEvent(Progress, Milliseconds(1));
    ai.bot.combat = true;
    UpdateCompanionFarming(&ai, 1);
    assert(ai.runtime->selected); // A fight suspends the travel watchdog.
    ai.bot.combat = false;
    UpdateCompanionFarming(&ai, 30000);
    assert(!ai.runtime->selected && ai.runtime->target.IsEmpty());
    Unit neutral, attacker;
    neutral.guid.value = 9;
    attacker.guid.value = 10;
    attacker.victim = &ai.bot;
    assert(!CompanionFarmAttackAllowed(&ai, &neutral));
    assert(CompanionFarmAttackAllowed(&ai, &attacker));
    auto& state = *ai.runtime;
    state.target = neutral.guid;
    state.selected = state.awaitingLoot = true;
    state.before = 3;
    Item materials{5};
    ai.items = {&materials};
    assert(FinishCompanionFarmLoot(&ai, neutral.guid));
    assert(GetCompanionFarmAssignment(&ai).harvested == 1 && !state.selected);
    state.target = neutral.guid;
    state.selected = state.awaitingLoot = true;
    state.before = 5;
    assert(FinishCompanionFarmLoot(&ai, neutral.guid));
    assert(GetCompanionFarmAssignment(&ai).harvested == 1); // A visit is not a successful harvest.
    state.source.kind = SourceKind::Creature;
    state.source.skin = 20;
    state.source.skill = SKILL_SKINNING;
    ai.bot.skin = true;
    state.target = neutral.guid;
    state.selected = state.awaitingLoot = true;
    assert(FinishCompanionFarmLoot(&ai, neutral.guid));
    assert(state.corpseLooted && state.selected && !state.awaitingLoot);
    state.skinning = state.awaitingLoot = true;
    materials.count = 6;
    assert(FinishCompanionFarmLoot(&ai, neutral.guid));
    assert(!state.selected && GetCompanionFarmAssignment(&ai).harvested == 2);

    state.target = neutral.guid;
    state.selected = state.awaitingLoot = true;
    ai.context.target.value.guid = neutral.guid;
    auto recall = GetCompanionFarmAssignment(&ai);
    recall.mode = Mode::Returning;
    recall.status = "return_requested";
    assert(SetCompanionFarmAssignment(&ai, recall));
    assert(restart.Load(path));
    assert(restart.Books.at("owner:bot").farming.mode == Mode::Returning);
    assert(restart.Books.at("owner:bot").farming.destinationZone == 3433);
    ObserveCompanionFarming(&ai, assigned.revision, "stale_gathering", 10);
    assert(GetCompanionFarmAssignment(&ai).status == "return_requested");
    assert(!IsCompanionFarmLoot(&ai, neutral.guid));
    UpdateCompanionFarming(&ai, 1000);
    assert(IsCompanionReturningFromFarm(&ai));
    assert(ai.bot.interrupts == 1 && ai.context.target.value.guid.IsEmpty());
    assert(!ai.runtime->selected && ai.changed == 2);
    ai.master.map = 0;
    ai.master.x = ai.bot.x;
    ai.master.phase = 2;
    UpdateCompanionFarming(&ai, 1000);
    assert(IsCompanionReturningFromFarm(&ai));
    ai.master.phase = 1;
    ai.bot.combat = true;
    UpdateCompanionFarming(&ai, 1000);
    assert(IsCompanionReturningFromFarm(&ai));
    ai.bot.combat = false;
    UpdateCompanionFarming(&ai, 1000);
    assert(GetCompanionFarmAssignment(&ai).mode == Mode::Follow);
    assert(GetCompanionFarmAssignment(&ai).status == "returned");
    assert(PBC_RollGroupBotsIntoEvent(quest, &ai.master, 100, "quest", true));
    assert(quest.respondingChars.size() == 1);
    UpdateCompanionFarming(&ai, 1000);
    assert(!ai.runtime);

    auto oldPath = history.Path;
    history.Path = path / "missing-directory/history.json";
    order = GetCompanionFarmAssignment(&ai);
    order.mode = Mode::Working;
    order.status = "start_requested";
    assert(!SetCompanionFarmAssignment(&ai, order));
    assert(!IsCompanionFarming(&ai)); // Failed persistence never starts an unrecorded job.
    history.Path = oldPath;
    assert(SetCompanionFarmAssignment(&ai, order));
    UpdateCompanionFarming(&ai, 0);
    ai.master.group = nullptr;
    UpdateCompanionFarming(&ai, 0);
    assert(!IsCompanionFarming(&ai) && !ai.runtime);

    assert(history.Save());
    auto json = pbc_json::parse(std::ifstream(path));
    auto legacy = json;
    legacy["characters"]["owner:bot"].erase("farming");
    std::ofstream(path) << legacy.dump();
    assert(restart.Load(path));
    assert(restart.Books.at("owner:bot").farming.mode == Mode::Follow);
    json["characters"]["owner:bot"]["farming"][0] = uint32(1);
    json["characters"]["owner:bot"]["farming"][6] = "gathering";
    std::ofstream(path) << json.dump();
    assert(restart.Load(path));
    assert(restart.Books.at("owner:bot").farming.status == "restart_revalidate");
    json["characters"]["owner:bot"]["farming"][3] = 200000;
    std::ofstream(path) << json.dump();
    assert(!restart.Load(path) && restart.Books.empty());
    json["characters"]["owner:bot"]["farming"][3] = 50;
    json["characters"]["owner:bot"]["farming"][0] = uint32(8);
    std::ofstream(path) << json.dump();
    assert(!restart.Load(path));
    std::filesystem::remove(path);
    std::cout << "PASS: actual assignment persistence, safe start, independent work, native loot confirmation and recall\n";
}
