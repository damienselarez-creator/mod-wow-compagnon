/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */
#include "CompanionGuildBank.h"
#include "CompanionGuildBankStore.h"
#include "CompanionErrands.h"
#include "Bag.h"
#include "CastCustomSpellAction.h"
#include "Config.h"
#include "DBCStores.h"
#include "GuildMgr.h"
#include "ItemUsageValue.h"
#include "ObjectAccessor.h"
#include "Playerbots.h"
#include "PlayerbotOperation.h"
#include "PlayerbotSpellRepository.h"
#include "PlayerbotWorldThreadProcessor.h"
#include "ScriptMgr.h"
#include "StatsWeightCalculator.h"
#include "UseItemAction.h"
#include "World.h"
#include <atomic>
#include <ctime>
#include <mutex>
#include <set>

namespace
{
    using Kind = CompanionSharing::Kind;
    std::atomic<bool> enabled{false};
    CompanionGuildBankStore history;
    std::mutex stockMutex;
    std::map<uint32, std::map<uint32, uint32>> sharedStock;
    std::map<ObjectGuid, uint64> nextVisit;
    std::map<uint32, uint64> stockObserved;
    constexpr uint64 VisitInterval = 300;

    struct BankItem
    {
        uint8 tab = 0;
        uint8 slot = 0;
        uint32 entry = 0;
        uint32 count = 0;
        int32 withdrawals = 0;
    };

    // Observe only this synchronous native query, including socket-less bot sessions.
    // The scope holds no item pointers and never captures unrelated players' packets.
    thread_local WorldSession* queriedSession = nullptr;
    thread_local std::vector<BankItem>* queriedItems = nullptr;

    class BankPacketScript : public ServerScript
    {
    public:
        BankPacketScript() : ServerScript("wow_companion_bank_snapshot") { }

        void OnPacketSent(WorldSession* session, WorldPacket const& packet) override
        {
            if (session != queriedSession || !queriedItems || packet.GetOpcode() != SMSG_GUILD_BANK_LIST)
                return;
            try
            {
                WorldPacket data(packet);
                data.rpos(0);
                uint64 money;
                uint8 tab, full, slots;
                int32 withdrawals;
                data >> money >> tab >> withdrawals >> full;
                if (!full || tab >= GUILD_BANK_MAX_TABS)
                    return;
                if (!tab)
                {
                    uint8 tabs;
                    data >> tabs;
                    for (uint8 index = 0; index < tabs; ++index)
                    {
                        std::string name, icon;
                        data >> name >> icon;
                    }
                }
                data >> slots;
                std::vector<BankItem> items;
                for (uint8 index = 0; index < slots; ++index)
                {
                    uint8 slot;
                    uint32 entry;
                    data >> slot >> entry;
                    if (!entry)
                        continue;
                    int32 flags, property, seed, count, enchantment;
                    uint8 charges, sockets;
                    data >> flags >> property;
                    if (property)
                        data >> seed;
                    data >> count >> enchantment >> charges >> sockets;
                    for (uint8 socket = 0; socket < sockets; ++socket)
                    {
                        uint8 position;
                        int32 gem;
                        data >> position >> gem;
                    }
                    if (slot < GUILD_BANK_MAX_SLOTS && count > 0)
                        items.push_back({tab, slot, entry, uint32(count), withdrawals});
                }
                queriedItems->insert(queriedItems->end(), items.begin(), items.end());
            }
            catch (...)
            {
                // Malformed or incompatible snapshots never authorize a withdrawal.
            }
        }
    };

    std::vector<BankItem> ReadTab(Guild* guild, Player* player, uint8 tab)
    {
        std::vector<BankItem> items;
        if (!guild->MemberHasTabRights(player->GetGUID(), tab, GUILD_BANK_RIGHT_VIEW_TAB))
            return items;
        struct QueryScope
        {
            QueryScope(WorldSession* session, std::vector<BankItem>* output)
            {
                queriedSession = session;
                queriedItems = output;
            }
            ~QueryScope()
            {
                queriedSession = nullptr;
                queriedItems = nullptr;
            }
        } query(player->GetSession(), &items);
        guild->SendBankTabData(player->GetSession(), tab, true);
        return items;
    }

    std::vector<BankItem> ReadBank(Guild* guild, Player* player)
    {
        std::vector<BankItem> result;
        for (uint8 tab = 0; tab < GUILD_BANK_MAX_TABS; ++tab)
        {
            auto items = ReadTab(guild, player, tab);
            result.insert(result.end(), items.begin(), items.end());
        }
        std::map<uint32, uint32> stock;
        for (auto const& item : result)
            stock[item.entry] += item.count;
        std::lock_guard<std::mutex> lock(stockMutex);
        sharedStock[guild->GetId()] = std::move(stock);
        stockObserved[guild->GetId()] = uint64(std::time(nullptr));
        return result;
    }

    bool CanWithdraw(Guild* guild, Player* player, BankItem const& item)
    {
        return item.withdrawals != 0 &&
            guild->MemberHasTabRights(player->GetGUID(), item.tab, GUILD_BANK_RIGHT_VIEW_TAB);
    }

    Kind ItemKind(ItemTemplate const* item)
    {
        switch (item->Class)
        {
            case ITEM_CLASS_CONTAINER: return Kind::Bags;
            case ITEM_CLASS_GEM: return item->GemProperties ? Kind::Gems : Kind::Materials;
            case ITEM_CLASS_ARMOR: return Kind::Armor;
            case ITEM_CLASS_WEAPON: return Kind::Weapons;
            case ITEM_CLASS_RECIPE: return Kind::Recipes;
            case ITEM_CLASS_CONSUMABLE: return Kind::Consumables;
            default: return item->TotemCategory ? Kind::Tools : Kind::Materials;
        }
    }

    bool NeedsRecipe(Player* player, ItemTemplate const* item)
    {
        if (item->Class != ITEM_CLASS_RECIPE || player->CanUseItem(item) != EQUIP_ERR_OK)
            return false;
        // The native recipe item uses its learned spell in the third item-spell slot.
        if (item->Spells[2].SpellId > 0)
        {
            uint32 spell = uint32(item->Spells[2].SpellId);
            auto const* line = PlayerbotSpellRepository::Instance().GetSkillLine(spell);
            return line && IsProfessionSkill(line->SkillLine) && player->HasSkill(line->SkillLine) &&
                !player->HasSpell(spell);
        }
        for (auto const& itemSpell : item->Spells)
            if (auto const* spell = sSpellMgr->GetSpellInfo(itemSpell.SpellId))
                for (auto const& effect : spell->GetEffects())
                    if (effect.IsEffect(SPELL_EFFECT_LEARN_SPELL) && !player->HasSpell(effect.TriggerSpell))
                        if (auto const* line = PlayerbotSpellRepository::Instance().GetSkillLine(effect.TriggerSpell))
                            if (IsProfessionSkill(line->SkillLine) && player->HasSkill(line->SkillLine))
                                return true;
        return false;
    }

    Item* EmptyGemSocket(Player* player, ItemTemplate const* item)
    {
        auto const* gem = sGemPropertiesStore.LookupEntry(item->GemProperties);
        if (!gem || player->CanUseItem(item) != EQUIP_ERR_OK)
            return nullptr;
        StatsWeightCalculator score(player);
        if (score.CalculateEnchant(gem->spellitemenchantement) <= 0.0f)
            return nullptr;
        for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
            if (Item* equipment = player->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                for (uint32 socket = 0; socket < MAX_GEM_SOCKETS; ++socket)
                    if ((gem->color & equipment->GetTemplate()->Socket[socket].Color) &&
                        !equipment->GetEnchantmentId(EnchantmentSlot(SOCK_ENCHANTMENT_SLOT + socket)))
                        return equipment;
        return nullptr;
    }

    bool NeedsArticle(PlayerbotAI* ai, ItemTemplate const* item)
    {
        Player* player = ai->GetBot();
        uint32 held = player->GetItemCount(item->ItemId, true);
        if (item->Class == ITEM_CLASS_CONTAINER)
            for (uint8 slot = INVENTORY_SLOT_BAG_START; slot < INVENTORY_SLOT_BAG_END; ++slot)
                if (Bag* bag = player->GetBagByPos(slot))
                    if (bag->GetEntry() == item->ItemId && held)
                        --held;
        if (held || player->CanUseItem(item) != EQUIP_ERR_OK)
            return false;
        if (item->Class == ITEM_CLASS_RECIPE)
            return NeedsRecipe(player, item);
        if (item->Class == ITEM_CLASS_GEM)
            return EmptyGemSocket(player, item) != nullptr;
        if (item->Class == ITEM_CLASS_CONTAINER)
        {
            if (item->BagFamily)
                return false;
            for (uint8 slot = INVENTORY_SLOT_BAG_START; slot < INVENTORY_SLOT_BAG_END; ++slot)
            {
                Bag* bag = player->GetBagByPos(slot);
                if (!bag || (!bag->GetTemplate()->BagFamily && bag->GetBagSize() < item->ContainerSlots))
                    return true;
            }
            return false;
        }
        if (item->TotemCategory)
            return !player->HasItemTotemCategory(item->TotemCategory) &&
                ai->GetAiObjectContext()->GetValue<ItemUsage>("item usage", item->ItemId)->Get() == ITEM_USAGE_SKILL;
        ItemUsage usage = ai->GetAiObjectContext()->GetValue<ItemUsage>("item usage", item->ItemId)->Get();
        return usage == ITEM_USAGE_EQUIP || usage == ITEM_USAGE_REPLACE || usage == ITEM_USAGE_USE;
    }

    struct InventoryPlan
    {
        std::map<uint32, uint32> retained;
        std::set<uint32> products;
        std::set<uint32> protectedItems;
    };

    InventoryPlan PlanInventory(Player* player)
    {
        InventoryPlan plan;
        for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
            if (auto const* quest = sObjectMgr->GetQuestTemplate(player->GetQuestSlotQuestId(slot)))
            {
                for (uint32 item : quest->RequiredItemId)
                    if (item)
                        plan.protectedItems.insert(item);
                if (quest->GetSrcItemId())
                    plan.protectedItems.insert(quest->GetSrcItemId());
            }
        for (auto const& [id, known] : player->GetSpellMap())
        {
            if (known->State == PLAYERSPELL_REMOVED || !known->Active)
                continue;
            auto const* spell = sSpellMgr->GetSpellInfo(id);
            if (!spell)
                continue;
            for (uint32 tool : spell->Totem)
                if (tool)
                    plan.retained[tool] = 1;
            for (uint32 index = 0; index < MAX_SPELL_REAGENTS; ++index)
                if (spell->Reagent[index] > 0 && spell->ReagentCount[index])
                    plan.retained[uint32(spell->Reagent[index])] =
                        std::max(CompanionSharing::RetainedReagents, spell->ReagentCount[index]);
            auto const* line = PlayerbotSpellRepository::Instance().GetSkillLine(id);
            if (line && IsProfessionSkill(line->SkillLine) && player->HasSkill(line->SkillLine))
                for (auto const& effect : spell->GetEffects())
                    if (effect.IsEffect(SPELL_EFFECT_CREATE_ITEM) && effect.ItemType)
                        plan.products.insert(effect.ItemType);
        }
        return plan;
    }

    uint32 DepositSurplus(PlayerbotAI* ai, Item* item, InventoryPlan const& plan)
    {
        Player* player = ai->GetBot();
        auto const* proto = item->GetTemplate();
        if (!Player::IsInventoryPos(item->GetBagSlot(), item->GetSlot()) || !item->CanBeTraded() ||
            item->IsInTrade() || item->IsWrapped() || proto->StartQuest || proto->Class == ITEM_CLASS_QUEST ||
            plan.protectedItems.count(item->GetEntry()))
            return 0;
        uint32 keep = 0;
        if (auto found = plan.retained.find(item->GetEntry()); found != plan.retained.end())
            keep = found->second;
        ItemUsage usage = ai->GetAiObjectContext()->GetValue<ItemUsage>("item usage", item->GetEntry())->Get();
        if (usage == ITEM_USAGE_KEEP || usage == ITEM_USAGE_QUEST)
            return 0;
        if (proto->TotemCategory)
            keep = std::max(keep, uint32(1));
        bool eligible = plan.products.count(item->GetEntry()) || proto->Class == ITEM_CLASS_TRADE_GOODS ||
            proto->Class == ITEM_CLASS_REAGENT || proto->Class == ITEM_CLASS_RECIPE || proto->Class == ITEM_CLASS_GEM;
        if (!eligible)
            return 0;
        if (proto->Class == ITEM_CLASS_CONTAINER)
        {
            if (Bag* bag = item->ToBag())
                if (!bag->IsEmpty())
                    return 0;
            if (NeedsArticle(ai, proto) || usage == ITEM_USAGE_EQUIP || usage == ITEM_USAGE_REPLACE)
                return 0;
        }
        else if (proto->Class == ITEM_CLASS_RECIPE)
            keep = std::max(keep, uint32(NeedsRecipe(player, proto)));
        else if (proto->Class == ITEM_CLASS_GEM && proto->GemProperties)
            keep = std::max(keep, uint32(EmptyGemSocket(player, proto) != nullptr));
        else if (usage == ITEM_USAGE_EQUIP || usage == ITEM_USAGE_REPLACE)
            return 0;
        else if (usage == ITEM_USAGE_USE || usage == ITEM_USAGE_SKILL || usage == ITEM_USAGE_AMMO)
            keep = std::max(keep, CompanionSharing::RetainedReagents);
        return std::min(item->GetCount(),
            CompanionSharing::Surplus(player->GetItemCount(item->GetEntry(), false), keep));
    }

    bool Withdraw(Guild* guild, Player* player, BankItem const& item, uint32 count)
    {
        auto current = ReadTab(guild, player, item.tab);
        auto found = std::find_if(current.begin(), current.end(), [&](BankItem const& live)
        {
            return live.slot == item.slot && live.entry == item.entry && live.count >= count &&
                CanWithdraw(guild, player, live);
        });
        if (!count || found == current.end())
            return false;
        ItemPosCountVec destination;
        if (player->CanStoreNewItem(NULL_BAG, NULL_SLOT, destination, item.entry, count) != EQUIP_ERR_OK)
            return false;
        uint32 before = player->GetItemCount(item.entry, false);
        guild->SwapItemsWithInventory(player, true, item.tab, item.slot, NULL_BAG, NULL_SLOT, count);
        return player->GetItemCount(item.entry, false) > before;
    }

    class BankUseItem : public UseItemAction
    {
    public:
        explicit BankUseItem(PlayerbotAI* ai) : UseItemAction(ai, "companion bank use", true) { }
        bool Learn(Item* item) { return UseItemAuto(item); }
        bool Socket(Item* equipment, Item* gem) { return SocketItem(equipment, gem, false); }
    };

    bool WithdrawArticle(Guild* guild, PlayerbotAI* ai, std::vector<BankItem> const& items,
        std::set<uint32> const& donated, uint64 now)
    {
        Player* player = ai->GetBot();
        for (auto const& item : items)
        {
            auto const* proto = sObjectMgr->GetItemTemplate(item.entry);
            if (!proto || donated.count(item.entry) || ItemKind(proto) == Kind::Materials ||
                !NeedsArticle(ai, proto) || !CanWithdraw(guild, player, item))
                continue;
            Kind kind = ItemKind(proto);
            if (!history.Reserve(player->GetGUID().GetCounter(), kind, now))
                continue;
            if (!Withdraw(guild, player, item, CompanionSharing::TakeCount(kind, item.count, 1)))
            {
                history.Cancel(player->GetGUID().GetCounter(), kind);
                continue;
            }
            LOG_INFO("playerbots", "Companion guild bank: {} takes item={} count=1; weekly family={}",
                player->GetName(), item.entry, uint32(kind));
            if (kind == Kind::Recipes || kind == Kind::Gems)
                for (Item* owned : ai->GetInventoryItems())
                    if (owned->GetEntry() == item.entry)
                    {
                        BankUseItem use(ai);
                        if (kind == Kind::Recipes)
                            use.Learn(owned);
                        else if (Item* equipment = EmptyGemSocket(player, proto))
                            use.Socket(equipment, owned);
                        break;
                    }
            return true;
        }
        return false;
    }

    bool WithdrawMaterials(Guild* guild, PlayerbotAI* ai, std::vector<BankItem> const& items, uint64 now)
    {
        Player* player = ai->GetBot();
        if (!history.Ready || !history.Quotas.Available(
            CompanionSharing::Key(player->GetGUID().GetCounter(), Kind::Materials), now))
            return false;
        std::map<uint32, uint32> available;
        for (auto const& item : items)
            if (CanWithdraw(guild, player, item))
                available[item.entry] += item.count;
        CraftRandomItemAction crafting(ai);
        std::map<uint32, uint32> basket;
        for (auto const& [id, known] : player->GetSpellMap())
        {
            if (known->State == PLAYERSPELL_REMOVED || !known->Active)
                continue;
            auto const* spell = sSpellMgr->GetSpellInfo(id);
            auto const* line = PlayerbotSpellRepository::Instance().GetSkillLine(id);
            if (!spell || !line || !IsProfessionSkill(line->SkillLine) || !player->HasSkill(line->SkillLine) ||
                spell->Effects[EFFECT_0].Effect != SPELL_EFFECT_CREATE_ITEM || crafting.GetSpellPriority(spell) != 100)
                continue;
            std::map<uint32, uint32> needed;
            bool possible = true;
            for (uint32 index = 0; index < MAX_SPELL_REAGENTS; ++index)
            {
                if (spell->Reagent[index] <= 0 || !spell->ReagentCount[index])
                    continue;
                uint32 entry = uint32(spell->Reagent[index]);
                uint32 held = player->GetItemCount(entry, false);
                uint32 missing = spell->ReagentCount[index] > held ? spell->ReagentCount[index] - held : 0;
                if (CompanionSharing::TakeCount(Kind::Materials, available[entry], missing) < missing)
                    possible = false;
                if (missing)
                    needed[entry] = missing;
            }
            if (possible && !needed.empty())
            {
                basket = std::move(needed);
                break;
            }
        }
        if (basket.empty() || !history.Reserve(player->GetGUID().GetCounter(), Kind::Materials, now))
            return false;
        bool moved = false;
        for (auto const& item : items)
        {
            auto found = basket.find(item.entry);
            if (found == basket.end() || !found->second)
                continue;
            uint32 amount = std::min(found->second, item.count);
            if (Withdraw(guild, player, item, amount))
            {
                found->second -= amount;
                moved = true;
            }
        }
        if (!moved)
            history.Cancel(player->GetGUID().GetCounter(), Kind::Materials);
        return moved;
    }

    class BankOperation : public PlayerbotOperation
    {
    public:
        BankOperation(ObjectGuid character, ObjectGuid owner, ObjectGuid bank)
            : _character(character), _owner(owner), _bank(bank) { }

        ObjectGuid GetBotGuid() const override { return _character; }
        std::string GetName() const override { return "Companion guild bank sharing"; }

        bool Execute() override
        {
            Player* player = ObjectAccessor::FindPlayer(_character);
            Player* master = ObjectAccessor::FindPlayer(_owner);
            auto* ai = player ? GET_PLAYERBOT_AI(player) : nullptr;
            if (!enabled.load() || !ai || !master || !player->IsInWorld() || !master->IsInWorld() ||
                ai->GetMaster() != master || !IsCompanionInventoryManaged(ai) || !IsManagedCompanion(ai) ||
                player->IsInCombat() || master->IsInCombat() || !player->IsAlive() || !master->IsAlive() ||
                player->GetGroup() != master->GetGroup() || !player->GetGroup() ||
                player->GetGuildId() != master->GetGuildId() || !player->GetGuildId() ||
                player->GetMap() != master->GetMap() || player->GetTradeData() ||
                player->IsNonMeleeSpellCast(false) || master->isMoving() || player->isMoving())
                return false;
            GameObject* bank = ai->GetGameObject(_bank);
            if (!bank || !player->GetGameObjectIfCanInteractWith(_bank, GAMEOBJECT_TYPE_GUILD_BANK) ||
                master->GetDistance(bank) > 30.0f)
                return false;
            Guild* guild = sGuildMgr->GetGuildById(player->GetGuildId());
            uint64 now = uint64(std::time(nullptr));
            if (!guild)
                return false;
            {
                std::lock_guard<std::mutex> lock(stockMutex);
                if (nextVisit[_character] > now)
                    return false;
                nextVisit[_character] = now + VisitInterval;
            }
            auto plan = PlanInventory(player);
            std::set<uint32> donated;
            uint32 deposits = 0;
            for (Item* item : ai->GetInventoryItems())
            {
                uint32 amount = DepositSurplus(ai, item, plan);
                if (!amount)
                    continue;
                uint32 entry = item->GetEntry();
                for (uint8 tab = 0; tab < GUILD_BANK_MAX_TABS; ++tab)
                {
                    if (!guild->MemberHasTabRights(_character, tab, GUILD_BANK_RIGHT_DEPOSIT_ITEM))
                        continue;
                    uint32 before = player->GetItemCount(entry, false);
                    uint8 bag = item->GetBagSlot();
                    uint8 slot = item->GetSlot();
                    guild->SwapItemsWithInventory(player, false, tab, NULL_SLOT, bag, slot, amount);
                    if (player->GetItemCount(entry, false) < before)
                    {
                        donated.insert(entry);
                        ++deposits;
                        LOG_INFO("playerbots", "Companion guild bank: {} deposits item={} count={}",
                            player->GetName(), entry, amount);
                        break;
                    }
                }
                if (deposits >= 6)
                    break;
            }
            auto items = ReadBank(guild, player);
            if (!WithdrawArticle(guild, ai, items, donated, now))
                WithdrawMaterials(guild, ai, items, now);
            ReadBank(guild, player);
            return true;
        }

    private:
        ObjectGuid _character;
        ObjectGuid _owner;
        ObjectGuid _bank;
    };

    class BankWorldScript : public WorldScript
    {
    public:
        BankWorldScript() : WorldScript("wow_companion_guild_bank") { }

        void OnStartup() override
        {
            bool requested = sConfigMgr->GetOption<bool>("WoWCompagnon.GuildBank.Enabled", false);
            auto path = sConfigMgr->GetOption<std::string>("WoWCompagnon.GuildBank.HistoryFile", "");
            if (path.empty())
                path = sWorld->GetDataPath() + "companion-guild-bank-history.json";
            if (requested && !history.Load(path))
                LOG_ERROR("playerbots", "Guild sharing history unavailable; automatic withdrawals disabled");
            enabled.store(requested);
            LOG_INFO("playerbots", "Companion guild sharing: enabled={}, durable weekly history={}",
                requested, history.Ready);
        }
    };
}

bool CompanionGuildBankEnabled()
{
    return enabled.load();
}

bool CompanionGuildBankVisitDue(ObjectGuid character)
{
    if (!enabled.load())
        return false;
    std::lock_guard<std::mutex> lock(stockMutex);
    return nextVisit[character] <= uint64(std::time(nullptr));
}

bool QueueCompanionGuildBank(PlayerbotAI* ai, ObjectGuid bank)
{
    if (!CompanionGuildBankEnabled() || !IsCompanionInventoryManaged(ai) || !IsManagedCompanion(ai))
        return false;
    return PlayerbotWorldThreadProcessor::instance().QueueOperation(
        std::make_unique<BankOperation>(ai->GetBot()->GetGUID(), ai->GetMaster()->GetGUID(), bank));
}

bool CompanionGuildBankHasProduct(uint32 guild, uint32 item)
{
    if (!enabled.load() || !guild)
        return false;
    std::lock_guard<std::mutex> lock(stockMutex);
    auto found = sharedStock.find(guild);
    if (found == sharedStock.end() || uint64(std::time(nullptr)) > stockObserved[guild] + 600)
        return false;
    auto product = found->second.find(item);
    return product != found->second.end() && product->second != 0;
}

void AddCompanionGuildBankScripts()
{
    new BankWorldScript();
    new BankPacketScript();
}
