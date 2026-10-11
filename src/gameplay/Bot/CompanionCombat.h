#ifndef COMPANION_COMBAT_H
#define COMPANION_COMBAT_H

#include "Define.h"

class PlayerbotAI;
class Unit;
class SpellInfo;
bool RefinedCompanionCombat(PlayerbotAI* ai);
bool CompanionCombatSpellAllowed(PlayerbotAI* ai, Unit* target, SpellInfo const* spell);
uint32 CompanionIncomingHealing(PlayerbotAI* ai, Unit* target);
float CompanionHealingScore(PlayerbotAI* ai, Unit* target);
float CompanionRescueUrgency(PlayerbotAI* ai, Unit* enemy);
void UpdateCompanionCombat(PlayerbotAI* ai);
#endif
