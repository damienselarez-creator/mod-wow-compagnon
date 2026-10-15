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
#include "CompanionErrandPolicy.h"
#include "CompanionProfessions.h"
#include "CompanionGoalPolicy.h"
#include "ProfessionPlan.h"

using uint8 = uint8_t;
using uint16 = uint16_t;
using uint32 = uint32_t;
using int32 = int32_t;
constexpr uint32 EQUIP_ERR_OK = 0, EFFECT_0 = 0, SPELL_EFFECT_CREATE_ITEM = 24;
constexpr uint8 NULL_SLOT = 255, INVENTORY_SLOT_BAG_START = 19, INVENTORY_SLOT_BAG_END = 23;
constexpr uint32 ITEM_CLASS_CONSUMABLE = 0, ITEM_CLASS_CONTAINER = 1, ITEM_CLASS_WEAPON = 2, ITEM_CLASS_ARMOR = 4;
constexpr uint32 MAX_SPELL_REAGENTS = 8, NO_BIND = 0, BIND_WHEN_EQUIPPED = 2, BIND_WHEN_USE = 3;
constexpr uint32 POWER_MANA = 0, BIND_WHEN_PICKED_UP = 1, PLAYERSPELL_REMOVED = 1;
constexpr uint32 ITEM_QUALITY_UNCOMMON = 2, ITEM_CLASS_TRADE_GOODS = 7;
constexpr uint32 SKILL_MINING = 186, SKILL_HERBALISM = 182;
enum ItemUsage { ITEM_USAGE_NONE, ITEM_USAGE_EQUIP, ITEM_USAGE_REPLACE, ITEM_USAGE_AMMO,
    ITEM_USAGE_QUEST, ITEM_USAGE_SKILL, ITEM_USAGE_USE };
struct Player;
struct ItemTemplate
{
    uint32 ItemId = 0, Class = 0, BagFamily = 0, ContainerSlots = 0, Bonding = 0;
    float score = 0;
    uint32 Quality = 1;
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
    struct EffectData
    {
        uint32 Effect = SPELL_EFFECT_CREATE_ITEM, ItemType = 0;
        int32 output = 1;
        int32 CalcValue(Player*) const { return output; }
    } Effects[1];
    uint32 ReagentCount[MAX_SPELL_REAGENTS] = {1}, SchoolMask = 0;
    int32 Reagent[MAX_SPELL_REAGENTS] = {1000};
    uint32 Totem[2]{}, TotemCategory[2]{};
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
auto* sSpellMgr = &spellMgr;
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
    bool combat = false, moving = false, casting = false;
    bool IsInCombat() const { return combat; }
    bool isMoving() const { return moving; }
    bool IsNonMeleeSpellCast(bool) const { return casting; }
    float distance = 0;
    uint32 GetGuildId() const { return 1; }
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
    std::map<uint32, uint32> skillValues;
    std::set<uint32> cooldowns;
    bool HasSpellCooldown(uint32 id) const { return cooldowns.count(id); }
    std::vector<std::pair<uint32, uint32>> defaultsRestored;
    uint32 GetPureSkillValue(uint32 id) const
    { auto it = skillValues.find(id); return it == skillValues.end() ? 0 : it->second; }
    uint32 GetPureMaxSkillValue(uint32) const { return 450; }
    void learnSkillRewardedSpells(uint32 id, uint32 value) { defaultsRestored.emplace_back(id, value); }
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
    Player* player = nullptr;
    Player* GetBot() const { return player; }
    Context context;
    bool managed = true;
    Context* GetAiObjectContext() { return &context; }
};
#define GET_PLAYERBOT_AI(player) ((player)->ai)
#define AI_VALUE2(type, name, id) (botAI->context.GetValue<type>(name, std::to_string(id))->Get())
bool IsCompanionInventoryManaged(PlayerbotAI* ai) { return ai->managed; }
bool IsManagedCompanion(PlayerbotAI* ai) { return ai->managed; }
bool craftPlan = true;
bool HasCompanionCraftPlan(PlayerbotAI*) { return craftPlan; }
bool bankStock = false;
bool CompanionGuildBankHasProduct(uint32, uint32) { return bankStock; }
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
bool skillUp = false;
struct ItemUsageValue
{
    static std::string GetConsumableType(ItemTemplate const*, uint32) { return "healing potion"; }
    static bool SpellGivesSkillUp(uint32, Player*) { return skillUp; }
};
struct SkillRecord { uint32 SkillLine = 164; };
struct PlayerbotSpellRepository
{
    SkillRecord line;
    std::map<uint32, SkillRecord> lines;
    static PlayerbotSpellRepository& Instance() { static PlayerbotSpellRepository r; return r; }
    SkillRecord const* GetSkillLine(uint32 id) const
    { auto it = lines.find(id); return it == lines.end() ? &line : &it->second; }
};
bool IsProfessionSkill(uint32) { return true; }
struct CraftRandomItemAction
{
    PlayerbotAI* botAI;
    Player* bot;
    explicit CraftRandomItemAction(PlayerbotAI* ai, Player* player = nullptr)
        : botAI(ai), bot(player ? player : ai->GetBot()) { }
    bool UseStrictPriority() const;
    bool isUseful();
    bool AcceptSpell(SpellInfo const*);
    uint32 GetSpellPriority(SpellInfo const*);
};
#include "production_crafting.inc"
bool professionAllowed = true;
bool CompanionGoalAllowed(PlayerbotAI*, CompanionGoals::Domain) { return professionAllowed; }
bool CompanionGoalPermitted(PlayerbotAI*, CompanionGoals::Domain) { return professionAllowed; }
#include "production_professions.inc"

void TestProfessionPlans()
{
    Player self;
    PlayerbotAI ai;
    ai.player = &self;
    self.ai = &ai;
    KnownSpell known;
    auto& repository = PlayerbotSpellRepository::Instance();
    repository.lines.clear();
    spellMgr.spells.clear();
    bankStock = false;
    skillUp = false;
    for (uint32 id : {500u, 501u, 502u, 503u})
        self.spells[id] = &known;
    objectMgr.items[5] = {5, ITEM_CLASS_TRADE_GOODS}; // Ore.
    objectMgr.items[6] = {6, ITEM_CLASS_TRADE_GOODS}; // Bars.
    objectMgr.items[7] = {7, ITEM_CLASS_TRADE_GOODS}; // Two components per cast.
    objectMgr.items[8] = {8, ITEM_CLASS_ARMOR};
    objectMgr.items[9] = {9, ITEM_CLASS_CONSUMABLE};
    auto recipe = [&](uint32 id, uint32 output, uint32 reagent, uint32 count, uint32 skill)
    {
        SpellInfo spell;
        spell.Id = id;
        spell.Effects[0].ItemType = output;
        spell.Reagent[0] = reagent;
        spell.ReagentCount[0] = count;
        spellMgr.spells[id] = spell;
        repository.lines[id] = {skill};
    };
    recipe(500, 6, 5, 2, SKILL_MINING);
    recipe(501, 7, 6, 1, 164);
    spellMgr.spells[501].Effects[0].output = 2;
    recipe(502, 8, 7, 3, 164);
    spellMgr.spells[502].Reagent[1] = 6;
    spellMgr.spells[502].ReagentCount[1] = 1;
    recipe(503, 9, 10, 1, 129);
    ai.context.usages[8] = ITEM_USAGE_EQUIP;
    ai.context.usages[9] = ITEM_USAGE_USE;
    self.counts[5] = 6;
    self.counts[10] = 8;
    auto plan = PlanCompanionProfession(&ai, {});
    assert(plan.root == 502 && plan.next == 500 && plan.primary && plan.missing.empty());
    self.counts[5] = 0;
    self.counts[6] = 3;
    plan = PlanCompanionProfession(&ai, {});
    assert(plan.root == 502 && plan.next == 501); // Replan from current stock, not a stale queue.
    self.counts[6] = 1;
    self.counts[7] = 4;
    plan = PlanCompanionProfession(&ai, {});
    assert(plan.root == 502 && plan.next == 502);
    self.counts[6] = self.counts[7] = 0;
    plan = PlanCompanionProfession(&ai, {});
    assert(plan.root == 502 && plan.next == 503 && !plan.primary && plan.materialsFor == 502);
    assert(plan.missing.at(5) == 6); // Search ore, not imaginary purchased bars.
    assert(!plan.missing.count(6) && !plan.missing.count(7));
    self.counts[10] = 0;
    plan = PlanCompanionProfession(&ai, {});
    assert(plan.root == 502 && !plan.next && plan.missing.at(5) == 6);
    self.counts[5] = 6;
    plan = PlanCompanionProfession(&ai, {500});
    assert(plan.next != 500); // A failed native recipe is not retried in a tight loop.
    self.cooldowns.insert(500);
    assert(PlanCompanionProfession(&ai, {}).next != 500);
    self.cooldowns.clear();
    known.Active = false;
    assert(!PlanCompanionProfession(&ai, {}).root);
    known.Active = true;
    professionAllowed = false;
    assert(!PlanCompanionProfession(&ai, {}).root);
    RestoreCompanionProfessionDefaults(&ai);
    assert(self.defaultsRestored.empty());
    professionAllowed = true;
    self.skillValues[164] = 1;
    RestoreCompanionProfessionDefaults(&ai);
    assert(!self.defaultsRestored.empty() && self.defaultsRestored.front() == std::make_pair(164u, 1u));
    assert(CompanionCraftBudgetAvailable(4, 23, 2, true));
    assert(!CompanionCraftBudgetAvailable(4, 23, 2, false));
    assert(!CompanionCraftBudgetAvailable(5, 1, 0, true));
    assert(!CompanionCraftBudgetAvailable(0, 24, 0, true));
    assert(CompanionErrands::SearchRadius(false, false) == 100.0f);
    assert(CompanionErrands::SearchRadius(true, false) == 180.0f);
    assert(CompanionErrands::GatherReturn(true, 80.0f, 121.0f));
    assert(!CompanionErrands::GatherReturn(false, 180.0f, 180.0f));
    assert(CompanionErrands::GatherReturn(false, 221.0f, 180.0f));
    assert(CompanionErrands::GatherScore(170.0f, true) < CompanionErrands::GatherScore(10.0f, false));
}

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
    assert(action.isUseful()); // Regression: inherited isUseful() formerly always returned false.
    craftPlan = false; assert(!action.isUseful()); craftPlan = true;
    self.combat = true; assert(!action.isUseful()); self.combat = false;
    self.moving = true; assert(!action.isUseful()); self.moving = false;
    self.casting = true; assert(!action.isUseful()); self.casting = false;
    self.alive = false; assert(!action.isUseful()); self.alive = true;
    KnownSpell known; self.spells[100] = &known;
    self.counts[1000] = 1;
    SpellInfo recipe; recipe.Id = 100; recipe.Effects[0].ItemType = 1;
    ai.context.usages[1] = ITEM_USAGE_EQUIP;
    friendAi.context.usages[1] = ITEM_USAGE_EQUIP;
    assert(action.AcceptSpell(&recipe));
    recipe.SchoolMask = 1;
    assert(action.AcceptSpell(&recipe)); // Physical profession spells remain eligible.
    ai.managed = false; assert(!action.AcceptSpell(&recipe));
    ai.managed = true;
    self.counts[1000] = 0; assert(!action.AcceptSpell(&recipe));
    self.counts[1000] = 1;
    recipe.Totem[0] = 5956; assert(!action.AcceptSpell(&recipe));
    self.counts[5956] = 1; assert(action.AcceptSpell(&recipe));
    recipe.TotemCategory[0] = 162; assert(!action.AcceptSpell(&recipe));
    recipe.TotemCategory[0] = 0; recipe.Totem[0] = 0;
    assert(action.GetSpellPriority(&recipe) == 100);
    ai.context.usages[1] = ITEM_USAGE_NONE;
    assert(action.GetSpellPriority(&recipe) == 50);
    bankStock = true;
    assert(action.GetSpellPriority(&recipe) == 0);
    ai.context.usages[1] = ITEM_USAGE_EQUIP;
    assert(action.GetSpellPriority(&recipe) == 100);
    ai.context.usages[1] = ITEM_USAGE_NONE;
    bankStock = false;
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
    human.equipped = nullptr;
    human.distance = 100;
    skillUp = true;
    assert(action.GetSpellPriority(&recipe) == 10); // Bounded practice using owned materials.
    self.skillValues[164] = 450;
    assert(action.GetSpellPriority(&recipe) == 0); // Training the next rank precedes more practice at the cap.
    self.skillValues[164] = 1;
    self.counts[4] = 1;
    assert(action.GetSpellPriority(&recipe) == 10); // Bounded practice can continue beyond one pending product.
    self.counts[4] = 3;
    assert(action.GetSpellPriority(&recipe) == 0); // Still no unlimited production of obsolete gear.
    self.counts[4] = 0;
    objectMgr.items[4].Quality = 4;
    assert(action.GetSpellPriority(&recipe) == 0); // No expensive practice product.
    objectMgr.items[4].Quality = 1;
    skillUp = false;
    assert(action.GetSpellPriority(&recipe) == 0); // Grey recipes give no practice benefit.
    objectMgr.items[5] = ItemTemplate{5, ITEM_CLASS_TRADE_GOODS};
    SpellInfo smelt;
    smelt.Id = 500;
    smelt.Effects[0].ItemType = 5;
    SpellInfo downstream;
    downstream.Id = 501;
    downstream.Effects[0].ItemType = 4;
    downstream.Reagent[0] = 5;
    downstream.ReagentCount[0] = 3;
    spellMgr.spells[501] = downstream;
    self.spells[501] = &known;
    ai.context.usages[4] = ITEM_USAGE_EQUIP;
    assert(action.GetSpellPriority(&smelt) == 60); // Grey smelting enables a useful recipe.
    self.counts[5] = 2;
    assert(action.GetSpellPriority(&smelt) == 60);
    self.counts[5] = 3;
    assert(action.GetSpellPriority(&smelt) == 0); // Do not convert the entire ore stock.
    self.counts[5] = 0;
    self.counts[4] = 1;
    assert(action.GetSpellPriority(&smelt) == 0);
    self.counts[4] = 0;
    known.Active = false;
    assert(action.GetSpellPriority(&smelt) == 0);
    known.Active = true;
    std::vector<std::pair<uint32, std::pair<uint32, Player*>>> candidates{
        {9999, {50, &other}}, {1, {100, &self}}, {10000, {0, &human}}};
    SortSpells(candidates, true);
    assert(candidates[0].first == 1 && candidates[1].first == 9999);
    SortSpells(candidates, false); assert(candidates[0].first == 10000);
    TestProfessionPlans();
    std::cout << "PASS: production crafting usefulness, self before group, learned recipes, "
        "caps, binding, priority sort\n";
}
