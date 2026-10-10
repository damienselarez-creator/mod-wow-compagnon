#include <cassert>
#include <initializer_list>
#include <string>

constexpr int BOT_STATE_NON_COMBAT = 0, PLAYER_FLAGS = 0, PLAYER_FLAGS_GHOST = 1;
struct Event { };
struct Motion
{
    int cleared = 0;
    void Clear() { ++cleared; }
};
struct Unit
{
    bool alive = true, ghost = false, battleground = false;
    int stopped = 0;
    Motion motion;
    bool IsAlive() const { return alive; }
    bool HasFlag(int, int) const { return ghost; }
    bool InBattleground() const { return battleground; }
    Motion* GetMotionMaster() { return &motion; }
    void StopMoving() { ++stopped; }
};
struct PlayerbotAI
{
    Unit* master = nullptr;
    bool companion = true, follow = true, stay = false;
    int announcements = 0, strategyChanges = 0;
    Unit* GetMaster() const { return master; }
    bool HasStrategy(std::string const&, int) const { return follow; }
    void TellMasterNoFacing(std::string const&) { ++announcements; }
    void ChangeStrategy(std::string const&, int) { follow = false; stay = true; ++strategyChanges; }
};
bool IsCompanionInventoryManaged(PlayerbotAI* ai) { return ai->companion && ai->master; }
struct PlayerbotTextMgr
{
    static PlayerbotTextMgr& instance() { static PlayerbotTextMgr mgr; return mgr; }
    std::string GetBotTextOrDefault(char const*, char const* fallback, std::initializer_list<int>) { return fallback; }
};
struct ReleaseSpiritAction
{
    Unit* bot;
    PlayerbotAI* botAI;
    bool Execute(Event event);
};
struct FollowAction
{
    Unit* bot;
    PlayerbotAI* botAI;
    bool CanDeadFollow(Unit* target);
};
#include "production.inc"

int main()
{
    Unit bot, master;
    PlayerbotAI ai{&master};
    ReleaseSpiritAction release{&bot, &ai};
    FollowAction follow{&bot, &ai};
    master.alive = false;
    master.ghost = true;
    assert(!release.Execute({}));
    assert(ai.follow && !ai.stay && ai.strategyChanges == 0);
    assert(bot.stopped == 1 && bot.motion.cleared == 1 && ai.announcements == 1);
    assert(!follow.CanDeadFollow(&master));
    // Resurrection elsewhere: no CMSG_RECLAIM_CORPSE event is delivered to the bot.
    master.alive = true;
    master.ghost = false;
    assert(follow.CanDeadFollow(&master) && ai.follow);
    // Delayed release event after resurrection must not stop or pin the living bot.
    release.Execute({});
    assert(bot.stopped == 1 && ai.strategyChanges == 0);
    // A deliberate stay order survives both the wait and the master's return.
    ai.follow = false;
    ai.stay = true;
    master.alive = false;
    release.Execute({});
    master.alive = true;
    assert(!ai.follow && ai.stay && ai.strategyChanges == 0);
    // Ghosts still walk together; a dead bot with a living master returns to its corpse.
    bot.alive = false;
    master.alive = false;
    master.ghost = true;
    assert(follow.CanDeadFollow(&master));
    master.alive = true;
    assert(!follow.CanDeadFollow(&master));
    bot.battleground = true;
    master.alive = false;
    assert(!follow.CanDeadFollow(&master));
    // Independent random bots keep their existing release behavior.
    bot.alive = true;
    bot.battleground = false;
    ai.companion = false;
    ai.follow = true;
    ai.stay = false;
    release.Execute({});
    assert(ai.stay && !ai.follow && ai.strategyChanges == 1);
}
