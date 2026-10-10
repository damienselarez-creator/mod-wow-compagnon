#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <vector>
using uint8 = uint8_t;
using uint32 = uint32_t;
constexpr uint32 ITEM_QUALITY_RARE = 3, ITEM_CLASS_WEAPON = 2, ITEM_CLASS_ARMOR = 4;
constexpr uint32 ITEM_SUBCLASS_WEAPON_MISC = 14, ITEM_SUBCLASS_WEAPON_FISHING_POLE = 20;
constexpr uint32 EQUIP_ERR_OK = 0, NULL_SLOT = 255, NULL_BAG = 255, INVENTORY_SLOT_BAG_0 = 255;
constexpr uint32 EQUIPMENT_SLOT_MAINHAND = 15;
enum ItemUsage { ITEM_USAGE_NONE, ITEM_USAGE_EQUIP, ITEM_USAGE_REPLACE, ITEM_USAGE_BAD_EQUIP };
struct ItemTemplate
{
    uint32 ItemId, Armor, BuyPrice, InventoryType = 10, Class = ITEM_CLASS_ARMOR;
    uint32 Quality = 1, BuyCount = 1, SubClass = 0;
};
std::map<uint32, ItemTemplate> templates;
struct Item
{
    uint32 id;
    ItemTemplate const* GetTemplate() { return &templates.at(id); }
    uint32 GetEntry() { return id; }
    int GetItemRandomPropertyId() { return 0; }
};
struct Offer { uint32 item, ExtendedCost = 0, maxcount = 0; };
struct VendorItemData
{
    std::vector<Offer> offers;
    bool Empty() const { return offers.empty(); }
    uint32 GetItemCount() const { return offers.size(); }
    Offer const* GetItem(uint32 slot) const { return &offers.at(slot); }
};
struct Creature { uint32 stock = 1; uint32 GetVendorItemCurrentCount(Offer const*) { return stock; } };
struct ObjectMgr
{
    ItemTemplate const* GetItemTemplate(uint32 id) { return &templates.at(id); }
} objectMgr;
auto* sObjectMgr = &objectMgr;
using ItemPosCountVec = std::vector<int>;
struct Player
{
    uint32 money = 10000;
    std::set<uint32> owned;
    Item worn{1};
    bool usable = true, room = true;
    uint32 GetMoney() { return money; }
    uint32 GetItemCount(uint32 id, bool) { return owned.count(id); }
    uint32 CanUseItem(ItemTemplate const*) { return usable ? 0 : 1; }
    Item* GetItemByPos(uint32, uint32) { return &worn; }
    float GetReputationPriceDiscount(Creature*) { return 1; }
    uint32 CanStoreNewItem(uint32, uint32, ItemPosCountVec&, uint32, uint32) { return room ? 0 : 1; }
};
struct Context
{
    ItemUsage usage = ITEM_USAGE_EQUIP;
    template<class T> Context* GetValue(char const*, uint32) { return this; }
    ItemUsage Get() { return usage; }
};
struct PlayerbotAI
{
    Player bot;
    Context context;
    Player* GetBot() { return &bot; }
    Context* GetAiObjectContext() { return &context; }
    uint8 FindEquipSlot(ItemTemplate const*, uint32, bool) { return 10; }
};
bool PrefersCompanionShield(PlayerbotAI*) { return true; }
bool IsCompanionShieldMainHand(uint32 type) { return type == 13; }
bool IsCompanionShieldReplacement(uint32, uint32) { return false; }
bool UsesCompanionArmorPriority(ItemTemplate const* item) { return item->Class == ITEM_CLASS_ARMOR; }
struct StatsWeightCalculator
{
    explicit StatsWeightCalculator(Player*) { }
    void SetItemSetBonus(bool) { }
    void SetOverflowPenalty(bool) { }
    float CalculateItem(uint32 id, int = 0) { return templates.at(id).Armor; }
};
namespace CompanionErrands
{
    bool CanSpend(uint32 money, uint32 price, uint32 reserve)
    {
        return price <= money && money - price >= reserve;
    }
}
struct PoisonPurchase { uint32 item = 0, slot = 0, batches = 0; };
#include "equipment.inc"
int main()
{
    templates.emplace(1, ItemTemplate{1, 100, 1});
    templates.emplace(2, ItemTemplate{2, 110, 100});
    templates.emplace(3, ItemTemplate{3, 140, 400});
    templates.emplace(4, ItemTemplate{4, 160, 800});
    PlayerbotAI ai;
    VendorItemData vendor{{{2}, {3}, {4}}};
    assert(ChooseEquipment(&ai, &vendor, 2000).item == 4);
    ai.bot.owned.insert(4);
    assert(ChooseEquipment(&ai, &vendor, 2000).item == 3);
    ai.bot.owned.insert(3);
    assert(!ChooseEquipment(&ai, &vendor, 2000).item); // A 10% gain does not justify an autonomous purchase.
    ai.bot.owned.clear();
    ai.bot.money = 3000;
    assert(!ChooseEquipment(&ai, &vendor, 600).item); // Single purchase capped at 10% of own gold.
    ai.bot.money = 10000;
    assert(!ChooseEquipment(&ai, &vendor, 9900).item);
    ai.bot.room = false; assert(!ChooseEquipment(&ai, &vendor, 2000).item); ai.bot.room = true;
    ai.bot.usable = false; assert(!ChooseEquipment(&ai, &vendor, 2000).item); ai.bot.usable = true;
    ai.context.usage = ITEM_USAGE_NONE; assert(!ChooseEquipment(&ai, &vendor, 2000).item);
    ai.context.usage = ITEM_USAGE_EQUIP;
    Creature npc;
    npc.stock = 0;
    vendor.offers = {{4, 0, 1}};
    assert(!ChooseEquipment(&ai, &vendor, 2000, &npc).item);
    vendor.offers = {{4, 1, 0}};
    assert(!ChooseEquipment(&ai, &vendor, 2000).item);
}
