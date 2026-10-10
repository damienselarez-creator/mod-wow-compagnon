/*
 * WoW Compagnon, GNU GPL v2 or later.
 */
#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

using uint8 = uint8_t;
using uint32 = uint32_t;
constexpr uint32 ITEM_CLASS_CONSUMABLE = 0, ITEM_CLASS_TRADE_GOODS = 7;
constexpr uint32 ITEM_SPELLTRIGGER_ON_USE = 0, ITEM_SPELLTRIGGER_ON_NO_DELAY_USE = 5;
constexpr uint32 SPELL_EFFECT_ENCHANT_ITEM = 53, EQUIP_ERR_OK = 0;
constexpr uint32 ITEM_ENCHANTMENT_TYPE_RESISTANCE = 4, SPELL_SCHOOL_NORMAL = 0;
constexpr uint32 SPELL_ATTR2_ALLOW_LOW_LEVEL_BUFF = 1, PERM_ENCHANTMENT_SLOT = 0;
constexpr uint8 EQUIPMENT_SLOT_START = 0, EQUIPMENT_SLOT_END = 4, INVENTORY_SLOT_BAG_0 = 0;
constexpr uint32 MAX_SPELL_ITEM_ENCHANTMENT_EFFECTS = 3;
struct ItemTemplate
{
    uint32 Class = ITEM_CLASS_CONSUMABLE, RequiredLevel = 1, ItemLevel = 20;
    struct Use { uint32 SpellTrigger = 0, SpellId = 1; } Spells[1];
};
struct SpellInfo
{
    uint32 BaseLevel = 1, MaxLevel = 0;
    bool lowLevelAllowed = false;
    struct Effect
    {
        uint32 EffectId = SPELL_EFFECT_ENCHANT_ITEM;
        int MiscValue = 1;
        bool IsEffect(uint32 value) const { return EffectId == value; }
    } Effects[1];
    auto const& GetEffects() const { return Effects; }
    bool HasAttribute(uint32) const { return lowLevelAllowed; }
} reinforcement;
struct SpellMgr
{
    SpellInfo const* GetSpellInfo(uint32 id) const { return id == 1 ? &reinforcement : nullptr; }
} spells;
auto* sSpellMgr = &spells;
struct Enchantment
{
    uint32 requiredLevel = 1, requiredSkill = 0, requiredSkillValue = 0;
    uint32 type[3] = {ITEM_ENCHANTMENT_TYPE_RESISTANCE}, spellid[3] = {SPELL_SCHOOL_NORMAL};
    uint32 amount[3] = {8};
} armorBonus;
struct EnchantmentStore
{
    Enchantment const* LookupEntry(uint32 id) const { return id == 1 ? &armorBonus : nullptr; }
} sSpellItemEnchantmentStore;
struct Item
{
    ItemTemplate proto;
    uint32 enchantment = 0;
    bool fits = true;
    ItemTemplate const* GetTemplate() const { return &proto; }
    uint32 GetEnchantmentId(uint32) const { return enchantment; }
    bool IsFitToSpellRequirements(SpellInfo const*) const { return fits; }
};
struct Player
{
    Item gear[4];
    bool usable = true;
    uint32 level = 20, skill = 0;
    uint32 CanUseItem(ItemTemplate const*) const { return usable ? EQUIP_ERR_OK : 1; }
    uint32 GetLevel() const { return level; }
    uint32 GetSkillValue(uint32) const { return skill; }
    Item* GetItemByPos(uint8, uint8 slot) { return &gear[slot]; }
};
struct StatsWeightCalculator
{
    explicit StatsWeightCalculator(Player*) { }
    float CalculateEnchant(uint32) const { return 0; }
};
#include "production_enhancements.inc"

int main()
{
    Player player;
    ItemTemplate kit;
    assert(EnhancementTargets(&player, &kit).size() == 4);
    player.gear[0].enchantment = 999;
    assert(EnhancementTargets(&player, &kit).size() == 3);
    player.gear[1].fits = false;
    assert(EnhancementTargets(&player, &kit).size() == 2);
    reinforcement.MaxLevel = 10;
    assert(EnhancementTargets(&player, &kit).empty());
    reinforcement.MaxLevel = 0;
    armorBonus.requiredSkill = 165;
    armorBonus.requiredSkillValue = 100;
    assert(EnhancementTargets(&player, &kit).empty());
    player.skill = 100;
    assert(EnhancementTargets(&player, &kit).size() == 2);
    player.usable = false;
    assert(EnhancementTargets(&player, &kit).empty());
    player.usable = true;
    player.level = 0;
    assert(EnhancementTargets(&player, &kit).empty());
    player.level = 20;
    armorBonus.amount[0] = 0;
    assert(EnhancementTargets(&player, &kit).empty());
    kit.Spells[0].SpellId = 999;
    assert(!EnhancementSpell(&kit));
    std::cout << "PASS: production enhancement eligibility, four useful pieces, no replacement, "
        "native item mask, level, profession and positive armor benefit\n";
}
