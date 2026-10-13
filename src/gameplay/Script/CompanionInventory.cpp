/*
 * WoW Compagnon, GNU GPL v2 or later. See AUTHORS.
 */
#include "CompanionErrands.h"
#include "Playerbots.h"
#include "PlayerScript.h"

namespace
{
    class CompanionInventoryScript : public PlayerScript
    {
    public:
        CompanionInventoryScript() : PlayerScript("wow_companion_inventory") { }

        void OnPlayerStoreNewItem(Player* player, Item*, uint32) override
        {
            NotifyCompanionInventoryChanged(GET_PLAYERBOT_AI(player));
        }

        void OnPlayerAfterMoveItemFromInventory(Player* player, Item*, uint8, uint8, bool) override
        {
            NotifyCompanionInventoryChanged(GET_PLAYERBOT_AI(player));
        }

        void OnPlayerAfterMoveItemToInventory(Player* player, Item*, bool) override
        {
            NotifyCompanionInventoryChanged(GET_PLAYERBOT_AI(player));
        }

        void OnPlayerEquip(Player* player, Item*, uint8, uint8, bool) override
        {
            NotifyCompanionInventoryChanged(GET_PLAYERBOT_AI(player));
        }

        void OnPlayerUnequip(Player* player, Item*) override
        {
            NotifyCompanionInventoryChanged(GET_PLAYERBOT_AI(player));
        }

        void OnPlayerAfterStoreOrEquipNewItem(Player* player, uint32, Item*, uint8, uint8, uint8,
            ItemTemplate const*, Creature*, VendorItem const*, bool) override
        {
            NotifyCompanionInventoryChanged(GET_PLAYERBOT_AI(player));
        }
    };
}

void AddCompanionInventoryScripts()
{
    new CompanionInventoryScript();
}
