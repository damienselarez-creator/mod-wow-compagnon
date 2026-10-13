#include "CompanionRotationPolicy.h"
#include <algorithm>
#include <cassert>
#include <ctime>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <vector>

using uint32 = uint32_t;
struct Unit { };
struct Event { };
struct NextAction
{
    std::string name;
    float relevance;
    NextAction(std::string n, float r = 0) : name(n), relevance(r) { }
};
struct Action
{
    std::string name;
    bool useful = true, possible = true, succeeds = true;
    CompanionRotation::Ability ability;
    std::string getName() const { return name; }
    bool isUseful() const { return useful; }
    bool isPossible() const { return possible; }
    void setRelevance(float) { }
};
struct ActionNode
{
    std::string name;
    std::vector<NextAction> prerequisites, alternatives, continuers;
    explicit ActionNode(std::string n) : name(n) { }
    std::string getName() const { return name; }
    auto getPrerequisites() const { return prerequisites; }
    auto getAlternatives() const { return alternatives; }
    auto getContinuers() const { return continuers; }
};
struct ActionBasket
{
    ActionNode* action;
    float relevance;
    bool skip;
    Event event;
    ActionBasket(ActionNode* a, float r, bool s, Event e = {}) : action(a), relevance(r), skip(s), event(e) { }
    ActionNode* getAction() const { return action; }
    float getRelevance() const { return relevance; }
    void setRelevance(float r) { relevance = r; }
    bool isSkipPrerequisites() const { return skip; }
    Event getEvent() const { return event; }
    bool isExpired(uint32) const { return false; }
};
struct Config { uint32 expireActionTime = 0, iterationsPerTick = 4; bool logValuesPerTick = false; } sPlayerbotAIConfig;
#include "queue_class.inc"
#include "queue.inc"
struct Rebuff { void RollBuffPendingCycle() { } };
struct PlayerbotAI { Rebuff forceRebuff; CompanionRotation::Situation situation; };
CompanionRotation::Situation BuildCompanionRotationSituation(PlayerbotAI* ai) { return ai->situation; }
float CompanionRotationPriority(PlayerbotAI*, Action* a, float r, CompanionRotation::Situation const& s)
{ return a ? CompanionRotation::Priority(s, a->ability, a->name, r) : r; }
struct Multiplier
{
    float GetValue(Action*) const { return 1; }
    std::string getName() const { return "native"; }
};
constexpr int PERF_MON_ACTION = 1;
struct PerfMonitorOperation { void finish() { } };
struct PerfMonitor
{
    PerfMonitorOperation* start(int, std::string const&, int*) { return nullptr; }
} sPerfMonitor;
struct Context { int performanceStack = 0; };
struct Engine
{
    PlayerbotAI* botAI;
    Queue queue;
    std::vector<Multiplier*> multipliers;
    std::map<std::string, Action> nativeActions;
    Context context;
    Context* aiObjectContext = &context;
    float lastRelevance = 0;
    std::vector<std::string> executed;
    explicit Engine(PlayerbotAI* ai) : botAI(ai) { }
    ~Engine() { while (auto* node = queue.Pop()) delete node; }
    bool DoNextAction(Unit*, uint32, bool);
    void LogAction(char const*, ...) { }
    void LogValues() { }
    void ProcessTriggers(bool) { }
    void PushDefaultActions() { }
    Action* InitializeAction(ActionNode* node)
    {
        auto found = nativeActions.find(node->name);
        return found == nativeActions.end() ? nullptr : &found->second;
    }
    void Add(std::string name, float relevance, bool possible = true)
    {
        nativeActions[name] = {name, true, possible};
        queue.Push(new ActionBasket(new ActionNode(name), relevance, false));
    }
    bool MultiplyAndPush(std::vector<NextAction> const& actions, float force, bool skip, Event event, char const*)
    {
        for (auto const& a : actions)
            queue.Push(new ActionBasket(new ActionNode(a.name), force > 0 ? force : a.relevance, skip, event));
        return !actions.empty();
    }
    void PushAgain(ActionNode* node, float r, Event e)
    { queue.Push(new ActionBasket(node, r, true, e)); }
    bool ListenAndExecute(Action* action, Event)
    {
        if (action->succeeds)
            executed.push_back(action->name);
        return action->succeeds;
    }
};
#include "engine.inc"

int main()
{
    PlayerbotAI ai;
    ai.situation.enabled = true;
    ai.situation.characterClass = 1;
    ai.situation.tank = true;
    {
        Engine engine(&ai);
        engine.Add("devastate", 25);
        engine.Add("shield slam", 5);
        assert(engine.queue.Peek()->getAction()->name == "devastate");
        assert(engine.DoNextAction(nullptr, 0, false));
        assert(engine.executed.front() == "shield slam"); // Actual Engine selects the dynamic winner.
    }
    {
        Engine engine(&ai);
        engine.Add("shield slam", 5, false);
        engine.Add("revenge", 5);
        engine.nativeActions["shield slam"].ability.cooldownRemaining = 2000;
        assert(engine.DoNextAction(nullptr, 0, false));
        assert(engine.executed.front() == "revenge");
        engine.nativeActions["shield slam"].possible = true;
        engine.nativeActions["shield slam"].ability.cooldownRemaining = 0;
        assert(engine.DoNextAction(nullptr, 0, false));
        assert(engine.executed.back() == "shield slam"); // Re-evaluates readiness on the next decision.
    }
    {
        ai.situation.enabled = false;
        Engine engine(&ai);
        engine.Add("devastate", 25);
        engine.Add("shield slam", 5);
        assert(engine.DoNextAction(nullptr, 0, false));
        assert(engine.executed.front() == "devastate"); // Unmanaged/disabled behaviour is unchanged.
        ai.situation.enabled = true;
    }
    {
        Engine engine(&ai);
        engine.Add("native unavailable action", 70, false);
        engine.Add("native alternate", 1);
        auto* original = engine.queue.Peek()->getAction();
        original->alternatives.emplace_back("native alternate");
        assert(engine.DoNextAction(nullptr, 0, false));
        assert(engine.executed.front() == "native alternate"); // Native fallback path remains intact.
    }
    {
        Engine engine(&ai);
        engine.Add("shield slam", 5);
        engine.nativeActions["native stance"] = {"native stance"};
        auto* original = engine.queue.Peek()->getAction();
        original->prerequisites.emplace_back("native stance");
        assert(engine.DoNextAction(nullptr, 0, false));
        assert(engine.executed.front() == "native stance");
        assert(engine.DoNextAction(nullptr, 0, false));
        assert(engine.executed.back() == "shield slam");
    }
    {
        Engine engine(&ai);
        ai.situation.rage = 100;
        engine.Add("heroic strike", 30);
        auto& dump = engine.nativeActions["heroic strike"].ability;
        dump.rageCost = true;
        dump.cost = 150;
        assert(!engine.DoNextAction(nullptr, 0, false));
        assert(engine.queue.Size() == 1); // A temporarily unsuitable action is retained for a later decision.
        ai.situation.rage = 350;
        assert(engine.DoNextAction(nullptr, 0, false));
    }
    {
        Engine engine(&ai);
        engine.Add("unknown native mechanic", 100);
        assert(engine.DoNextAction(nullptr, 0, true));
        assert(engine.executed.front() == "unknown native mechanic");
    }
    {
        Engine engine(&ai);
        engine.queue.Push(new ActionBasket(new ActionNode("missing action"), 100, false));
        engine.Add("devastate", 5);
        assert(engine.DoNextAction(nullptr, 0, false));
        assert(engine.executed.front() == "devastate"); // A missing context action retains the native null path.
    }
}
