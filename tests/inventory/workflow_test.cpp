#include <algorithm>
#include <cassert>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include "CompanionInventoryPolicy.h"

using uint8 = uint8_t;
using uint16 = uint16_t;
using uint32 = uint32_t;
using uint64 = uint64_t;
using ObjectGuid = uint32;
constexpr uint8 INVENTORY_SLOT_BAG_0 = 255, INVENTORY_SLOT_ITEM_START = 23, INVENTORY_SLOT_ITEM_END = 39;
constexpr uint8 INVENTORY_SLOT_BAG_START = 19, INVENTORY_SLOT_BAG_END = 23;
constexpr int EQUIP_ERR_OK = 0;
enum class GoalDomain { Guild };
struct ItemTemplate
{
    uint32 ItemId = 1, maximum = 20, BagFamily = 0;
    uint32 GetMaxStackSize() const
    {
        return maximum;
    }
};
struct Item
{
    ItemTemplate proto;
    uint32 guid = 0, count = 1;
    uint8 bag = 255, slot = 23;
    bool removed = false, trade = false, wrapped = false, refund = false, bop = false, sale = false;
    int property = 0;
    uint32 GetEntry() const
    {
        return proto.ItemId;
    }
    uint32 GetGUID() const
    {
        return guid;
    }
    uint32 GetCount() const
    {
        assert(!removed); return count;
    }
    ItemTemplate const* GetTemplate() const
    {
        return &proto;
    }
    uint8 GetBagSlot() const
    {
        return bag;
    }
    uint8 GetSlot() const
    {
        return slot;
    }
    int GetItemRandomPropertyId() const
    {
        return property;
    }
    bool IsInTrade() const
    {
        return trade;
    }
    bool IsWrapped() const
    {
        return wrapped;
    }
    bool IsRefundable() const
    {
        return refund;
    }
    bool IsBOPTradable() const
    {
        return bop;
    }
    int CanBeMergedPartlyWith(ItemTemplate const* other) const
    {
        return proto.ItemId == other->ItemId ? 0 : 1;
    }
};
struct Bag
{
    ItemTemplate proto;
    uint32 free = 0;
    ItemTemplate const* GetTemplate() const
    {
        return &proto;
    }
    uint32 GetFreeSlots() const
    {
        return free;
    }
};
struct Player
{
    std::map<uint16, Item*> items;
    std::map<uint8, Bag*> bags;
    uint32 guild = 1, moves = 0;
    bool refuse = false;
    static bool IsInventoryPos(uint8 bag, uint8 slot)
    {
        return bag != 255 || (slot >= 23 && slot < 39);
    }
    uint32 GetGuildId() const
    {
        return guild;
    }
    Item* GetItemByPos(uint8 bag, uint8 slot) const
    {
        auto found = items.find(uint16(bag << 8) | slot);
        return found == items.end() ? nullptr : found->second;
    }
    Item* GetItemByGuid(ObjectGuid guid) const
    {
        for (auto const& [pos, item] : items)
            if (item->guid == guid)
                return item;
        return nullptr;
    }
    Bag* GetBagByPos(uint8 slot) const
    {
        auto found = bags.find(slot);
        return found == bags.end() ? nullptr : found->second;
    }
    void Put(uint8 slot, Item& item)
    {
        item.slot = slot;
        items[uint16(255 << 8) | slot] = &item;
    }
    void SwapItem(uint16 from, uint16 to)
    {
        if (refuse)
            return;
        auto* source = items.at(from);
        auto* target = items.at(to);
        assert(source->count + target->count <= target->proto.maximum);
        target->count += source->count;
        source->removed = true;
        items.erase(from);
        ++moves;
    }
    void SplitItem(uint16 from, uint16 to, uint32 count)
    {
        if (refuse)
            return;
        auto* source = items.at(from);
        auto* target = items.at(to);
        assert(count < source->count && target->count + count <= target->proto.maximum);
        source->count -= count;
        target->count += count;
        ++moves;
    }
};
struct CompanionErrandState
{
    bool townDue = false, inventoryDue = false, bagInventoryDue = false;
    uint64 storageAttemptSignature = 0;
    std::set<ObjectGuid> failedSales;
};
struct PlayerbotAI
{
    Player bot, master;
    bool managed = true, surplus = false, guildAllowed = true;
    std::shared_ptr<CompanionErrandState> companionErrands = std::make_shared<CompanionErrandState>();
    Player* GetBot()
    {
        return &bot;
    }
    Player* GetMaster()
    {
        return &master;
    }
    std::vector<Item*> GetInventoryItems()
    {
        std::vector<Item*> result;
        for (auto const& [pos, item] : bot.items)
            result.push_back(item);
        return result;
    }
};
bool bankEnabled = true;
bool IsCompanionInventoryManaged(PlayerbotAI* ai)
{
    return ai && ai->managed;
}
bool CanSellCompanionItem(PlayerbotAI*, Item* item)
{
    return item->sale;
}
bool CompanionGuildBankEnabled()
{
    return bankEnabled;
}
bool CompanionGoalAllowed(PlayerbotAI* ai, GoalDomain)
{
    return ai->guildAllowed;
}
bool HasCompanionGuildDeposit(PlayerbotAI* ai)
{
    return ai->surplus;
}
#include "free.inc"
namespace
{
#include "workflow.inc"
#include "stacks.inc"

struct Creature;
struct VendorItem;
struct PlayerScript
{
    explicit PlayerScript(char const*) { }
    virtual void OnPlayerStoreNewItem(Player*, Item*, uint32) { }
    virtual void OnPlayerAfterMoveItemFromInventory(Player*, Item*, uint8, uint8, bool) { }
    virtual void OnPlayerAfterMoveItemToInventory(Player*, Item*, bool) { }
    virtual void OnPlayerEquip(Player*, Item*, uint8, uint8, bool) { }
    virtual void OnPlayerUnequip(Player*, Item*) { }
    virtual void OnPlayerAfterStoreOrEquipNewItem(Player*, uint32, Item*, uint8, uint8, uint8,
        ItemTemplate const*, Creature*, VendorItem const*, bool) { }
};
Player* hookedPlayer = nullptr;
PlayerbotAI* hookedAI = nullptr;
#define GET_PLAYERBOT_AI(player) ((player) == hookedPlayer ? hookedAI : nullptr)
#include "hooks.inc"

int main()
{
    using CompanionInventory::Step;
    for (bool sales : {false, true})
        for (bool deposits : {false, true})
        {
            assert(CompanionInventory::Next(false, sales, deposits) == Step::None);
            assert(!CompanionInventory::BuyBags(false, Step::None));
        }
    PlayerbotAI ai;
    std::vector<Item> stock(16);
    for (uint8 index = 0; index < 16; ++index)
    {
        stock[index].guid = index + 1;
        stock[index].proto.ItemId = index < 3 ? 10 : 20 + index;
        stock[index].count = index < 3 ? 12 : 1;
        ai.bot.Put(23 + index, stock[index]);
    }
    assert(FreeGeneralBagSlots(&ai.bot) == 0);
    assert(InventoryCleanupStep(&ai, false) == Step::Expand);
    assert(InventoryCleanupStep(&ai, true) == Step::Sell);
    ai.surplus = true;
    assert(InventoryCleanupStep(&ai, false) == Step::Deposit);
    assert(!CompanionInventory::BuyBags(false, Step::Deposit));
    assert(CompanionInventory::BuyBags(true, Step::Deposit));
    stock[3].sale = true;
    assert(HasCompanionSale(&ai));
    ai.companionErrands->failedSales.insert(stock[3].guid);
    assert(!HasCompanionSale(&ai));
    FinishCompanionInventoryStorage(&ai);
    assert(InventoryCleanupStep(&ai, false) == Step::Expand); // Full/denied bank was actually tried.
    ++stock[4].count;
    assert(InventoryCleanupStep(&ai, false) == Step::Deposit); // A genuinely changed stock can retry.
    FinishCompanionInventoryStorage(&ai, true);
    assert(InventoryCleanupStep(&ai, false) == Step::Deposit); // A bounded successful batch can continue.
    ai.guildAllowed = false;
    assert(InventoryCleanupStep(&ai, false) == Step::Expand);
    ai.guildAllowed = true;
    ai.master.guild = 2;
    assert(InventoryCleanupStep(&ai, false) == Step::Expand);
    ai.master.guild = 1;

    ai.bot.refuse = true;
    ConsolidateCompanionStacks(&ai);
    assert(FreeGeneralBagSlots(&ai.bot) == 0 && ai.bot.moves == 0);
    ai.bot.refuse = false;
    stock[1].trade = true;
    stock[2].wrapped = true;
    ConsolidateCompanionStacks(&ai);
    assert(ai.bot.moves == 0);
    stock[1].trade = stock[2].wrapped = false;
    ConsolidateCompanionStacks(&ai);
    assert(FreeGeneralBagSlots(&ai.bot) == 1 && ai.bot.moves == 2);
    assert(stock[0].count == 20 && stock[1].count == 16 && stock[2].removed);
    assert(InventoryCleanupStep(&ai, false) == Step::None);
    auto moves = ai.bot.moves;
    ConsolidateCompanionStacks(&ai);
    assert(ai.bot.moves == moves);
    ai.companionErrands->bagInventoryDue = false;
    ai.managed = false;
    NotifyCompanionInventoryChanged(&ai);
    assert(!ai.companionErrands->bagInventoryDue);
    ai.managed = true;
    NotifyCompanionInventoryChanged(&ai);
    assert(ai.companionErrands->bagInventoryDue && ai.companionErrands->inventoryDue);
    hookedPlayer = &ai.bot;
    hookedAI = &ai;
    CompanionInventoryScript hook;
    ai.companionErrands->bagInventoryDue = false;
    hook.OnPlayerStoreNewItem(&ai.bot, &stock[0], 1);
    assert(ai.companionErrands->bagInventoryDue);
    ai.companionErrands->bagInventoryDue = false;
    hook.OnPlayerStoreNewItem(&ai.master, &stock[0], 1);
    assert(!ai.companionErrands->bagInventoryDue);
    hook.OnPlayerEquip(&ai.bot, &stock[0], 255, 19, true);
    assert(ai.companionErrands->bagInventoryDue);
}
