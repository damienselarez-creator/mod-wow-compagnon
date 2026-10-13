#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "CompanionCombatPolicy.h"
#include "CompanionActivityPolicy.h"

using uint8 = uint8_t;
using uint32 = uint32_t;
using uint64 = uint64_t;
using int32 = int32_t;
using ObjectGuid = uint32;
using GuidVector = std::vector<ObjectGuid>;
enum Effect
{
    SPELL_EFFECT_NONE, SPELL_EFFECT_INTERRUPT_CAST, SPELL_EFFECT_APPLY_AURA, SPELL_EFFECT_SCHOOL_DAMAGE,
    SPELL_EFFECT_WEAPON_DAMAGE, SPELL_EFFECT_WEAPON_DAMAGE_NOSCHOOL, SPELL_EFFECT_NORMALIZED_WEAPON_DMG,
    SPELL_EFFECT_WEAPON_PERCENT_DAMAGE, SPELL_EFFECT_HEALTH_LEECH, SPELL_EFFECT_HEAL, SPELL_EFFECT_ATTACK_ME
};
enum Aura
{
    SPELL_AURA_NONE, SPELL_AURA_MOD_SILENCE, SPELL_AURA_PERIODIC_DAMAGE, SPELL_AURA_PERIODIC_DAMAGE_PERCENT,
    SPELL_AURA_PERIODIC_LEECH, SPELL_AURA_MOD_TAUNT
};
enum CastType { CURRENT_GENERIC_SPELL, CURRENT_CHANNELED_SPELL, CURRENT_AUTOREPEAT_SPELL };
enum { EFFECT_0, EFFECT_1, EFFECT_2 };
enum { SPELL_STATE_FINISHED, SPELL_STATE_CASTING, SPELL_STATE_PREPARING };
enum { POWER_MANA, POWER_RAGE };
enum { SPELL_PREVENTION_TYPE_NONE, SPELL_PREVENTION_TYPE_SILENCE };
enum { SPELL_INTERRUPT_FLAG_INTERRUPT = 1, CHANNEL_INTERRUPT_FLAG_INTERRUPT = 1 };
enum { TARGET_UNIT_TARGET_ENEMY, TARGET_UNIT_SRC_AREA_ENEMY, TARGET_UNIT_CASTER };
enum { HEAL };
struct Unit;
struct Player;
struct Pet;
struct Item
{
    uint32 entry, count;
    uint32 GetEntry() { return entry; }
    uint32 GetCount() { return count; }
};
enum
{
    SKILL_BLACKSMITHING, SKILL_LEATHERWORKING, SKILL_ALCHEMY, SKILL_HERBALISM, SKILL_MINING,
    SKILL_TAILORING, SKILL_ENGINEERING, SKILL_ENCHANTING, SKILL_SKINNING, SKILL_JEWELCRAFTING,
    SKILL_INSCRIPTION, SKILL_FIRST_AID, SKILL_COOKING, SKILL_FISHING
};
struct Spell;
struct Group;
struct PlayerbotAI;
struct Targets
{
    ObjectGuid unit = 0;
    ObjectGuid GetUnitTargetGUID() { return unit; }
};
struct TargetInfo
{
    bool area = false;
    int type = TARGET_UNIT_TARGET_ENEMY;
    bool IsArea() const { return area; }
    int GetTarget() const { return type; }
};
struct SpellEffectInfo
{
    int Effect = SPELL_EFFECT_NONE;
    int ApplyAuraName = SPELL_AURA_NONE;
    TargetInfo TargetA, TargetB;
    uint32 ChainTarget = 0;
    float radius = 0;
    float CalcRadius(Player*) const { return radius; }
};
struct SpellInfo
{
    std::array<SpellEffectInfo, 3> Effects{};
    int PreventionType = SPELL_PREVENTION_TYPE_SILENCE;
    int InterruptFlags = 1, ChannelInterruptFlags = 1;
    auto const& GetEffects() const { return Effects; }
    bool HasEffect(int effect) const
    {
        for (auto const& data : Effects)
            if (data.Effect == effect)
                return true;
        return false;
    }
    bool HasAura(int aura) const
    {
        for (auto const& data : Effects)
            if (data.ApplyAuraName == aura)
                return true;
        return false;
    }
};
struct Spell
{
    SpellInfo const* m_spellInfo;
    Targets m_targets;
    int state = SPELL_STATE_PREPARING, duration = 1500, remaining = 500, amount = 1000;
    int getState() { return state; }
    int GetCastTime() { return duration; }
    int GetCastTimeRemaining() { return remaining; }
    int CalculateSpellDamage(uint8, Unit*) { return amount; }
};
struct Unit
{
    ObjectGuid guid = 0;
    bool alive = true, world = true, los = true, friendly = false, cc = false, immune = false;
    float health = 100, maxHealth = 1000, x = 0;
    int map = 1, phase = 1;
    Unit* victim = nullptr;
    std::array<Spell*, 3> casts{};
    bool withinRange = true, casting = false;
    bool IsWithinCombatRange(Unit*, float) { return withinRange; }
    bool IsNonMeleeSpellCast(bool) { return casting; }
    int stops = 0;
    virtual Player* ToPlayer() { return nullptr; }
    bool IsAlive() { return alive; }
    bool IsInWorld() { return world; }
    int GetMap() { return map; }
    int GetPhaseMask() { return phase; }
    bool IsWithinLOSInMap(Unit*) { return los; }
    bool IsFriendlyTo(Unit* unit) { return unit->friendly; }
    ObjectGuid GetGUID() { return guid; }
    float GetHealthPct() { return health; }
    uint32 GetMaxHealth() { return uint32(maxHealth); }
    float GetDistance(Unit* unit) { return std::abs(x - unit->x); }
    float GetDistance2d(Unit* unit) { return GetDistance(unit); }
    float GetCombatReach() { return 1; }
    Unit* GetVictim() { return victim; }
    bool HasBreakableByDamageCrowdControlAura() { return cc; }
    bool IsImmunedToSpellEffect(SpellInfo const*, uint8) { return immune; }
    float healingTaken = 1;
    uint32 SpellHealingBonusTaken(Player*, SpellInfo const*, uint32 amount, int) { return uint32(amount * healingTaken); }
    Spell* GetCurrentSpell(CastType type) { return casts[type]; }
    void InterruptSpell(CastType type) { casts[type] = nullptr; }
    void AttackStop() { ++stops; victim = nullptr; }
};
struct GroupReference
{
    Player* player = nullptr;
    GroupReference* following = nullptr;
    Player* GetSource() { return player; }
    GroupReference* next() { return following; }
};
struct Group
{
    GroupReference* first = nullptr;
    bool raid = false;
    GroupReference* GetFirstMember() { return first; }
    bool isRaidGroup() { return raid; }
};
struct Pet : Unit { };
struct Player : Unit
{
    Group* group = nullptr;
    Pet* pet = nullptr;
    bool tank = false, healer = false, combat = false;
    float mana = 100;
    int power = POWER_MANA;
    uint32 level = 10;
    std::map<uint32, uint32> skills, spells;
    uint32 GetLevel() { return level; }
    uint32 GetSkillValue(uint32 skill) { return skills[skill]; }
    auto const& GetSpellMap() { return spells; }
    Player* ToPlayer() override { return this; }
    Group* GetGroup() { return group; }
    bool IsInSameGroupWith(Player* player) { return group && group == player->group; }
    Pet* GetPet() { return pet; }
    int getPowerType() { return power; }
    float GetPowerPct(int) { return mana; }
    bool IsInCombat() { return combat; }
    uint32 SpellHealingBonusDone(Unit*, SpellInfo const*, uint32 amount, int, uint8) { return amount; }
};
struct Config { bool companionCombatRefinement = true; float lowHealth = 35, healDistance = 40, meleeDistance = 5; };
Config sPlayerbotAIConfig;
struct Value { GuidVector units; GuidVector Get() { return units; } };
struct Context
{
    Value value;
    template<class T> Value* GetValue(std::string const&) { return &value; }
};
struct PlayerbotAI
{
    Player* bot;
    bool managed = true;
    bool stay = false;
    Context context;
    std::map<ObjectGuid, Unit*> units;
    std::vector<Item*> items;
    auto const& GetInventoryItems() { return items; }
    Player* GetBot() { return bot; }
    bool IsTank(Player* player) { return player->tank; }
    bool IsHeal(Player* player) { return player->healer; }
    Context* GetAiObjectContext() { return &context; }
    Unit* GetUnit(ObjectGuid guid) { return units[guid]; }
    bool HasStrategy(std::string const&, int) { return stay; }
    int GetState() { return 1; }
};
bool IsCompanionInventoryManaged(PlayerbotAI* ai) { return ai->managed; }
#include "combat.inc"
struct Event { };
#define AI_VALUE(type, name) target
struct ReachTargetAction
{
    PlayerbotAI* botAI;
    Player* bot;
    Unit* target;
    float distance = 30;
    int losMoves = 0, rangeMoves = 0;
    bool Execute(Event);
    bool isUseful();
    Unit* GetTarget() { return target; }
    std::string GetTargetName() { return "current target"; }
    bool MoveToLOS(Unit*, bool) { ++losMoves; return true; }
    bool ReachCombatTo(Unit*, float) { ++rangeMoves; return true; }
};
constexpr float CONTACT_DISTANCE = 0.5f;
struct OutOfRangeTrigger
{
    PlayerbotAI* botAI;
    Player* bot;
    Unit* target;
    float distance = 30;
    std::string GetTargetName() { return "current target"; }
    bool IsActive();
};
#include "reach.inc"
#include "range.inc"
#include "signature.inc"
struct OpportunityState
{
    uint64 inventorySignature = 0, progressionSignature = 0;
    uint32 observedLevel = 0;
    bool mountDue = false, townDue = false, craftDue = false, herbDue = false, bagInventoryDue = false;
    std::set<uint32> rejectedTrainers, rejectedLessons, rejectedCrafts, rejectedSupplyVendors;
};
void RefreshOpportunities(PlayerbotAI* botAI, OpportunityState& state)
{
    Player* bot = botAI->GetBot();
    #include "opportunities.inc"
}

int main()
{
    Player bot, human, healer;
    bot.guid = 1; human.guid = 2; healer.guid = 3;
    bot.friendly = human.friendly = healer.friendly = true;
    GroupReference last{&healer}, middle{&human, &last}, first{&bot, &middle};
    Group group{&first};
    bot.group = human.group = healer.group = &group;
    PlayerbotAI ai{&bot};
    Unit enemy; enemy.guid = 4; enemy.cc = true;
    ai.units[4] = &enemy;
    SpellInfo damage; damage.Effects[0].Effect = SPELL_EFFECT_SCHOOL_DAMAGE;
    assert(!CompanionCombatSpellAllowed(&ai, &enemy, &damage));
    ai.managed = false;
    assert(CompanionCombatSpellAllowed(&ai, &enemy, &damage));
    ai.managed = true;
    sPlayerbotAIConfig.companionCombatRefinement = false;
    assert(CompanionCombatSpellAllowed(&ai, &enemy, &damage));
    sPlayerbotAIConfig.companionCombatRefinement = true;
    SpellInfo area = damage;
    area.Effects[0].TargetA = {true, TARGET_UNIT_SRC_AREA_ENEMY};
    area.Effects[0].radius = 10;
    ai.context.value.units = {4}; enemy.x = 8;
    assert(!CompanionCombatSpellAllowed(&ai, &bot, &area));
    enemy.x = 20;
    assert(CompanionCombatSpellAllowed(&ai, &bot, &area));
    enemy.x = 0; enemy.phase = 2;
    assert(CompanionCombatSpellAllowed(&ai, &bot, &area));
    enemy.phase = 1;
    bot.victim = &enemy;
    Spell automaticShot{&damage, {enemy.guid}};
    bot.casts[CURRENT_AUTOREPEAT_SPELL] = &automaticShot;
    Pet pet; pet.victim = &enemy; bot.pet = &pet;
    UpdateCompanionCombat(&ai);
    assert(bot.stops == 1 && pet.stops == 1 && !bot.victim && !pet.victim);
    assert(!bot.casts[CURRENT_AUTOREPEAT_SPELL]);
    enemy.cc = false;

    SpellInfo heal; heal.Effects[0].Effect = SPELL_EFFECT_HEAL;
    Spell arriving{&heal, {human.guid}};
    healer.casts[CURRENT_GENERIC_SPELL] = &arriving;
    human.health = 60;
    assert(CompanionIncomingHealing(&ai, &human) == 800);
    assert(!CompanionCombatSpellAllowed(&ai, &human, &heal));
    SpellInfo groupHeal = heal;
    groupHeal.Effects[0].TargetA.area = true;
    assert(CompanionCombatSpellAllowed(&ai, &human, &groupHeal));
    SpellInfo chainHeal = heal;
    chainHeal.Effects[0].ChainTarget = 3;
    assert(CompanionCombatSpellAllowed(&ai, &human, &chainHeal));
    SpellInfo dualHeal = heal;
    dualHeal.Effects[1].Effect = SPELL_EFFECT_HEAL;
    dualHeal.Effects[1].TargetA.type = TARGET_UNIT_CASTER;
    assert(CompanionCombatSpellAllowed(&ai, &human, &dualHeal));
    human.healingTaken = 0.25f;
    assert(CompanionIncomingHealing(&ai, &human) == 200);
    assert(CompanionCombatSpellAllowed(&ai, &human, &heal));
    human.healingTaken = 1;
    human.health = 20;
    assert(CompanionCombatSpellAllowed(&ai, &human, &heal));
    assert(CompanionHealingScore(&ai, &human) < 35);
    arriving.state = SPELL_STATE_FINISHED;
    human.health = 60;
    assert(CompanionIncomingHealing(&ai, &human) == 0);
    assert(CompanionCombatSpellAllowed(&ai, &human, &heal));
    arriving.state = SPELL_STATE_PREPARING; arriving.remaining = 3000;
    assert(CompanionIncomingHealing(&ai, &human) == 0);
    arriving.remaining = 500; healer.phase = 2;
    assert(CompanionIncomingHealing(&ai, &human) == 0);
    healer.phase = 1;
    healer.casts[0] = nullptr;
    human.health = 100;
    assert(CompanionHealingScore(&ai, &human) >= 100);

    SpellInfo interrupt; interrupt.Effects[0].Effect = SPELL_EFFECT_INTERRUPT_CAST;
    Spell hostileCast{&damage}, plannedInterrupt{&interrupt, {enemy.guid}};
    enemy.casts[0] = &hostileCast;
    assert(CompanionCombatSpellAllowed(&ai, &enemy, &interrupt));
    healer.casts[0] = &plannedInterrupt;
    assert(!CompanionCombatSpellAllowed(&ai, &enemy, &interrupt));
    hostileCast.remaining = 200;
    assert(CompanionCombatSpellAllowed(&ai, &enemy, &interrupt));
    hostileCast.remaining = 500;
    plannedInterrupt.state = SPELL_STATE_FINISHED;
    assert(CompanionCombatSpellAllowed(&ai, &enemy, &interrupt));
    hostileCast.state = SPELL_STATE_FINISHED;
    assert(!CompanionCombatSpellAllowed(&ai, &enemy, &interrupt));
    hostileCast.state = SPELL_STATE_PREPARING;
    damage.InterruptFlags = 0;
    assert(!CompanionCombatSpellAllowed(&ai, &enemy, &interrupt));
    damage.InterruptFlags = 1;
    enemy.immune = true;
    assert(!CompanionCombatSpellAllowed(&ai, &enemy, &interrupt));
    enemy.immune = false;

    SpellInfo taunt; taunt.Effects[0].Effect = SPELL_EFFECT_ATTACK_ME;
    enemy.victim = &human; human.tank = true;
    assert(!CompanionCombatSpellAllowed(&ai, &enemy, &taunt));
    assert(CompanionRescueUrgency(&ai, &enemy) == 0);
    human.tank = false; human.health = 20;
    float dpsUrgency = CompanionRescueUrgency(&ai, &enemy);
    human.healer = true;
    assert(CompanionRescueUrgency(&ai, &enemy) > dpsUrgency);
    assert(CompanionCombatSpellAllowed(&ai, &enemy, &taunt));
    human.group = nullptr;
    assert(CompanionRescueUrgency(&ai, &enemy) == 0);
    human.group = &group;
    bot.healer = true;
    assert(!CompanionCombatSpellAllowed(&ai, &enemy, &damage));
    SpellInfo healingNova = area;
    healingNova.Effects[1] = groupHeal.Effects[0];
    assert(CompanionCombatSpellAllowed(&ai, &bot, &healingNova));
    bot.mana = 0;
    assert(CompanionCombatSpellAllowed(&ai, &enemy, &damage));

    using Domain = CompanionGoals::Domain;
    ReachTargetAction reach{&ai, &bot, &enemy};
    OutOfRangeTrigger range{&ai, &bot, &enemy};
    bot.healer = false;
    bot.los = true;
    assert(!reach.isUseful() && !range.IsActive());
    bot.los = false;
    assert(reach.isUseful() && range.IsActive());
    assert(reach.Execute({}) && reach.losMoves == 1 && reach.rangeMoves == 0);
    bot.casting = true;
    assert(!reach.isUseful() && !reach.Execute({}));
    bot.casting = false;
    ai.stay = true;
    assert(!reach.isUseful());
    ai.stay = false;
    enemy.cc = true;
    assert(!reach.isUseful());
    enemy.cc = false;
    bot.los = true;
    bot.withinRange = false;
    assert(reach.isUseful() && range.IsActive());
    assert(reach.Execute({}) && reach.rangeMoves == 1);
    enemy.phase = 2;
    assert(!reach.isUseful());
    enemy.phase = 1;
    ai.managed = false;
    bot.withinRange = true;
    bot.los = false;
    assert(!reach.isUseful() && !range.IsActive());
    ai.managed = true;

    OpportunityState opportunities;
    opportunities.rejectedSupplyVendors.insert(7);
    RefreshOpportunities(&ai, opportunities);
    assert(opportunities.mountDue && opportunities.townDue && opportunities.craftDue);
    opportunities.mountDue = opportunities.townDue = opportunities.craftDue = opportunities.herbDue = false;
    opportunities.rejectedTrainers.insert(8);
    opportunities.rejectedCrafts.insert(9);
    RefreshOpportunities(&ai, opportunities);
    assert(!opportunities.townDue && !opportunities.craftDue && opportunities.rejectedCrafts.count(9));
    Item ore{100, 5};
    ai.items = {&ore};
    RefreshOpportunities(&ai, opportunities);
    assert(opportunities.craftDue && opportunities.townDue && !opportunities.rejectedCrafts.count(9));
    assert(opportunities.rejectedTrainers.count(8) && opportunities.rejectedSupplyVendors.count(7));
    opportunities.townDue = opportunities.craftDue = false;
    bot.skills[SKILL_BLACKSMITHING] = 75;
    RefreshOpportunities(&ai, opportunities);
    assert(opportunities.townDue && opportunities.craftDue && !opportunities.rejectedTrainers.count(8));
    opportunities.rejectedLessons.insert(8);
    bot.level = 20;
    RefreshOpportunities(&ai, opportunities);
    assert(opportunities.mountDue && !opportunities.rejectedLessons.count(8));
    assert(opportunities.rejectedSupplyVendors.count(7));

    using CompanionActivity::Score;
    auto training = Score(Domain::ClassTraining, 0, 0, 20, false, false, false);
    assert(Score(Domain::Maintenance, -10000, 0, 500, true, false, false) < training);
    assert(Score(Domain::Bags, 14000, 0, 100, false, true, false) < training);
    assert(Score(Domain::Crafting, 18000, 40000, 0, false, false, false) < training);
    assert(Score(Domain::Guild, 16000, 40000, 10, false, false, false) < training);
    assert(Score(Domain::Crafting, 18000, 40000, 0, false, false, false) <
        Score(Domain::Guild, 16000, 0, 10, false, false, false));
    assert(Score(Domain::Mount, 12000, 0, 50, false, false, true) < training);
    assert(!CompanionActivity::ReturnToAdventure(false, 200, false, false));
    assert(CompanionActivity::ReturnToAdventure(true, 70, false, false));
    assert(!CompanionActivity::ReturnToAdventure(true, 70, true, false));
    assert(CompanionActivity::ReturnToAdventure(true, 130, true, false));
    assert(CompanionActivity::ReturnToAdventure(true, 0, true, true));
}
