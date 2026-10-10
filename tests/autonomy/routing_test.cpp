#include <cassert>

struct Event { };
struct Creature { };
struct PlayerbotAI { bool managed = true; };
bool IsCompanionInventoryManaged(PlayerbotAI* ai) { return ai->managed; }
Creature* requested = nullptr;
bool accepted = true;
bool RequestCompanionTrainerVisit(PlayerbotAI*, Creature* npc)
{
    requested = npc;
    return accepted;
}
struct Position
{
    Creature* npc;
    Creature* GetCreature() { return npc; }
};
struct RpgHelper
{
    Creature visited;
    bool scheduled = false;
    Position guidP() { return {&visited}; }
    void AfterExecute(bool result, bool) { scheduled = result; }
};
struct RpgSubAction
{
    int legacyCalls = 0;
    bool Execute(Event) { ++legacyCalls; return true; }
};
struct RpgTrainAction : RpgSubAction
{
    PlayerbotAI* botAI;
    RpgHelper* rpg;
    bool Execute(Event event);
};
#include "routing.inc"

int main()
{
    PlayerbotAI ai;
    RpgHelper rpg;
    RpgTrainAction action{{}, &ai, &rpg};
    assert(action.Execute({}));
    assert(requested == &rpg.visited && rpg.scheduled);
    assert(action.legacyCalls == 0); // Never use the human's selected target for an autonomous visit.
    accepted = false;
    assert(!action.Execute({}));
    assert(!rpg.scheduled); // A refused purchase plan cannot become a successful visit.
    ai.managed = false;
    assert(action.Execute({}));
    assert(action.legacyCalls == 1); // Random bots retain their original behaviour.
}
