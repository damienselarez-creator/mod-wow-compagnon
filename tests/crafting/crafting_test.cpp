/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <map>
#include <string>
#include <vector>

using uint8 = uint8_t;
using uint16 = uint16_t;
using uint32 = uint32_t;
using int32 = int32_t;
constexpr uint32 EQUIP_ERR_OK = 0, EFFECT_0 = 0, SPELL_EFFECT_CREATE_ITEM = 24;
constexpr uint8 NULL_SLOT = 255, INVENTORY_SLOT_BAG_START = 19, INVENTORY_SLOT_BAG_END = 23;
constexpr uint32 ITEM_CLASS_CONSUMABLE = 0, ITEM_CLASS_CONTAINER = 1, ITEM_CLASS_WEAPON = 2, ITEM_CLASS_ARMOR = 4;
constexpr uint32 MAX_SPELL_REAGENTS = 8, NO_BIND = 0, BIND_WHEN_EQUIPPED = 2, BIND_WHEN_USE = 3;
constexpr uint32 POWER_MANA = 0, BIND_WHEN_PICKED_UP = 1, PLAYERSPELL_REMOVED = 1;
enum ItemUsage { ITEM_USAGE_NONE, ITEM_USAGE_EQUIP, ITEM_USAGE_REPLACE, ITEM_USAGE_AMMO,
    ITEM_USAGE_QUEST, ITEM_USAGE_SKILL, ITEM_USAGE_USE };
struct ItemTemplate
{
    uint32 ItemId = 0, Class = 0, BagFamily = 0, ContainerSlots = 0, Bonding = 0;
    float score = 0;
};
struct Item
{
    ItemTemplate const* proto;
    ItemTemplate const* GetTemplate() const { return proto; }
    uint32 GetEntry() const { return proto->ItemId; }
    int32 GetItemRandomPropertyId() const { return 0; }
};
struct Bag : Item { uint32 GetBagSize() const { return proto->ContainerSlots; } };
struct SpellInfo
{
    uint32 Id = 0;
    struct EffectData { uint32 Effect = SPELL_EFFECT_CREATE_ITEM, ItemType = 0; } Effects[1];
    uint32 ReagentCount[MAX_SPELL_REAGENTS] = {1}, SchoolMask = 0;
    int32 Reagent[MAX_SPELL_REAGENTS] = {1000};
    uint32 Totem[2]{}, TotemCategory[2]{};
};
struct KnownSpell { uint32 State = 0; bool Active = true; };
struct PlayerbotAI;
struct Group;
struct Player
{
    PlayerbotAI* ai = nullptr;
    Group* group = nullptr;
    std::map<uint32, uint32> counts;
    std::map<uint32, KnownSpell*> spells;
    std::map<uint8, Bag*> bags;
    Item* equipped = nullptr;
    bool usable = true, alive = true, world = true, profession = true;
    float distance = 0;
    uint32 GetItemCount(uint32 id, bool) const { auto f = counts.find(id); return f == counts.end() ? 0 : f->second; }
    uint32 CanUseItem(ItemTemplate const*) const { return usable ? EQUIP_ERR_OK : 1; }
    Bag* GetBagByPos(uint8 slot) const { auto f = bags.find(slot); return f == bags.end() ? nullptr : f->second; }
    uint32 GetMaxPower(uint32) const { return 100; }
    uint32 CanEquipNewItem(uint8, uint16& dest, uint32, bool) const { dest = 0; return usable ? 0 : 1; }
    Item* GetItemByPos(uint16) const { return equipped; }
    Group* GetGroup() const { return group; }
    bool IsAlive() const { return alive; }
    bool IsInWorld() const { return world; }
    int GetMap() const { return 1; }
    float GetDistance(Player* other) const { return other->distance; }
    bool HasItemTotemCategory(uint32) const { return false; }
    bool HasSkill(uint32) const { return profession; }
    auto const& GetSpellMap() const { return spells; }
};
struct GroupReference
{
    Player* player;
    GroupReference* following = nullptr;
    GroupReference* next() const { return following; }
    Player* GetSource() const { return player; }
};
struct Group { GroupReference* first; GroupReference* GetFirstMember() const { return first; } };
struct Context
{
    std::map<uint32, ItemUsage> usages;
    ItemUsage current = ITEM_USAGE_NONE;
    template<class T> Context* GetValue(std::string const&, std::string const& id)
    {
        auto f = usages.find(std::stoul(id)); current = f == usages.end() ? ITEM_USAGE_NONE : f->second; return this;
    }
    ItemUsage Get() const { return current; }
};
struct PlayerbotAI
{
    Context context;
    bool managed = true;
    Context* GetAiObjectContext() { return &context; }
};
#define GET_PLAYERBOT_AI(player) ((player)->ai)
#define AI_VALUE2(type, name, id) (botAI->context.GetValue<type>(name, std::to_string(id))->Get())
bool IsCompanionInventoryManaged(PlayerbotAI* ai) { return ai->managed; }
bool IsManagedCompanion(PlayerbotAI* ai) { return ai->managed; }
struct ObjectMgr
{
    std::map<uint32, ItemTemplate> items;
    ItemTemplate const* GetItemTemplate(uint32 id) const
    { auto f = items.find(id); return f == items.end() ? nullptr : &f->second; }
} objectMgr;
ObjectMgr* sObjectMgr = &objectMgr;
struct StatsWeightCalculator
{
    explicit StatsWeightCalculator(Player*) {}
    void SetItemSetBonus(bool) {}
    void SetOverflowPenalty(bool) {}
    float CalculateItem(uint32 id, int32 = 0) { return objectMgr.items.at(id).score; }
};
struct ItemUsageValue
{
    static std::string GetConsumableType(ItemTemplate const*, uint32) { return "healing potion"; }
    static bool SpellGivesSkillUp(uint32, Player*) { return true; }
};
struct SkillRecord { uint32 SkillLine = 164; };
struct PlayerbotSpellRepository
{
    SkillRecord line;
    static PlayerbotSpellRepository& Instance() { static PlayerbotSpellRepository r; return r; }
    auto GetSkillLine(uint32) const { return &line; }
};
bool IsProfessionSkill(uint32) { return true; }
struct CraftRandomItemAction
{
    PlayerbotAI* botAI;
    Player* bot;
    bool UseStrictPriority() const;
    bool AcceptSpell(SpellInfo const*);
    uint32 GetSpellPriority(SpellInfo const*);
};
#include "production_crafting.inc"

void SortSpells(std::vector<std::pair<uint32, std::pair<uint32, Player*>>>& spellList, bool strictPriority)
{
#include "production_sort.inc"
}

int main()
{
    objectMgr.items[1] = {1, ITEM_CLASS_ARMOR, 0, 0, 0, 10};
    objectMgr.items[2] = {2, ITEM_CLASS_CONSUMABLE};
    objectMgr.items[3] = {3, ITEM_CLASS_CONTAINER, 0, 12};
    objectMgr.items[4] = {4, ITEM_CLASS_ARMOR, 0, 0, 0, 20};
    PlayerbotAI ai, friendAi;
    Player self, other, human;
    self.ai = &ai; other.ai = &friendAi;
    GroupReference humanRef{&human}, otherRef{&other, &humanRef};
    Group group{&otherRef}; self.group = &group;
    CraftRandomItemAction action{&ai, &self};
    KnownSpell known; self.spells[100] = &known;
    self.counts[1000] = 1;
    SpellInfo recipe; recipe.Id = 100; recipe.Effects[0].ItemType = 1;
    ai.context.usages[1] = ITEM_USAGE_EQUIP;
    friendAi.context.usages[1] = ITEM_USAGE_EQUIP;
    assert(action.AcceptSpell(&recipe));
    self.counts[1000] = 0; assert(!action.AcceptSpell(&recipe));
    self.counts[1000] = 1;
    recipe.Totem[0] = 5956; assert(!action.AcceptSpell(&recipe));
    self.counts[5956] = 1; assert(action.AcceptSpell(&recipe));
    recipe.TotemCategory[0] = 162; assert(!action.AcceptSpell(&recipe));
    recipe.TotemCategory[0] = 0; recipe.Totem[0] = 0;
    assert(action.GetSpellPriority(&recipe) == 100);
    ai.context.usages[1] = ITEM_USAGE_NONE;
    assert(action.GetSpellPriority(&recipe) == 50);
    self.counts[1] = 1;
    assert(action.GetSpellPriority(&recipe) == 0); // Pending group batch prevents repeats.
    self.counts.clear(); objectMgr.items[1].Bonding = BIND_WHEN_PICKED_UP;
    assert(action.GetSpellPriority(&recipe) == 0);
    ai.context.usages[1] = ITEM_USAGE_EQUIP;
    assert(action.GetSpellPriority(&recipe) == 100); // Bind-on-pickup is allowed for oneself.
    self.counts[1000] = 1;
    assert(action.AcceptSpell(&recipe));
    known.Active = false; assert(!action.AcceptSpell(&recipe));
    known.Active = true; known.State = PLAYERSPELL_REMOVED; assert(!action.AcceptSpell(&recipe));
    known.State = 0; self.profession = false; assert(!action.AcceptSpell(&recipe));
    self.profession = true;
    recipe.Effects[0].ItemType = 2;
    ai.context.usages[2] = ITEM_USAGE_USE;
    self.counts[2] = 9; assert(action.GetSpellPriority(&recipe) == 100);
    self.counts[2] = 10; assert(action.GetSpellPriority(&recipe) == 0);
    self.counts.clear(); ai.context.usages[2] = ITEM_USAGE_NONE;
    other.alive = false; human.distance = 41;
    assert(action.GetSpellPriority(&recipe) == 0);
    human.distance = 0; assert(action.GetSpellPriority(&recipe) == 50);
    recipe.Effects[0].ItemType = 3;
    assert(action.GetSpellPriority(&recipe) == 100); // Personal bag comes before group needs.
    Bag bag{{&objectMgr.items[3]}};
    for (uint8 slot = 19; slot < 22; ++slot) self.bags[slot] = &bag;
    self.counts[3] = 3;
    assert(action.GetSpellPriority(&recipe) == 100); // Existing equipped copies still allow the fourth bag.
    self.bags[22] = &bag;
    self.counts[3] = 4;
    assert(action.GetSpellPriority(&recipe) == 0); // Four equipped copies also prevent a group batch.
    self.counts[3] = 0;
    assert(action.GetSpellPriority(&recipe) == 50);
    recipe.Effects[0].ItemType = 4;
    Item gear{&objectMgr.items[1]}; human.equipped = &gear;
    assert(action.GetSpellPriority(&recipe) == 50);
    human.equipped = new Item{&objectMgr.items[4]};
    assert(action.GetSpellPriority(&recipe) == 0);
    delete human.equipped;
    std::vector<std::pair<uint32, std::pair<uint32, Player*>>> candidates{
        {9999, {50, &other}}, {1, {100, &self}}, {10000, {0, &human}}};
    SortSpells(candidates, true);
    assert(candidates[0].first == 1 && candidates[1].first == 9999);
    SortSpells(candidates, false); assert(candidates[0].first == 10000);
    std::cout << "PASS: production crafting usefulness, self before group, learned recipes, "
        "caps, binding, priority sort\n";
}
