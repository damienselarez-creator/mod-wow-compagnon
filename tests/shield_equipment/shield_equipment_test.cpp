#include <cassert>
#include <cstdint>
#include <map>
#include <set>
#include <vector>

using uint32 = uint32_t;
constexpr int CLASS_WARRIOR = 1, CLASS_PALADIN = 2, CLASS_ROGUE = 4;
constexpr int WARRIOR_TAB_PROTECTION = 2, PALADIN_TAB_PROTECTION = 1;
constexpr uint32 INVTYPE_WEAPON = 13, INVTYPE_SHIELD = 14, INVTYPE_2HWEAPON = 17;
constexpr uint32 INVTYPE_WEAPONMAINHAND = 21, INVTYPE_WEAPONOFFHAND = 22, INVTYPE_HOLDABLE = 23;
struct Player
{
    int cls = CLASS_WARRIOR, tab = 2;
    bool hasTalents = true;
    int getClass() const { return cls; }
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
constexpr uint32 INVTYPE_HEAD = 1, INVTYPE_SHOULDERS = 3, INVTYPE_CHEST = 5, INVTYPE_WAIST = 6;
constexpr uint32 INVTYPE_LEGS = 7, INVTYPE_FEET = 8, INVTYPE_WRISTS = 9, INVTYPE_HANDS = 10;
constexpr uint32 INVTYPE_CLOAK = 16, INVTYPE_ROBE = 20;
struct ItemTemplate
{
    uint32 InventoryType, Class = ITEM_CLASS_ARMOR, Armor = 0, Quality = 0, ItemLevel = 0;
};
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
    void EquipItem(FindItemByIdVisitor* visitor) { order.push_back(visitor->id); }
    void EquipItems(ItemIds ids);
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
    ai.bot.tab = 1;
    action.order.clear();
    action.EquipItems({1, 2, 3});
    assert((action.order == std::vector<uint32>{1, 2, 3}));
}
