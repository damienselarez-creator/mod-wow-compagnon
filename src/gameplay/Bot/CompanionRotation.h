#ifndef WOW_COMPAGNON_ROTATION_H
#define WOW_COMPAGNON_ROTATION_H

#include "CompanionRotationPolicy.h"
#include "Action.h"

class PlayerbotAI;
CompanionRotation::Situation BuildCompanionRotationSituation(PlayerbotAI* ai);
float CompanionRotationPriority(PlayerbotAI* ai, Action* action, float relevance,
    CompanionRotation::Situation const& situation);
std::vector<NextAction> CompanionRotationDefaults(PlayerbotAI* ai);
#endif
