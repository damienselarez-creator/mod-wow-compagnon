/*
 * WoW Compagnon, GNU GPL v2 or later. See the preserved component author notices.
 */
#include "CompanionFarming.h"
#include "CompanionErrands.h"
#include "CompanionGoals.h"
#include "CompanionLootSources.h"
#include "CompanionProfessions.h"
#include "AttackAction.h"
#include "Bag.h"
#include "CellImpl.h"
#include "EventMap.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "LootAction.h"
#include "LootObjectStack.h"
#include "MotionMaster.h"
#include "NewRpgBaseAction.h"
#include "Playerbots.h"
#include "World.h"
#include "pbc_companion_language.h"
#include <algorithm>
#include <limits>
#include <map>
#include <set>

namespace
{
    enum FarmEvent : uint32 { Decision = 1, Retry, Progress, Expired };
    enum class SourceKind : uint8 { Deposit, Creature };
    struct FarmSource
    {
        SourceKind kind = SourceKind::Deposit;
        uint32 spawn = 0;
        uint32 loot = 0;
        uint32 skin = 0;
        float x = 0, y = 0, z = 0;
        uint32 skill = 0, required = 0, level = 0;
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
    auto const& state = CompanionFarmRuntime(ai);
    return (state && target->GetGUID() == state->target) || target->IsInCombatWith(bot) ||
        (target->GetVictim() && target->GetVictim()->GetCharmerOrOwnerPlayerOrPlayerItself() == bot);
}

void CancelCompanionFarmRuntime(PlayerbotAI* ai)
{
    if (!IsCompanionInventoryManaged(ai))
        return;
    auto& stored = CompanionFarmRuntime(ai);
    if (!stored)
        return;
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
            if (!FarmSafe(bot))
                return false;
            if (assignment.mode != CompanionFarm::Mode::Working || assignment.revision != state.revision ||
                assignment.status == "start_requested" || assignment.status == "awaiting_safe_start")
                return true;
            if (!state.ready)
                return true;
            state.ready = false;
            state.events.RescheduleEvent(Decision, Milliseconds(1000));
            if (!CompanionGoalAllowed(botAI, CompanionGoals::Domain::Gathering))
                return Wait("gathering_deferred_or_forbidden", "Je mets la recolte en pause, comme demande.",
                    "I am pausing gathering as requested.");
            if (bot->GetMapId() != assignment.map)
                return Wait("assignment_map_changed", "Je ne suis plus dans ma zone de recolte.",
                    "I am no longer on the map assigned for gathering.");
            if (!HasRoom(bot))
                return Wait("bags_full", "Mes sacs sont pleins. J'arrete la recolte en attendant tes instructions.",
                    "My bags are full. I am pausing gathering until you give me instructions.");
            if (bot->IsNonMeleeSpellCast(false) || !bot->GetLootGUID().IsEmpty())
                return true;
            if (!state.indexed)
                Index();
            if (!state.selected && !Select())
                return Wait("no_accessible_resource", "Je ne trouve pas de ressource exploitable ici pour mes metiers.",
                    "I cannot find an accessible resource here for my professions.");
            if (state.awaitingLoot)
                return true;
            WorldObject* object = state.source.kind == SourceKind::Deposit ?
                static_cast<WorldObject*>(ObjectAccessor::GetSpawnedGameObjectByDBGUID(assignment.map, state.source.spawn)) :
                static_cast<WorldObject*>(ObjectAccessor::GetSpawnedCreatureByDBGUID(assignment.map, state.source.spawn));
            float distance = object ? bot->GetDistance(object) :
                bot->GetDistance(state.source.x, state.source.y, state.source.z);
            if (distance + 2.0f < state.closest ||
                bot->GetExactDist(state.progressX, state.progressY, state.progressZ) > 10.0f)
            {
                state.closest = distance;
                state.progressX = bot->GetPositionX();
                state.progressY = bot->GetPositionY();
                state.progressZ = bot->GetPositionZ();
                state.events.RescheduleEvent(Progress, Milliseconds(30000));
            }
            if (!object || distance > 25.0f)
            {
                if (!object && distance < 15.0f)
                    return Reject("resource_not_spawned");
                ObserveCompanionFarming(botAI, state.revision, "travelling_to_resource");
                if (!MoveFarTo(WorldPosition(assignment.map, state.source.x, state.source.y, state.source.z), false) &&
                    !bot->isMoving() && !IsWaitingForLastMove(MovementPriority::MOVEMENT_NORMAL))
                    return Reject("resource_route_unavailable");
                return true;
            }
            if (!bot->CanSeeOrDetect(object) || !bot->IsWithinLOSInMap(object) || !Safe(object))
                return Reject("resource_unsafe_or_invisible");
            if (GameObject* deposit = object->ToGameObject(); deposit &&
                (!deposit->isSpawned() || deposit->getLootState() != GO_READY ||
                    deposit->HasFlag(GAMEOBJECT_FLAGS, GO_FLAG_NOT_SELECTABLE)))
                return Reject("resource_unavailable");
            for (GroupReference* ref = bot->GetGroup()->GetFirstMember(); ref; ref = ref->next())
                if (Player* member = ref->GetSource(); member && member != bot && member->IsInWorld() &&
                    member->GetMap() == bot->GetMap())
                    if (PlayerbotAI* other = GET_PLAYERBOT_AI(member); other &&
                        IsCompanionGatherLoot(other, object->GetGUID()))
                        return Reject("resource_claimed_by_companion");
            state.target = object->GetGUID();
            if (Creature* creature = object->ToCreature(); creature && creature->IsAlive())
            {
                if (!bot->IsValidAttackTarget(creature) || creature->hasLootRecipient() ||
                    creature->GetLevel() > bot->GetLevel() || creature->GetCreatureTemplate()->rank != CREATURE_ELITE_NORMAL)
                    return Reject("creature_not_solo_target");
                context->GetValue<Unit*>("current target")->Set(creature);
                return FarmAttack(botAI).Start(creature);
            }
            if (Creature* creature = object->ToCreature(); creature && state.corpseLooted &&
                !creature->HasFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_SKINNABLE))
                return true; // Native loot release may enable skinning on the next session update.
            if (distance > INTERACTION_DISTANCE - 2.0f)
            {
                MoveTo(assignment.map, object->GetPositionX(), object->GetPositionY(), object->GetPositionZ(),
                    false, false, true, true);
                return true;
            }
            LootObject loot(bot, state.target);
            if (loot.IsEmpty() || !loot.IsLootPossible(bot))
                return Reject("native_loot_unavailable");
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
            state.before = CarriedCount(botAI);
            state.skinning = state.corpseLooted;
            state.awaitingLoot = true;
            context->GetValue<LootObject>("loot target")->Set(loot);
            if (!OpenLootAction(botAI).Execute(Event()))
            {
                state.awaitingLoot = false;
                return Reject("native_gathering_failed");
            }
            ObserveCompanionFarming(botAI, state.revision, "gathering");
            return true;
        }

    private:
        class FarmAttack : public AttackAction
        {
        public:
            explicit FarmAttack(PlayerbotAI* ai) : AttackAction(ai, "farm resource creature") { }
            bool Start(Unit* target) { return Attack(target); }
        };
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
            ClearFarmTarget(botAI, state);
            ObserveCompanionFarming(botAI, state.revision, reason);
            return true;
        }

        bool AccessibleMap(uint32 map, uint32 phase) const
        {
            return map == assignment.map && (phase & bot->GetPhaseMask());
        }

        bool Needed(FarmSource const& source, CompanionProfessionPlan const& plan) const
        {
            for (auto const& [item, quantity] : plan.missing)
                if (quantity && (CompanionLootSources::Has(source.kind == SourceKind::Deposit ?
                    CompanionLootSourceIndex::Kind::Gameobject : CompanionLootSourceIndex::Kind::Creature,
                        source.loot, item) || (source.skill &&
                        CompanionLootSources::Has(CompanionLootSourceIndex::Kind::Skinning, source.skin, item))))
                    return true;
            return false;
        }

        float Score(FarmSource const& source, bool needed) const
        {
            uint32 skill = source.skill && bot->GetPureSkillValue(source.skill) <
                bot->GetPureMaxSkillValue(source.skill) ? bot->GetPureSkillValue(source.skill) : 0;
            return CompanionFarm::ResourceScore(bot->GetDistance(source.x, source.y, source.z), needed,
                skill, source.required, source.level, bot->GetLevel());
        }

        void Index()
        {
            RestoreCompanionProfessionDefaults(botAI);
            auto plan = PlanCompanionProfession(botAI, {});
            std::multimap<float, FarmSource> candidates;
            auto retain = [&](FarmSource const& source)
            {
                bool needed = Needed(source, plan);
                bool skin = source.kind == SourceKind::Creature && source.skin && source.skill;
                bool level = source.level && source.level <= bot->GetLevel() &&
                    bot->GetLevel() - source.level <= 4 &&
                    bot->GetLevel() < sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL);
                if (source.kind == SourceKind::Creature && !needed && !skin && !level)
                    return;
                candidates.emplace(Score(source, needed), source);
                if (candidates.size() > 512)
                    candidates.erase(std::prev(candidates.end()));
            };
            for (auto const& [spawn, data] : sObjectMgr->GetAllGOData())
            {
                if (!AccessibleMap(data.mapid, data.phaseMask))
                    continue;
                auto const* object = sObjectMgr->GetGameObjectTemplate(data.id);
                uint32 required = 0;
                uint32 skill = GatherSkill(bot, object, required);
                if (skill)
                    retain({SourceKind::Deposit, uint32(spawn), object->GetLootId(), 0,
                        data.posX, data.posY, data.posZ, skill, required, 0});
            }
            for (auto const& [spawn, data] : sObjectMgr->GetAllCreatureData())
            {
                if (!AccessibleMap(data.mapid, data.phaseMask))
                    continue;
                auto const* creature = sObjectMgr->GetCreatureTemplate(data.id);
                if (!creature || creature->rank != CREATURE_ELITE_NORMAL || creature->maxlevel > bot->GetLevel())
                    continue;
                auto const* faction = sFactionTemplateStore.LookupEntry(creature->faction);
                if (!faction || !bot->GetFactionTemplateEntry()->IsHostileTo(*faction))
                    continue;
                uint32 skill = creature->GetRequiredLootSkill();
                uint32 level = creature->maxlevel;
                // Same native threshold as LootObject::Refresh, checked again against the spawned creature.
                uint32 required = level < 10 ? 1 : level < 20 ? (level - 10) * 10 : level * 5;
                bool gather = bot->HasSkill(skill) && bot->GetSkillValue(skill) >= required;
                retain({SourceKind::Creature, uint32(spawn), creature->lootid, gather ? creature->SkinLootId : 0,
                    data.posX, data.posY, data.posZ, gather ? skill : 0, required, level});
            }
            for (auto const& [score, source] : candidates)
                state.sources.push_back(source);
            state.indexed = true;
            state.events.RescheduleEvent(Retry, Milliseconds(300000));
        }

        bool Select()
        {
            auto plan = PlanCompanionProfession(botAI, {});
            float best = std::numeric_limits<float>::max();
            for (auto const& source : state.sources)
            {
                if (state.rejected.count(source.Key()))
                    continue;
                bool needed = Needed(source, plan);
                float score = Score(source, needed);
                if (score >= best)
                    continue;
                best = score;
                state.source = source;
            }
            if (best == std::numeric_limits<float>::max())
                return false;
            state.selected = true;
            state.corpseLooted = false;
            state.skinning = false;
            state.closest = std::numeric_limits<float>::max();
            state.events.RescheduleEvent(Progress, Milliseconds(30000));
            state.progressX = bot->GetPositionX();
            state.progressY = bot->GetPositionY();
            state.progressZ = bot->GetPositionZ();
            float distance = bot->GetDistance(state.source.x, state.source.y, state.source.z);
            uint32 deadline = uint32(std::clamp(180.0f + distance / 2.0f, 300.0f, 3600.0f) * 1000.0f);
            state.events.RescheduleEvent(Expired, Milliseconds(deadline));
            uint32 zone = bot->GetMap()->GetZoneId(bot->GetPhaseMask(),
                state.source.x, state.source.y, state.source.z);
            ObserveCompanionFarming(botAI, state.revision, "destination_selected", 0, zone);
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
    state.events.Update(elapsed);
    while (uint32 event = state.events.ExecuteEvent())
        if (event == Decision)
            state.ready = true;
        else if (event == Retry)
        {
            state.indexed = false;
            state.sources.clear();
            state.rejected.clear();
        }
        else if ((event == Progress || event == Expired) && state.selected)
        {
            if (bot->IsInCombat())
            {
                state.events.RescheduleEvent(event, Milliseconds(30000));
                continue;
            }
            state.rejected.insert(state.source.Key());
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
