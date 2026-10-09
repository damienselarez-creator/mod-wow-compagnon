#include "pbc_companion_language.h"
#include "Player.h"
#include "Playerbots.h"
#include "WorldSession.h"

uint8_t PBC_CompanionClientLocale(Player* companion, Player* listener)
{
    // Capture on the map thread; queued work uses only the copied locale value.
    auto localeOf = [](Player* player) -> int
    {
        auto* session = player ? player->GetSession() : nullptr;
        return session && !session->IsHeadless() ? session->GetSessionDbLocaleIndex() : -1;
    };
    int listenerLocale = localeOf(listener);
    if (listenerLocale >= 0)
        return PBC_SelectCompanionClientLocale(listenerLocale, -1, -1);
    auto* ai = companion ? GET_PLAYERBOT_AI(companion) : nullptr;
    return PBC_SelectCompanionClientLocale(-1, localeOf(ai ? ai->GetMaster() : nullptr), localeOf(companion));
}
