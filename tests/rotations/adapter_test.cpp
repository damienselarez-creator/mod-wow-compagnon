#include "CompanionRotationPolicy.h"
#include <any>
#include <cassert>
#include <map>
#include <string>
#include <vector>

using uint8 = uint8_t;
using uint32 = uint32_t;
using ObjectGuid = uint32;
using GuidVector = std::vector<ObjectGuid>;
constexpr int BOT_STATE_COMBAT = 1, POWER_MANA = 0, POWER_RAGE = 1, POWER_ENERGY = 3;
namespace Powers { constexpr int POWER_MANA = 0; }
constexpr int CLASS_WARRIOR = 1, CLASS_PALADIN = 2, CLASS_HUNTER = 3, CLASS_ROGUE = 4;
constexpr int CLASS_PRIEST = 5, CLASS_DEATH_KNIGHT = 6, CLASS_SHAMAN = 7, CLASS_MAGE = 8;
constexpr int CLASS_WARLOCK = 9, CLASS_DRUID = 11;
constexpr int SPELL_EFFECT_SCHOOL_DAMAGE = 1, SPELL_EFFECT_WEAPON_DAMAGE = 2;
constexpr int SPELL_EFFECT_WEAPON_DAMAGE_NOSCHOOL = 3, SPELL_EFFECT_NORMALIZED_WEAPON_DMG = 4;
constexpr int SPELL_EFFECT_WEAPON_PERCENT_DAMAGE = 5, SPELL_EFFECT_APPLY_AURA = 6, SPELL_EFFECT_HEAL = 7;
constexpr int SPELL_AURA_PERIODIC_DAMAGE = 1, SPELL_AURA_PERIODIC_LEECH = 2;
constexpr int SPELL_AURA_PERIODIC_DAMAGE_PERCENT = 3, SPELL_AURA_PERIODIC_HEAL = 4;
constexpr int TARGET_UNIT_CASTER = 1, TARGET_UNIT_SRC_AREA_ENEMY = 2;
constexpr int SPELL_ATTR5_EXTRA_INITIAL_PERIOD = 512;
struct Player;
struct Creature;
struct Aura
{
    int duration = 0;
    uint8 stacks = 1;
    bool owned = true;
    int GetDuration() const { return duration; }
    uint8 GetStackAmount() const { return stacks; }
};
struct Unit
{
    virtual ~Unit() = default;
    float health = 1000, maximum = 1000, distance = 0;
    bool alive = true, world = true, hostile = true, controlled = false;
    uint32 phase = 1;
    void* map = reinterpret_cast<void*>(1);
    Unit* victim = nullptr;
    std::map<std::string, Aura> auras;
    bool IsAlive() const { return alive; }
    bool IsInWorld() const { return world; }
    void* GetMap() const { return map; }
    uint32 GetPhaseMask() const { return phase; }
    uint32 GetHealth() const { return uint32(health); }
    uint32 GetMaxHealth() const { return uint32(maximum); }
    float GetHealthPct() const { return health * 100 / maximum; }
    Unit* GetVictim() const { return victim; }
    virtual Creature* ToCreature() { return nullptr; }
    virtual Player* ToPlayer() { return nullptr; }
    float GetDistance(Unit* target) const { return target->distance; }
    float GetCombatReach() const { return 1; }
    bool HasBreakableByDamageCrowdControlAura() const { return controlled; }
};
struct Creature : Unit
{
    bool boss = false;
    bool IsDungeonBoss() const { return boss; }
    Creature* ToCreature() override { return this; }
};
struct Cooldown { uint32 category = 0, delay = 0; };
struct Player : Unit
{
    uint8 cls = 1, spec = 0, combo = 0;
    bool tank = true, healer = false;
    Unit* comboTarget = nullptr;
    std::map<int, uint32> power{{0, 100}, {1, 100}, {3, 100}};
    std::map<uint32, Cooldown> cooldowns;
    uint8 getClass() const { return cls; }
    int getPowerType() const { return cls == 4 ? POWER_ENERGY : POWER_MANA; }
    uint32 GetMaxPower(int) const { return 100; }
    float GetPowerPct(int type) { return float(power[type]); }
    uint32 GetPower(int type) { return power[type]; }
    bool IsHostileTo(Unit* target) const { return target != this && target->hostile; }
    uint8 GetComboPoints(Unit* target) const { return target == comboTarget ? combo : 0; }
    bool HasSpell(uint32 id) const { return id != 0; }
    bool HasSpellCooldown(uint32 id) const { return GetSpellCooldownDelay(id) > 0; }
    uint32 GetSpellCooldownDelay(uint32 id) const
    {
        auto found = cooldowns.find(id);
        return found == cooldowns.end() ? 0 : found->second.delay;
    }
    auto const& GetSpellCooldownMap() const { return cooldowns; }
    Player* ToPlayer() override { return this; }
};
template<class T> struct Value { T data{}; T Get() const { return data; } };
struct Context
{
    std::map<std::string, std::any> values;
    template<class T> Value<T>* GetValue(std::string name, std::string qualifier = "")
    {
        auto [found, inserted] = values.try_emplace(name + qualifier, Value<T>{});
        return &std::any_cast<Value<T>&>(found->second);
    }
};
struct PlayerbotAI
{
    Player bot;
    Context context;
    std::map<uint32, Unit*> units;
    bool refined = true;
    int state = BOT_STATE_COMBAT;
    Player* GetBot() { return &bot; }
    int GetState() const { return state; }
    bool IsTank(Player* player) const { return player->tank; }
    bool IsHeal(Player* player) const { return player->healer; }
    Context* GetAiObjectContext() { return &context; }
    Unit* GetUnit(uint32 guid) { return units[guid]; }
    bool HasSpell(std::string name) { return context.GetValue<uint32>("spell id", name)->Get() != 0; }
    Aura* GetAura(std::string name, Unit* unit, bool owner = false, bool duration = false)
    {
        auto found = unit->auras.find(name);
        return found == unit->auras.end() || (owner && !found->second.owned) ||
            (duration && found->second.duration == -1) ? nullptr : &found->second;
    }
};
bool RefinedCompanionCombat(PlayerbotAI* ai) { return ai && ai->refined; }
uint32 incoming = 0;
uint32 CompanionIncomingHealing(PlayerbotAI*, Unit*) { return incoming; }
struct AiFactory { static uint8 GetPlayerSpecTab(Player* bot) { return bot->spec; } };
struct Target
{
    bool area = false;
    int target = 0;
    bool IsArea() const { return area; }
    int GetTarget() const { return target; }
};
struct SpellEffectInfo
{
    int Effect = 0, ApplyAuraName = 0;
    uint32 Amplitude = 0, ChainTarget = 0;
    Target TargetA, TargetB;
    float radius = 0;
    float CalcRadius(Player*) const { return radius; }
};
struct SpellInfo
{
    uint32 Id = 1, castTime = 0, duration = 0, cooldown = 0, category = 0, attributes = 0;
    float Speed = 0;
    int PowerType = POWER_MANA, cost = 0;
    std::vector<SpellEffectInfo> effects;
    uint32 CalcCastTime(Player*) const { return castTime; }
    int GetDuration() const { return int(duration); }
    uint32 GetRecoveryTime() const { return cooldown; }
    uint32 GetCategory() const { return category; }
    int CalcPowerCost(Player*, int) const { return cost; }
    int GetSchoolMask() const { return 1; }
    bool HasAttribute(int attribute) const { return (attributes & uint32(attribute)) != 0; }
    auto const& GetEffects() const { return effects; }
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
namespace ai::spell
{
    bool HasSpellOrCategoryCooldown(Player* bot, uint32 id) { return bot->HasSpellCooldown(id); }
}
struct Action
{
    virtual ~Action() = default;
    Unit* target = nullptr;
    virtual Unit* GetTarget() { return target; }
};
struct CastSpellAction : Action
{
    std::string name;
    std::string getSpell() const { return name; }
};
enum class HealingManaEfficiency { LOW, MEDIUM, HIGH };
struct CastHealingSpellAction : CastSpellAction
{
    HealingManaEfficiency manaEfficiency = HealingManaEfficiency::LOW;
    uint8 estAmount = 50;
};
struct NextAction
{
    std::string name;
    float relevance;
    NextAction(std::string n, float r) : name(n), relevance(r) { }
};
struct Config { uint8 saveManaThreshold = 20, lowHealth = 40, mediumHealth = 70; } sPlayerbotAIConfig;
struct HealerAutoSaveManaMultiplier
{
    PlayerbotAI* botAI;
    Player* bot;
    float GetValue(Action* action);
};
#include "adapter.inc"
#include "mana.inc"

int main()
{
    PlayerbotAI ai;
    Creature enemy;
    enemy.distance = 30;
    ai.context.GetValue<Unit*>("current target")->data = &enemy;
    ai.context.GetValue<float>("estimated group dps")->data = 500;
    ai.context.GetValue<GuidVector>("attackers")->data = {1};
    ai.units[1] = &enemy;
    ai.bot.auras["sword and board"] = {-1, 1};
    auto situation = BuildCompanionRotationSituation(&ai);
    assert(!situation.procs); // Passive talent names do not masquerade as temporary procs.
    ai.bot.auras["sword and board"].duration = 5000;
    situation = BuildCompanionRotationSituation(&ai);
    assert(situation.procs & CompanionRotation::SwordBoard);
    ai.context.GetValue<uint32>("spell id", "shield slam")->data = 1;
    SpellInfo shield;
    shield.Id = 1;
    shield.category = 9;
    shield.effects.push_back({SPELL_EFFECT_SCHOOL_DAMAGE});
    spellMgr.spells[1] = shield;
    CastSpellAction cast;
    cast.target = &enemy;
    cast.name = "shield slam";
    assert(CompanionRotationPriority(&ai, &cast, 5, situation) == 38);
    ai.bot.cooldowns[2] = {9, 2000};
    assert(CompanionRotationPriority(&ai, &cast, 5, situation) == 5); // Shared category recharge is included.
    ai.bot.cooldowns.clear();
    spellMgr.spells[1].Speed = 10;
    assert(CompanionRotationPriority(&ai, &cast, 5, situation) == 0); // 3 s flight cannot hit a 2 s target.
    spellMgr.spells[1].Speed = 0;
    spellMgr.spells[1].effects = {{SPELL_EFFECT_APPLY_AURA, SPELL_AURA_PERIODIC_DAMAGE, 10000}};
    spellMgr.spells[1].duration = 4000;
    ai.context.GetValue<float>("estimated group dps")->data = 100;
    enemy.health = 700;
    assert(CompanionRotationPriority(&ai, &cast, 5, situation) == 0);
    spellMgr.spells[1].attributes = SPELL_ATTR5_EXTRA_INITIAL_PERIOD;
    assert(CompanionRotationPriority(&ai, &cast, 5, situation) == 38); // Native first-tick flag changes impact timing.
    ai.bot.cls = CLASS_ROGUE;
    ai.bot.spec = 2;
    ai.bot.tank = false;
    ai.bot.comboTarget = &enemy;
    ai.bot.combo = 2;
    ai.context.GetValue<uint32>("spell id", "slice and dice")->data = 3;
    spellMgr.spells[3] = {};
    cast.name = "slice and dice";
    ai.bot.auras[cast.name] = {10000, 1};
    situation = BuildCompanionRotationSituation(&ai);
    assert(CompanionRotationPriority(&ai, &cast, 5, situation) == 0); // Buff is on the rogue, not its enemy target.
    ai.bot.auras.erase(cast.name);
    assert(CompanionRotationPriority(&ai, &cast, 5, situation) == 34);
    auto defaults = CompanionRotationDefaults(&ai);
    assert(defaults.front().name == "hemorrhage");
    Creature other;
    ai.context.GetValue<Unit*>("current target")->data = &other;
    assert(BuildCompanionRotationSituation(&ai).combo == 0); // Combo points cannot transfer to a new enemy.
    other.alive = false;
    cast.target = &other;
    cast.name = "rebirth";
    assert(CompanionRotationPriority(&ai, &cast, 70, situation) == 70);
    ai.bot.cls = CLASS_MAGE;
    ai.bot.auras["fireball!"] = {8000, 1};
    assert(BuildCompanionRotationSituation(&ai).procs & CompanionRotation::BrainFreeze);
    CastHealingSpellAction heal;
    Player injured;
    injured.health = 250;
    injured.tank = false;
    heal.target = &injured;
    ai.bot.power[POWER_MANA] = 5;
    HealerAutoSaveManaMultiplier mana{&ai, &ai.bot};
    ai.refined = false;
    assert(mana.GetValue(&heal) == 0);
    ai.refined = true;
    assert(mana.GetValue(&heal) == 1); // Mana conservation does not suppress urgent healing for managed companions.
    ai.state = 0;
    assert(!BuildCompanionRotationSituation(&ai).enabled && CompanionRotationDefaults(&ai).empty());
}
