/*
 * WoW Compagnon, GNU GPL v2 or later. See the preserved component author notices.
 */
#include "CompanionProfessions.h"
#include "CompanionErrands.h"
#include "CompanionErrandPolicy.h"
#include "CompanionGoals.h"
#include "CastCustomSpellAction.h"
#include "ItemUsageValue.h"
#include "Playerbots.h"
#include "PlayerbotSpellRepository.h"
#include "ProfessionPlan.h"

void RestoreCompanionProfessionDefaults(PlayerbotAI* ai)
{
    if (!IsCompanionInventoryManaged(ai) ||
        !CompanionGoalPermitted(ai, CompanionGoals::Domain::ProfessionTraining))
        return;
    Player* bot = ai->GetBot();
    // Replay native automatic skill rewards only. Paid trainer lessons and ranks stay with the trainer.
    for (uint32 skill : {164u, 165u, 171u, 182u, 186u, 197u, 202u, 333u, 393u, 755u, 773u, 129u, 185u, 356u})
        if (bot->HasSkill(skill))
            bot->learnSkillRewardedSpells(skill, bot->GetPureSkillValue(skill));
}

CompanionProfessionPlan PlanCompanionProfession(PlayerbotAI* ai, std::set<uint32_t> const& rejected)
{
    CompanionProfessionPlan result;
    if (!IsCompanionInventoryManaged(ai) || !CompanionGoalAllowed(ai, CompanionGoals::Domain::Crafting))
        return result;
    Player* bot = ai->GetBot();
    CraftRandomItemAction crafting(ai);
    std::vector<SelfbotCraft::Recipe> recipes;
    std::map<uint32, uint32> inventory;
    std::map<uint32, uint32> scores;
    std::map<uint32, uint32> skills;
    for (auto const& [id, known] : bot->GetSpellMap())
    {
        auto const* spell = sSpellMgr->GetSpellInfo(id);
        auto const* skill = PlayerbotSpellRepository::Instance().GetSkillLine(id);
        if (!spell || !skill || !IsProfessionSkill(skill->SkillLine) || !bot->HasSkill(skill->SkillLine) ||
            known->State == PLAYERSPELL_REMOVED || !known->Active || rejected.count(id) || bot->HasSpellCooldown(id) ||
            spell->Effects[EFFECT_0].Effect != SPELL_EFFECT_CREATE_ITEM ||
            !spell->Effects[EFFECT_0].ItemType)
            continue;
        bool tools = true;
        for (uint32 tool : spell->Totem)
            tools = tools && (!tool || bot->GetItemCount(tool, false));
        for (uint32 category : spell->TotemCategory)
            tools = tools && (!category || bot->HasItemTotemCategory(category));
        if (!tools)
            continue;
        SelfbotCraft::Recipe recipe;
        recipe.spell = id;
        recipe.item = spell->Effects[EFFECT_0].ItemType;
        recipe.output = uint32(std::max(1, spell->Effects[EFFECT_0].CalcValue(bot)));
        if (recipe.output > 20)
            continue;
        for (uint32 index = 0; index < MAX_SPELL_REAGENTS; ++index)
            if (spell->Reagent[index] > 0 && spell->ReagentCount[index])
            {
                uint32 item = uint32(spell->Reagent[index]);
                recipe.reagents[item] += spell->ReagentCount[index];
                inventory[item] = bot->GetItemCount(item, false);
            }
        if (recipe.reagents.empty())
            continue;
        inventory[recipe.item] = bot->GetItemCount(recipe.item, false);
        uint32 rank = crafting.GetSpellPriority(spell);
        // Rank 60 is the old intermediate-only hint. Dependencies are now resolved by the shared ledger.
        if (rank && rank != 60)
            scores[id] = (skill->SkillLine == SKILL_MINING || skill->SkillLine == SKILL_HERBALISM ? 600u :
                CompanionErrands::IsPrimary(skill->SkillLine) ? 1000u : 0u) + rank;
        skills[id] = skill->SkillLine;
        recipes.push_back(std::move(recipe));
    }
    // Missing raw materials are observations, not permission to buy or travel to an invented source.
    std::set<uint32> intermediates;
    for (auto const& recipe : recipes)
        intermediates.insert(recipe.item);
    SelfbotCraft::Planner planner(recipes, [&intermediates](uint32 item)
    {
        return intermediates.count(item) ? -1.0 : 10.0;
    });
    uint32 best = 0;
    double bestCost = 0;
    uint32 readyBest = 0;
    double readyCost = 0;
    CompanionProfessionPlan ready;
    std::vector<SelfbotCraft::Recipe const*> roots;
    for (auto const& recipe : recipes)
        if (scores.count(recipe.spell))
            roots.push_back(&recipe);
    std::sort(roots.begin(), roots.end(), [&scores](auto const* left, auto const* right)
    {
        if (scores[left->spell] != scores[right->spell])
            return scores[left->spell] > scores[right->spell];
        if (left->reagents.size() != right->reagents.size())
            return left->reagents.size() < right->reagents.size();
        return left->spell < right->spell;
    });
    uint32 tested = 0;
    for (auto const* root : roots)
    {
        if (++tested > 64)
            break;
        auto const& recipe = *root;
        auto score = scores.find(recipe.spell);
        if (score == scores.end())
            continue;
        auto plan = planner.Build(recipe, inventory);
        if (!plan.valid || plan.steps.size() > 32)
            continue;
        CompanionProfessionPlan candidate;
        candidate.root = recipe.spell;
        candidate.materialsFor = recipe.spell;
        candidate.skill = skills[recipe.spell];
        candidate.primary = CompanionErrands::IsPrimary(candidate.skill);
        for (auto const& step : plan.steps)
            if (!step.spell)
                candidate.missing[step.item] += step.quantity;
            else if (!candidate.next && crafting.AcceptSpell(sSpellMgr->GetSpellInfo(step.spell)))
                candidate.next = step.spell;
        if (score->second > best || (score->second == best && plan.cost < bestCost))
        {
            best = score->second;
            bestCost = plan.cost;
            result = candidate;
        }
        if (candidate.next && (score->second > readyBest ||
            (score->second == readyBest && plan.cost < readyCost)))
        {
            readyBest = score->second;
            readyCost = plan.cost;
            ready = std::move(candidate);
        }
    }
    // Prefer work actually possible now; retain the main project's material needs during secondary work.
    if (ready.next)
    {
        if (best > readyBest)
        {
            ready.missing = result.missing;
            ready.materialsFor = result.materialsFor;
        }
        return ready;
    }
    return result;
}
