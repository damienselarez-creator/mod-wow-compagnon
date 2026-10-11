/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */

#include "EnemyHealerTargetValue.h"
#include "CompanionCombat.h"
#include "Playerbots.h"
#include "ServerFacade.h"

Unit* EnemyHealerTargetValue::Calculate()
{
    std::string const spell = qualifier;

    GuidVector attackers = botAI->GetAiObjectContext()->GetValue<GuidVector>("attackers")->Get();
    Unit* target = botAI->GetAiObjectContext()->GetValue<Unit*>("current target")->Get();
    Unit* dangerous = nullptr;
    float best = -1.0f;
    bool refined = RefinedCompanionCombat(botAI);
    for (ObjectGuid const guid : attackers)
    {
        Unit* unit = botAI->GetUnit(guid);
        if (!unit || unit == target)
            continue;

        if (ServerFacade::instance().GetDistance2d(bot, unit) > botAI->GetRange("spell"))
            continue;

        if (!botAI->IsInterruptableSpellCasting(unit, spell))
            continue;

        if (refined)
        {
            uint32 id = botAI->GetAiObjectContext()->GetValue<uint32>("spell id", spell)->Get();
            auto const* info = sSpellMgr->GetSpellInfo(id);
            if (!info || !botAI->CanCastSpell(id, unit) || !bot->IsWithinLOSInMap(unit))
                continue;
            for (auto type : {CURRENT_GENERIC_SPELL, CURRENT_CHANNELED_SPELL})
            {
                Spell* cast = unit->GetCurrentSpell(type);
                if (!cast)
                    continue;
                Player* victim = ObjectAccessor::FindPlayer(cast->m_targets.GetUnitTargetGUID());
                bool threatensGroup = victim && victim->IsInWorld() && victim->IsAlive() &&
                    victim->GetMap() == bot->GetMap() && victim->IsInSameGroupWith(bot);
                float score = cast->m_spellInfo->IsPositive() ? 50.0f : 20.0f;
                if (threatensGroup)
                    score += 100.0f - victim->GetHealthPct() + (botAI->IsHeal(victim) ? 30.0f : 0.0f);
                // Among equally dangerous casts, stop the one closest to completion.
                score += 10.0f / (1.0f + std::max(0, cast->GetCastTimeRemaining()) / 1000.0f);
                if (score > best)
                {
                    best = score;
                    dangerous = unit;
                }
            }
            continue;
        }
        Spell* spell = unit->GetCurrentSpell(CURRENT_GENERIC_SPELL);
        if (spell && spell->m_spellInfo->IsPositive())
            return unit;

        spell = unit->GetCurrentSpell(CURRENT_CHANNELED_SPELL);
        if (spell && spell->m_spellInfo->IsPositive())
            return unit;
    }

    return dangerous;
}
