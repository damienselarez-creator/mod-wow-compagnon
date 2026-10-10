#include <array>
#include <cassert>
#include <cmath>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

using uint32 = unsigned;
constexpr uint32 SKILL_RIDING = 762, SPELL_EFFECT_LEARN_SPELL = 36;
struct Player
{
    uint32 money = 100, guid = 1;
    std::set<uint32> skills, spells;
    bool HasSkill(uint32 id) { return skills.count(id); }
    bool HasSpell(uint32 id) { return spells.count(id); }
    uint32 GetMoney() { return money; }
    uint32 GetGUID() { return guid; }
    std::string GetName() { return "AnyCompanion"; }
};
struct CompanionErrandState { std::set<uint32> rejectedLessons; uint32 reserve = 20; };
struct PlayerbotAI
{
    Player bot;
    std::shared_ptr<CompanionErrandState> companionErrands;
    bool managed = true, allowed = true;
    Player* GetBot() { return &bot; }
};
struct Vocation { std::array<uint32, 2> professions{164, 186}; } vocation;
struct CompanionVocation { static Vocation Get(Player*) { return vocation; } };
struct Config { std::map<std::string, std::array<uint32, 2>> companionProfessionPlans; } sPlayerbotAIConfig;
std::string focus = "normal";
std::string GetCompanionNarrativeFocus(uint32) { return focus; }
bool CanVisitTrainer(PlayerbotAI* ai) { return ai->allowed; }
bool IsManagedCompanion(PlayerbotAI* ai) { return ai->managed; }
bool IsSecondaryProfession(uint32 skill) { return skill == 129 || skill == 185 || skill == 356; }
bool IsProfession(uint32 skill) { return skill && skill != SKILL_RIDING; }
std::map<uint32, uint32> spellSkills;
uint32 ProfessionForSpell(uint32 id) { return spellSkills[id]; }
bool TeachesRiding(uint32 id) { return spellSkills[id] == SKILL_RIDING; }
namespace CompanionErrands
{
    bool CanSpend(uint32 money, uint32 cost, uint32 reserve)
    {
        return cost <= money && money - cost >= reserve;
    }
}
namespace Trainer
{
    enum class Type { Class, Tradeskill, Mount, Other };
    struct Spell { uint32 SpellId, MoneyCost, ReqSkillLine; };
    struct Trainer
    {
        Type type = Type::Class;
        bool valid = true;
        std::vector<Spell> spells;
        std::set<uint32> unavailable;
        bool IsTrainerValidForPlayer(Player*) { return valid; }
        Type GetTrainerType() { return type; }
        auto const& GetSpells() { return spells; }
        Spell const* GetSpell(uint32 id)
        {
            for (auto const& spell : spells)
                if (spell.SpellId == id)
                    return &spell;
            return nullptr;
        }
        bool CanTeachSpell(Player* bot, Spell const* spell)
        {
            return !bot->HasSpell(spell->SpellId) && !unavailable.count(spell->SpellId);
        }
    };
}
struct Effect
{
    uint32 type, TriggerSpell;
    bool IsEffect(uint32 value) const { return type == value; }
};
struct SpellInfo
{
    std::vector<Effect> effects;
    auto const& GetEffects() const { return effects; }
};
struct SpellManager
{
    std::map<uint32, SpellInfo> spells;
    SpellInfo const* GetSpellInfo(uint32 id)
    {
        auto found = spells.find(id);
        return found == spells.end() ? nullptr : &found->second;
    }
} manager;
auto* sSpellMgr = &manager;
#include "verification.inc"
namespace
{
#include "lessons.inc"

int main()
{
    PlayerbotAI ai;
    Trainer::Trainer trainer;
    trainer.spells = {{12, 30, 0}, {10, 5, 0}, {11, 5, 0}};
    assert(ChooseLesson(&ai, &trainer, 1)->SpellId == 10); // No errand state yet: spontaneous visit.
    ai.bot.spells.insert(10);
    assert(ChooseLesson(&ai, &trainer, 1)->SpellId == 11);
    trainer.unavailable.insert(11);
    assert(ChooseLesson(&ai, &trainer, 1)->SpellId == 12);
    ai.bot.money = 25;
    assert(!ChooseLesson(&ai, &trainer, 1));
    assert(ChooseLesson(&ai, &trainer, 1, false)->SpellId == 12);
    assert(ChooseLesson(&ai, &trainer, 0.5f)->SpellId == 12);
    ai.bot.money = 100;
    ai.companionErrands = std::make_shared<CompanionErrandState>();
    ai.companionErrands->rejectedLessons.insert(12);
    assert(!ChooseLesson(&ai, &trainer, 1)); // Failed lessons are not charged in a loop.
    ai.companionErrands.reset();
    trainer.type = Trainer::Type::Tradeskill;
    trainer.spells = {{20, 5, 164}, {21, 1, 171}, {22, 6, 129}, {23, 7, 186}};
    assert(ChooseLesson(&ai, &trainer, 1)->SpellId == 20); // Agreed profession, not cheaper alchemy.
    ai.bot.spells.insert(20);
    assert(ChooseLesson(&ai, &trainer, 1)->SpellId == 22); // Secondary profession.
    ai.managed = false;
    vocation.professions = {};
    assert(!ChooseLesson(&ai, &trainer, 1)); // No unsolicited primary profession.
    ai.bot.skills.insert(186);
    assert(ChooseLesson(&ai, &trainer, 1)->SpellId == 23); // Existing profession needs no saved plan.
    focus = "training_class";
    assert(!ChooseLesson(&ai, &trainer, 1));
    focus = "training_professions";
    trainer.type = Trainer::Type::Class;
    assert(!ChooseLesson(&ai, &trainer, 1));
    focus = "normal";
    trainer.type = Trainer::Type::Mount;
    trainer.spells = {{30, 10, SKILL_RIDING}};
    assert(ChooseLesson(&ai, &trainer, 1)->SpellId == 30);
    trainer.valid = false;
    assert(!ChooseLesson(&ai, &trainer, 1));
    assert(!ChooseLesson(&ai, nullptr, 1));
    assert(!HasLearnedLesson(&ai.bot, 100));
    manager.spells[100].effects = {{SPELL_EFFECT_LEARN_SPELL, 101}, {SPELL_EFFECT_LEARN_SPELL, 102}};
    ai.bot.spells.insert(101);
    assert(!HasLearnedLesson(&ai.bot, 100));
    ai.bot.spells.insert(102);
    assert(HasLearnedLesson(&ai.bot, 100)); // All effects must actually be learned.
}
