/*
 * WoW Compagnon, GNU GPL v2 or later. See the preserved component author notices.
 */
#include "CompanionFarming.h"
#include "CompanionErrands.h"
#include "CompanionGoals.h"
#include "CompanionLootSources.h"
#include "CompanionProfessions.h"
#include "CompanionFarmRegionPolicy.h"
#include "CompanionGuildBank.h"
#include "Bag.h"
#include "CastCustomSpellAction.h"
#include "CellImpl.h"
#include "EventMap.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "LootAction.h"
#include "LootObjectStack.h"
#include "MotionMaster.h"
#include "NewRpgBaseAction.h"
#include "PlayerbotSpellRepository.h"
#include "Playerbots.h"
#include "World.h"
#include "pbc_companion_language.h"
#include <algorithm>
#include <limits>
#include <map>
#include <set>

namespace
{
    enum FarmEvent : uint32 { Decision = 1, Retry, Progress, Expired, PlanDue, WorkReset, TrackingDue, BankDone };
    enum class SourceKind : uint8 { Deposit, Creature, Survey, Workshop, Bank };
    struct FarmSource
    {
        SourceKind kind = SourceKind::Deposit;
        uint32 spawn = 0;
        uint32 loot = 0;
        uint32 skin = 0;
        float x = 0, y = 0, z = 0;
        uint32 skill = 0, required = 0, level = 0;
        uint32 zone = 0, tier = 0;
        uint64 Key() const { return (uint64(kind) << 32) | spawn; }
    };

    bool FarmOwner(PlayerbotAI* ai)
    {
        if (!IsCompanionInventoryManaged(ai))
            return false;
        Player* master = ai->GetMaster();
        Player* bot = ai->GetBot();
        return master->GetSession() && bot->GetSession() &&
            !master->GetSession()->IsLoggingOut() &&
            master->GetSession()->GetAccountId() == bot->GetSession()->GetAccountId() &&
            master->GetGroup() && master->GetGroup() == bot->GetGroup();
    }

    bool FarmSafe(Player* bot)
    {
        return bot && bot->IsInWorld() && bot->IsAlive() && !bot->IsInCombat() &&
            !bot->IsBeingTeleported() && !bot->IsInFlight() && !bot->IsFlying() && !bot->IsFalling() &&
            !bot->GetTransport() && !bot->GetVehicle() && !bot->InBattleground() && !bot->GetMap()->IsDungeon();
    }

    uint32 GatherSkill(Player* bot, GameObjectTemplate const* object, uint32& required)
    {
        auto const* lock = object && object->type == GAMEOBJECT_TYPE_CHEST ?
            sLockStore.LookupEntry(object->GetLockId()) : nullptr;
        if (!lock)
            return 0;
        for (uint32 index = 0; index < 8; ++index)
            if (lock->Type[index] == LOCK_KEY_SKILL)
            {
                uint32 skill = SkillByLockType(LockType(lock->Index[index]));
                if ((skill == SKILL_MINING || skill == SKILL_HERBALISM) && bot->HasSkill(skill) &&
                    bot->GetSkillValue(skill) >= lock->Skill[index] && bot->HasSpell(skill == SKILL_MINING ? 2575 : 2366))
                {
                    required = lock->Skill[index];
                    return skill;
                }
            }
        return 0;
    }

    uint32 CarriedCount(PlayerbotAI* ai)
    {
        uint32 count = 0;
        for (Item* item : ai->GetInventoryItems())
            count += item->GetCount();
        return count;
    }

    bool HasRoom(Player* bot)
    {
        for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
            if (!bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                return true;
        for (uint8 slot = INVENTORY_SLOT_BAG_START; slot < INVENTORY_SLOT_BAG_END; ++slot)
            if (Bag* bag = bot->GetBagByPos(slot); bag && !bag->GetTemplate()->BagFamily && bag->GetFreeSlots())
                return true;
        return false;
    }
}

struct CompanionFarmingState
{
    EventMap events;
    bool ready = true;
    uint64 revision = UINT64_MAX;
    bool indexed = false;
    bool selected = false;
    bool awaitingLoot = false;
    bool corpseLooted = false;
    bool skinning = false;
    bool planDue = true, trackingDue = true, awaitingCraft = false, bankPending = false;
    uint32 products = 0, actions = 0, secondary = 0;
    uint32 craftSpell = 0, craftOutput = 0, craftBefore = 0, craftSkill = 0, skillBefore = 0;
    uint32 lastRecipe = 0, lastSkill = 0, lastSkillBefore = 0, lastSkillAfter = 0;
    bool craftPrimary = false, craftIntermediate = false;
    uint64 skills = 0;
    ObjectGuid defenseTarget;
    CompanionProfessionPlan plan;
    CompanionFarm::Region region;
    std::vector<CompanionFarm::Site> sites;
    std::set<uint64> visited;
    std::set<uint32> rejectedCrafts;
    std::vector<FarmSource> facilities;
    float closest = std::numeric_limits<float>::max();
    float progressX = 0, progressY = 0, progressZ = 0;
    uint32 before = 0;
    ObjectGuid target;
    FarmSource source;
    std::vector<FarmSource> sources;
    std::set<uint64> rejected;
    std::string notice;
};

bool IsCompanionFarming(PlayerbotAI* ai)
{
    return IsCompanionInventoryManaged(ai) &&
        GetCompanionFarmAssignment(ai).mode == CompanionFarm::Mode::Working;
}

bool IsCompanionReturningFromFarm(PlayerbotAI* ai)
{
    return IsCompanionInventoryManaged(ai) &&
        GetCompanionFarmAssignment(ai).mode == CompanionFarm::Mode::Returning;
}

namespace
{
    void ClearFarmTarget(PlayerbotAI* ai, CompanionFarmingState& state)
    {
        auto* context = ai->GetAiObjectContext();
        if (!state.target.IsEmpty())
        {
            if (context->GetValue<LootObject>("loot target")->Get().guid == state.target)
                context->GetValue<LootObject>("loot target")->Set(LootObject());
            context->GetValue<LootObjectStack*>("available loot")->Get()->Remove(state.target);
        }
        if (state.awaitingLoot && !ai->GetBot()->IsInCombat())
            ai->GetBot()->InterruptNonMeleeSpells(false);
        if (FarmSafe(ai->GetBot()))
        {
            ai->GetBot()->StopMoving();
            ai->GetBot()->GetMotionMaster()->Clear();
        }
        state.events.CancelEvent(Progress);
        state.events.CancelEvent(Expired);
        state.selected = state.awaitingLoot = state.corpseLooted = state.skinning = false;
        state.target.Clear();
    }
}

bool CompanionFarmAttackAllowed(PlayerbotAI* ai, Unit* target)
{
    if (!IsCompanionFarming(ai) || !target)
        return true;
    Player* bot = ai->GetBot();
    return CompanionFarm::DefensiveAttack(target->IsHostileTo(bot), target->IsInCombatWith(bot),
        target->GetVictim() && target->GetVictim()->GetCharmerOrOwnerPlayerOrPlayerItself() == bot);
}

void CancelCompanionFarmRuntime(PlayerbotAI* ai)
{
    if (!IsCompanionInventoryManaged(ai) || !ai->companionErrands)
        return;
    auto& stored = CompanionFarmRuntime(ai);
    if (!stored)
        return;
    if (stored->awaitingCraft && !ai->GetBot()->IsInCombat())
        ai->GetBot()->InterruptNonMeleeSpells(false);
    ClearFarmTarget(ai, *stored);
    stored.reset();
}

bool IsCompanionFarmLoot(PlayerbotAI* ai, ObjectGuid guid)
{
    if (!IsCompanionFarming(ai))
        return false;
    auto const& stored = CompanionFarmRuntime(ai);
    return stored && stored->revision == GetCompanionFarmAssignment(ai).revision &&
        stored->awaitingLoot && stored->target == guid;
}

bool FinishCompanionFarmLoot(PlayerbotAI* ai, ObjectGuid guid)
{
    if (!IsCompanionFarmLoot(ai, guid))
        return false;
    auto& state = *CompanionFarmRuntime(ai);
    uint32 count = CarriedCount(ai);
    ObserveCompanionFarming(ai, state.revision, count > state.before ? "harvest_confirmed" : "visit_finished",
        count > state.before ? 1 : 0);
    state.awaitingLoot = false;
    if (!state.skinning && state.source.kind == SourceKind::Creature && state.source.skin &&
        state.source.skill && ai->GetBot()->HasSkill(state.source.skill))
    {
        state.corpseLooted = true;
        state.events.RescheduleEvent(Progress, Milliseconds(5000));
        return true;
    }
    state.selected = false;
    state.target.Clear();
    state.rejected.insert(state.source.Key());
    state.events.CancelEvent(Progress);
    state.events.CancelEvent(Expired);
    state.notice.clear();
    state.planDue = true;
    return true;
}

namespace
{
    class FarmingAction : public NewRpgBaseAction
    {
    public:
        explicit FarmingAction(PlayerbotAI* ai) : NewRpgBaseAction(ai, "independent companion farming"),
            state(*CompanionFarmRuntime(ai)), assignment(GetCompanionFarmAssignment(ai)) { }

        bool Execute(Event) override
        {
            if (!FarmSafe(bot) || bot->GetTradeData())
                return false;
            if (assignment.mode != CompanionFarm::Mode::Working || assignment.revision != state.revision ||
                assignment.status == "start_requested" || assignment.status == "awaiting_safe_start")
                return true;
            if (!state.ready)
                return true;
            state.ready = false;
            state.events.RescheduleEvent(Decision, Milliseconds(1000));
            if (bot->IsNonMeleeSpellCast(false) || !bot->GetLootGUID().IsEmpty() || state.bankPending)
                return true;
            if (state.awaitingCraft)
                return FinishCraft();
            if (!CompanionGoalAllowed(botAI, CompanionGoals::Domain::Gathering))
                return Wait("gathering_deferred_or_forbidden", "Je mets mon travail en pause, comme demande.",
                    "I am pausing my work as requested.");
            if (bot->GetMapId() != assignment.map)
                return Wait("assignment_map_changed", "Je dois choisir une nouvelle region de travail ici.",
                    "I need to choose a new working region on this map.");
            RefreshPlan();
            if (!state.indexed)
                Index();
            if (!state.selected)
            {
                if (!HasRoom(bot) && SelectBank())
                    return true;
                if (SelectWork())
                    return true;
                if (SelectBank())
                    return true;
                if (!HasRoom(bot))
                    return Wait("bags_full", "Mes sacs sont pleins, je ne peux plus recolter pour le moment.",
                        "My bags are full; I cannot gather more for now.");
                if (EnsureTracking())
                    return true;
                if (!SelectDefenceLoot() && !SelectVisible() && !SelectSurvey())
                    return Wait("no_useful_profession_region", "Je n'ai pas de ressource adaptee a chercher ici. "
                        "Je garde mes materiaux pour mon artisanat.",
                        "I have no suitable resource to seek here. I am keeping my materials for crafting.");
            }
            else if (state.source.kind == SourceKind::Survey)
            {
                if (EnsureTracking())
                    return true;
                if (!SelectDefenceLoot())
                    SelectVisible();
            }
            if (state.awaitingLoot)
                return true;
            if (state.source.kind == SourceKind::Survey)
            {
                if (bot->GetDistance(state.source.x, state.source.y, state.source.z) < 40.0f)
                {
                    state.visited.insert(CompanionFarm::SurveyCell(state.source.x, state.source.y));
                    ClearFarmTarget(botAI, state);
                    return true;
                }
                return Travel("scouting_profession_region");
            }
            WorldObject* object = state.source.kind == SourceKind::Creature ?
                static_cast<WorldObject*>(botAI->GetCreature(state.target)) :
                state.source.kind == SourceKind::Deposit ?
                static_cast<WorldObject*>(botAI->GetGameObject(state.target)) :
                static_cast<WorldObject*>(ObjectAccessor::GetSpawnedGameObjectByDBGUID(assignment.map,
                    state.source.spawn));
            float distance = object ? bot->GetDistance(object) :
                bot->GetDistance(state.source.x, state.source.y, state.source.z);
            if (!object || distance > 25.0f)
            {
                if (!object && distance < 15.0f)
                    return Reject("observed_resource_disappeared");
                return Travel(state.source.kind == SourceKind::Workshop ? "travelling_to_workshop" :
                    state.source.kind == SourceKind::Bank ? "travelling_to_guild_bank" : "approaching_observed_resource");
            }
            if (!bot->CanSeeOrDetect(object) || !bot->IsWithinLOSInMap(object) || !Safe(object))
                return Reject("resource_unsafe_or_invisible");
            if (GameObject* go = object->ToGameObject(); go &&
                (!go->isSpawned() || go->HasFlag(GAMEOBJECT_FLAGS, GO_FLAG_NOT_SELECTABLE)))
                return Reject("resource_unavailable");
            if (Creature* creature = object->ToCreature(); creature && creature->IsAlive())
                return Reject("farming_does_not_initiate_attacks");
            if (distance > INTERACTION_DISTANCE - 2.0f)
            {
                MoveTo(assignment.map, object->GetPositionX(), object->GetPositionY(), object->GetPositionZ(),
                    false, false, true, true);
                return true;
            }
            if (bot->isMoving())
            {
                bot->StopMoving();
                return true;
            }
            if (bot->IsMounted())
            {
                bot->RemoveAurasByType(SPELL_AURA_MOUNTED);
                return true;
            }
            if (state.source.kind == SourceKind::Workshop)
                return CastWork();
            if (state.source.kind == SourceKind::Bank)
            {
                if (!QueueCompanionGuildBank(botAI, object->GetGUID()))
                    return Reject("guild_operation_unavailable");
                ClearFarmTarget(botAI, state);
                state.bankPending = true;
                state.events.RescheduleEvent(BankDone, Milliseconds(10000));
                ObserveCompanionFarming(botAI, state.revision, "guild_transfer_requested");
                return true;
            }
            state.target = object->GetGUID();
            if (Creature* creature = object->ToCreature(); creature && state.corpseLooted &&
                !creature->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_SKINNABLE))
                return true;
            LootObject loot(bot, state.target);
            if (loot.IsEmpty() || !loot.IsLootPossible(bot))
                return Reject("native_loot_unavailable");
            state.before = CarriedCount(botAI);
            state.skinning = state.corpseLooted;
            state.awaitingLoot = true;
            context->GetValue<LootObject>("loot target")->Set(loot);
            if (!OpenLootAction(botAI).Execute(Event()))
                return Reject("native_gathering_failed");
            ObserveCompanionFarming(botAI, state.revision, "gathering_observed_resource");
            return true;
        }

    private:
        CompanionFarmingState& state;
        CompanionFarm::Assignment assignment;

        bool Wait(std::string const& reason, char const* french, char const* english)
        {
            if (state.selected)
                ClearFarmTarget(botAI, state);
            ObserveCompanionFarming(botAI, state.revision, reason);
            if (state.notice != reason)
            {
                botAI->TellMaster(PBC_IsFrenchClient(PBC_CompanionClientLocale(bot, botAI->GetMaster())) ? french : english);
                state.notice = reason;
            }
            return true;
        }

        bool Reject(std::string const& reason)
        {
            state.rejected.insert(state.source.Key());
            if (state.source.kind == SourceKind::Survey)
                state.visited.insert(CompanionFarm::SurveyCell(state.source.x, state.source.y));
            ClearFarmTarget(botAI, state);
            ObserveCompanionFarming(botAI, state.revision, reason);
            return true;
        }

        void Start(FarmSource const& source, ObjectGuid guid = ObjectGuid::Empty)
        {
            ClearFarmTarget(botAI, state);
            state.source = source;
            state.selected = true;
            state.target = guid;
            state.closest = std::numeric_limits<float>::max();
            state.progressX = bot->GetPositionX();
            state.progressY = bot->GetPositionY();
            state.progressZ = bot->GetPositionZ();
            state.events.RescheduleEvent(Progress, Milliseconds(30000));
            float distance = bot->GetDistance(source.x, source.y, source.z);
            uint32 deadline = uint32(std::clamp(180.0f + distance / 2.0f, 300.0f, 3600.0f) * 1000.0f);
            state.events.RescheduleEvent(Expired, Milliseconds(deadline));
        }

        bool Travel(char const* status)
        {
            float distance = bot->GetDistance(state.source.x, state.source.y, state.source.z);
            if (distance + 2.0f < state.closest ||
                bot->GetExactDist(state.progressX, state.progressY, state.progressZ) > 10.0f)
            {
                state.closest = distance;
                state.progressX = bot->GetPositionX();
                state.progressY = bot->GetPositionY();
                state.progressZ = bot->GetPositionZ();
                state.events.RescheduleEvent(Progress, Milliseconds(30000));
            }
            ObserveCompanionFarming(botAI, state.revision, status);
            if (!MoveFarTo(WorldPosition(assignment.map, state.source.x, state.source.y, state.source.z), false) &&
                !bot->isMoving() && !IsWaitingForLastMove(MovementPriority::MOVEMENT_NORMAL))
                return Reject("resource_route_unavailable");
            return true;
        }

        void RefreshPlan()
        {
            if (!state.planDue)
                return;
            state.planDue = false;
            state.events.RescheduleEvent(PlanDue, Milliseconds(5000));
            RestoreCompanionProfessionDefaults(botAI);
            auto plan = PlanCompanionProfession(botAI, state.rejectedCrafts);
            uint64 skills = 0;
            for (uint32 skill : {SKILL_MINING, SKILL_HERBALISM, SKILL_SKINNING})
                skills = skills * 997 + bot->GetPureSkillValue(skill);
            if (skills != state.skills || plan.missing != state.plan.missing || plan.root != state.plan.root)
                state.indexed = false;
            state.skills = skills;
            state.plan = std::move(plan);
        }

        bool Needed(uint32 loot) const
        {
            for (auto const& [item, quantity] : state.plan.missing)
                if (quantity && CompanionLootSources::Has(CompanionLootSourceIndex::Kind::Gameobject, loot, item))
                    return true;
            return false;
        }

        uint32 Tier(FarmSource const& source) const
        {
            return CompanionFarm::GatheringTier(Needed(source.loot), bot->GetPureSkillValue(source.skill),
                bot->GetPureMaxSkillValue(source.skill), source.required);
        }

        void Index()
        {
            state.sources.clear();
            state.sites.clear();
            state.facilities.clear();
            for (auto const& [spawn, data] : sObjectMgr->GetAllGOData())
            {
                if (data.mapid != assignment.map || !(data.phaseMask & bot->GetPhaseMask()))
                    continue;
                auto const* object = sObjectMgr->GetGameObjectTemplate(data.id);
                if (!object)
                    continue;
                uint32 required = 0;
                uint32 skill = GatherSkill(bot, object, required);
                if (skill && state.sources.size() < 16384)
                {
                    FarmSource source{SourceKind::Deposit, uint32(spawn), object->GetLootId(), 0,
                        data.posX, data.posY, data.posZ, skill, required};
                    source.tier = Tier(source);
                    if (!source.tier)
                        continue;
                    source.zone = bot->GetMap()->GetZoneId(bot->GetPhaseMask(), source.x, source.y, source.z);
                    auto const* area = sAreaTableStore.LookupEntry(source.zone);
                    if (!area || area->area_level > int32(bot->GetLevel()))
                        continue;
                    state.sources.push_back(source);
                    state.sites.push_back({source.Key(), source.zone, source.skill, source.tier,
                        source.x, source.y, source.z});
                }
                else if (state.facilities.size() < 4096 &&
                    (object->type == GAMEOBJECT_TYPE_SPELL_FOCUS || object->type == GAMEOBJECT_TYPE_GUILD_BANK))
                    state.facilities.push_back({object->type == GAMEOBJECT_TYPE_GUILD_BANK ?
                        SourceKind::Bank : SourceKind::Workshop, uint32(spawn), 0, 0,
                        data.posX, data.posY, data.posZ, 0, object->type == GAMEOBJECT_TYPE_SPELL_FOCUS ?
                        object->spellFocus.focusId : 0});
            }
            state.indexed = true;
            state.events.RescheduleEvent(Retry, Milliseconds(300000));
        }

        bool EnsureTracking()
        {
            uint32 skill = state.region.skill ? state.region.skill :
                bot->HasSkill(SKILL_MINING) ? SKILL_MINING : bot->HasSkill(SKILL_HERBALISM) ? SKILL_HERBALISM : 0;
            uint32 spell = skill == SKILL_MINING ? 2580 : skill == SKILL_HERBALISM ? 2383 : 0;
            if (!spell || !state.trackingDue || bot->HasAura(spell))
                return false;
            state.trackingDue = false;
            state.events.RescheduleEvent(TrackingDue, Milliseconds(30000));
            if (!bot->HasSpell(spell))
                return false;
            if (botAI->CanCastSpell(spell, bot) && botAI->CastSpell(spell, bot))
            {
                ObserveCompanionFarming(botAI, state.revision, "resource_tracking_requested");
                return true;
            }
            return false;
        }

        bool SelectVisible()
        {
            std::list<GameObject*> objects;
            AnyGameObjectInObjectRangeCheck check(bot, 180.0f);
            Acore::GameObjectListSearcher<AnyGameObjectInObjectRangeCheck> search(bot, objects, check);
            Cell::VisitObjects(bot, search, 180.0f);
            FarmSource best;
            ObjectGuid guid;
            float distance = std::numeric_limits<float>::max();
            for (GameObject* object : objects)
            {
                uint32 required = 0;
                uint32 skill = GatherSkill(bot, object->GetGOInfo(), required);
                if (!skill || !object->isSpawned() || object->getLootState() != GO_READY ||
                    object->HasFlag(GAMEOBJECT_FLAGS, GO_FLAG_NOT_SELECTABLE) || !bot->CanSeeOrDetect(object))
                    continue;
                FarmSource source{SourceKind::Deposit, uint32(object->GetSpawnId()), object->GetGOInfo()->GetLootId(),
                    0, object->GetPositionX(), object->GetPositionY(), object->GetPositionZ(), skill, required};
                source.tier = Tier(source);
                if (!source.tier || state.rejected.count(source.Key()) || !Safe(object))
                    continue;
                LootObject loot(bot, object->GetGUID());
                if (loot.IsEmpty() || !loot.IsLootPossible(bot, false))
                    continue;
                bool claimed = false;
                for (GroupReference* ref = bot->GetGroup()->GetFirstMember(); ref; ref = ref->next())
                    if (Player* member = ref->GetSource(); member && member != bot && member->IsInWorld() &&
                        member->GetMap() == bot->GetMap())
                        if (PlayerbotAI* other = GET_PLAYERBOT_AI(member))
                            claimed = claimed || IsCompanionGatherLoot(other, object->GetGUID());
                if (claimed)
                    continue;
                float candidate = bot->GetDistance(object);
                if (source.tier > best.tier || (source.tier == best.tier && candidate < distance))
                {
                    best = source;
                    guid = object->GetGUID();
                    distance = candidate;
                }
            }
            if (guid.IsEmpty())
                return false;
            Start(best, guid);
            ObserveCompanionFarming(botAI, state.revision, "resource_observed");
            return true;
        }

        bool SelectSurvey()
        {
            std::vector<CompanionFarm::Site> remaining;
            for (auto const& site : state.sites)
                if (!state.visited.count(CompanionFarm::SurveyCell(site.x, site.y)))
                    remaining.push_back(site);
            auto region = CompanionFarm::ChooseRegion(remaining, bot->GetPositionX(), bot->GetPositionY(),
                state.region.zone);
            size_t next = CompanionFarm::NextSurvey(remaining, region, state.visited,
                bot->GetPositionX(), bot->GetPositionY());
            if (next == remaining.size())
                return false;
            if (region.zone != state.region.zone || region.skill != state.region.skill)
            {
                state.trackingDue = true;
                LOG_INFO("playerbots.companion", "[FarmPlan] {} region={} skill={} value={} maximum={} areas={} tier={}",
                    bot->GetName(), region.zone, region.skill, bot->GetPureSkillValue(region.skill),
                    bot->GetPureMaxSkillValue(region.skill), region.coverage, region.tier);
            }
            state.region = region;
            auto const& site = remaining[next];
            FarmSource source;
            source.kind = SourceKind::Survey;
            source.x = site.x;
            source.y = site.y;
            source.z = site.z;
            Start(source);
            ObserveCompanionFarming(botAI, state.revision, "profession_region_selected", 0, region.zone);
            return true;
        }

        bool SelectDefenceLoot()
        {
            Creature* creature = botAI->GetCreature(state.defenseTarget);
            if (!creature || creature->IsAlive() || bot->GetDistance(creature) > 80.0f)
                return false;
            auto const* info = creature->GetCreatureTemplate();
            FarmSource source{SourceKind::Creature, uint32(creature->GetSpawnId()), info->lootid, info->SkinLootId,
                creature->GetPositionX(), creature->GetPositionY(), creature->GetPositionZ(), info->GetRequiredLootSkill()};
            LootObject loot(bot, creature->GetGUID());
            if (loot.IsEmpty() || !loot.IsLootPossible(bot, false) || state.rejected.count(source.Key()) || !Safe(creature))
                return false;
            Start(source, creature->GetGUID());
            state.defenseTarget.Clear();
            return true;
        }

        bool SelectWork()
        {
            if (!state.plan.next || !CompanionGoalAllowed(botAI, CompanionGoals::Domain::Crafting) ||
                !CompanionFarm::WorkBudget(state.products, state.actions, state.secondary, state.plan.primary))
                return false;
            auto const* spell = sSpellMgr->GetSpellInfo(state.plan.next);
            CraftRandomItemAction crafting(botAI);
            if (!spell || bot->HasSpellCooldown(spell->Id) || !crafting.AcceptSpell(spell))
                return false;
            state.craftSpell = spell->Id;
            if (botAI->CanCastSpell(spell->Id, bot))
                return CastWork();
            if (!spell->RequiresSpellFocus)
                return false;
            FarmSource const* best = nullptr;
            float distance = std::numeric_limits<float>::max();
            for (auto const& station : state.facilities)
                if (station.kind == SourceKind::Workshop && station.required == spell->RequiresSpellFocus &&
                    !state.rejected.count(station.Key()))
                    if (float candidate = bot->GetDistance(station.x, station.y, station.z); candidate < distance)
                    {
                        best = &station;
                        distance = candidate;
                    }
            if (!best)
                return false;
            Start(*best);
            return true;
        }

        bool CastWork()
        {
            auto const* spell = sSpellMgr->GetSpellInfo(state.craftSpell);
            CraftRandomItemAction crafting(botAI);
            if (!spell || !CompanionGoalAllowed(botAI, CompanionGoals::Domain::Crafting) ||
                !CompanionFarm::WorkBudget(state.products, state.actions, state.secondary, state.plan.primary) ||
                !crafting.AcceptSpell(spell) || !botAI->CanCastSpell(spell->Id, bot))
                return Reject("workshop_recipe_not_ready");
            if (bot->isMoving())
            {
                bot->StopMoving();
                return true;
            }
            if (bot->IsMounted())
            {
                bot->RemoveAurasByType(SPELL_AURA_MOUNTED);
                return true;
            }
            auto const* skill = PlayerbotSpellRepository::Instance().GetSkillLine(spell->Id);
            state.craftOutput = spell->Effects[EFFECT_0].ItemType;
            state.craftBefore = bot->GetItemCount(state.craftOutput, false);
            state.craftSkill = skill ? skill->SkillLine : state.plan.skill;
            state.skillBefore = bot->GetPureSkillValue(state.craftSkill);
            state.craftPrimary = state.plan.primary;
            state.craftIntermediate = spell->Id != state.plan.root;
            ClearFarmTarget(botAI, state);
            state.awaitingCraft = botAI->CastSpell(spell->Id, bot);
            if (!state.awaitingCraft)
            {
                state.rejectedCrafts.insert(spell->Id);
                state.planDue = true;
            }
            ObserveCompanionFarming(botAI, state.revision, state.awaitingCraft ? "craft_requested" : "craft_refused");
            return true;
        }

        bool FinishCraft()
        {
            bool completed = bot->GetItemCount(state.craftOutput, false) > state.craftBefore;
            state.awaitingCraft = false;
            if (completed)
            {
                if (!state.actions)
                    state.events.RescheduleEvent(WorkReset, Milliseconds(300000));
                ++state.actions;
                if (!state.craftIntermediate)
                    ++state.products;
                if (!state.craftPrimary)
                    ++state.secondary;
                state.lastRecipe = state.craftSpell;
                state.lastSkill = state.craftSkill;
                state.lastSkillBefore = state.skillBefore;
                state.lastSkillAfter = bot->GetPureSkillValue(state.craftSkill);
                if (CompanionGoalPermitted(botAI, CompanionGoals::Domain::Equipment))
                    botAI->DoSpecificAction("equip upgrade", Event(), true);
            }
            else
                state.rejectedCrafts.insert(state.craftSpell);
            LOG_INFO("playerbots.companion", "[FarmCraft] {} spell={} item={} created={} skill={} before={} after={}",
                bot->GetName(), state.craftSpell, state.craftOutput, completed, state.craftSkill,
                state.skillBefore, bot->GetPureSkillValue(state.craftSkill));
            ObserveCompanionFarming(botAI, state.revision, completed ? "craft_confirmed" : "craft_not_created");
            state.craftSpell = 0;
            state.planDue = true;
            return true;
        }

        bool SelectBank()
        {
            Player* master = botAI->GetMaster();
            if (!CompanionGuildBankEnabled() || !bot->GetGuildId() || bot->GetGuildId() != master->GetGuildId() ||
                !CompanionGoalAllowed(botAI, CompanionGoals::Domain::Guild) ||
                !CompanionGuildBankVisitDue(bot->GetGUID()) || !HasCompanionGuildDeposit(botAI))
                return false;
            FarmSource const* best = nullptr;
            float distance = std::numeric_limits<float>::max();
            for (auto const& bank : state.facilities)
                if (bank.kind == SourceKind::Bank && !state.rejected.count(bank.Key()))
                    if (float candidate = bot->GetDistance(bank.x, bank.y, bank.z); candidate < distance)
                    {
                        best = &bank;
                        distance = candidate;
                    }
            if (!best)
                return false;
            Start(*best);
            return true;
        }

        bool Safe(WorldObject* target)
        {
            std::list<Unit*> enemies;
            Acore::AnyUnfriendlyUnitInObjectRangeCheck check(bot, bot, 60.0f);
            Acore::UnitListSearcher<Acore::AnyUnfriendlyUnitInObjectRangeCheck> search(bot, enemies, check);
            Cell::VisitObjects(bot, search, 60.0f);
            for (Unit* enemy : enemies)
                if (enemy != target && enemy->IsAlive() && enemy->IsHostileTo(bot) &&
                    enemy->IsWithinDistInMap(target, 25.0f))
                    return false;
            return true;
        }
    };
}

CompanionProfessionPlan GetCompanionFarmProfessionPlan(PlayerbotAI* ai)
{
    if (!ai || !ai->companionErrands)
        return {};
    auto const& state = CompanionFarmRuntime(ai);
    return state ? state->plan : CompanionProfessionPlan{};
}

CompanionFarmWorkSnapshot GetCompanionFarmWorkSnapshot(PlayerbotAI* ai)
{
    CompanionFarmWorkSnapshot result;
    if (!ai || !ai->companionErrands)
        return result;
    auto const& state = CompanionFarmRuntime(ai);
    if (!state)
        return result;
    result.skill = state->region.skill;
    result.region = state->region.zone;
    result.products = state->products;
    result.actions = state->actions;
    result.lastRecipe = state->lastRecipe;
    result.lastSkill = state->lastSkill;
    result.skillBefore = state->lastSkillBefore;
    result.skillAfter = state->lastSkillAfter;
    uint32 spell = result.skill == SKILL_MINING ? 2580 : result.skill == SKILL_HERBALISM ? 2383 : 0;
    result.trackingKnown = spell && ai->GetBot()->HasSpell(spell);
    result.trackingActive = spell && ai->GetBot()->HasAura(spell);
    return result;
}

void UpdateCompanionFarming(PlayerbotAI* ai, uint32_t elapsed)
{
    if (!IsCompanionInventoryManaged(ai))
        return;
    auto assignment = GetCompanionFarmAssignment(ai);
    if (assignment.mode == CompanionFarm::Mode::Follow)
    {
        CancelCompanionFarmRuntime(ai);
        return;
    }
    Player* bot = ai->GetBot();
    Player* master = ai->GetMaster();
    auto& stored = CompanionFarmRuntime(ai);
    if (!stored || stored->revision != assignment.revision)
    {
        CancelCompanionFarmRuntime(ai);
        CancelCompanionErrands(ai);
        SetCompanionNarrativeFocus(bot->GetGUID(), "normal");
        ai->ChangeStrategy("+follow,-stay,-passive", BOT_STATE_NON_COMBAT);
        RequestCompanionTraining(ai);
        stored = std::make_shared<CompanionFarmingState>();
        stored->revision = assignment.revision;
    }
    if (!FarmOwner(ai))
    {
        CancelCompanionFarmRuntime(ai);
        assignment.mode = CompanionFarm::Mode::Follow;
        assignment.status = "owner_or_group_unavailable";
        SetCompanionFarmAssignment(ai, assignment);
        return;
    }
    if (assignment.status == "start_requested" || assignment.status == "awaiting_safe_start")
    {
        if (bot->GetMap()->IsDungeon() || bot->InBattleground())
        {
            assignment.mode = CompanionFarm::Mode::Follow;
            assignment.status = "start_requires_outdoor_world";
            SetCompanionFarmAssignment(ai, assignment);
            return;
        }
        if (!FarmSafe(bot))
        {
            ObserveCompanionFarming(ai, assignment.revision, "awaiting_safe_start");
            return;
        }
        assignment.map = bot->GetMapId();
        assignment.zone = bot->GetZoneId();
        assignment.x = bot->GetPositionX();
        assignment.y = bot->GetPositionY();
        assignment.z = bot->GetPositionZ();
        assignment.status = "searching";
        if (!SetCompanionFarmAssignment(ai, assignment))
            return;
        assignment = GetCompanionFarmAssignment(ai);
        if (assignment.mode != CompanionFarm::Mode::Working)
            return;
        stored->revision = assignment.revision;
    }
    if (assignment.mode == CompanionFarm::Mode::Returning)
    {
        if (FarmSafe(bot) && master->IsInWorld() && !master->IsBeingTeleported() && master->IsAlive() &&
            bot->GetMap() == master->GetMap() &&
            (bot->GetPhaseMask() & master->GetPhaseMask()) && bot->GetDistance(master) < 12.0f)
        {
            assignment.mode = CompanionFarm::Mode::Follow;
            assignment.status = "returned";
            SetCompanionFarmAssignment(ai, assignment);
        }
        return;
    }
    if (!stored)
        stored = std::make_shared<CompanionFarmingState>();
    auto& state = *stored;
    if (bot->IsInCombat())
        if (Unit* victim = bot->GetVictim(); victim && CompanionFarmAttackAllowed(ai, victim))
            state.defenseTarget = victim->GetGUID();
    state.events.Update(elapsed);
    while (uint32 event = state.events.ExecuteEvent())
        if (event == Decision)
            state.ready = true;
        else if (event == PlanDue)
            state.planDue = true;
        else if (event == TrackingDue)
            state.trackingDue = true;
        else if (event == BankDone)
        {
            state.bankPending = false;
            state.planDue = true;
            state.indexed = false;
        }
        else if (event == WorkReset)
        {
            state.products = state.actions = state.secondary = 0;
            state.planDue = true;
        }
        else if (event == Retry)
        {
            state.indexed = false;
            state.planDue = true;
            state.sources.clear();
            state.rejected.clear();
            state.visited.clear();
            state.rejectedCrafts.clear();
        }
        else if ((event == Progress || event == Expired) && state.selected)
        {
            if (bot->IsInCombat())
            {
                state.events.RescheduleEvent(event, Milliseconds(30000));
                continue;
            }
            state.rejected.insert(state.source.Key());
            if (state.source.kind == SourceKind::Survey)
                state.visited.insert(CompanionFarm::SurveyCell(state.source.x, state.source.y));
            ClearFarmTarget(ai, state);
            ObserveCompanionFarming(ai, state.revision, "resource_unreachable_or_timed_out");
        }
}

bool ExecuteCompanionFarming(PlayerbotAI* ai)
{
    if (!IsCompanionFarming(ai) || !FarmOwner(ai))
        return false;
    if (!CompanionFarmRuntime(ai))
        UpdateCompanionFarming(ai, 0);
    if (!CompanionFarmRuntime(ai) || !IsCompanionFarming(ai))
        return false;
    return FarmingAction(ai).Execute(Event());
}
