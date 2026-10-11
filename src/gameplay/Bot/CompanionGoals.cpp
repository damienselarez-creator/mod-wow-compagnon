#include "CompanionGoals.h"
#include "CompanionGoalDialogue.h"
#include "CompanionGoalStore.h"
#include "CompanionErrands.h"
#include "Bag.h"
#include "Config.h"
#include "EventMap.h"
#include "Playerbots.h"
#include "ScriptMgr.h"
#include "World.h"
#include "pbc_config.h"
#include "pbc_companion_language.h"
#include "pbc_event_dispatch.h"
#include <ctime>
#include <mutex>

namespace
{
    using namespace CompanionGoals;
    std::mutex goalsMutex;
    CompanionGoalStore history;
    constexpr std::array<char const*, size_t(Domain::Count)> names =
    {
        "bags", "class_training", "profession_training", "equipment", "maintenance", "supplies",
        "mount", "gathering", "crafting", "guild", "talents"
    };
    constexpr std::array<char const*, 8> states =
    {
        "pending", "travelling", "verifying", "waiting_money", "waiting_opportunity", "interrupted", "failed", "done"
    };
    std::string Key(Player* bot)
    {
        return std::to_string(bot->GetSession()->GetAccountId()) + ":" + bot->GetGUID().ToString();
    }
    uint64_t Now() { return uint64_t(std::time(nullptr)); }

    Book* Find(Player* bot, bool create = false)
    {
        if (!history.Ready || !bot || !bot->GetSession())
            return nullptr;
        auto found = history.Books.find(Key(bot));
        if (found != history.Books.end())
            return &found->second;
        if (!create || history.Books.size() >= 4096)
            return nullptr;
        return &history.Books[Key(bot)];
    }

    bool Authorized(Player* player, Player* bot)
    {
        auto* ai = bot ? GET_PLAYERBOT_AI(bot) : nullptr;
        return g_PBC_Enable && ai && IsCompanionInventoryManaged(ai) && player &&
            player == ai->GetMaster() && player->IsInWorld() && bot->IsInWorld() &&
            player->GetSession() && bot->GetSession() && !player->GetSession()->IsHeadless() &&
            bot->GetSession()->IsHeadless() && !player->GetSession()->IsLoggingOut() &&
            player->GetSession()->GetAccountId() == bot->GetSession()->GetAccountId() &&
            player->GetGroup() && player->GetGroup() == bot->GetGroup() &&
            player->GetMap() == bot->GetMap() && bot->IsWithinDistInMap(player, 40.0f);
    }

    Domain Topic(Player* bot)
    {
        std::lock_guard<std::mutex> lock(goalsMutex);
        auto* book = Find(bot);
        return book && book->topicUntil >= Now() ? book->topic : Domain::Count;
    }

    class GoalWorldScript : public WorldScript
    {
    public:
        GoalWorldScript() : WorldScript("wow_companion_goals") { }
        void OnStartup() override
        {
            auto path = sConfigMgr->GetOption<std::string>("WoWCompagnon.GoalsFile", "");
            if (path.empty())
                path = sWorld->GetDataPath() + "companion-goals.json";
            std::lock_guard<std::mutex> lock(goalsMutex);
            if (!history.Load(path))
                LOG_ERROR("playerbots.companion", "Companion goal history unavailable; requests cannot be stored");
            LOG_INFO("playerbots.companion", "Companion goals: history_ready={} characters={}", history.Ready, history.Books.size());
            events.ScheduleEvent(1, Milliseconds(30000));
        }
        void OnUpdate(uint32 elapsed) override
        {
            events.Update(elapsed);
            if (!events.ExecuteEvent())
                return;
            std::lock_guard<std::mutex> lock(goalsMutex);
            if (history.Ready && history.Dirty && !history.Save())
                LOG_ERROR("playerbots.companion", "Companion goal history save failed");
            events.ScheduleEvent(1, Milliseconds(30000));
        }
        void OnShutdown() override
        {
            std::lock_guard<std::mutex> lock(goalsMutex);
            if (history.Ready && history.Dirty && !history.Save())
                LOG_ERROR("playerbots.companion", "Companion goal shutdown save failed");
        }
    private:
        EventMap events;
    };
}

bool CompanionGoalPermitted(PlayerbotAI* ai, CompanionGoals::Domain domain)
{
    if (!IsCompanionInventoryManaged(ai))
        return true;
    std::lock_guard<std::mutex> lock(goalsMutex);
    auto* book = Find(ai->GetBot());
    return history.Ready && (!book || book->Permitted(domain, Now()));
}

bool CompanionGoalAllowed(PlayerbotAI* ai, CompanionGoals::Domain domain)
{
    if (!IsCompanionInventoryManaged(ai))
        return true;
    std::lock_guard<std::mutex> lock(goalsMutex);
    auto* book = Find(ai->GetBot());
    return history.Ready && (!book || book->Allowed(domain, Now(), ai->GetBot()->GetMoney()));
}

bool CompanionGoalRequested(PlayerbotAI* ai, CompanionGoals::Domain domain)
{
    std::lock_guard<std::mutex> lock(goalsMutex);
    auto* book = ai ? Find(ai->GetBot()) : nullptr;
    return book && book->goals[size_t(domain)].requested;
}

uint32_t CompanionGoalTarget(PlayerbotAI* ai, CompanionGoals::Domain domain)
{
    std::lock_guard<std::mutex> lock(goalsMutex);
    auto* book = ai ? Find(ai->GetBot()) : nullptr;
    return book ? book->goals[size_t(domain)].target : 0;
}

uint32_t CompanionGoalPriority(PlayerbotAI* ai, CompanionGoals::Domain domain)
{
    std::lock_guard<std::mutex> lock(goalsMutex);
    auto* book = ai ? Find(ai->GetBot()) : nullptr;
    return book ? book->Priority(domain, Now()) : 0;
}

uint64_t CompanionGoalRevision(PlayerbotAI* ai)
{
    std::lock_guard<std::mutex> lock(goalsMutex);
    auto* book = ai ? Find(ai->GetBot()) : nullptr;
    return book ? book->revision : 0;
}

void ObserveCompanionGoal(PlayerbotAI* ai, CompanionGoals::Domain domain, CompanionGoals::State state,
    std::string const& reason, uint32_t requiredMoney, uint32_t confirmed)
{
    if (!IsCompanionInventoryManaged(ai) || domain >= CompanionGoals::Domain::Count)
        return;
    std::lock_guard<std::mutex> lock(goalsMutex);
    auto* book = Find(ai->GetBot(), true);
    if (!book || !book->Observe(domain, state, reason, Now(), requiredMoney, confirmed))
        return;
    history.Dirty = true;
    LOG_INFO("playerbots.companion", "[Goal] {} domain={} state={} reason={} required_money={} confirmed={}",
        ai->GetBot()->GetName(), names[size_t(domain)], states[size_t(state)], reason, requiredMoney, confirmed);
    // A single factual notice when a requested objective first becomes blocked; no repeated solicitation.
    auto& goal = book->goals[size_t(domain)];
    if (state == CompanionGoals::State::Money && goal.requested && !goal.quiet)
    {
        goal.quiet = true;
        book->topic = domain;
        book->topicUntil = Now() + 600;
        uint32 missing = requiredMoney > ai->GetBot()->GetMoney() ? requiredMoney - ai->GetBot()->GetMoney() : 0;
        bool french = PBC_IsFrenchClient(PBC_CompanionClientLocale(ai->GetBot(), ai->GetMaster()));
        ai->TellMaster(std::string(french ? "Mon projet attend de l'argent : il me manque " :
            "My plan is waiting for funds: I need another ") + std::to_string(missing / 10000) +
            (french ? " or, " : " gold, ") + std::to_string((missing % 10000) / 100) +
            (french ? " argent et " : " silver and ") + std::to_string(missing % 100) +
            (french ? " cuivre, selon mon budget habituel. Je poursuis l'aventure." :
                " copper under my usual budget. I will continue the adventure."));
    }
}

bool ReconsiderCompanionGoals(PlayerbotAI* ai)
{
    std::lock_guard<std::mutex> lock(goalsMutex);
    auto* book = ai ? Find(ai->GetBot()) : nullptr;
    if (!book || !book->Reconsider(Now(), ai->GetBot()->GetMoney()))
        return false;
    history.Dirty = true;
    return true;
}

uint32_t CompanionBagCapacity(Player* bot)
{
    uint32 capacity = INVENTORY_SLOT_ITEM_END - INVENTORY_SLOT_ITEM_START;
    for (uint8 slot = INVENTORY_SLOT_BAG_START; slot < INVENTORY_SLOT_BAG_END; ++slot)
        if (Bag* bag = bot->GetBagByPos(slot); bag && !bag->GetTemplate()->BagFamily)
            capacity += bag->GetBagSize();
    return capacity;
}

std::string CompanionGoalsContext(Player* bot)
{
    auto* ai = bot ? GET_PLAYERBOT_AI(bot) : nullptr;
    if (!IsCompanionInventoryManaged(ai))
        return "";
    pbc_json facts = {{"money_copper", bot->GetMoney()}, {"general_capacity", CompanionBagCapacity(bot)}};
    facts["riding_skill"] = bot->HasSkill(SKILL_RIDING) ? bot->GetSkillValue(SKILL_RIDING) : 0;
    facts["bags"] = pbc_json::array();
    uint32 free = 0;
    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        free += !bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
    for (uint8 slot = INVENTORY_SLOT_BAG_START; slot < INVENTORY_SLOT_BAG_END; ++slot)
        if (Bag* bag = bot->GetBagByPos(slot))
        {
            facts["bags"].push_back({{"capacity", bag->GetBagSize()}, {"free", bag->GetFreeSlots()},
                {"specialized", bool(bag->GetTemplate()->BagFamily)}});
            if (!bag->GetTemplate()->BagFamily)
                free += bag->GetFreeSlots();
        }
    facts["general_free"] = free;
    facts["goals"] = pbc_json::object();
    {
        std::lock_guard<std::mutex> lock(goalsMutex);
        facts["free_talent_points"] = bot->GetFreeTalentPoints();
        facts["goal_history_ready"] = history.Ready;
        if (auto* book = Find(bot))
        {
            facts["last_request_saved"] = book->lastRequestSaved;
            for (size_t i = 0; i < book->goals.size(); ++i)
            {
                auto const& goal = book->goals[i];
                facts["goals"][names[i]] = {{"requested", goal.requested}, {"forbidden", goal.forbidden},
                    {"deferred", Now() < goal.deferredUntil}, {"state", states[size_t(goal.state)]},
                    {"reason", goal.reason}, {"required_money_copper", goal.requiredMoney},
                    {"missing_money_copper", goal.requiredMoney > bot->GetMoney() ? goal.requiredMoney - bot->GetMoney() : 0},
                    {"confirmed_operations", goal.confirmed}, {"quiet", goal.quiet}};
            }
        }
    }
    return "\n[COMPANION OBJECTIVES AND VERIFIED INVENTORY]\n" + facts.dump() +
        "\nExplain these facts in the requested language and your established personality. "
        "A missing/false requested flag means a request has not been saved. If goal_history_ready or last_request_saved is false, say the plan could not be saved. "
        "Only confirmed_operations attest completed native actions; travelling/pending are intentions. "
        "Never invent bag names, vendor prices, purchases, learned spells or completed projects. "
        "A zero required_money means no verified quote yet, not a free purchase. "
        "When deferred/quiet, accept the constraint and continue the adventure without asking for money again. "
        "Forbidden means do not perform this activity until a new explicit request. "
        "A request creates a plan; if location, combat, trade, tools, materials or funds prevent it, explain "
        "without promising immediate execution. You do not execute gameplay through your free-text reply.\n";
}

bool PBC_CompanionGoalChat(Player* player, Player* bot, std::string const& message)
{
    using namespace CompanionGoals;
    if (!Authorized(player, bot))
        return false;
    auto intent = Parse(message, Topic(bot));
    if (!intent)
        return false;
    bool saved = false;
    {
        std::lock_guard<std::mutex> lock(goalsMutex);
        if (auto* book = Find(bot, true))
        {
            auto previous = *book;
            uint32 target = intent->domain == Domain::Bags ? CompanionBagCapacity(bot) + 1 : 0;
            book->Apply(intent->domain, intent->directive, Now(), target);
            auto words = PBCVocationPolicy::Words(message);
            if (intent->domain == Domain::ClassTraining && PBCVocationPolicy::Has(words, "maitres") &&
                !PBCVocationPolicy::Has(words, "classe"))
                book->Apply(Domain::ProfessionTraining, intent->directive, Now());
            if (intent->directive == Directive::Status)
                saved = true;
            else
            {
                history.Dirty = true;
                saved = history.Save();
                if (!saved)
                    *book = previous;
                book->lastRequestSaved = saved;
            }
        }
    }
    if (saved && intent->directive != Directive::Status)
    {
        auto* ai = GET_PLAYERBOT_AI(bot);
        CancelCompanionErrands(ai);
        if (intent->directive == Directive::Request || intent->directive == Directive::Resume)
        {
            SetCompanionNarrativeFocus(bot->GetGUID(), "normal");
            RequestCompanionTraining(ai);
        }
    }
    if (!saved)
        LOG_ERROR("playerbots.companion", "[Goal] {} request persistence failed", bot->GetName());
    return true;
}

bool PBC_CompanionGoalConversation(Player* player, std::vector<Player*> const& bots, std::string const& message)
{
    std::vector<Player*> eligible, named, topics;
    auto words = PBCVocationPolicy::Words(message);
    for (Player* bot : bots)
        if (Authorized(player, bot))
        {
            eligible.push_back(bot);
            if (PBCVocationPolicy::Has(words, bot->GetName()))
                named.push_back(bot);
            if (Topic(bot) != CompanionGoals::Domain::Count)
                topics.push_back(bot);
        }
    Player* target = named.size() == 1 ? named.front() : named.empty() && topics.size() == 1 ? topics.front() :
        named.empty() && topics.empty() && eligible.size() == 1 ? eligible.front() : nullptr;
    if (!target || !PBC_CompanionGoalChat(player, target, message))
        return false;
    PBC_DispatchWhisperEvent(player, target, message);
    return true;
}

void AddCompanionGoalScripts()
{
    new GoalWorldScript();
}
