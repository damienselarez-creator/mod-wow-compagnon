/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "CompanionErrandPolicy.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <set>
#include <vector>

using uint8 = uint8_t;
using uint16 = uint16_t;
using uint32 = uint32_t;
using uint64 = uint64_t;
constexpr uint32 MAX_MONEY_AMOUNT = 2147483647;
using int32 = int32_t;
constexpr uint32 SKILL_MINING = 186, SKILL_SKINNING = 393, SKILL_BLACKSMITHING = 164;
constexpr uint32 SKILL_ENGINEERING = 202, SKILL_INSCRIPTION = 773;
constexpr uint32 PLAYERSPELL_REMOVED = 1, MAX_SPELL_REAGENTS = 8;
constexpr uint32 EQUIP_ERR_OK = 0, NULL_BAG = 255, NULL_SLOT = 255;
constexpr uint8 INVENTORY_SLOT_BAG_0 = 255, INVENTORY_SLOT_BAG_START = 19, INVENTORY_SLOT_BAG_END = 23;
constexpr uint8 INVENTORY_SLOT_ITEM_START = 23, INVENTORY_SLOT_ITEM_END = 39;
constexpr uint32 ITEM_CLASS_CONTAINER = 1;
using ItemPosCountVec = std::vector<uint32>;

struct ItemTemplate
{
    uint32 ItemId = 0, TotemCategory = 0, BuyCount = 1, stack = 20;
    int32 BuyPrice = 1;
    uint32 Class = 0, BagFamily = 0, ContainerSlots = 0;
    uint32 GetMaxStackSize() const { return stack; }
};

struct Item
{
    ItemTemplate const* proto;
    ItemTemplate const* GetTemplate() const { return proto; }
    bool IsBag() const { return proto->Class == ITEM_CLASS_CONTAINER; }
};
struct Bag : Item
{
    uint32 GetBagSize() const { return proto->ContainerSlots; }
};

struct ObjectMgr
{
    std::map<uint32, ItemTemplate> items;
    ItemTemplate const* GetItemTemplate(uint32 id) const
    {
        auto found = items.find(id);
        return found == items.end() ? nullptr : &found->second;
    }
} objectMgr;
ObjectMgr* sObjectMgr = &objectMgr;

struct SpellInfo
{
    uint32 Totem[2]{}, TotemCategory[2]{}, ReagentCount[MAX_SPELL_REAGENTS]{};
    int32 Reagent[MAX_SPELL_REAGENTS]{};
};
struct SpellMgr
{
    std::map<uint32, SpellInfo> spells;
    SpellInfo const* GetSpellInfo(uint32 id) const
    {
        auto found = spells.find(id);
        return found == spells.end() ? nullptr : &found->second;
    }
} spellMgr;
SpellMgr* sSpellMgr = &spellMgr;

struct SkillLineRecord
{
    uint32 SkillLine = 0, MinSkillLineRank = 0, TrivialSkillLineRankHigh = 0;
};
struct PlayerbotSpellRepository
{
    std::map<uint32, SkillLineRecord> lines;
    static PlayerbotSpellRepository& Instance()
    {
        static PlayerbotSpellRepository repository;
        return repository;
    }
    SkillLineRecord const* GetSkillLine(uint32 id) const
    {
        auto found = lines.find(id);
        return found == lines.end() ? nullptr : &found->second;
    }
};
bool IsProfession(uint32 skill) { return CompanionErrands::IsPrimary(skill) || skill == 185; }

struct KnownSpell
{
    uint32 State = 0;
    bool Active = true;
};
struct VendorItem
{
    uint32 item = 0, ExtendedCost = 0, maxcount = 0;
};
struct VendorItemData
{
    std::vector<VendorItem> offers;
    bool Empty() const { return offers.empty(); }
    uint32 GetItemCount() const { return uint32(offers.size()); }
    VendorItem const* GetItem(uint32 index) const { return &offers.at(index); }
};
struct Creature
{
    uint32 available = 100;
    uint32 GetVendorItemCurrentCount(VendorItem const*) const { return available; }
};
struct Player
{
    std::map<uint32, uint32> skills, inventory;
    std::map<uint16, Item*> positions;
    Item* GetItemByPos(uint8 bag, uint8 slot) const
    {
        auto found = positions.find(uint16(bag << 8) | slot);
        return found == positions.end() ? nullptr : found->second;
    }
    Bag* GetBagByPos(uint8 slot) const
    {
        return static_cast<Bag*>(GetItemByPos(INVENTORY_SLOT_BAG_0, slot));
    }
    void SwapItem(uint16 source, uint16 target) { std::swap(positions[source], positions[target]); }
    std::map<uint32, KnownSpell*> spells;
    std::set<uint32> categories;
    uint32 money = 100, maxSkill = 75;
    float discount = 1.0f;
    bool room = true;
    bool HasSkill(uint32 skill) const { return skills.count(skill); }
    uint32 GetSkillValue(uint32 skill) const { return skills.at(skill); }
    uint32 GetMaxSkillValue(uint32) const { return maxSkill; }
    bool HasItemTotemCategory(uint32 category) const { return categories.count(category); }
    bool IsTotemCategoryCompatiableWith(ItemTemplate const* item, uint32 category) const
    {
        return item->TotemCategory == category;
    }
    std::map<uint32, KnownSpell*> const& GetSpellMap() const { return spells; }
    uint32 GetItemCount(uint32 id, bool) const
    {
        auto found = inventory.find(id);
        return found == inventory.end() ? 0 : found->second;
    }
    uint32 GetMoney() const { return money; }
    uint32 CanUseItem(ItemTemplate const*) const { return EQUIP_ERR_OK; }
    float GetReputationPriceDiscount(Creature*) const { return discount; }
    uint32 CanStoreNewItem(uint32, uint32, ItemPosCountVec&, uint32, uint32) const
    {
        return room ? EQUIP_ERR_OK : 1;
    }
};
struct PoisonPurchase
{
    uint32 item = 0, slot = 0, batches = 0;
};

// Execute the real production need-discovery and vendor-selection functions with isolated core data.
#include "production_supplies.inc"

int main()
{
    objectMgr.items[5956] = {5956, 162, 1, 1, 5};
    objectMgr.items[6219] = {6219, 14, 1, 1, 5};
    objectMgr.items[2901] = {2901, 165, 1, 1, 5};
    objectMgr.items[7005] = {7005, 166, 1, 1, 5};
    objectMgr.items[39505] = {39505, 121, 1, 1, 5};
    objectMgr.items[3371] = {3371, 0, 5, 20, 4}; // Example vendor pack of vials.
    objectMgr.items[39354] = {39354, 0, 5, 20, 4}; // Parchment.
    objectMgr.items[999] = {999, 162, 1, 1, 8}; // Equivalent hammer.
    Player bot;
    bot.skills[164] = 1;
    VendorItemData vendor{{{3371}, {5956}, {999}}};
    auto needs = NeededProfessionSupplies(&bot);
    auto purchase = ChooseProfessionSupply(&bot, &vendor, 20, needs);
    assert(purchase.item == 5956 && purchase.batches == 1);
    bot.inventory[5956] = 1;
    bot.categories.insert(162);
    assert(!ChooseProfessionSupply(&bot, &vendor, 20, NeededProfessionSupplies(&bot)).item);

    // Gathering and inscription tools need no previously learned crafting recipe.
    bot.skills = {{186, 1}, {393, 1}, {773, 1}};
    needs = NeededProfessionSupplies(&bot);
    assert(needs.desired.at(2901) == 1 && needs.desired.at(7005) == 1 && needs.desired.at(39505) == 1);
    assert(!needs.desired.count(5956));

    // Only known, active recipes belonging to a learned profession generate reagent demand.
    KnownSpell active, removed{PLAYERSPELL_REMOVED, true}, inactive{0, false};
    bot.skills = {{171, 1}, {773, 1}};
    bot.spells = {{100, &active}, {101, &removed}, {102, &inactive}};
    spellMgr.spells[100].Reagent[0] = 3371;
    spellMgr.spells[100].ReagentCount[0] = 1;
    spellMgr.spells[101].Reagent[0] = 39354;
    spellMgr.spells[101].ReagentCount[0] = 1;
    spellMgr.spells[102] = spellMgr.spells[101];
    auto& lines = PlayerbotSpellRepository::Instance().lines;
    lines[100] = {171, 1, 75};
    lines[101] = {773, 1, 75};
    lines[102] = lines[101];
    needs = NeededProfessionSupplies(&bot);
    assert(needs.desired.at(3371) == 10 && !needs.desired.count(39354));
    purchase = ChooseProfessionSupply(&bot, &vendor, 20, needs);
    assert(purchase.item == 3371 && purchase.batches == 2);
    bot.inventory[3371] = 10;
    assert(!ChooseProfessionSupply(&bot, &vendor, 20, NeededProfessionSupplies(&bot)).item);
    bot.inventory.clear();
    bot.skills.erase(171);
    assert(!NeededProfessionSupplies(&bot).desired.count(3371));
    bot.skills[171] = 1;

    // Parchment is handled through its recipe reagent, without a language-specific item-name lookup.
    bot.spells[101] = &active;
    needs = NeededProfessionSupplies(&bot);
    VendorItemData parchment{{{39354}}};
    assert(ChooseProfessionSupply(&bot, &parchment, 20, needs).batches == 2);
    bot.skills[773] = 74;
    lines[101].TrivialSkillLineRankHigh = 50;
    assert(!NeededProfessionSupplies(&bot).desired.count(39354));
    bot.skills[773] = bot.maxSkill;
    assert(NeededProfessionSupplies(&bot).desired.count(39354));
    bot.maxSkill = 450;
    bot.skills[773] = 450;
    assert(!NeededProfessionSupplies(&bot).desired.count(39354));

    // Non-stackable consumable components, such as a copper rod for enchanting, are purchased once.
    objectMgr.items[6217] = {6217, 0, 1, 1, 5};
    spellMgr.spells[103].Reagent[0] = 6217;
    spellMgr.spells[103].ReagentCount[0] = 1;
    lines[103] = {333, 1, 75};
    Player enchanter;
    enchanter.skills[333] = 1;
    enchanter.spells[103] = &active;
    VendorItemData rods{{{6217}}};
    assert(ChooseProfessionSupply(&enchanter, &rods, 20,
        NeededProfessionSupplies(&enchanter)).batches == 1);
    enchanter.inventory[6217] = 1;
    assert(!ChooseProfessionSupply(&enchanter, &rods, 20,
        NeededProfessionSupplies(&enchanter)).item);

    // Tools expressed as categories accept a compatible vendor tool and skip an owned equivalent.
    spellMgr.spells[100].TotemCategory[0] = 162;
    bot.categories.clear();
    VendorItemData equivalent{{{999}}};
    assert(ChooseProfessionSupply(&bot, &equivalent, 20, NeededProfessionSupplies(&bot)).item == 999);
    bot.categories.insert(162);
    assert(!ChooseProfessionSupply(&bot, &equivalent, 20, NeededProfessionSupplies(&bot)).item);

    // No room, special currencies, empty stock, or protected money means no purchase.
    needs = NeededProfessionSupplies(&bot);
    bot.room = false;
    assert(!ChooseProfessionSupply(&bot, &vendor, 20, needs).item);
    bot.room = true;
    bot.money = 20;
    assert(!ChooseProfessionSupply(&bot, &vendor, 20, needs).item);
    bot.money = 100;
    vendor.offers[0].ExtendedCost = 1;
    assert(!ChooseProfessionSupply(&bot, &vendor, 20, needs).item);
    vendor.offers[0].ExtendedCost = 0;
    vendor.offers[0].maxcount = 10;
    Creature npc;
    npc.available = 4;
    assert(!ChooseProfessionSupply(&bot, &vendor, 20, needs, &npc).item);
    npc.available = 5;
    assert(ChooseProfessionSupply(&bot, &vendor, 20, needs, &npc).batches == 1);
    bot.discount = 0.9f;
    bot.money = 23;
    assert(!ChooseProfessionSupply(&bot, &vendor, 20, needs, &npc).item);

    assert(CompanionErrands::SupplyBatches(19, 20, 5, 1, 100, 20, 100) == 0);
    assert(CompanionErrands::SupplyBatches(0, 10, 5, 4, 24, 20, 100) == 1);
    assert(CompanionErrands::SupplyBatches(10, 10, 5, 4, 100, 20, 100) == 0);
    assert(CompanionErrands::SupplyBatches(0, 10, 0, 4, 100, 20, 100) == 0);
    assert(CompanionErrands::SupplyBatches(0, 10, UINT32_MAX, 4, 100, 20, UINT32_MAX) == 0);
    // Execute actual bag selection: affordability, direct empty-slot equip, and protected specialty bags.
    objectMgr.items[200] = {200, 0, 1, 1, 10, ITEM_CLASS_CONTAINER, 0, 6};
    objectMgr.items[201] = {201, 0, 1, 1, 40, ITEM_CLASS_CONTAINER, 0, 12};
    objectMgr.items[202] = {202, 0, 1, 1, 90, ITEM_CLASS_CONTAINER, 0, 20};
    objectMgr.items[203] = {203, 0, 1, 1, 5, ITEM_CLASS_CONTAINER, 1, 24};
    objectMgr.items[204] = {204, 0, 1, 1, 30, ITEM_CLASS_CONTAINER, 0, 12};
    VendorItemData bags{{{200}, {201}, {202}, {203}, {204}}};
    Player bagBot;
    auto chosen = ChooseBag(&bagBot, &bags, 20);
    assert(chosen.item == 204 && chosen.equipSlot == 19 && chosen.storageSlot == 19);
    bagBot.money = 20;
    assert(!ChooseBag(&bagBot, &bags, 20).item);
    bagBot.money = 100;
    Bag small{{&objectMgr.items[200]}}, large{{&objectMgr.items[201]}}, specialty{{&objectMgr.items[203]}};
    bagBot.positions[uint16(255 << 8) | 19] = &specialty;
    bagBot.positions[uint16(255 << 8) | 20] = &small;
    bagBot.positions[uint16(255 << 8) | 21] = &large;
    assert(ChooseBag(&bagBot, &bags, 20).equipSlot == 22);
    bagBot.positions[uint16(255 << 8) | 22] = &large;
    chosen = ChooseBag(&bagBot, &bags, 20);
    assert(chosen.item == 204 && chosen.equipSlot == 20 && chosen.storageSlot == 23);
    Item filler{&objectMgr.items[3371]};
    for (uint8 slot = 23; slot < 39; ++slot)
        bagBot.positions[uint16(255 << 8) | slot] = &filler;
    assert(!ChooseBag(&bagBot, &bags, 20).item);
    bagBot.positions[uint16(255 << 8) | 23] = nullptr;
    bags.offers[4].ExtendedCost = 1;
    bags.offers[1].maxcount = 1;
    npc.available = 0;
    assert(!ChooseBag(&bagBot, &bags, 20, &npc).item);
    npc.available = 1;
    assert(ChooseBag(&bagBot, &bags, 20, &npc).item == 201);
    bagBot.positions[uint16(255 << 8) | 23] = &large;
    assert(!ChooseBag(&bagBot, &bags, 20).item);
    EquipOwnedBagUpgrade(&bagBot);
    assert(bagBot.GetBagByPos(20) == &large && bagBot.GetBagByPos(19) == &specialty);
    assert(!ChooseBag(&bagBot, &bags, 20).item); // No downgrade or duplicate once all general bags are 12 slots.
    for (uint8 slot = 19; slot < 23; ++slot)
        bagBot.positions[uint16(255 << 8) | slot] = &specialty;
    assert(!ChooseBag(&bagBot, &bags, 20).item);
    std::cout << "PASS: real profession supply discovery and vendor choice, tools, vials, parchment, reserves, affordable bag upgrades\n";
}
