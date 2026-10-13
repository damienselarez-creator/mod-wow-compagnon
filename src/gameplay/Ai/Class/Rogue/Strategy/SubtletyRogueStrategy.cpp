/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "SubtletyRogueStrategy.h"

class SubtletyRogueStrategyActionNodeFactory : public NamedObjectFactory<ActionNode>
{
public:
    SubtletyRogueStrategyActionNodeFactory()
    {
        creators["hemorrhage"] = &hemorrhage;
        creators["backstab"] = &backstab;
        creators["rupture"] = &rupture;
    }

private:
    static ActionNode* hemorrhage(PlayerbotAI*)
    {
        return new ActionNode("hemorrhage", {}, {NextAction("backstab")}, {});
    }
    static ActionNode* backstab(PlayerbotAI*)
    {
        return new ActionNode("backstab", {}, {NextAction("sinister strike")}, {});
    }
    static ActionNode* rupture(PlayerbotAI*)
    {
        return new ActionNode("rupture", {}, {NextAction("eviscerate")}, {});
    }
};

SubtletyRogueStrategy::SubtletyRogueStrategy(PlayerbotAI* botAI) : GenericRogueStrategy(botAI)
{
    actionNodeFactories.Add(new SubtletyRogueStrategyActionNodeFactory());
}

std::vector<NextAction> SubtletyRogueStrategy::getDefaultActions()
{
    return {
        NextAction("hemorrhage", 5.7f), NextAction("backstab", 5.6f),
        NextAction("shadow dance", 5.5f), NextAction("melee", ACTION_DEFAULT)
    };
}

void SubtletyRogueStrategy::InitTriggers(std::vector<TriggerNode*>& triggers)
{
    GenericRogueStrategy::InitTriggers(triggers);
    triggers.push_back(new TriggerNode("high energy available", {NextAction("premeditation", ACTION_HIGH + 8),
        NextAction("ambush", ACTION_HIGH + 7), NextAction("garrote", ACTION_HIGH + 6)}));
    triggers.push_back(new TriggerNode("slice and dice", {NextAction("slice and dice", ACTION_HIGH + 5)}));
    triggers.push_back(new TriggerNode("combo points 5 available", {NextAction("eviscerate", ACTION_HIGH + 4),
        NextAction("rupture", ACTION_HIGH + 3)}));
    triggers.push_back(new TriggerNode("target with combo points almost dead", {
        NextAction("eviscerate", ACTION_HIGH + 6)}));
    triggers.push_back(new TriggerNode("enemy out of melee", {NextAction("shadowstep", ACTION_MOVE + 2)}));
    triggers.push_back(new TriggerNode("medium threat", {NextAction("vanish", ACTION_HIGH + 9)}));
    triggers.push_back(new TriggerNode("low health", {NextAction("evasion", ACTION_MOVE + 5),
        NextAction("cloak of shadows", ACTION_MOVE + 4)}));
    triggers.push_back(new TriggerNode("critical health", {NextAction("evasion", ACTION_EMERGENCY + 2),
        NextAction("cloak of shadows", ACTION_EMERGENCY + 1)}));
}
