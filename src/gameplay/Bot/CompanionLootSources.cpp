/*
 * WoW Compagnon, GNU GPL v2 or later. See the preserved component author notices.
 */
#include "CompanionLootSources.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "Field.h"
#include "Log.h"
#include "MySQLConnection.h"
#include "MySQLPreparedStatement.h"
#include "PreparedStatement.h"
#include "QueryResult.h"
#include <atomic>
#include <memory>

class CompanionLootConnection : public MySQLConnection
{
public:
    explicit CompanionLootConnection(MySQLConnectionInfo& info) : MySQLConnection(info) { }

    void DoPrepareStatements() override
    {
        if (!m_reconnecting)
            m_stmts.resize(4);

        PrepareStatement(0, "SELECT Entry,Item,Reference,Chance,QuestRequired,LootMode,GroupId,MaxCount "
            "FROM creature_loot_template", CONNECTION_SYNCH);
        PrepareStatement(1, "SELECT Entry,Item,Reference,Chance,QuestRequired,LootMode,GroupId,MaxCount "
            "FROM skinning_loot_template", CONNECTION_SYNCH);
        PrepareStatement(2, "SELECT Entry,Item,Reference,Chance,QuestRequired,LootMode,GroupId,MaxCount "
            "FROM gameobject_loot_template", CONNECTION_SYNCH);
        PrepareStatement(3, "SELECT Entry,Item,Reference,Chance,QuestRequired,LootMode,GroupId,MaxCount "
            "FROM reference_loot_template", CONNECTION_SYNCH);
    }

    inline static std::atomic<std::shared_ptr<CompanionLootSourceIndex const>> index;
};

bool CompanionLootSources::Load()
{
    // This short-lived, read-only connection runs before map threads start.
    // It never writes the world database or changes native loot rolls.
    MySQLConnectionInfo info(sConfigMgr->GetOption<std::string>("WorldDatabaseInfo", ""));
    CompanionLootConnection connection(info);
    if (connection.Open() || !connection.PrepareStatements())
    {
        LOG_ERROR("playerbots.companion", "[Companion] Loot source index unavailable; gathering search disabled");
        return false;
    }

    auto index = std::make_shared<CompanionLootSourceIndex>();
    uint64 count = 0;
    for (uint32 kind = 0; kind < 4; ++kind)
    {
        PreparedStatement<CompanionLootConnection> statement(kind, 0);
        PreparedQueryResult result(connection.Query(&statement));
        if (!result || !result->GetRowCount())
            continue;

        do
        {
            Field* fields = result->Fetch();
            int64 reference = fields[2].Get<int32>();
            if (reference < 0)
                reference = -reference;
            int32 maxCount = fields[7].Get<uint8>();
            index->Add(static_cast<CompanionLootSourceIndex::Kind>(kind), fields[0].Get<uint32>(),
                {fields[1].Get<uint32>(), static_cast<uint32>(reference), fields[3].Get<float>(),
                 fields[4].Get<bool>(), fields[5].Get<uint16>(), fields[6].Get<uint8>(),
                 maxCount > 0 ? static_cast<uint32>(maxCount) : 0});
            ++count;
        } while (result->NextRow());
    }
    CompanionLootConnection::index.store(std::move(index));
    LOG_INFO("server.loading", "[Companion] Native-compatible loot source index: {} rows", count);
    return true;
}

bool CompanionLootSources::Has(CompanionLootSourceIndex::Kind kind, uint32_t entry, uint32_t item)
{
    auto index = CompanionLootConnection::index.load();
    return index && index->Has(kind, entry, item);
}
