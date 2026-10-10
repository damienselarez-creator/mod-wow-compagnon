#include "CompanionErrandPolicy.h"
#include <array>
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <vector>

using uint32 = uint32_t;
using int32 = int32_t;
constexpr uint32 SKILL_RIDING = 762;
constexpr uint32 SPELL_EFFECT_SKILL = 118, SPELL_EFFECT_LEARN_SPELL = 36;
constexpr uint32 SPELL_AURA_MOUNTED = 78, SPELL_AURA_MOD_INCREASE_MOUNTED_SPEED = 32;
constexpr uint32 SPELL_AURA_MOD_INCREASE_MOUNTED_FLIGHT_SPEED = 207;
constexpr uint32 ITEM_SPELLTRIGGER_LEARN_SPELL_ID = 6, PLAYERSPELL_REMOVED = 1;
constexpr uint32 EQUIP_ERR_OK = 0, NULL_BAG = 255, NULL_SLOT = 255;
using ItemPosCountVec = std::vector<uint32>;
struct SpellEffect
{
    uint32 Effect = 0;
    int32 MiscValue = 0;
    uint32 TriggerSpell = 0, ApplyAuraName = 0;
    int32 BasePoints = 0;
};
struct SpellInfo
{
    std::array<SpellEffect, 3> Effects{};
    bool mounted = false;
    bool HasAura(uint32 aura) const { return aura == SPELL_AURA_MOUNTED && mounted; }
};
struct SpellMgr
{
    std::map<uint32, SpellInfo> spells;
    SpellInfo const* GetSpellInfo(uint32 id) const
    {
        auto it = spells.find(id);
        return it == spells.end() ? nullptr : &it->second;
    }
} spellMgr;
auto* sSpellMgr = &spellMgr;
struct SkillLineRecord { uint32 SkillLine = 0; };
struct PlayerbotSpellRepository
{
    std::map<uint32, SkillLineRecord> lines;
    static PlayerbotSpellRepository& Instance() { static PlayerbotSpellRepository value; return value; }
    SkillLineRecord const* GetSkillLine(uint32 id) const
    {
        auto it = lines.find(id);
        return it == lines.end() ? nullptr : &it->second;
    }
};
struct ItemSpell { int32 SpellId = 0; uint32 SpellTrigger = 0; };
struct ItemTemplate
{
    uint32 ItemId = 0, BuyCount = 1;
    int32 BuyPrice = 100;
    bool usable = true;
    std::array<ItemSpell, 5> Spells{};
};
struct Item
{
    ItemTemplate const* proto;
    ItemTemplate const* GetTemplate() const { return proto; }
};
struct KnownSpell { uint32 State = 0; bool Active = true; };
struct Creature;
struct Player
{
    uint32 money = 1000;
    bool room = true;
    float discount = 1.0f;
    std::map<uint32, KnownSpell*> spells;
    auto const& GetSpellMap() const { return spells; }
    uint32 GetMoney() const { return money; }
    uint32 CanUseItem(ItemTemplate const* item) const { return item->usable ? 0 : 1; }
    float GetReputationPriceDiscount(Creature*) const { return discount; }
    uint32 CanStoreNewItem(uint32, uint32, ItemPosCountVec&, uint32, uint32) const { return room ? 0 : 1; }
};
struct PlayerbotAI
{
    Player bot;
    std::vector<Item*> items;
    Player* GetBot() { return &bot; }
    auto const& GetInventoryItems() const { return items; }
};
struct VendorItem { uint32 item, ExtendedCost = 0, maxcount = 0; };
struct VendorItemData
{
    std::vector<VendorItem> items;
    bool Empty() const { return items.empty(); }
    uint32 GetItemCount() const { return uint32(items.size()); }
    VendorItem const* GetItem(uint32 slot) const { return &items.at(slot); }
};
struct Creature
{
    uint32 stock = 1;
    uint32 GetVendorItemCurrentCount(VendorItem const*) const { return stock; }
};
struct ObjectMgr
{
    std::map<uint32, ItemTemplate> items;
    ItemTemplate const* GetItemTemplate(uint32 id) const
    {
        auto it = items.find(id);
        return it == items.end() ? nullptr : &it->second;
    }
} objectMgr;
auto* sObjectMgr = &objectMgr;
struct PoisonPurchase { uint32 item = 0, slot = 0, batches = 0; };

#include "production_travel.inc"

int main()
{
    assert(CompanionErrands::HerbWithinLeash(79, 79, 20));
    assert(!CompanionErrands::HerbWithinLeash(81, 60, 20));
    assert(!CompanionErrands::HerbWithinLeash(60, 81, 20));
    assert(!CompanionErrands::HerbWithinLeash(60, 60, 41));
    spellMgr.spells[1].Effects[0] = {SPELL_EFFECT_SKILL, SKILL_RIDING};
    spellMgr.spells[2].Effects[0] = {SPELL_EFFECT_LEARN_SPELL, 0, 1};
    spellMgr.spells[3].Effects[0] = {SPELL_EFFECT_SKILL, 186};
    spellMgr.spells[4].Effects[0] = {SPELL_EFFECT_LEARN_SPELL, 0, 4};
    assert(TeachesRiding(1) && TeachesRiding(2));
    assert(!TeachesRiding(3) && !TeachesRiding(4) && !TeachesRiding(999));
    PlayerbotSpellRepository::Instance().lines[5] = {SKILL_RIDING};
    assert(TeachesRiding(5));
    for (uint32 id : {10, 11, 12})
    {
        auto& spell = spellMgr.spells[id];
        spell.mounted = true;
        spell.Effects[0].ApplyAuraName = id == 12 ? SPELL_AURA_MOD_INCREASE_MOUNTED_FLIGHT_SPEED :
            SPELL_AURA_MOD_INCREASE_MOUNTED_SPEED;
        spell.Effects[0].BasePoints = id == 10 ? 59 : id == 11 ? 99 : 149;
    }
    for (uint32 id : {10, 11, 12})
    {
        auto& item = objectMgr.items[id];
        item.ItemId = id;
        item.Spells[1] = {int32(id), ITEM_SPELLTRIGGER_LEARN_SPELL_ID};
    }
    PlayerbotAI ai;
    VendorItemData goods{{{10}}};
    Creature vendor;
    assert(ChooseMount(&ai, &goods, 200).item == 10);
    VendorItemData upgrades{{{10}, {11}}};
    objectMgr.items[11].BuyPrice = 500;
    assert(ChooseMount(&ai, &upgrades, 200).item == 11);
    assert(ChooseMount(&ai, &upgrades, 501).item == 10);
    assert(ChooseMount(&ai, &goods, 901).item == 0);
    ai.bot.room = false;
    assert(ChooseMount(&ai, &goods, 0).item == 0);
    ai.bot.room = true;
    objectMgr.items[10].usable = false;
    assert(ChooseMount(&ai, &goods, 0).item == 0);
    objectMgr.items[10].usable = true;
    goods.items[0].ExtendedCost = 1;
    assert(ChooseMount(&ai, &goods, 0).item == 0);
    goods.items[0].ExtendedCost = 0;
    goods.items[0].maxcount = 1;
    vendor.stock = 0;
    assert(ChooseMount(&ai, &goods, 0, &vendor).item == 0);
    vendor.stock = 1;
    ai.bot.money = 80;
    ai.bot.discount = 0.8f;
    assert(ChooseMount(&ai, &goods, 0, &vendor).item == 10);
    ai.bot.money = 1000;
    Item held{&objectMgr.items[10]};
    ai.items.push_back(&held);
    assert(!NeedsMount(&ai, DescribeMount(10)));
    assert(NeedsMount(&ai, DescribeMount(11)));
    assert(NeedsMount(&ai, DescribeMount(12)));
    ai.items.clear();
    KnownSpell known;
    ai.bot.spells[11] = &known;
    assert(!NeedsMount(&ai, DescribeMount(10)) && !NeedsMount(&ai, DescribeMount(11)));
    assert(NeedsMount(&ai, DescribeMount(12)));
    known.State = PLAYERSPELL_REMOVED;
    assert(NeedsMount(&ai, DescribeMount(10)));
    objectMgr.items[10].Spells[1].SpellTrigger = 0;
    assert(!MountFromItem(&objectMgr.items[10]).spell);
    assert(!NeedsMount(&ai, DescribeMount(999)));
    std::cout << "Riding, native eligibility gates, budget, stock, duplicates and gather bounds: PASS\n";
}
