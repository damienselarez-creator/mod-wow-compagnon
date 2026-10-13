#include <algorithm>
#include <cassert>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "CompanionConsumablePolicy.h"

using uint8 = uint8_t;
using uint16 = uint16_t;
using uint32 = uint32_t;
using int32 = int32_t;
constexpr uint8 INVENTORY_SLOT_BAG_0 = 255, INVENTORY_SLOT_BAG_START = 19, INVENTORY_SLOT_BAG_END = 23;
constexpr uint8 INVENTORY_SLOT_ITEM_START = 23, INVENTORY_SLOT_ITEM_END = 39, NULL_SLOT = 255;
constexpr int EQUIP_ERR_OK = 0, ITEM_QUALITY_POOR = 0, ITEM_QUALITY_RARE = 3;
constexpr int ITEM_CLASS_CONSUMABLE = 0, ITEM_CLASS_QUEST = 12, ITEM_CLASS_WEAPON = 2;
constexpr int ITEM_CLASS_PROJECTILE = 6, ITEM_CLASS_KEY = 13, ITEM_SUBCLASS_SCROLL = 4;
constexpr int ITEM_SUBCLASS_WEAPON_MISC = 14, EQUIPMENT_SLOT_MAINHAND = 15;
constexpr int ITEM_FIELD_MAXDURABILITY = 0, ITEM_FIELD_DURABILITY = 1;
constexpr int MAX_QUEST_LOG_SIZE = 25, MAX_INSPECTED_ENCHANTMENT_SLOT = 2, PLAYERSPELL_REMOVED = 1;
using EnchantmentSlot = int;
using ItemPosCountVec = std::vector<uint16>;
uint16 Pos(uint8 bag, uint8 slot) { return uint16(bag << 8) | slot; }
struct ItemTemplate
{
    uint32 ContainerSlots = 0, BagFamily = 0, ItemId = 1, SellPrice = 1, StartQuest = 0, TotemCategory = 0;
    int Quality = 1, Class = 1, SubClass = 0, InventoryType = 0;
};
struct Item
{
    ItemTemplate proto;
    uint8 bag = 255, slot = 23;
    bool trade = false, wrapped = false, usable = true;
    uint32 enchantment = 0;
    std::map<uint8, Item*> contents;
    explicit Item(uint32 size = 0, uint32 family = 0) { proto.ContainerSlots = size; proto.BagFamily = family; }
    bool IsBag() const { return proto.ContainerSlots; }
    bool IsNotEmptyBag() const { return IsBag() && !contents.empty(); }
    bool IsInTrade() const { return trade; }
    bool IsWrapped() const { return wrapped; }
    ItemTemplate const* GetTemplate() const { return &proto; }
    uint32 GetBagSize() const { return proto.ContainerSlots; }
    uint8 GetBagSlot() const { return bag; }
    uint8 GetSlot() const { return slot; }
    uint32 GetEntry() const { return proto.ItemId; }
    uint32 GetEnchantmentId(int) const { return enchantment; }
    uint32 GetUInt32Value(int) const { return 0; }
    int GetItemRandomPropertyId() const { return 0; }
};
using Bag = Item;
struct Session { bool logout = false; bool IsLoggingOut() const { return logout; } };
struct Guid { bool empty = true; bool IsEmpty() const { return empty; } };
struct GroupReference;
struct Group { GroupReference* GetFirstMember() { return nullptr; } };
struct GroupReference
{
    GroupReference* next() { return nullptr; }
    struct Player* GetSource() { return nullptr; }
};
struct KnownSpell { int State = 0; };
struct Player
{
    std::map<uint8, Item*> backpack;
    std::map<uint32, KnownSpell*> spells;
    Session session;
    Guid loot;
    bool alive = true, combat = false, world = true, cast = false, teleport = false, flight = false;
    bool flying = false, falling = false, trading = false, blocked = false;
    uint32 swaps = 0, phase = 1;
    void* map = reinterpret_cast<void*>(1);
    Item* GetItemByPos(uint8 bag, uint8 slot) const
    {
        auto const& storage = bag == 255 ? backpack : GetBagByPos(bag)->contents;
        auto found = storage.find(slot);
        return found == storage.end() ? nullptr : found->second;
    }
    Bag* GetBagByPos(uint8 slot) const
    {
        auto found = backpack.find(slot);
        return found == backpack.end() ? nullptr : found->second;
    }
    void Put(uint8 bag, uint8 slot, Item* item)
    {
        auto& storage = bag == 255 ? backpack : GetBagByPos(bag)->contents;
        if (item)
        {
            storage[slot] = item;
            item->bag = bag;
            item->slot = slot;
        }
        else
            storage.erase(slot);
    }
    void SwapItem(uint16 source, uint16 destination)
    {
        ++swaps;
        if (blocked || combat || !alive)
            return;
        uint8 sb = source >> 8, ss = uint8(source), db = destination >> 8, ds = uint8(destination);
        Item* src = GetItemByPos(sb, ss);
        Item* dst = GetItemByPos(db, ds);
        if (!src || src->trade || (dst && dst->trade))
            return;
        bool bagSlot = db == 255 && ds >= 19 && ds < 23;
        if (bagSlot)
        {
            if (!src->IsBag() || src->IsNotEmptyBag() || sb == ds || !src->usable)
                return;
            if (dst)
            {
                // Native swap transfers contents before putting the old empty bag into the backpack.
                if (sb != 255 || dst->contents.size() > src->GetBagSize())
                    return;
                src->contents = std::move(dst->contents);
                dst->contents.clear();
                for (auto const& pair : src->contents)
                {
                    pair.second->bag = ds;
                    pair.second->slot = pair.first;
                }
            }
        }
        Put(sb, ss, dst);
        Put(db, ds, src);
    }
    int CanUseItem(ItemTemplate const*) const { return EQUIP_ERR_OK; }
    int CanStoreItem(uint8 bag, uint8 slot, ItemPosCountVec&, Item*, bool) const
    {
        return GetBagByPos(bag) && slot < GetBagByPos(bag)->GetBagSize() ? EQUIP_ERR_OK : 1;
    }
    static bool IsInventoryPos(uint8 bag, uint8 slot)
    { return bag != 255 || (slot >= 23 && slot < 39); }
    uint32 GetQuestSlotQuestId(uint8) const { return 0; }
    auto const& GetSpellMap() const { return spells; }
    bool IsInWorld() const { return world; }
    Session* GetSession() { return &session; }
    bool IsAlive() const { return alive; }
    bool IsBeingTeleported() const { return teleport; }
    void* GetMap() const { return map; }
    uint32 GetPhaseMask() const { return phase; }
    bool IsInCombat() const { return combat; }
    void* GetTradeData() const { return trading ? map : nullptr; }
    bool IsInFlight() const { return flight; }
    bool IsFlying() const { return flying; }
    bool IsFalling() const { return falling; }
    void* GetTransport() const { return nullptr; }
    void* GetVehicle() const { return nullptr; }
    bool IsNonMeleeSpellCast(bool) const { return cast; }
    Guid GetLootGUID() const { return loot; }
    Group* GetGroup() const { return nullptr; }
    std::string GetName() const { return "Companion"; }
};
struct Quest
{
    std::vector<uint32> RequiredItemId;
    uint32 source = 0;
    uint32 GetSrcItemId() const { return source; }
};
struct ObjectMgr
{
    Quest* quest = nullptr;
    Quest const* GetQuestTemplate(uint32) const { return quest; }
} objectMgr;
ObjectMgr* sObjectMgr = &objectMgr;
struct SpellInfo { std::vector<int32> Reagent; std::vector<uint32> Totem; };
struct SpellMgr
{
    SpellInfo* spell = nullptr;
    SpellInfo const* GetSpellInfo(uint32) const { return spell; }
} spellMgr;
SpellMgr* sSpellMgr = &spellMgr;
enum class GoalDomain { Bags };
enum class GoalState { Done, Verifying };
struct CompanionErrandState { bool inventoryDue = false, townDue = false; };
enum ItemUsage { ITEM_USAGE_NONE, ITEM_USAGE_VENDOR, ITEM_USAGE_AH };
struct Context
{
    template<class T> Context* GetValue(char const*, uint32) { return this; }
    ItemUsage Get() const { return ITEM_USAGE_NONE; }
};
struct PlayerbotAI
{
    Player bot, master;
    bool managed = true, permitted = true, pendingFlight = false, requested = false;
    std::shared_ptr<CompanionErrandState> companionErrands = std::make_shared<CompanionErrandState>();
    Context context;
    Player* GetBot() { return &bot; }
    Player* GetMaster() { return &master; }
    Context* GetAiObjectContext() { return &context; }
    uint8 FindEquipSlot(ItemTemplate const*, int, bool) const { return NULL_SLOT; }
};
bool IsCompanionInventoryManaged(PlayerbotAI* ai) { return ai && ai->managed; }
bool HasPendingCompanionFlight(PlayerbotAI* ai) { return ai->pendingFlight; }
bool CompanionGoalPermitted(PlayerbotAI* ai, GoalDomain) { return ai->permitted; }
bool CompanionGoalRequested(PlayerbotAI* ai, GoalDomain) { return ai->requested; }
uint32 CompanionGoalTarget(PlayerbotAI*, GoalDomain) { return 64; }
uint32 observations = 0;
void ObserveCompanionGoal(PlayerbotAI*, GoalDomain, GoalState, std::string const&, int, uint32) { ++observations; }
uint32 CompanionBagCapacity(Player* bot)
{
    uint32 capacity = 16;
    for (uint8 slot = 19; slot < 23; ++slot)
        if (Bag* bag = bot->GetBagByPos(slot); bag && !bag->proto.BagFamily)
            capacity += bag->GetBagSize();
    return capacity;
}
#define LOG_INFO(...) do { } while (false)
struct Profile { CompanionConsumables::Kind kind = CompanionConsumables::Kind::Unknown; };
Profile DescribeCompanionConsumable(PlayerbotAI*, ItemTemplate const*) { return {}; }
bool IsUsefulCompanionScroll(PlayerbotAI*, Profile const&) { return false; }
bool UsesCompanionArmorPriority(ItemTemplate const*) { return false; }
bool PrefersCompanionShield(PlayerbotAI*) { return false; }
bool IsCompanionShieldMainHand(int) { return false; }
int CompareCompanionArmor(ItemTemplate const*, ItemTemplate const*) { return 0; }
namespace CompanionErrands { bool CanSellGrey(int, bool, bool unused) { return unused; } }
struct Config { float equipUpgradeThreshold = 1.05f; } sPlayerbotAIConfig;
struct StatsWeightCalculator
{
    explicit StatsWeightCalculator(Player*) { }
    void SetItemSetBonus(bool) { }
    void SetOverflowPenalty(bool) { }
    float CalculateItem(uint32, int) { return 0; }
};
#include "selection.inc"
#include "equip.inc"
#include "protection.inc"
bool IsCompanionCraftingStock(PlayerbotAI*, Item*) { return false; }
#include "sale.inc"
#include "eligibility.inc"
void ConsolidateCompanionStacks(PlayerbotAI*) { }
#include "maintenance.inc"

int main()
{
    PlayerbotAI ai;
    Bag small[4] = {Bag(6), Bag(6), Bag(6), Bag(6)};
    Bag upgrades[4] = {Bag(12), Bag(16), Bag(14), Bag(18)};
    Item fillers[64];
    for (uint8 slot = 19; slot < 23; ++slot)
        ai.bot.Put(255, slot, &small[slot - 19]);
    for (uint8 slot = 23; slot < 39; ++slot)
        ai.bot.Put(255, slot, &fillers[slot]);
    for (uint8 slot = 0; slot < 6; ++slot)
        ai.bot.Put(19, slot, slot < 4 ? &upgrades[slot] : &fillers[slot]);
    for (uint8 bag = 20; bag < 23; ++bag)
        for (uint8 slot = 0; slot < 6; ++slot)
            ai.bot.Put(bag, slot, &fillers[40 + (bag - 20) * 6 + slot]);
    assert(FindOwnedBagUpgrade(&ai.bot, 6) == Pos(19, 3)); // Largest first, even with full backpack/bag.
    ai.bot.combat = true;
    MaintainCompanionBags(&ai);
    assert(ai.bot.swaps == 0);
    ai.bot.combat = false;
    ai.permitted = false;
    MaintainCompanionBags(&ai);
    assert(ai.bot.swaps == 0);
    ai.permitted = true;
    ai.bot.trading = true;
    MaintainCompanionBags(&ai);
    assert(ai.bot.swaps == 0);
    ai.bot.trading = false;
    ai.bot.cast = true;
    MaintainCompanionBags(&ai);
    assert(ai.bot.swaps == 0);
    ai.bot.cast = false;
    ai.requested = true;
    MaintainCompanionBags(&ai);
    assert(CompanionBagCapacity(&ai.bot) == 76);
    assert(observations == 1 && ai.companionErrands->inventoryDue && ai.companionErrands->townDue);
    assert(ai.bot.GetBagByPos(19) == &upgrades[3]);
    for (uint8 index = 0; index < 64; ++index)
        if (index == 4 || index == 5 || (index >= 23 && index < 39) || (index >= 40 && index < 58))
            assert(ai.bot.GetItemByPos(fillers[index].bag, fillers[index].slot) == &fillers[index]);
    for (Bag& bag : small)
    {
        assert(bag.contents.empty());
        assert(CanSellCompanionItem(&ai, &bag));
    }
    assert(!CanSellCompanionItem(&ai, &upgrades[0])); // Equipped bags cannot be sold.
    Bag spare(12), specialty(24, 1), future(20);
    assert(CanSellCompanionItem(&ai, &spare));
    assert(!CanSellCompanionItem(&ai, &specialty));
    assert(!CanSellCompanionItem(&ai, &future));
    spare.trade = true;
    assert(!CanSellCompanionItem(&ai, &spare));
    spare.trade = false;
    spare.enchantment = 1;
    assert(!CanSellCompanionItem(&ai, &spare));
    spare.enchantment = 0;
    Quest quest{{spare.proto.ItemId}};
    objectMgr.quest = &quest;
    assert(!CanSellCompanionItem(&ai, &spare));
    objectMgr.quest = nullptr;
    KnownSpell known;
    SpellInfo spell{{int32(spare.proto.ItemId)}, {}};
    ai.bot.spells[1] = &known;
    spellMgr.spell = &spell;
    assert(!CanSellCompanionItem(&ai, &spare));
    ai.bot.spells.clear();
    spellMgr.spell = nullptr;
    spare.contents[0] = &fillers[0];
    assert(!CanSellCompanionItem(&ai, &spare));
    spare.contents.clear();
    spare.proto.Quality = 4;
    assert(!CanSellCompanionItem(&ai, &spare));
    spare.proto.Quality = 1;
    ai.bot.Put(255, 22, nullptr);
    assert(!CanSellCompanionItem(&ai, &spare)); // An unfilled slot prevents disposal of a useful bag.
    ai.bot.Put(255, 22, &specialty);
    assert(CanSellCompanionItem(&ai, &spare));
    ai.managed = false;
    assert(!CanSellCompanionItem(&ai, &spare));
    ai.managed = true;
    Player buyer;
    Bag ordinary(12);
    buyer.Put(255, 19, &ordinary);
    for (uint8 slot = 23; slot < 39; ++slot)
        buyer.Put(255, slot, &fillers[slot]);
    EquipOwnedBagUpgrade(&buyer);
    assert(!buyer.GetItemByPos(255, 23) && buyer.GetItemByPos(19, 0) == &fillers[23]);
    // No owned upgrade: preserve a backpack slot for the next native vendor purchase.
    ai.bot.blocked = true;
    ai.bot.Put(255, 23, &future);
    auto capacity = CompanionBagCapacity(&ai.bot);
    MaintainCompanionBags(&ai);
    assert(CompanionBagCapacity(&ai.bot) == capacity && observations == 1);
}
