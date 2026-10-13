#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <ctime>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

using uint8 = uint8_t;
using uint32 = uint32_t;
using uint64 = uint64_t;
using ObjectGuid = uint32;
constexpr uint8 GUILD_BANK_MAX_TABS = 2, NULL_SLOT = 255;
constexpr int GAMEOBJECT_TYPE_GUILD_BANK = 1, GUILD_BANK_RIGHT_DEPOSIT_ITEM = 1;
namespace CompanionGoals
{
    enum class Domain { Guild };
    enum class State { Opportunity, Done };
}
struct ItemTemplate { uint32 ItemId = 1; };
struct Item
{
    ItemTemplate proto;
    uint32 count = 0, keep = 20;
    uint8 slot = 23;
    uint32 GetEntry() const
    {
        return proto.ItemId;
    }
    ItemTemplate const* GetTemplate() const
    {
        return &proto;
    }
    uint8 GetBagSlot() const
    {
        return 255;
    }
    uint8 GetSlot() const
    {
        return slot;
    }
};
struct GameObject { } bank;
struct Session { bool IsLoggingOut() const { return false; } } session;
struct Player
{
    uint32 guid = 1;
    bool moving = false, access = true;
    std::vector<Item*> items;
    bool IsInWorld() const
    {
        return true;
    }
    bool IsInCombat() const
    {
        return false;
    }
    bool IsAlive() const
    {
        return true;
    }
    void* GetGroup() const
    {
        return reinterpret_cast<void*>(1);
    }
    uint32 GetGuildId() const
    {
        return 1;
    }
    void* GetMap() const
    {
        return reinterpret_cast<void*>(1);
    }
    void* GetTradeData() const
    {
        return nullptr;
    }
    bool IsBeingTeleported() const
    {
        return false;
    }
    Session* GetSession()
    {
        return &session;
    }
    bool IsNonMeleeSpellCast(bool) const
    {
        return false;
    }
    bool isMoving() const
    {
        return moving;
    }
    GameObject* GetGameObjectIfCanInteractWith(ObjectGuid, int)
    {
        return access ? &bank : nullptr;
    }
    float GetDistance(GameObject*) const
    {
        return 0;
    }
    ObjectGuid GetGUID() const
    {
        return guid;
    }
    std::string GetName() const
    {
        return "test";
    }
    uint32 GetItemCount(uint32 entry, bool) const
    {
        uint32 count = 0;
        for (auto const* item : items)
            if (item->GetEntry() == entry)
                count += item->count;
        return count;
    }
};
struct GuidCounter
{
    uint32 value;
    uint32 GetCounter() const
    {
        return value;
    }
};
// The native code's GUID counter is needed only by unchanged quota/diagnostic paths.
struct PlayerWithGuid : Player
{
    GuidCounter GetGUID() const { return {guid}; }
};
struct PlayerbotAI
{
    PlayerWithGuid* bot;
    PlayerWithGuid* master;
    PlayerWithGuid* GetBot()
    {
        return bot;
    }
    PlayerWithGuid* GetMaster()
    {
        return master;
    }
    GameObject* GetGameObject(ObjectGuid)
    {
        return &bank;
    }
    std::vector<Item*> GetInventoryItems()
    {
        return bot->items;
    }
};
PlayerWithGuid bot, master;
PlayerbotAI ai{&bot, &master};
namespace ObjectAccessor
{
    PlayerWithGuid* FindPlayer(ObjectGuid guid)
    {
        return guid == 1 ? &bot : &master;
    }
}
#define Player PlayerWithGuid
#define GET_PLAYERBOT_AI(player) ((player) == &bot ? &::ai : nullptr)
#define LOG_INFO(...)
bool IsCompanionInventoryManaged(PlayerbotAI* value)
{
    return value == &ai;
}
bool CompanionGoalAllowed(PlayerbotAI*, CompanionGoals::Domain)
{
    return true;
}
uint32 finished = 0, changed = 0;
bool retryRequested = false;
void FinishCompanionInventoryStorage(PlayerbotAI*, bool retry = false)
{
    ++finished;
    retryRequested = retry;
}
void NotifyCompanionInventoryChanged(PlayerbotAI*)
{
    ++changed;
}
std::string reason;
void ObserveCompanionGoal(PlayerbotAI*, CompanionGoals::Domain, CompanionGoals::State,
    std::string const& value, int = 0, uint32 = 0) { reason = value; }
struct Guild
{
    bool permits = true, full = false;
    uint32 donated = 0;
    bool MemberHasTabRights(ObjectGuid, uint8, int) const
    {
        return permits;
    }
    void SwapItemsWithInventory(Player* player, bool, uint8, uint8, uint8, uint8 slot, uint32 amount)
    {
        if (full)
            return;
        for (auto* item : player->items)
            if (item->slot == slot)
            {
                assert(amount <= item->count);
                item->count -= amount;
                donated += amount;
                return;
            }
    }
} guild;
struct GuildMgr { Guild* GetGuildById(uint32) { return &guild; } } manager;
auto* sGuildMgr = &manager;
std::atomic<bool> enabled{true};
std::mutex stockMutex;
std::map<ObjectGuid, uint64> nextVisit;
constexpr uint64 VisitInterval = 300;
struct InventoryPlan { };
InventoryPlan PlanInventory(Player*) { return {}; }
uint32 DepositSurplus(PlayerbotAI*, Item* item, InventoryPlan const&)
{
    return item->count > item->keep ? item->count - item->keep : 0;
}
bool HasCompanionGuildDeposit(PlayerbotAI*)
{
    for (auto* item : bot.items)
        if (DepositSurplus(&ai, item, {}))
            return true;
    return false;
}
struct BankItem { };
std::vector<BankItem> ReadBank(Guild*, Player*) { return {}; }
uint32 withdrawals = 0;
bool WithdrawArticle(Guild*, PlayerbotAI*, std::vector<BankItem> const&, std::set<uint32> const&, uint64)
{
    ++withdrawals;
    return false;
}
bool WithdrawMaterials(Guild*, PlayerbotAI*, std::vector<BankItem> const&, uint64)
{
    ++withdrawals;
    return false;
}
bool EnhancementSpell(ItemTemplate const*)
{
    return false;
}
std::vector<Item*> EnhancementTargets(Player*, ItemTemplate const*) { return {}; }
struct BankUseItem
{
    explicit BankUseItem(PlayerbotAI*) { }
    bool Enhance(Item*, Item*)
    {
        return false;
    }
};
struct PlayerbotOperation
{
    virtual ObjectGuid GetBotGuid() const = 0;
    virtual std::string GetName() const = 0;
    virtual bool Execute() = 0;
};
#include "bank.inc"

int main()
{
    master.guid = 2;
    Item material;
    material.count = 45;
    bot.items = {&material};
    nextVisit[1] = uint64(std::time(nullptr)) + 300;
    BankOperation deposit(1, 2, 3, true);
    assert(deposit.Execute()); // Urgent deposits bypass only the ordinary visit cooldown.
    assert(material.count == 20 && guild.donated == 25 && withdrawals == 0 && finished == 1);
    assert(!retryRequested && reason == "guild_items_transferred");
    BankOperation normal(1, 2, 3, false);
    assert(!normal.Execute() && reason == "guild_visit_cooldown");
    assert(withdrawals == 0 && finished == 1);
    material.count = 40;
    guild.permits = false;
    assert(deposit.Execute());
    assert(material.count == 40 && finished == 2 && reason == "no_permitted_useful_transfer");
    guild.permits = true;
    guild.full = true;
    assert(deposit.Execute());
    assert(material.count == 40 && finished == 3 && withdrawals == 0);
    guild.full = false;
    bot.access = false;
    assert(!deposit.Execute());
    assert(finished == 4 && reason == "guild_bank_not_accessible");
    bot.access = true;
    nextVisit[1] = 0;
    assert(normal.Execute());
    assert(material.count == 20 && withdrawals == 2 && changed == 1);
}
