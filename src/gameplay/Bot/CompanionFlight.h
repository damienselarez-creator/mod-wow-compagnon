#ifndef COMPANION_FLIGHT_H
#define COMPANION_FLIGHT_H

#include <string>

class Player;
class PlayerbotAI;

bool RequestCompanionFlight(PlayerbotAI* ai);
bool HasPendingCompanionFlight(PlayerbotAI* ai);
std::string CompanionFlightContext(Player* bot);

#endif
