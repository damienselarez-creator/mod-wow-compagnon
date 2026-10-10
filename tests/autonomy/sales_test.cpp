#include <cassert>
#include <map>
#include <string>

using uint8 = unsigned char;
constexpr int ITEM_QUALITY_POOR = 0, ITEM_QUALITY_RARE = 3, ITEM_CLASS_WEAPON = 2;
constexpr int ITEM_SUBCLASS_WEAPON_MISC = 14, EQUIP_ERR_OK = 0, NULL_SLOT = 255;
constexpr int EQUIPMENT_SLOT_MAINHAND = 15, INVENTORY_SLOT_BAG_0 = 255;
constexpr int ITEM_FIELD_MAXDURABILITY = 0, ITEM_FIELD_DURABILITY = 1;
enum ItemUsage { ITEM_USAGE_NONE, ITEM_USAGE_VENDOR, ITEM_USAGE_AH, ITEM_USAGE_EQUIP };
struct ItemTemplate { int Quality = 1, Class = 4, SubClass = 3, InventoryType = 10, armor = 68; };
struct Item
{
    ItemTemplate proto;
    int id = 1, maxDurability = 20, durability = 20;
    bool protectedItem = false;
    auto const* GetTemplate() { return &proto; }
    int GetEntry() { return id; }
    int GetItemRandomPropertyId() { return 0; }
    int GetUInt32Value(int field) { return field == ITEM_FIELD_MAXDURABILITY ? maxDurability : durability; }
};
struct Player
{
    Item* equipped = nullptr;
    bool usable = true;
    int CanUseItem(ItemTemplate const*) { return usable ? EQUIP_ERR_OK : 1; }
    Item* GetItemByPos(int, int) { return equipped; }
};
struct Context
{
    ItemUsage usage = ITEM_USAGE_NONE;
    template<class T> Context* GetValue(char const*, int) { return this; }
    ItemUsage Get() { return usage; }
};
struct PlayerbotAI
{
    bool managed = true, shield = true;
    Player bot;
    Context context;
    uint8 slot = 10;
    Player* GetBot() { return &bot; }
    Context* GetAiObjectContext() { return &context; }
    uint8 FindEquipSlot(ItemTemplate const*, int, bool) { return slot; }
};
bool IsCompanionInventoryManaged(PlayerbotAI* ai) { return ai && ai->managed; }
bool Protected(PlayerbotAI*, Item* item) { return item->protectedItem; }
bool PrefersCompanionShield(PlayerbotAI* ai) { return ai->shield; }
bool UsesCompanionArmorPriority(ItemTemplate const* proto) { return proto->Class == 4 && proto->armor; }
bool IsCompanionShieldMainHand(int type) { return type == 13 || type == 21; }
int CompareCompanionArmor(ItemTemplate const* a, ItemTemplate const* b) { return a->armor - b->armor; }
namespace CompanionErrands { bool CanSellGrey(int, bool, bool unused) { return unused; } }
struct Config { float equipUpgradeThreshold = 1.05f; } sPlayerbotAIConfig;
std::map<int, float> scores;
struct StatsWeightCalculator
{
    explicit StatsWeightCalculator(Player*) { }
    void SetItemSetBonus(bool) { }
    void SetOverflowPenalty(bool) { }
    float CalculateItem(int id, int) { return scores[id]; }
};
#include "sales.inc"

int main()
{
    PlayerbotAI ai;
    Item old, replacement;
    assert(!CanSellCompanionItem(&ai, &old)); // Better item in bags alone cannot authorize a sale.
    ai.bot.equipped = &replacement;
    assert(!CanSellCompanionItem(&ai, &old)); // Equal item retained.
    replacement.proto.armor = 73;
    assert(CanSellCompanionItem(&ai, &old));
    old.proto.armor = 80;
    assert(!CanSellCompanionItem(&ai, &old)); // Bag upgrade retained.
    old.proto.armor = 68;
    old.protectedItem = true;
    assert(!CanSellCompanionItem(&ai, &old));
    old.protectedItem = false;
    ai.bot.usable = false;
    assert(!CanSellCompanionItem(&ai, &old)); // Future-level equipment retained.
    ai.bot.usable = true;
    replacement.durability = 0;
    assert(!CanSellCompanionItem(&ai, &old)); // Broken replacement does not justify a sale.
    replacement.durability = 20;
    old.proto.Quality = 4;
    assert(!CanSellCompanionItem(&ai, &old));
    old.proto.Quality = 2;
    ai.context.usage = ITEM_USAGE_EQUIP;
    assert(!CanSellCompanionItem(&ai, &old));
    ai.context.usage = ITEM_USAGE_NONE;
    ai.shield = false;
    replacement.id = 2;
    scores = {{1, 10}, {2, 11}};
    assert(CanSellCompanionItem(&ai, &old));
    scores[2] = 10;
    assert(!CanSellCompanionItem(&ai, &old));
    old.proto.Class = ITEM_CLASS_WEAPON;
    old.proto.SubClass = ITEM_SUBCLASS_WEAPON_MISC;
    assert(!CanSellCompanionItem(&ai, &old));
    old.proto.SubClass = 0;
    old.proto.InventoryType = 17;
    assert(!CanSellCompanionItem(&ai, &old)); // Alternate weapon layout retained.
    ai.managed = false;
    assert(!CanSellCompanionItem(&ai, &old));
    assert(!CanSellCompanionItem(nullptr, &old));
}
