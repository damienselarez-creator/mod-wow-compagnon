#include <cassert>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

using uint32 = uint32_t;
using int32 = int32_t;
constexpr uint32 ITEM_CLASS_CONTAINER = 1, INVTYPE_NON_EQUIP = 0, EQUIP_ERR_OK = 0;
constexpr int ITERATE_ITEMS_IN_BAGS = 1;
enum ItemUsage { ITEM_USAGE_NONE, ITEM_USAGE_EQUIP, ITEM_USAGE_REPLACE, ITEM_USAGE_BAD_EQUIP };
#define AI_VALUE2(type, name, value) ITEM_USAGE_NONE
struct Item;
constexpr int CLASS_WARRIOR = 1, CLASS_PALADIN = 2, CLASS_ROGUE = 4;
constexpr int WARRIOR_TAB_PROTECTION = 2, PALADIN_TAB_PROTECTION = 1;
constexpr uint32 INVTYPE_WEAPON = 13, INVTYPE_SHIELD = 14, INVTYPE_2HWEAPON = 17;
constexpr uint32 INVTYPE_WEAPONMAINHAND = 21, INVTYPE_WEAPONOFFHAND = 22, INVTYPE_HOLDABLE = 23;
struct Player
{
    int cls = CLASS_WARRIOR, tab = 2;
    bool hasTalents = true;
    int getClass() const { return cls; }
    uint32 CanUseItem(Item*) const { return EQUIP_ERR_OK; }
};
struct PlayerbotAI
{
    Player bot;
    bool managed = true;
    Player* GetBot() { return &bot; }
};
bool IsCompanionInventoryManaged(PlayerbotAI* ai) { return ai && ai->managed; }
struct AiFactory
{
    static int GetPlayerSpecTab(Player* bot) { return bot->tab; }
    static std::map<int, int> GetPlayerSpecTabs(Player* bot)
    {
        return {{bot->tab, bot->hasTalents ? 1 : 0}};
    }
};
struct CompanionVocation
{
    bool managed = false;
    int tab = -1;
    static CompanionVocation Get(Player*);
};
CompanionVocation vocation;
CompanionVocation CompanionVocation::Get(Player*) { return vocation; }
constexpr uint32 ITEM_CLASS_ARMOR = 4;
constexpr uint32 ITEM_CLASS_WEAPON = 2, ITEM_SUBCLASS_WEAPON_MISC = 14, ITEM_SUBCLASS_WEAPON_FISHING_POLE = 20;
constexpr uint32 INVTYPE_HEAD = 1, INVTYPE_SHOULDERS = 3, INVTYPE_CHEST = 5, INVTYPE_WAIST = 6;
constexpr uint32 INVTYPE_LEGS = 7, INVTYPE_FEET = 8, INVTYPE_WRISTS = 9, INVTYPE_HANDS = 10;
constexpr uint32 INVTYPE_CLOAK = 16, INVTYPE_ROBE = 20;
struct ItemTemplate
{
    uint32 InventoryType, Class = ITEM_CLASS_ARMOR, Armor = 0, Quality = 0, ItemLevel = 0, SubClass = 0, ItemId = 0;
};
struct Item
{
    ItemTemplate* proto;
    ItemTemplate const* GetTemplate() const { return proto; }
    bool IsInTrade() const { return false; }
    int32 GetItemRandomPropertyId() const { return 0; }
};
struct CollectItemsVisitor { std::vector<Item*> items; };
struct ObjectMgr
{
    std::map<uint32, ItemTemplate> items;
    ItemTemplate const* GetItemTemplate(uint32 id) { return &items.at(id); }
} objectMgr;
#define sObjectMgr (&objectMgr)
using ItemIds = std::set<uint32>;
struct FindItemByIdVisitor
{
    uint32 id;
    explicit FindItemByIdVisitor(uint32 value) : id(value) { }
};
struct EquipAction
{
    PlayerbotAI* botAI;
    std::vector<uint32> order;
    Player* bot = botAI->GetBot();
    std::vector<Item*> inventory;
    void IterateItems(CollectItemsVisitor* visitor, int) { visitor->items = inventory; }
    void EquipItem(FindItemByIdVisitor* visitor) { order.push_back(visitor->id); }
    void EquipItems(ItemIds ids);
    ItemIds SelectInventoryItemsToEquip();
};
#include "production.inc"

int main()
{
    ItemTemplate worn{INVTYPE_HANDS, ITEM_CLASS_ARMOR, 68, 2, 15};
    ItemTemplate upgrade{INVTYPE_HANDS, ITEM_CLASS_ARMOR, 73, 1, 12};
    assert(UsesCompanionArmorPriority(&upgrade));
    assert(CompareCompanionArmor(&upgrade, &worn) > 0);
    assert(CompareCompanionArmor(&worn, &upgrade) < 0);
    upgrade.Armor = worn.Armor;
    assert(CompareCompanionArmor(&upgrade, &worn) < 0);
    upgrade.Quality = worn.Quality;
    upgrade.ItemLevel = 16;
    assert(CompareCompanionArmor(&upgrade, &worn) > 0);
    upgrade.ItemLevel = worn.ItemLevel;
    assert(CompareCompanionArmor(&upgrade, &worn) == 0);
    assert(!UsesCompanionArmorPriority(nullptr));
    ItemTemplate hammer{INVTYPE_WEAPON, ITEM_CLASS_WEAPON};
    ItemTemplate shield{INVTYPE_SHIELD, ITEM_CLASS_ARMOR};
    assert(IsCompanionLayoutCandidate(&hammer));
    assert(IsCompanionLayoutCandidate(&shield));
    hammer.SubClass = ITEM_SUBCLASS_WEAPON_MISC;
    assert(!IsCompanionLayoutCandidate(&hammer));
    hammer.SubClass = 0;
    hammer.InventoryType = INVTYPE_2HWEAPON;
    assert(!IsCompanionLayoutCandidate(&hammer));
    PlayerbotAI ai;
    assert(PrefersCompanionShield(&ai));
    assert(!PrefersCompanionShield(nullptr));
    ai.managed = false;
    assert(!PrefersCompanionShield(&ai));
    ai.managed = true;
    ai.bot.tab = 1;
    assert(!PrefersCompanionShield(&ai)); // Fury is free to dual wield.
    vocation = {true, 2};
    assert(!PrefersCompanionShield(&ai)); // Existing Fury talents override the saved vocation.
    ai.bot.hasTalents = false;
    assert(PrefersCompanionShield(&ai)); // Chosen vocation applies before talent points exist.
    ai.bot.hasTalents = true;
    vocation = {};
    ai.bot.cls = CLASS_PALADIN;
    assert(PrefersCompanionShield(&ai));
    ai.bot.cls = CLASS_ROGUE;
    assert(!PrefersCompanionShield(&ai));
    ai.bot.cls = CLASS_WARRIOR;
    ai.bot.tab = 2;

    assert(IsCompanionShieldRejected(INVTYPE_2HWEAPON));
    assert(IsCompanionShieldRejected(INVTYPE_WEAPONOFFHAND));
    assert(IsCompanionShieldRejected(INVTYPE_HOLDABLE));
    assert(!IsCompanionShieldRejected(INVTYPE_SHIELD));
    assert(!IsCompanionShieldRejected(INVTYPE_WEAPON));
    assert(IsCompanionShieldReplacement(INVTYPE_WEAPON, INVTYPE_2HWEAPON));
    assert(IsCompanionShieldReplacement(INVTYPE_SHIELD, INVTYPE_WEAPON));
    assert(!IsCompanionShieldReplacement(INVTYPE_WEAPON, INVTYPE_WEAPONMAINHAND));
    assert(!IsCompanionShieldReplacement(INVTYPE_SHIELD, INVTYPE_SHIELD));
    assert(!IsCompanionShieldReplacement(INVTYPE_2HWEAPON, INVTYPE_WEAPON));

    objectMgr.items = {{1, {INVTYPE_SHIELD}}, {2, {INVTYPE_WEAPON}}, {3, {INVTYPE_WEAPONMAINHAND}}};
    EquipAction action{&ai, {}};
    action.EquipItems({1, 2, 3});
    assert((action.order == std::vector<uint32>{2, 3, 1}));
    // A generic usage cache that says NONE must not block the Protection layout in the actual selector.
    objectMgr.items[1].ItemId = 1;
    objectMgr.items[2].ItemId = 2;
    objectMgr.items[2].Class = ITEM_CLASS_WEAPON;
    Item availableShield{&objectMgr.items[1]}, availableHammer{&objectMgr.items[2]};
    action.inventory = {&availableShield, &availableHammer};
    action.order.clear();
    action.EquipItems(action.SelectInventoryItemsToEquip());
    assert((action.order == std::vector<uint32>{2, 1}));
    objectMgr.items[2].SubClass = ITEM_SUBCLASS_WEAPON_MISC;
    assert(action.SelectInventoryItemsToEquip() == ItemIds{1});
    objectMgr.items[2].SubClass = 0;
    ai.bot.tab = 1;
    action.order.clear();
    action.EquipItems({1, 2, 3});
    assert((action.order == std::vector<uint32>{1, 2, 3}));
}
