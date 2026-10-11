#include "pbc_json.h"
#include <algorithm>
#include <cstdint>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using uint32_t = std::uint32_t;
using int32_t = std::int32_t;
struct AreaTableEntry
{
    uint32_t zone;
    char const* area_name[1];
};
struct Store
{
    std::map<uint32_t, AreaTableEntry> entries;
    AreaTableEntry const* LookupEntry(uint32_t id) const
    {
        auto entry = entries.find(id);
        return entry == entries.end() ? nullptr : &entry->second;
    }
} sAreaTableStore;
std::string_view PBC_DbcString(char const* const* values) { return values[0]; }
std::string PBC_Localize(char const* value) { return value; }
std::map<int, std::string> g_PBC_WmoAreaNames;
std::string PBC_GetWmoAreaName(int32_t, int32_t, int32_t group) { return g_PBC_WmoAreaNames[group]; }
struct Map
{
    bool indoors = true;
    bool GetAreaInfo(uint32_t, float, float, float, uint32_t& flags, int32_t& adt,
        int32_t& root, int32_t& group)
    {
        flags = 0;
        adt = 1;
        root = 1150;
        group = 3061;
        return indoors;
    }
};
struct Player
{
    uint32_t area = 1497, zone = 1497;
    bool moving = false, flight = false, teleport = false;
    Map map;
    uint32_t GetAreaId() const { return area; }
    uint32_t GetZoneId() const { return zone; }
    uint32_t GetMapId() const { return 0; }
    uint32_t GetPhaseMask() const { return 1; }
    float GetPositionX() const { return 0; }
    float GetPositionY() const { return 0; }
    float GetPositionZ() const { return 0; }
    Map* GetMap() { return &map; }
    bool isMoving() const { return moving; }
    bool IsInFlight() const { return flight; }
    bool IsBeingTeleported() const { return teleport; }
};
#include "location.inc"

void Check(bool value, char const* reason)
{
    if (!value)
        throw std::runtime_error(reason);
}
int main()
{
    Player player;
    // City ID and WMO group from the installed WotLK DBC, not a character record.
    sAreaTableStore.entries[1497] = {0, {"Fossoyeuse"}};
    g_PBC_WmoAreaNames[3061] = "Quartier de la guerre";
    Check(PBC_BuildPlaceName(&player) == "Quartier de la guerre (Fossoyeuse)", "District lost its city");
    Check(PBC_BuildZoneName(&player) == "Fossoyeuse", "District replaced the current city");
    auto context = PBC_BuildLocationContext(&player);
    auto first = context.find('{');
    auto facts = pbc_json::parse(context.substr(first));
    Check(facts["zone_name"] == "Fossoyeuse" && facts["zone_id"] == 1497, "Current zone not explicit");
    Check(facts["local_place"] == "Quartier de la guerre (Fossoyeuse)", "Place hierarchy missing");
    Check(facts["moving"] == false && !facts.contains("waiting_for"), "Invented purpose for immobility");
    player.area = 999999;
    Check(PBC_BuildPlaceName(&player) == "Quartier de la guerre (Fossoyeuse)", "Missing area lost live zone");
    player.map.indoors = false;
    Check(PBC_BuildPlaceName(&player) == "Fossoyeuse", "Outdoor fallback retained interior name");
    sAreaTableStore.entries[10] = {20, {"Village"}};
    sAreaTableStore.entries[20] = {10, {"Region"}};
    player.area = 10;
    Check(PBC_BuildPlaceName(&player) == "Village (Region, Fossoyeuse)", "Cyclic parents repeated places");
    player.area = 30;
    sAreaTableStore.entries[30] = {1497, {"District"}};
    player.map.indoors = true;
    g_PBC_WmoAreaNames[3061] = "Fossoyeuse";
    Check(PBC_BuildPlaceName(&player) == "District (Fossoyeuse)", "Container WMO duplicated its city");
    player.area = 1497;
    sAreaTableStore.entries[1497] = {0, {"Undercity"}};
    g_PBC_WmoAreaNames[3061] = "War Quarter";
    Check(PBC_BuildPlaceName(&player) == "War Quarter (Undercity)", "English hierarchy changed");
    player.flight = true;
    Check(PBC_BuildLocationContext(&player).find("\"taxi_flight\":true") != std::string::npos,
        "Flight confused with standing in a city");
    player.area = 0;
    player.zone = 0;
    player.map.indoors = false;
    Check(PBC_BuildPlaceName(&player) == "Unknown" && PBC_BuildZoneName(&player) == "Unknown",
        "Missing location invented a city");
    std::cout << "PASS: actual location functions, WMO containment, fallback, cycle, English and unknown\n";
}
