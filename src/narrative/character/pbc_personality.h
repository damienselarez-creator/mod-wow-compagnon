#ifndef PBC_PERSONALITY_H
#define PBC_PERSONALITY_H

#include "pbc_json.h"
#include <cstdint>
#include <string>

class Player;
bool PBC_LoadPersonality(std::string const& catalogPath, std::string const& statePath, std::string& status);
pbc_json PBC_PersonalitySheet(Player* companion);
std::string PBC_PersonalityContext(pbc_json const& sheet, uint8_t gender, uint8_t locale);
void AddPBCPersonalityScripts();

#endif
