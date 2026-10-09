#include "pbc_vocation.h"
#include "pbc_personality.h"
#include "pbc_config.h"
#include "pbc_world.h"
#include "pbc_commands.h"
#include "pbc_log.h"
#include "pbc_player_scripts.h"
#include "pbc_group_scripts.h"
#include "pbc_quest_scripts.h"
#include "pbc_adventure.h"

void AddCompanionNarrativeScripts()
{
    PBC_Log(PBC_LogLevel::PBC_DEFAULT, "Registering WoW Compagnon narrative scripts.");

    new PBC_WorldScript();
    new PBC_PlayerEvents();
    new PBC_GroupEvents();
    new PBC_AllCreatureQuestScript();
    new PBC_AllGameObjectQuestScript();
    new PBC_AllItemQuestScript();
    new PBC_CommandScript();
    AddPBCAdventureScripts();
    AddPBCVocationScripts();
    AddPBCPersonalityScripts();
}

