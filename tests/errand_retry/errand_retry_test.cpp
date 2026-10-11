#include <cassert>
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <vector>

using uint32 = uint32_t;
using uint64 = uint64_t;
using Milliseconds = uint32;
enum Timer : uint32
{
    Decision = 1, TownScan, HerbScan, Deadline, Progress, Resume, ClearRejected, VerifyLesson,
    MountRetry, InventoryScan, CraftCooldown, CraftReset
};
enum class Errand
{
    None, Trainer, Herb, Return, PoisonVendor, Bags, ProfessionVendor, GuildBank, MountVendor,
    EquipmentVendor, Craft, BagVendor
};
enum class GoalDomain { Bags, Supplies, Count };
enum class GoalState { Opportunity, Failed };
struct EventMap
{
    std::vector<uint32> due;
    void Update(uint32) { }
    uint32 ExecuteEvent()
    {
        if (due.empty())
            return 0;
        auto event = due.front();
        due.erase(due.begin());
        return event;
    }
    void ScheduleEvent(uint32, uint32) { }
    void RescheduleEvent(uint32, uint32) { }
    void CancelEvent(uint32) { }
};
struct CompanionErrandState
{
    EventMap events;
    Errand kind = Errand::None;
    GoalDomain goal = GoalDomain::Count;
    uint64 goalRevision = 0;
    uint32 observedMoney = 100;
    uint32 pendingLesson = 0, learned = 0, reserve = 0, crafts = 0;
    uint32 spawn = 42, owner = 1, map = 1, zone = 1, settlement = 1;
    float x = 0, y = 0, z = 0;
    bool townDue = false, herbDue = false, inventoryDue = false, ready = false;
    bool paused = false, craftDue = false, warnedBags = false, mountDue = false, verificationDue = false;
    std::set<uint32> rejectedTrainers, rejectedSupplyVendors, rejectedBagVendors, rejectedVendors;
    std::set<uint32> failedSales, rejectedLessons, rejectedHerbs, rejectedCrafts;
};
struct Player
{
    uint32 money = 100;
    uint32 GetMoney() const { return money; }
    uint32 GetGUID() const { return 1; }
    uint32 GetMapId() const { return 1; }
    uint32 GetZoneId() const { return 1; }
    float GetDistance(float, float, float) const { return 0; }
    float GetDistance(Player*) const { return 0; }
};
struct PlayerbotAI
{
    std::shared_ptr<CompanionErrandState> companionErrands = std::make_shared<CompanionErrandState>();
    Player bot, master;
    uint64 revision = 0;
    bool reconsider = false;
    bool eligible = true;
    std::vector<std::string> notices;
    Player* GetBot() { return &bot; }
    Player* GetMaster() { return &master; }
    void TellMaster(std::string const& text) { notices.push_back(text); }
};
constexpr float TownRadius = 1500;
uint32 observations = 0;
GoalState lastState = GoalState::Opportunity;
std::string lastReason;
bool ReconsiderCompanionGoals(PlayerbotAI* ai) { return ai->reconsider; }
uint64 CompanionGoalRevision(PlayerbotAI* ai) { return ai->revision; }
bool IsCompanionInventoryManaged(PlayerbotAI*) { return true; }
bool CompanionGoalAllowed(PlayerbotAI*, GoalDomain) { return true; }
bool Eligible(PlayerbotAI* ai) { return ai->eligible; }
bool InTown(Player*) { return true; }
uint32 Settlement(Player*) { return 1; }
std::string GetCompanionNarrativeFocus(uint32) { return "normal"; }
void RecordTraining(Player*, std::string const&, uint32, uint32, uint32) { }
void StopTrip(PlayerbotAI* ai, bool) { ai->companionErrands->kind = Errand::None; }
void CancelCompanionErrands(PlayerbotAI* ai) { StopTrip(ai, false); }
void ObserveCompanionGoal(PlayerbotAI* ai, GoalDomain, GoalState state, std::string const& reason)
{
    ++observations;
    ++ai->revision;
    lastState = state;
    lastReason = reason;
}
void Returning(PlayerbotAI* ai) { StopTrip(ai, false); }
#include "finish.inc"
#include "update.inc"

void RejectAll(PlayerbotAI& ai)
{
    auto& state = *ai.companionErrands;
    state.rejectedTrainers.insert(42);
    state.rejectedSupplyVendors.insert(42);
    state.rejectedBagVendors.insert(42);
    state.rejectedVendors.insert(42);
}
void AssertRejected(PlayerbotAI& ai, bool rejected)
{
    auto& state = *ai.companionErrands;
    assert(bool(state.rejectedTrainers.count(42)) == rejected);
    assert(bool(state.rejectedSupplyVendors.count(42)) == rejected);
    assert(bool(state.rejectedBagVendors.count(42)) == rejected);
    assert(bool(state.rejectedVendors.count(42)) == rejected);
}
int main()
{
    PlayerbotAI ai;
    RejectAll(ai);
    // Ordinary goal progress used to invalidate every exhausted vendor, even without any new funds.
    for (int tick = 0; tick < 40; ++tick)
    {
        ++ai.revision;
        UpdateCompanionErrands(&ai, 1000);
        AssertRejected(ai, true);
    }
    // Our own purchases decrease funds and must not reopen the same failing offers.
    ai.bot.money = 80;
    UpdateCompanionErrands(&ai, 1000);
    AssertRejected(ai, true);
    // Receiving funds, a real blocked-condition change or an explicit new request can reopen offers.
    ai.bot.money = 120;
    UpdateCompanionErrands(&ai, 1000);
    AssertRejected(ai, false);
    RejectAll(ai);
    ai.reconsider = true;
    UpdateCompanionErrands(&ai, 1000);
    AssertRejected(ai, false);
    ai.reconsider = false;
    RejectAll(ai);
    RequestCompanionTraining(&ai);
    AssertRejected(ai, false);
    assert(ai.companionErrands->townDue && ai.companionErrands->ready);
    RejectAll(ai);
    ai.companionErrands->events.due.push_back(ClearRejected);
    UpdateCompanionErrands(&ai, 300000);
    AssertRejected(ai, false);

    for (auto kind : {Errand::BagVendor, Errand::ProfessionVendor, Errand::EquipmentVendor,
        Errand::MountVendor, Errand::PoisonVendor})
    {
        PlayerbotAI vendorAI;
        auto& state = *vendorAI.companionErrands;
        state.kind = kind;
        state.goal = GoalDomain::Supplies;
        FinishVendorVisit(&vendorAI, false);
        assert(lastState == GoalState::Opportunity && lastReason == "no_available_vendor_purchase");
        assert(state.kind == Errand::None && state.goal == GoalDomain::Count);
        assert(vendorAI.notices.empty());
        UpdateCompanionErrands(&vendorAI, 1000);
        assert(kind == Errand::PoisonVendor ? state.rejectedVendors.count(42) :
            state.rejectedSupplyVendors.count(42));
        state.kind = kind;
        state.goal = GoalDomain::Supplies;
        FinishVendorVisit(&vendorAI, true);
        assert(lastState == GoalState::Failed && lastReason == "native_purchase_not_confirmed");
        assert(vendorAI.notices.empty());
        // A visit with a previously confirmed outcome must not overwrite that result on departure.
        auto before = observations;
        state.kind = kind;
        FinishVendorVisit(&vendorAI, false);
        assert(observations == before);
    }
}
