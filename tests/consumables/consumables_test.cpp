#include "CompanionConsumablePolicy.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

using uint8 = uint8_t;
using uint32 = uint32_t;
using int32 = int32_t;
enum
{
    CLASS_WARRIOR = 1, CLASS_PALADIN, CLASS_HUNTER, CLASS_ROGUE, CLASS_PRIEST,
    CLASS_DEATH_KNIGHT, CLASS_SHAMAN, CLASS_MAGE, CLASS_WARLOCK, CLASS_DRUID,
    DRUID_TAB_FERAL = 1, SHAMAN_TAB_ENHANCEMENT = 1, PALADIN_TAB_HOLY = 0,
    PALADIN_TAB_PROTECTION = 1, WARRIOR_TAB_PROTECTION = 2, DEATH_KNIGHT_TAB_BLOOD = 0,
    EQUIP_ERR_OK = 0, POWER_MANA = 0, ITEM_CLASS_CONSUMABLE = 0,
    ITEM_SUBCLASS_POTION = 1, ITEM_SUBCLASS_SCROLL = 4, ITEM_SUBCLASS_BANDAGE = 7,
    ITEM_SPELLTRIGGER_ON_USE = 0, SPELL_AURA_MOD_STAT = 29, SPELL_AURA_MOD_RESISTANCE = 22,
    SPELL_AURA_PERIODIC_HEAL = 8, SPELL_AURA_PERIODIC_DAMAGE = 3,
    SPELL_AURA_PERIODIC_DAMAGE_PERCENT = 89, SPELL_AURA_PERIODIC_LEECH = 53,
    STAT_STRENGTH = 0, STAT_AGILITY = 1, STAT_STAMINA = 2, STAT_INTELLECT = 3, STAT_SPIRIT = 4,
    SPELL_SCHOOL_MASK_NORMAL = 1, SPELL_EFFECT_HEAL = 10, SPELL_EFFECT_ENERGIZE = 30,
    SPELL_GROUP_STACK_RULE_DEFAULT = 0, UNIT_STATE_LOST_CONTROL = 1,
    BOT_STATE_NON_COMBAT = 0, TRIGGERED_NONE = 0, SPELL_CAST_OK = 0,
    CURRENT_CHANNELED_SPELL = 1, CURRENT_GENERIC_SPELL = 2, CMSG_USE_ITEM = 1, TARGET_FLAG_UNIT = 2,
    SPELL_SPECIFIC_SCROLL = 99
};
using AuraType = int;
struct ObjectGuid
{
    uint32 value = 0;
    ObjectGuid WriteAsPacked() const { return *this; }
};
struct Player;
struct SpellEffectInfo
{
    int Effect = 0, ApplyAuraName = 0, MiscValue = 0, Amplitude = 0;
    float value = 0;
    bool IsAura() const { return Effect == 6; }
    bool IsEffect(int effect) const { return Effect == effect; }
    float CalcValue(Player*) const { return value; }
};
struct SpellInfo
{
    uint32 Id = 0, family = 0;
    int duration = 8000;
    bool channel = false;
    std::array<SpellEffectInfo, 3> Effects;
    auto const& GetEffects() const { return Effects; }
    bool IsRankOf(SpellInfo const* other) const { return family && family == other->family; }
    int GetSpellSpecific() const { return family ? SPELL_SPECIFIC_SCROLL : 0; }
    bool IsAuraExclusiveBySpecificWith(SpellInfo const* other) const { return family && other->family; }
    bool IsChanneled() const { return channel; }
    int GetDuration() const { return duration; }
    uint32 CalcCastTime(Player*) const { return 0; }
};
struct AuraEffect
{
    SpellInfo const* info = nullptr;
    int misc = 0, amount = 0;
    SpellInfo const* GetSpellInfo() const { return info; }
    int GetMiscValue() const { return misc; }
    int GetAmount() const { return amount; }
    int GetEffIndex() const { return 0; }
};
struct SpellMgr
{
    std::map<uint32, SpellInfo> spells;
    SpellInfo const* GetSpellInfo(uint32 id) { return spells.count(id) ? &spells.at(id) : nullptr; }
    int CheckSpellGroupStackRules(SpellInfo const* a, SpellInfo const* b) { return a->IsRankOf(b); }
} manager;
SpellMgr* sSpellMgr = &manager;
struct ItemTemplate
{
    uint32 ItemId = 0;
    int Class = ITEM_CLASS_CONSUMABLE, SubClass = ITEM_SUBCLASS_POTION, StartQuest = 0;
    struct Use { int SpellId = 0, SpellTrigger = ITEM_SPELLTRIGGER_ON_USE; };
    std::array<Use, 5> Spells;
};
struct Item
{
    ItemTemplate proto;
    uint32 count = 1;
    bool trade = false, wrapped = false, usable = true;
    auto const* GetTemplate() { return &proto; }
    uint32 GetEntry() { return proto.ItemId; }
    ObjectGuid GetGUID() { return {proto.ItemId}; }
    bool IsInTrade() { return trade; }
    bool IsWrapped() { return wrapped; }
    uint8 GetBagSlot() { return 255; }
    uint8 GetSlot() { return uint8(proto.ItemId); }
};
struct SpellCastTargets { void SetUnitTarget(Player*) { } };
struct Spell
{
    Player* bot;
    SpellInfo const* info;
    Item* m_CastItem = nullptr;
    SpellCastTargets m_targets;
    Spell(Player* bot, SpellInfo const* info, int) : bot(bot), info(info) { }
    int CheckCast(bool);
    auto const* GetSpellInfo() { return info; }
};
struct WorldPacket
{
    std::vector<uint32> fields;
    explicit WorldPacket(int) { }
    WorldPacket& operator<<(uint32 v) { fields.push_back(v); return *this; }
    WorldPacket& operator<<(ObjectGuid guid) { fields.push_back(guid.value); return *this; }
};
struct Group;
struct GroupReference
{
    Player* member = nullptr;
    GroupReference* following = nullptr;
    Player* GetSource() { return member; }
    GroupReference* next() { return following; }
};
struct Group { GroupReference* first = nullptr; GroupReference* GetFirstMember() { return first; } };
struct Session
{
    Player* player = nullptr;
    bool logout = false, accept = true;
    uint32 calls = 0, lastItem = 0;
    bool IsLoggingOut() { return logout; }
    void HandleUseItemOpcode(WorldPacket& packet);
};
struct Player
{
    Session session{this};
    int cls = CLASS_WARRIOR, tab = 2, powerType = 1, maxMana = 0, mana = 0, health = 300, maxHealth = 1000;
    bool alive = true, world = true, combat = true, moving = false, teleport = false, cast = false;
    bool mounted = false, flight = false, fall = false, control = false, transport = false, vehicle = false;
    int map = 1, phase = 1, distance = 0;
    Group* group = nullptr;
    std::vector<int> attackers;
    std::vector<Item*> items;
    std::vector<AuraEffect*> auras;
    std::map<uint32, bool> cooldown;
    std::map<int, bool> harmful;
    Spell* current = nullptr;
    int getClass() { return cls; }
    int getPowerType() { return powerType; }
    int GetMaxPower(int) { return maxMana; }
    int GetPower(int) { return mana; }
    float GetPowerPct(int) { return maxMana ? mana * 100.0f / maxMana : 0; }
    float GetHealthPct() { return health * 100.0f / maxHealth; }
    int GetMaxHealth() { return maxHealth; }
    int GetHealth() { return health; }
    bool IsAlive() { return alive; }
    bool IsInWorld() { return world; }
    Session* GetSession() { return &session; }
    bool IsBeingTeleported() { return teleport; }
    bool HasUnitState(int) { return control; }
    bool IsMounted() { return mounted; }
    bool IsInFlight() { return flight; }
    bool IsFalling() { return fall; }
    bool GetTransport() { return transport; }
    bool GetVehicle() { return vehicle; }
    bool GetTradeData() { return false; }
    bool IsNonMeleeSpellCast(bool) { return cast || current; }
    bool IsInCombat() { return combat; }
    bool isMoving() { return moving; }
    int GetMap() { return map; }
    int GetPhaseMask() { return phase; }
    int GetDistance(Player*) { return distance; }
    Group* GetGroup() { return group; }
    auto const& getAttackers() { return attackers; }
    bool HasAuraType(int type) { return harmful[type]; }
    auto const& GetAuraEffectsByType(int) { return auras; }
    int CanUseItem(Item* item) { return item->usable ? EQUIP_ERR_OK : 1; }
    uint32 GetItemCount(uint32 entry, bool)
    {
        uint32 count = 0;
        for (auto* item : items) if (item->GetEntry() == entry) count += item->count;
        return count;
    }
    Spell* GetCurrentSpell(int) { return current; }
    ObjectGuid GetGUID() { return {1000}; }
};
int Spell::CheckCast(bool) { return bot->cooldown[info->Id] ? 1 : SPELL_CAST_OK; }
void Session::HandleUseItemOpcode(WorldPacket& packet)
{
    assert(packet.fields.size() == 9 && packet.fields[7] == TARGET_FLAG_UNIT && packet.fields[8] == 1000);
    ++calls;
    lastItem = packet.fields[4];
    if (!accept) return;
    for (auto* item : player->items)
        if (item->GetEntry() == lastItem)
        {
            --item->count;
            player->cooldown[packet.fields[3]] = true;
            return;
        }
}
struct PlayerbotAI
{
    Player bot, master;
    bool managed = true, errand = false, pendingFlight = false, passive = false, healer = false;
    uint32 delay = 0;
    Player* GetBot() { return &bot; }
    Player* GetMaster() { return &master; }
    bool HasStrategy(char const*, int) { return passive; }
    bool IsHeal(Player*) { return healer; }
    bool IsTank(Player*) { return false; }
    auto GetInventoryItems() { return bot.items; }
    void SetNextCheckDelay(uint32 value) { delay = value; }
};
namespace AiFactory
{
    std::map<uint8, uint32> talents = {{0, 0}, {1, 0}, {2, 1}};
    auto GetPlayerSpecTabs(Player*) { return talents; }
    int GetPlayerSpecTab(Player* bot) { return bot->tab; }
}
namespace CompanionVocation
{
    struct Choice { bool managed = false; int tab = -1; } choice;
    Choice Get(Player*) { return choice; }
}
bool IsCompanionInventoryManaged(PlayerbotAI* ai) { return ai && ai->managed; }
bool HasPendingCompanionFlight(PlayerbotAI* ai) { return ai->pendingFlight; }
bool IsCompanionErrandActive(PlayerbotAI* ai) { return ai->errand; }
struct Event { };
struct Action
{
    PlayerbotAI* botAI;
    Action(PlayerbotAI* ai, char const*) : botAI(ai) { }
    virtual bool isUseful() { return false; }
    virtual bool Execute(Event) { return false; }
    virtual ~Action() = default;
};
template<class T> struct NamedObjectContext { std::map<std::string, T*(*)(PlayerbotAI*)> creators; };
struct CompanionConsumable
{
    CompanionConsumables::Kind kind = CompanionConsumables::Kind::Unknown;
    CompanionConsumables::Stat stat = CompanionConsumables::Stat::Unknown;
    uint32 spell = 0;
    float amount = 0;
};
CompanionConsumable DescribeCompanionConsumable(PlayerbotAI*, ItemTemplate const*);
bool IsUsefulCompanionScroll(PlayerbotAI*, CompanionConsumable const&);
struct Config { uint32 reactDelay = 100; } sPlayerbotAIConfig;
#define LOG_INFO(...) ((void)0)
#include "consumables.inc"

Item MakeItem(uint32 id, int subclass, SpellEffectInfo effect, uint32 family = 0)
{
    SpellInfo spell;
    spell.Id = id;
    spell.family = family;
    spell.Effects[0] = effect;
    manager.spells[id] = spell;
    Item item;
    item.proto.ItemId = id;
    item.proto.SubClass = subclass;
    item.proto.Spells[0].SpellId = int(id);
    return item;
}

int main()
{
    using Kind = CompanionConsumables::Kind;
    using Stat = CompanionConsumables::Stat;
    PlayerbotAI ai;
    auto small = MakeItem(10, ITEM_SUBCLASS_POTION, {SPELL_EFFECT_HEAL, 0, 0, 0, 400});
    auto adequate = MakeItem(11, ITEM_SUBCLASS_POTION, {SPELL_EFFECT_HEAL, 0, 0, 0, 750});
    auto excessive = MakeItem(12, ITEM_SUBCLASS_POTION, {SPELL_EFFECT_HEAL, 0, 0, 0, 1500});
    ai.bot.items = {&small, &excessive, &adequate};
    assert(SelectConsumable(&ai) == &adequate); // Smallest dose covering missing health.
    adequate.usable = false;
    assert(SelectConsumable(&ai) == &excessive);
    ai.bot.cooldown[12] = true;
    assert(SelectConsumable(&ai) == &small); // A stronger cooling-down item cannot hide a usable one.
    ai.bot.session.accept = false;
    assert(!UseCompanionPotion(&ai, true) && small.count == 1 && ai.delay == 0);
    ai.bot.session.accept = true;
    assert(UseCompanionPotion(&ai, true) && small.count == 0 && ai.bot.session.calls == 2);
    assert(!UseCompanionPotion(&ai, true));

    auto mp = MakeItem(13, ITEM_SUBCLASS_POTION, {SPELL_EFFECT_ENERGIZE, 0, POWER_MANA, 0, 300});
    ai.bot.items = {&mp};
    assert(!SelectConsumable(&ai));
    ai.bot.cls = CLASS_PRIEST;
    ai.bot.powerType = POWER_MANA;
    ai.bot.maxMana = 1000;
    ai.bot.mana = 0;
    assert(SelectConsumable(&ai) == &mp); // Empty mana is still a mana user.
    ai.bot.mana = 600;
    assert(!SelectConsumable(&ai));

    ai.bot.combat = false;
    ai.master.combat = false;
    ai.bot.cls = CLASS_WARRIOR;
    ai.bot.powerType = 1;
    ai.bot.maxMana = 0;
    ai.bot.mana = 0;
    Group group;
    ai.bot.group = ai.master.group = &group;
    auto bandage = MakeItem(14, ITEM_SUBCLASS_BANDAGE, {6, SPELL_AURA_PERIODIC_HEAL, 0, 1000, 100});
    manager.spells[14].channel = true;
    ai.bot.items = {&bandage};
    assert(DescribeCompanionConsumable(&ai, &bandage.proto).amount == 800);
    assert(SelectConsumable(&ai) == &bandage);
    ai.master.moving = true;
    assert(!SelectConsumable(&ai));
    ai.master.moving = false;
    ai.bot.harmful[SPELL_AURA_PERIODIC_DAMAGE] = true;
    assert(!SelectConsumable(&ai));
    ai.bot.harmful.clear();
    ai.bot.attackers = {1};
    assert(!SelectConsumable(&ai));
    ai.bot.attackers.clear();
    ai.bot.cooldown[14] = true; // Native cast refusal includes recently bandaged.
    assert(!SelectConsumable(&ai));
    ai.bot.cooldown.clear();
    assert(UseConsumable(&ai, SelectConsumable(&ai)) && ai.delay == 8100);

    auto strength = MakeItem(15, ITEM_SUBCLASS_SCROLL, {6, SPELL_AURA_MOD_STAT, STAT_STRENGTH, 0, 8}, 1);
    auto intellect = MakeItem(16, ITEM_SUBCLASS_SCROLL, {6, SPELL_AURA_MOD_STAT, STAT_INTELLECT, 0, 8}, 2);
    auto agility = MakeItem(17, ITEM_SUBCLASS_SCROLL, {6, SPELL_AURA_MOD_STAT, STAT_AGILITY, 0, 8}, 3);
    auto armor = MakeItem(18, ITEM_SUBCLASS_SCROLL, {6, SPELL_AURA_MOD_RESISTANCE, SPELL_SCHOOL_MASK_NORMAL, 0, 100}, 4);
    auto unknown = MakeItem(19, ITEM_SUBCLASS_SCROLL, {5, 0, 0, 0, 1});
    auto negative = MakeItem(20, ITEM_SUBCLASS_SCROLL, {6, SPELL_AURA_MOD_STAT, STAT_STAMINA, 0, -5});
    assert(DescribeCompanionConsumable(&ai, &unknown.proto).kind == Kind::Unknown);
    assert(DescribeCompanionConsumable(&ai, &negative.proto).kind == Kind::Unknown);
    for (int cls = CLASS_WARRIOR; cls <= CLASS_DRUID; ++cls)
        for (int tab = 0; tab < 3; ++tab)
        {
            ai.bot.cls = cls;
            ai.bot.tab = tab;
            bool const mana = cls != CLASS_WARRIOR && cls != CLASS_ROGUE && cls != CLASS_DEATH_KNIGHT;
            ai.bot.maxMana = mana ? 1000 : 0;
            assert(IsUsefulCompanionScroll(&ai, DescribeCompanionConsumable(&ai, &armor.proto)));
            assert(IsUsefulCompanionScroll(&ai, DescribeCompanionConsumable(&ai, &intellect.proto)) == mana);
            bool const caster = cls == CLASS_PRIEST || cls == CLASS_MAGE || cls == CLASS_WARLOCK ||
                (cls == CLASS_PALADIN && tab == 0) || (cls == CLASS_DRUID && tab != 1) ||
                (cls == CLASS_SHAMAN && tab != 1);
            assert(IsUsefulCompanionScroll(&ai, DescribeCompanionConsumable(&ai, &strength.proto)) ==
                (!caster && cls != CLASS_HUNTER));
            assert(IsUsefulCompanionScroll(&ai, DescribeCompanionConsumable(&ai, &agility.proto)) == !caster);
        }
    ai.bot.cls = CLASS_PALADIN;
    ai.bot.tab = 0;
    AiFactory::talents = {{0, 0}, {1, 0}, {2, 0}};
    CompanionVocation::choice = {true, 1};
    assert(IsUsefulCompanionScroll(&ai, DescribeCompanionConsumable(&ai, &strength.proto)));
    AiFactory::talents = {{0, 1}, {1, 0}, {2, 0}};
    assert(!IsUsefulCompanionScroll(&ai, DescribeCompanionConsumable(&ai, &strength.proto)));

    ai.bot.cls = CLASS_WARRIOR;
    ai.bot.maxMana = 0;
    ai.bot.tab = WARRIOR_TAB_PROTECTION;
    ai.bot.health = 1000;
    ai.bot.items = {&strength, &intellect, &unknown};
    ai.bot.cooldown.clear();
    assert(SelectConsumable(&ai) == &strength);
    ai.bot.items.push_back(&armor);
    assert(SelectConsumable(&ai) == &armor); // Protection selects defense before strength.
    AuraEffect armorCovered{manager.GetSpellInfo(18), SPELL_SCHOOL_MASK_NORMAL, 100};
    ai.bot.auras = {&armorCovered};
    assert(!SelectConsumable(&ai)); // No weaker-family scroll can replace the preferred active scroll.
    ai.bot.auras.clear();
    ai.bot.items.pop_back();
    auto stronger = MakeItem(21, ITEM_SUBCLASS_SCROLL, {6, SPELL_AURA_MOD_STAT, STAT_STRENGTH, 0, 12}, 1);
    ai.bot.items.push_back(&stronger);
    assert(SelectConsumable(&ai) == &stronger);
    AuraEffect covered{manager.GetSpellInfo(21), STAT_STRENGTH, 12};
    ai.bot.auras = {&covered};
    assert(!SelectConsumable(&ai));
    covered.amount = 4;
    assert(SelectConsumable(&ai) == &stronger);
    ai.bot.items.push_back(&armor);
    assert(SelectConsumable(&ai) == &armor); // A better role family may replace a weaker family once.
    ai.bot.items.pop_back();
    ai.bot.auras.clear();
    ai.pendingFlight = true;
    assert(!SelectConsumable(&ai));
    ai.pendingFlight = false;
    ai.errand = true;
    assert(!SelectConsumable(&ai));
    ai.errand = false;
    ai.managed = false;
    assert(!SelectConsumable(&ai));
}
