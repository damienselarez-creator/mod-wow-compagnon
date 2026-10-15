#include "CompanionErrands.h"
#include "CompanionActivityPolicy.h"
#include "CompanionGoals.h"
#include "CompanionFlight.h"
#include "PlayerbotFactory.h"
#include "CompanionGuildBank.h"
#include "CompanionEquipment.h"
#include "CompanionInventoryPolicy.h"
#include "CompanionConsumables.h"
#include "CompanionProfessions.h"
#include "CompanionLootSources.h"
#include "CompanionFarming.h"
#include "StatsWeightCalculator.h"
#include "CompanionVocation.h"
#include "CastCustomSpellAction.h"

#include "Bag.h"
#include "CellImpl.h"
#include "CompanionErrandPolicy.h"
#include "EventMap.h"
#include "InventoryPolicy.h"
#include "ItemPackets.h"
#include "ItemUsageValue.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "LootObjectStack.h"
#include "MotionMaster.h"
#include "Playerbots.h"
#include "PlayerbotSpellRepository.h"
#include "Trainer.h"
#include "NearestGameObjects.h"
#include "UseItemAction.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <mutex>
#include <map>
#include <chrono>

namespace
{
    std::mutex narrativeFocusMutex;
    std::map<ObjectGuid, std::string> narrativeFocus;

    using TrainingClock = std::chrono::steady_clock;
    std::mutex trainingMutex;
    std::map<ObjectGuid, std::pair<TrainingClock::time_point, CompanionTrainingSnapshot>> trainingSnapshots;

    void RecordTraining(Player* bot, std::string const& status, uint32 trainer, uint32 spell, uint32 learned)
    {
        std::lock_guard<std::mutex> lock(trainingMutex);
        if (trainingSnapshots.size() >= 4096 && !trainingSnapshots.count(bot->GetGUID()))
            trainingSnapshots.erase(trainingSnapshots.begin());
        trainingSnapshots[bot->GetGUID()] = {TrainingClock::now(),
            {status, trainer, spell, learned, bot->GetMoney(), 0}};
    }

    enum Timer : uint32
    {
        Decision = 1, TownScan, HerbScan, Deadline, Progress, Resume, ClearRejected, VerifyLesson,
        MountRetry, InventoryScan, CraftCooldown, CraftReset, TownRest
    };
    enum class Errand
    {
        None, Trainer, Herb, Return, PoisonVendor, Bags, ProfessionVendor, GuildBank, MountVendor, EquipmentVendor, Craft, BagVendor
    };
    using GoalDomain = CompanionGoals::Domain;
    using GoalState = CompanionGoals::State;
    constexpr float TownRadius = 1500.0f;

    bool Eligible(PlayerbotAI* ai)
    {
        if (HasPendingCompanionFlight(ai))
            return false;
        Player* bot = ai->GetBot();
        Player* master = ai->GetMaster();
        if (!IsCompanionInventoryManaged(ai) || !master || !master->IsInWorld() || !master->GetSession() ||
            master->GetSession()->IsLoggingOut() || !master->IsAlive() || !bot->IsAlive() ||
            master->IsBeingTeleported() || bot->IsBeingTeleported() || master->GetMap() != bot->GetMap() ||
            !(master->GetPhaseMask() & bot->GetPhaseMask()) || bot->InBattleground() || bot->GetMap()->IsDungeon() ||
            bot->IsInCombat() || master->IsInCombat() || bot->IsFlying() || master->IsFlying() ||
            bot->IsFalling() || master->IsFalling() || bot->IsInFlight() || master->IsInFlight() ||
            bot->GetTransport() || master->GetTransport() || bot->GetVehicle() || master->GetVehicle() ||
            bot->GetTradeData() || master->GetTradeData() ||
            !ai->HasStrategy("follow", BOT_STATE_NON_COMBAT) ||
            ai->HasStrategy("stay", BOT_STATE_NON_COMBAT) || ai->HasStrategy("passive", BOT_STATE_NON_COMBAT))
            return false;
        if (Group* group = bot->GetGroup())
            for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
                if (Player* member = ref->GetSource())
                    if (member->IsInCombat())
                        return false;
        return bot->GetGroup() && bot->GetGroup() == master->GetGroup();
    }

    uint32 Settlement(uint32 areaId, uint32 zoneId)
    {
        auto const* zone = sAreaTableStore.LookupEntry(zoneId);
        if (zone && (zone->flags & AREA_FLAG_CAPITAL))
            return zoneId;
        auto const* area = sAreaTableStore.LookupEntry(areaId);
        if (area && (area->flags & (AREA_FLAG_TOWN | AREA_FLAG_CAPITAL)))
            return areaId;
        return 0;
    }

    uint32 Settlement(Player* player)
    {
        return Settlement(player->GetAreaId(), player->GetZoneId());
    }

    bool InTown(Player* player)
    {
        return Settlement(player) || player->HasFlag(PLAYER_FLAGS, PLAYER_FLAGS_RESTING);
    }

    bool IsSecondaryProfession(uint32 skill)
    {
        return skill == SKILL_FISHING || skill == SKILL_COOKING || skill == SKILL_FIRST_AID;
    }

    bool IsProfession(uint32 skill)
    {
        return CompanionErrands::IsPrimary(skill) || IsSecondaryProfession(skill);
    }

    uint32 ProfessionForSpell(uint32 id, uint32 depth = 0)
    {
        if (depth > 3)
            return 0;
        auto const* line = PlayerbotSpellRepository::Instance().GetSkillLine(id);
        if (line && IsProfession(line->SkillLine))
            return line->SkillLine;
        auto const* spell = sSpellMgr->GetSpellInfo(id);
        if (spell)
            for (auto const& effect : spell->Effects)
            {
                if (effect.Effect == SPELL_EFFECT_SKILL && IsProfession(effect.MiscValue))
                    return effect.MiscValue;
                if (effect.Effect == SPELL_EFFECT_LEARN_SPELL)
                    if (uint32 skill = ProfessionForSpell(effect.TriggerSpell, depth + 1))
                        return skill;
            }
        return 0;
    }

    bool TeachesRiding(uint32 id, uint32 depth = 0)
    {
        if (depth > 3)
            return false;
        auto const* line = PlayerbotSpellRepository::Instance().GetSkillLine(id);
        if (line && line->SkillLine == SKILL_RIDING)
            return true;
        if (auto const* spell = sSpellMgr->GetSpellInfo(id))
            for (auto const& effect : spell->Effects)
            {
                if (effect.Effect == SPELL_EFFECT_SKILL && effect.MiscValue == SKILL_RIDING)
                    return true;
                if (effect.Effect == SPELL_EFFECT_LEARN_SPELL && TeachesRiding(effect.TriggerSpell, depth + 1))
                    return true;
            }
        return false;
    }

    struct MountProfile
    {
        uint32 spell = 0;
        int32 speed = 0;
        bool flying = false;
    };

    MountProfile DescribeMount(uint32 id)
    {
        auto const* spell = sSpellMgr->GetSpellInfo(id);
        if (!spell || !spell->HasAura(SPELL_AURA_MOUNTED))
            return {};
        MountProfile result{id};
        for (auto const& effect : spell->Effects)
        {
            if (effect.ApplyAuraName == SPELL_AURA_MOD_INCREASE_MOUNTED_FLIGHT_SPEED)
            {
                result.flying = true;
                result.speed = std::max(result.speed, effect.BasePoints);
            }
            else if (effect.ApplyAuraName == SPELL_AURA_MOD_INCREASE_MOUNTED_SPEED)
                result.speed = std::max(result.speed, effect.BasePoints);
        }
        return result;
    }

    MountProfile MountFromItem(ItemTemplate const* item)
    {
        if (item)
            for (auto const& spell : item->Spells)
                if (spell.SpellTrigger == ITEM_SPELLTRIGGER_LEARN_SPELL_ID && spell.SpellId > 0)
                    return DescribeMount(uint32(spell.SpellId));
        return {};
    }

    bool NeedsMount(PlayerbotAI* ai, MountProfile const& candidate)
    {
        if (!candidate.spell || candidate.speed <= 0)
            return false;
        Player* bot = ai->GetBot();
        for (auto const& [id, known] : bot->GetSpellMap())
        {
            if (known->State == PLAYERSPELL_REMOVED || !known->Active)
                continue;
            auto mount = DescribeMount(id);
            if (mount.spell && mount.flying == candidate.flying && mount.speed >= candidate.speed)
                return false;
        }
        // An unconsumed purchase must block another colour or another vendor's equivalent.
        for (Item* item : ai->GetInventoryItems())
        {
            auto mount = MountFromItem(item->GetTemplate());
            if (mount.spell && mount.flying == candidate.flying && mount.speed >= candidate.speed)
                return false;
        }
        return true;
    }

    class MountItemUse : public UseItemAction
    {
    public:
        explicit MountItemUse(PlayerbotAI* ai) : UseItemAction(ai, "companion mount use", true) { }
        bool Learn(Item* item) { return UseItemAuto(item); }
    };

    bool Protected(PlayerbotAI* ai, Item* item)
    {
        Player* player = ai->GetBot();
        auto const* proto = item->GetTemplate();
        if (!Player::IsInventoryPos(item->GetBagSlot(), item->GetSlot()) || item->IsNotEmptyBag() ||
            !proto->SellPrice || proto->StartQuest || proto->Class == ITEM_CLASS_QUEST ||
            (proto->Class == ITEM_CLASS_CONSUMABLE && proto->SubClass != ITEM_SUBCLASS_SCROLL) ||
            proto->Class == ITEM_CLASS_PROJECTILE ||
            proto->Class == ITEM_CLASS_KEY || proto->TotemCategory || item->IsInTrade() ||
            item->IsWrapped())
            return true;
        for (uint8 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
        {
            Quest const* quest = sObjectMgr->GetQuestTemplate(player->GetQuestSlotQuestId(slot));
            if (!quest)
                continue;
            for (uint32 id : quest->RequiredItemId)
                if (id == proto->ItemId)
                    return true;
            if (quest->GetSrcItemId() == proto->ItemId)
                return true;
        }
        // Keep reagents and tools for every known spell, including later profession plans.
        for (auto const& pair : player->GetSpellMap())
        {
            if (pair.second->State == PLAYERSPELL_REMOVED)
                continue;
            SpellInfo const* spell = sSpellMgr->GetSpellInfo(pair.first);
            if (!spell)
                continue;
            for (int32 reagent : spell->Reagent)
                if (reagent > 0 && uint32(reagent) == proto->ItemId)
                    return true;
            for (uint32 tool : spell->Totem)
                if (tool == proto->ItemId)
                    return true;
        }
        for (uint32 slot = 0; slot < MAX_INSPECTED_ENCHANTMENT_SLOT; ++slot)
            if (item->GetEnchantmentId(EnchantmentSlot(slot)))
                return true;
        return false;
    }

    uint32 FreeGeneralBagSlots(Player* bot)
    {
        uint32 free = 0;
        for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
            if (!bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                ++free;
        for (uint8 slot = INVENTORY_SLOT_BAG_START; slot < INVENTORY_SLOT_BAG_END; ++slot)
            if (Bag* bag = bot->GetBagByPos(slot))
                if (!bag->GetTemplate()->BagFamily)
                    free += bag->GetFreeSlots();
        return free;
    }

    bool NeedsBagSpace(Player* bot)
    {
        return SelfbotInventoryPolicy::NeedsSpace(FreeGeneralBagSlots(bot), CompanionBagCapacity(bot));
    }

    bool NeedsEquipmentRepair(Player* bot)
    {
        for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
            if (Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                if (SelfbotInventoryPolicy::NeedsRepair(item->GetUInt32Value(ITEM_FIELD_MAXDURABILITY),
                    item->GetUInt32Value(ITEM_FIELD_DURABILITY)))
                    return true;
        return false;
    }

    bool HasBagRoom(Player* bot)
    {
        uint32 free = 0;
        for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
            if (!bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                ++free;
        for (uint8 slot = INVENTORY_SLOT_BAG_START; slot < INVENTORY_SLOT_BAG_END; ++slot)
            if (Bag* bag = bot->GetBagByPos(slot))
                if (!bag->GetTemplate()->BagFamily)
                    free += bag->GetFreeSlots();
        return free >= 2;
    }

    std::array<uint32, 2> PoisonStock(Player* bot)
    {
        std::array<uint32, 2> stock{};
        auto count = [&](Item* item)
        {
            if (!item || bot->CanUseItem(item->GetTemplate()) != EQUIP_ERR_OK)
                return;
            int family = CompanionErrands::PoisonFamily(item->GetTemplate()->Name1);
            if (family >= 0)
                stock[family] += item->GetCount();
        };
        for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
            count(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot));
        for (uint8 slot = INVENTORY_SLOT_BAG_START; slot < INVENTORY_SLOT_BAG_END; ++slot)
            if (Bag* bag = bot->GetBagByPos(slot))
                for (uint32 i = 0; i < bag->GetBagSize(); ++i)
                    count(bag->GetItemByPos(i));
        return stock;
    }

    struct PoisonPurchase
    {
        uint32 item = 0;
        uint32 slot = 0;
        uint32 batches = 0;
    };

    PoisonPurchase ChoosePoison(Player* bot, VendorItemData const* goods, uint32 reserve,
        std::array<uint32, 2> const& stock, Creature* vendor = nullptr)
    {
        if (bot->getClass() != CLASS_ROGUE || !goods || goods->Empty())
            return {};
        std::array<ItemTemplate const*, 2> best{};
        std::array<uint32, 2> slots{};
        for (uint32 slot = 0; slot < goods->GetItemCount(); ++slot)
        {
            auto const* offer = goods->GetItem(slot);
            auto const* item = offer ? sObjectMgr->GetItemTemplate(offer->item) : nullptr;
            if (!item || offer->ExtendedCost || item->BuyPrice < 0 ||
                bot->CanUseItem(item) != EQUIP_ERR_OK)
                continue;
            int family = CompanionErrands::PoisonFamily(item->Name1);
            if (family < 0 || stock[family] >= 3 || (vendor && offer->maxcount &&
                vendor->GetVendorItemCurrentCount(offer) < item->BuyCount))
                continue;
            if (!best[family] || item->RequiredLevel > best[family]->RequiredLevel)
            {
                best[family] = item;
                slots[family] = slot;
            }
        }
        for (size_t family = 0; family < best.size(); ++family)
        {
            auto const* item = best[family];
            if (!item)
                continue;
            float discount = vendor ? bot->GetReputationPriceDiscount(vendor) : 1.0f;
            // Full-price affordability is conservative: the native purchase discounts the entire batch.
            uint32 batches = CompanionErrands::PoisonBatches(stock[family], item->BuyCount,
                item->BuyPrice, bot->GetMoney(), reserve);
            auto const* offer = goods->GetItem(slots[family]);
            if (vendor && offer->maxcount && item->BuyCount)
                batches = std::min(batches, vendor->GetVendorItemCurrentCount(offer) / item->BuyCount);
            ItemPosCountVec dest;
            if (batches && bot->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, item->ItemId,
                batches * item->BuyCount) == EQUIP_ERR_OK &&
                CompanionErrands::CanSpend(bot->GetMoney(),
                    uint32(std::floor(uint64(item->BuyPrice) * batches * discount)), reserve))
                return {item->ItemId, slots[family], batches};
        }
        return {};
    }

    PoisonPurchase ChooseEquipment(PlayerbotAI* ai, VendorItemData const* goods, uint32 reserve,
        Creature* vendor = nullptr, bool budget = true)
    {
        if (!goods || goods->Empty())
            return {};
        Player* bot = ai->GetBot();
        PoisonPurchase best;
        float bestGain = 0;
        uint32 bestPrice = std::numeric_limits<uint32>::max();
        StatsWeightCalculator calculator(bot);
        calculator.SetItemSetBonus(false);
        calculator.SetOverflowPenalty(false);
        for (uint32 slot = 0; slot < goods->GetItemCount(); ++slot)
        {
            auto const* offer = goods->GetItem(slot);
            auto const* item = offer ? sObjectMgr->GetItemTemplate(offer->item) : nullptr;
            if (!item || offer->ExtendedCost || item->BuyCount != 1 || item->BuyPrice <= 0 ||
                item->Quality > ITEM_QUALITY_RARE || bot->GetItemCount(item->ItemId, true) ||
                bot->CanUseItem(item) != EQUIP_ERR_OK ||
                (!UsesCompanionArmorPriority(item) && item->Class != ITEM_CLASS_WEAPON) ||
                (item->Class == ITEM_CLASS_WEAPON && (item->SubClass == ITEM_SUBCLASS_WEAPON_MISC ||
                    item->SubClass == ITEM_SUBCLASS_WEAPON_FISHING_POLE)))
                continue;
            ItemUsage usage = ai->GetAiObjectContext()->GetValue<ItemUsage>("item upgrade", item->ItemId)->Get();
            if (usage != ITEM_USAGE_EQUIP && usage != ITEM_USAGE_REPLACE && usage != ITEM_USAGE_BAD_EQUIP)
                continue;
            uint8 target = ai->FindEquipSlot(item, NULL_SLOT, true);
            if (PrefersCompanionShield(ai) && IsCompanionShieldMainHand(item->InventoryType))
                target = EQUIPMENT_SLOT_MAINHAND;
            if (target == NULL_SLOT)
                continue;
            Item* current = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, target);
            bool armor = PrefersCompanionShield(ai) && UsesCompanionArmorPriority(item);
            float oldScore = current ? (armor ? float(current->GetTemplate()->Armor) :
                calculator.CalculateItem(current->GetEntry(), current->GetItemRandomPropertyId())) : 0;
            float newScore = armor ? float(item->Armor) : calculator.CalculateItem(item->ItemId);
            bool layout = PrefersCompanionShield(ai) && current &&
                IsCompanionShieldReplacement(item->InventoryType, current->GetTemplate()->InventoryType);
            float gain = !current || layout ? 2.0f : newScore / std::max(1.0f, oldScore) - 1.0f;
            uint32 price = uint32(std::ceil(item->BuyPrice *
                (vendor ? bot->GetReputationPriceDiscount(vendor) : 1.0f)));
            ItemPosCountVec destination;
            if (gain < 0.15f || (budget && (price > bot->GetMoney() / 10 ||
                !CompanionErrands::CanSpend(bot->GetMoney(), price, reserve))) ||
                (vendor && offer->maxcount && !vendor->GetVendorItemCurrentCount(offer)) ||
                bot->CanStoreNewItem(NULL_BAG, NULL_SLOT, destination, item->ItemId, 1) != EQUIP_ERR_OK ||
                gain < bestGain || (gain == bestGain && price >= bestPrice))
                continue;
            best = {item->ItemId, slot, 1};
            bestGain = gain;
            bestPrice = price;
        }
        return best;
    }

    PoisonPurchase ChooseMount(PlayerbotAI* ai, VendorItemData const* goods, uint32 reserve,
        Creature* vendor = nullptr, bool budget = true)
    {
        if (!goods || goods->Empty())
            return {};
        Player* bot = ai->GetBot();
        PoisonPurchase best;
        uint32 bestPrice = std::numeric_limits<uint32>::max();
        int32 bestSpeed = 0;
        for (uint32 slot = 0; slot < goods->GetItemCount(); ++slot)
        {
            auto const* offer = goods->GetItem(slot);
            auto const* item = offer ? sObjectMgr->GetItemTemplate(offer->item) : nullptr;
            if (!item || offer->ExtendedCost || item->BuyCount != 1 || item->BuyPrice <= 0 ||
                bot->CanUseItem(item) != EQUIP_ERR_OK || !NeedsMount(ai, MountFromItem(item)))
                continue;
            int32 speed = MountFromItem(item).speed;
            uint32 price = uint32(std::ceil(item->BuyPrice *
                (vendor ? bot->GetReputationPriceDiscount(vendor) : 1.0f)));
            ItemPosCountVec destination;
            if (speed < bestSpeed || (speed == bestSpeed && price >= bestPrice) ||
                (budget && !CompanionErrands::CanSpend(bot->GetMoney(), price, reserve)) ||
                (vendor && offer->maxcount && !vendor->GetVendorItemCurrentCount(offer)) ||
                bot->CanStoreNewItem(NULL_BAG, NULL_SLOT, destination, item->ItemId, 1) != EQUIP_ERR_OK)
                continue;
            best = {item->ItemId, slot, 1};
            bestPrice = price;
            bestSpeed = speed;
        }
        return best;
    }

    PoisonPurchase ChooseCombatSupply(PlayerbotAI* ai, VendorItemData const* goods, uint32 reserve,
        Creature* vendor = nullptr, bool budget = true)
    {
        if (!goods || goods->Empty())
            return {};
        using Kind = CompanionConsumables::Kind;
        Player* bot = ai->GetBot();
        uint32 healing = 0, mana = 0, bandages = 0;
        for (Item* held : ai->GetInventoryItems())
        {
            if (bot->CanUseItem(held->GetTemplate()) != EQUIP_ERR_OK)
                continue;
            auto profile = DescribeCompanionConsumable(ai, held->GetTemplate());
            if (profile.kind == Kind::Healing)
                healing += held->GetCount();
            if (profile.kind == Kind::Mana || profile.manaAmount)
                mana += held->GetCount();
            if (profile.kind == Kind::Bandage)
                bandages += held->GetCount();
        }
        PoisonPurchase best;
        uint32 bestPriority = 0, bestAmount = 0, bestPrice = MAX_MONEY_AMOUNT;
        for (uint32 slot = 0; slot < goods->GetItemCount(); ++slot)
        {
            auto const* offer = goods->GetItem(slot);
            auto const* item = offer ? sObjectMgr->GetItemTemplate(offer->item) : nullptr;
            if (!item || offer->ExtendedCost || item->BuyPrice <= 0 || !item->BuyCount ||
                bot->CanUseItem(item) != EQUIP_ERR_OK)
                continue;
            auto profile = DescribeCompanionConsumable(ai, item);
            uint32 held = 3, priority = 0, amount = profile.amount;
            if (profile.kind == Kind::Healing && healing < 3)
            {
                held = healing;
                priority = 3;
            }
            else if ((profile.kind == Kind::Mana || profile.manaAmount) && mana < 3 &&
                bot->GetMaxPower(POWER_MANA))
            {
                held = mana;
                priority = 2;
                amount = profile.manaAmount ? profile.manaAmount : profile.amount;
            }
            else if (profile.kind == Kind::Bandage && bandages < 3)
            {
                held = bandages;
                priority = 1;
            }
            if (!priority)
                continue;
            uint32 price = uint32(std::ceil(item->BuyPrice *
                (vendor ? bot->GetReputationPriceDiscount(vendor) : 1.0f)));
            uint32 available = vendor && offer->maxcount ? vendor->GetVendorItemCurrentCount(offer) : 20;
            uint32 batches = CompanionErrands::SupplyBatches(held, 3, item->BuyCount, budget ? price : 0,
                bot->GetMoney(), budget ? reserve : 0, available);
            ItemPosCountVec destination;
            if (!batches || bot->CanStoreNewItem(NULL_BAG, NULL_SLOT, destination,
                item->ItemId, batches * item->BuyCount) != EQUIP_ERR_OK || priority < bestPriority ||
                (priority == bestPriority && (amount < bestAmount || (amount == bestAmount && price >= bestPrice))))
                continue;
            best = {item->ItemId, slot, batches};
            bestPriority = priority;
            bestAmount = amount;
            bestPrice = price;
        }
        return best;
    }

    struct ProfessionSupplies
    {
        std::map<uint32, uint32> desired;
        std::map<uint32, uint32> held;
        std::set<uint32> categories;
    };

    ProfessionSupplies NeededProfessionSupplies(Player* bot)
    {
        ProfessionSupplies needs;
        auto addTool = [&](uint32 item)
        {
            auto const* proto = sObjectMgr->GetItemTemplate(item);
            if (proto && (!proto->TotemCategory || !bot->HasItemTotemCategory(proto->TotemCategory)))
                needs.desired[item] = 1;
        };
        // Gathering tools must be available even before the first crafting recipe.
        if (bot->HasSkill(SKILL_MINING))
            addTool(2901); // Mining Pick
        if (bot->HasSkill(SKILL_SKINNING))
            addTool(7005); // Skinning Knife
        if (bot->HasSkill(SKILL_BLACKSMITHING) || bot->HasSkill(SKILL_ENGINEERING))
            addTool(5956); // Blacksmith Hammer
        if (bot->HasSkill(SKILL_ENGINEERING))
            addTool(6219); // Arclight Spanner

        if (bot->HasSkill(SKILL_INSCRIPTION))
            addTool(39505); // Virtuoso Inking Set

        for (auto const& [id, known] : bot->GetSpellMap())
        {
            if (known->State == PLAYERSPELL_REMOVED || !known->Active)
                continue;
            auto const* line = PlayerbotSpellRepository::Instance().GetSkillLine(id);
            auto const* spell = sSpellMgr->GetSpellInfo(id);
            if (!line || !spell || !IsProfession(line->SkillLine) || !bot->HasSkill(line->SkillLine))
                continue;
            uint32 skill = bot->GetSkillValue(line->SkillLine);
            // Keep useful recipes; at the skill cap keep the recent tier rather than every old vial type.
            if (skill < line->MinSkillLineRank || (skill >= line->TrivialSkillLineRankHigh &&
                (skill < bot->GetMaxSkillValue(line->SkillLine) || skill > line->MinSkillLineRank + 75)))
                continue;
            for (uint32 tool : spell->Totem)
                if (tool)
                    addTool(tool);
            for (uint32 category : spell->TotemCategory)
                if (category && !bot->HasItemTotemCategory(category))
                    needs.categories.insert(category);
            for (uint32 i = 0; i < MAX_SPELL_REAGENTS; ++i)
            {
                if (spell->Reagent[i] <= 0 || !spell->ReagentCount[i])
                    continue;
                uint32 item = uint32(spell->Reagent[i]);
                auto const* proto = sObjectMgr->GetItemTemplate(item);
                if (!proto)
                    continue;
                uint32 desired = std::min<uint32>(proto->GetMaxStackSize(), CompanionErrands::SupplyStockLimit);
                desired = std::min(desired, std::max(CompanionErrands::SupplyStock, spell->ReagentCount[i]));
                needs.desired[item] = std::max(needs.desired[item], desired);
            }
        }
        for (auto const& [item, desired] : needs.desired)
            needs.held[item] = bot->GetItemCount(item, false);
        return needs;
    }

    PoisonPurchase ChooseProfessionSupply(Player* bot, VendorItemData const* goods, uint32 reserve,
        ProfessionSupplies const& needs, Creature* vendor = nullptr, bool budget = true)
    {
        if (!goods || goods->Empty())
            return {};
        PoisonPurchase best;
        uint32 bestPrice = std::numeric_limits<uint32>::max();
        bool bestTool = false;
        for (uint32 slot = 0; slot < goods->GetItemCount(); ++slot)
        {
            auto const* offer = goods->GetItem(slot);
            auto const* item = offer ? sObjectMgr->GetItemTemplate(offer->item) : nullptr;
            if (!item || offer->ExtendedCost || item->BuyPrice < 0 || !item->BuyCount ||
                bot->CanUseItem(item) != EQUIP_ERR_OK)
                continue;
            uint32 desired = 0;
            uint32 held = 0;
            if (auto found = needs.desired.find(item->ItemId); found != needs.desired.end())
            {
                desired = found->second;
                held = needs.held.at(item->ItemId);
            }
            if (!desired && item->TotemCategory)
                for (uint32 category : needs.categories)
                    if (bot->IsTotemCategoryCompatiableWith(item, category))
                    {
                        desired = 1;
                        held = bot->GetItemCount(item->ItemId, false);
                        break;
                    }
            if (!desired || held >= desired || (desired == 1 && item->BuyCount != 1))
                continue;
            float discount = vendor ? bot->GetReputationPriceDiscount(vendor) : 1.0f;
            uint32 price = uint32(std::ceil(item->BuyPrice * discount));
            uint32 available = offer->maxcount ? (vendor ? vendor->GetVendorItemCurrentCount(offer) :
                offer->maxcount) : std::numeric_limits<uint32>::max();
            uint32 batches = CompanionErrands::SupplyBatches(held, desired, item->BuyCount, price,
                budget ? bot->GetMoney() : MAX_MONEY_AMOUNT, budget ? reserve : 0, available);
            uint64 rawPrice = uint64(item->BuyPrice) * batches;
            if (rawPrice > MAX_MONEY_AMOUNT || (budget && !CompanionErrands::CanSpend(bot->GetMoney(),
                uint32(std::floor(uint32(rawPrice) * discount)), reserve)))
                continue;
            ItemPosCountVec destination;
            if (!batches || bot->CanStoreNewItem(NULL_BAG, NULL_SLOT, destination, item->ItemId,
                batches * item->BuyCount) != EQUIP_ERR_OK)
                continue;
            // Tools first, then the cheapest available supply; unrelated equipment scores do not apply.
            bool tool = desired == 1;
            if (best.item && (bestTool > tool || (bestTool == tool && bestPrice <= price)))
                continue;
            best = {item->ItemId, slot, batches};
            bestPrice = price;
            bestTool = tool;
        }
        return best;
    }

    struct BagPurchase
    {
        uint32 item = 0;
        uint32 vendorSlot = 0;
        uint8 equipSlot = NULL_SLOT;
        uint8 storageSlot = NULL_SLOT;
    };

    uint8 SmallestGeneralBagSlot(Player* bot)
    {
        uint8 selected = NULL_SLOT;
        uint32 capacity = std::numeric_limits<uint32>::max();
        for (uint8 slot = INVENTORY_SLOT_BAG_START; slot < INVENTORY_SLOT_BAG_END; ++slot)
        {
            Bag* bag = bot->GetBagByPos(slot);
            if (!bag)
                return slot;
            if (!bag->GetTemplate()->BagFamily && bag->GetBagSize() < capacity)
            {
                selected = slot;
                capacity = bag->GetBagSize();
            }
        }
        return selected;
    }

    uint16 FindOwnedBagUpgrade(Player* bot, uint32 capacity)
    {
        uint16 best = 0;
        auto consider = [bot, &capacity, &best](Item* item, uint16 position)
        {
            if (item && item->IsBag() && !item->IsNotEmptyBag() && !item->IsWrapped() &&
                !item->IsInTrade() && !item->GetTemplate()->BagFamily &&
                bot->CanUseItem(item->GetTemplate()) == EQUIP_ERR_OK &&
                item->GetTemplate()->ContainerSlots > capacity)
            {
                capacity = item->GetTemplate()->ContainerSlots;
                best = position;
            }
        };
        for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
            consider(bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot), uint16(INVENTORY_SLOT_BAG_0 << 8) | slot);
        for (uint8 slot = INVENTORY_SLOT_BAG_START; slot < INVENTORY_SLOT_BAG_END; ++slot)
            if (Bag* bag = bot->GetBagByPos(slot))
                for (uint32 inside = 0; inside < bag->GetBagSize(); ++inside)
                    consider(bot->GetItemByPos(slot, uint8(inside)), uint16(slot << 8) | uint8(inside));
        return best;
    }

    bool CanSellObsoleteBag(Player* bot, Item* item)
    {
        if (!item || !item->IsBag() || item->IsNotEmptyBag() || item->GetTemplate()->BagFamily ||
            bot->CanUseItem(item->GetTemplate()) != EQUIP_ERR_OK)
            return false;
        uint8 target = SmallestGeneralBagSlot(bot);
        Bag* smallest = target != NULL_SLOT ? bot->GetBagByPos(target) : nullptr;
        // An empty equipped slot still needs a bag. Specialty bags never justify selling general capacity.
        return smallest && item->GetTemplate()->ContainerSlots <= smallest->GetBagSize();
    }

    BagPurchase ChooseBag(Player* bot, VendorItemData const* goods, uint32 reserve, Creature* vendor = nullptr,
        bool budget = true)
    {
        uint8 target = SmallestGeneralBagSlot(bot);
        if (!goods || target == NULL_SLOT)
            return {};
        Bag* current = bot->GetBagByPos(target);
        uint32 capacity = current ? current->GetBagSize() : 0;
        uint8 storage = current ? NULL_SLOT : target;
        if (FindOwnedBagUpgrade(bot, capacity))
            return {};
        for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        {
            Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
            if (!item && current && storage == NULL_SLOT)
                storage = slot;
        }
        if (storage == NULL_SLOT)
            return {};
        BagPurchase best;
        uint32 bestCapacity = capacity;
        uint32 bestPrice = std::numeric_limits<uint32>::max();
        for (uint32 slot = 0; slot < goods->GetItemCount(); ++slot)
        {
            auto const* offer = goods->GetItem(slot);
            auto const* item = offer ? sObjectMgr->GetItemTemplate(offer->item) : nullptr;
            if (!item || item->Class != ITEM_CLASS_CONTAINER || item->BagFamily || item->BuyCount != 1 ||
                offer->ExtendedCost || item->BuyPrice < 0 || bot->CanUseItem(item) != EQUIP_ERR_OK ||
                item->ContainerSlots <= capacity || (vendor && offer->maxcount &&
                vendor->GetVendorItemCurrentCount(offer) < 1))
                continue;
            float discount = vendor ? bot->GetReputationPriceDiscount(vendor) : 1.0f;
            uint32 price = uint32(std::floor(item->BuyPrice * discount));
            if ((budget && !CompanionErrands::CanSpend(bot->GetMoney(), price, reserve)) ||
                (budget && item->ContainerSlots < bestCapacity) ||
                ((!budget || item->ContainerSlots == bestCapacity) && price >= bestPrice))
                continue;
            best = {item->ItemId, slot, target, storage};
            bestCapacity = item->ContainerSlots;
            bestPrice = price;
        }
        return best;
    }

    void EquipOwnedBagUpgrade(Player* bot)
    {
        uint8 target = SmallestGeneralBagSlot(bot);
        if (target == NULL_SLOT)
            return;
        Bag* current = bot->GetBagByPos(target);
        uint32 capacity = current ? current->GetBagSize() : 0;
        uint16 source = FindOwnedBagUpgrade(bot, capacity);
        if (!source)
        {
            // Keep room for a later native purchase, even when only an equipped bag has free space.
            for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
                if (!bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                    return;
            for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
            {
                Item* occupant = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
                if (occupant->IsBag() || occupant->IsInTrade() || occupant->IsWrapped())
                    continue;
                for (uint8 bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
                    if (Bag* bag = bot->GetBagByPos(bagSlot); bag && !bag->GetTemplate()->BagFamily)
                        for (uint32 inside = 0; inside < bag->GetBagSize(); ++inside)
                            if (!bot->GetItemByPos(bagSlot, uint8(inside)))
                            {
                                ItemPosCountVec destination;
                                if (bot->CanStoreItem(bagSlot, uint8(inside), destination, occupant, false) != EQUIP_ERR_OK)
                                    continue;
                                bot->SwapItem(uint16(INVENTORY_SLOT_BAG_0 << 8) | slot,
                                    uint16(bagSlot << 8) | uint8(inside));
                                if (!bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                                    return;
                            }
            }
            return;
        }
        // Move an upgrade out of another bag before exchanging bag contents natively.
        if (uint8(source >> 8) != INVENTORY_SLOT_BAG_0)
        {
            Item* upgrade = bot->GetItemByPos(uint8(source >> 8), uint8(source));
            for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
            {
                Item* occupant = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
                if (occupant && (occupant->IsBag() || occupant->IsInTrade() || occupant->IsWrapped()))
                    continue;
                // Exchange with a backpack item when completely full; no extra free slot is needed.
                ItemPosCountVec destination;
                if (occupant && bot->CanStoreItem(uint8(source >> 8), uint8(source), destination,
                    occupant, true) != EQUIP_ERR_OK)
                    continue;
                uint16 temporary = uint16(INVENTORY_SLOT_BAG_0 << 8) | slot;
                bot->SwapItem(source, temporary);
                if (bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot) != upgrade)
                    return;
                source = temporary;
                break;
            }
            if (uint8(source >> 8) != INVENTORY_SLOT_BAG_0)
                return;
        }
        bot->SwapItem(source, uint16(INVENTORY_SLOT_BAG_0 << 8) | target);
    }

    uint32 RepairEquipment(Player* bot, Creature* vendor, uint32 reserve)
    {
        Creature* repairer = bot->GetNPCIfCanInteractWith(vendor->GetGUID(), UNIT_NPC_FLAG_REPAIR);
        if (!repairer)
            return 0;
        float discount = bot->GetReputationPriceDiscount(repairer);
        uint32 spent = 0;
        for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
        {
            Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
            if (!item)
                continue;
            uint32 maximum = item->GetUInt32Value(ITEM_FIELD_MAXDURABILITY);
            uint32 current = item->GetUInt32Value(ITEM_FIELD_DURABILITY);
            if (!maximum || current >= maximum)
                continue;
            auto const* proto = item->GetTemplate();
            auto const* costs = sDurabilityCostsStore.LookupEntry(proto->ItemLevel);
            auto const* quality = sDurabilityQualityStore.LookupEntry((proto->Quality + 1) * 2);
            if (!costs || !quality)
                continue;
            // Same price calculation as Player::DurabilityRepair in this repository.
            uint32 index = ItemSubClassToDurabilityMultiplierId(proto->Class, proto->SubClass);
            uint32 multiplier = costs->multiplier[index];
            uint32 price = uint32((maximum - current) * multiplier * double(quality->quality_mod));
            price = std::max<uint32>(1, uint32(price * discount * sWorld->getRate(RATE_REPAIRCOST)));
            if (!SelfbotInventoryPolicy::CanRepair(bot->GetMoney(), reserve, price))
                continue;
            uint32 before = bot->GetMoney();
            bot->DurabilityRepair(uint16(INVENTORY_SLOT_BAG_0 << 8) | slot, true, discount, false);
            spent += before - bot->GetMoney();
        }
        return spent;
    }

    bool SafeHerb(Player* bot, GameObject* herb)
    {
        std::list<Unit*> units;
        float radius = std::min(150.0f, bot->GetDistance(herb) + 30.0f);
        Acore::AnyUnfriendlyUnitInObjectRangeCheck check(bot, bot, radius);
        Acore::UnitListSearcher<Acore::AnyUnfriendlyUnitInObjectRangeCheck> search(bot, units, check);
        Cell::VisitObjects(bot, search, radius);
        float dx = herb->GetPositionX() - bot->GetPositionX();
        float dy = herb->GetPositionY() - bot->GetPositionY();
        float dz = herb->GetPositionZ() - bot->GetPositionZ();
        float length = dx * dx + dy * dy;
        for (Unit* unit : units)
        {
            if (!unit->IsAlive() || !unit->IsHostileTo(bot) || !bot->CanSeeOrDetect(unit))
                continue;
            float t = length > 0.01f ? std::clamp(((unit->GetPositionX() - bot->GetPositionX()) * dx +
                (unit->GetPositionY() - bot->GetPositionY()) * dy) / length, 0.0f, 1.0f) : 0.0f;
            float x = bot->GetPositionX() + t * dx;
            float y = bot->GetPositionY() + t * dy;
            if (unit->GetDistance(x, y, bot->GetPositionZ() + t * dz) < 25.0f)
                return false;
        }
        return true;
    }
}

struct CompanionErrandState
{
    std::shared_ptr<CompanionFarmingState> farming;
    EventMap events;
    Errand kind = Errand::None;
    bool ready = true;
    bool townDue = true;
    bool herbDue = true;
    bool paused = false;
    bool gathering = false;
    bool mountDue = true;
    bool inventoryDue = true;
    bool bagInventoryDue = true;
    uint64 storageAttemptSignature = 0;
    bool guildDepositOnly = false;
    bool craftDue = true;
    bool professionDue = true;
    bool professionDefaultsChecked = false;
    CompanionProfessionPlan professionPlan;
    bool townRest = false;
    bool restWaiting = false;
    uint64 progressionSignature = 0;
    uint32 observedLevel = 0;
    uint64 inventorySignature = 0;
    GoalDomain goal = GoalDomain::Count;
    uint64 goalRevision = 0;
    uint32 observedMoney = 0;
    uint32 crafts = 0;
    uint32 craftActions = 0;
    uint32 secondaryCrafts = 0;
    uint32 craftSkill = 0;
    uint32 craftSkillBefore = 0;
    bool craftPrimary = false;
    bool craftIntermediate = false;
    uint32 craftSpell = 0;
    uint32 craftOutput = 0;
    uint32 craftBefore = 0;
    bool awaitingCraft = false;
    std::set<uint32> rejectedCrafts;
    uint32 pendingMount = 0;
    uint32 spawn = 0;
    uint32 map = 0;
    uint32 zone = 0;
    uint32 settlement = 0;
    uint32 reserve = 0;
    uint32 learned = 0;
    uint32 pendingLesson = 0;
    uint32 pendingTrainer = 0;
    uint32 moneyBeforeLesson = 0;
    bool verificationDue = false;
    ObjectGuid target;
    ObjectGuid owner;
    float x = 0, y = 0, z = 0;
    float closest = std::numeric_limits<float>::max();
    uint32 sold = 0;
    uint32 repairs = 0;
    CompanionInventory::Step bagNotice = CompanionInventory::Step::None;
    std::set<ObjectGuid> failedSales;
    std::set<uint32> rejectedBagVendors;
    std::set<uint32> rejectedTrainers;
    std::set<uint32> rejectedLessons;
    std::set<uint32> rejectedVendors;
    std::set<uint32> rejectedSupplyVendors;
    std::set<ObjectGuid> rejectedHerbs;
};

std::shared_ptr<CompanionFarmingState>& CompanionFarmRuntime(PlayerbotAI* ai)
{
    if (!ai->companionErrands)
    {
        ai->companionErrands = std::make_shared<CompanionErrandState>();
        ai->companionErrands->reserve = ai->GetBot()->GetMoney() / 5;
        ai->companionErrands->events.ScheduleEvent(ClearRejected, Milliseconds(300000));
    }
    return ai->companionErrands->farming;
}

CompanionProfessionPlan GetCompanionProfessionPlan(PlayerbotAI* ai)
{
    if (IsCompanionFarming(ai))
        return GetCompanionFarmProfessionPlan(ai);
    return ai && ai->companionErrands ? ai->companionErrands->professionPlan : CompanionProfessionPlan{};
}

CompanionTrainingSnapshot GetCompanionTrainingSnapshot(ObjectGuid guid)
{
    std::lock_guard<std::mutex> lock(trainingMutex);
    auto it = trainingSnapshots.find(guid);
    if (it == trainingSnapshots.end())
        return {};
    auto age = std::chrono::duration_cast<std::chrono::seconds>(TrainingClock::now() - it->second.first).count();
    if (age > 600)
    {
        trainingSnapshots.erase(it);
        return {};
    }
    auto result = it->second.second;
    result.ageSeconds = uint32(age);
    return result;
}

bool IsCompanionInventoryManaged(PlayerbotAI* ai)
{
    return ai && ai->GetBot() && ai->GetMaster() && ai->GetMaster() != ai->GetBot() &&
        !IsSelfBot(ai->GetBot()) && IsRealPlayer(ai->GetMaster());
}

bool CanSellCompanionItem(PlayerbotAI* ai, Item* item)
{
    if (!IsCompanionInventoryManaged(ai) || !item || Protected(ai, item) || IsCompanionCraftingStock(ai, item))
        return false;
    auto const* proto = item->GetTemplate();
    if (item->IsBag())
        return proto->Quality <= ITEM_QUALITY_RARE && CanSellObsoleteBag(ai->GetBot(), item);
    if (proto->Class == ITEM_CLASS_CONSUMABLE)
    {
        auto const profile = DescribeCompanionConsumable(ai, proto);
        return profile.kind == CompanionConsumables::Kind::Scroll && !IsUsefulCompanionScroll(ai, profile);
    }
    ItemUsage usage = ai->GetAiObjectContext()->GetValue<ItemUsage>("item usage", item->GetEntry())->Get();
    bool const unused = usage == ITEM_USAGE_VENDOR || usage == ITEM_USAGE_AH || usage == ITEM_USAGE_NONE;
    if (proto->Quality == ITEM_QUALITY_POOR)
        return CompanionErrands::CanSellGrey(proto->Quality, false, unused);
    // Sell ordinary obsolete gear only after its usable replacement is actually equipped.
    // Future gear, alternate layouts, jewellery, tools and epic keepsakes remain protected.
    Player* bot = ai->GetBot();
    if (!unused || proto->Quality > ITEM_QUALITY_RARE || bot->CanUseItem(proto) != EQUIP_ERR_OK ||
        (!UsesCompanionArmorPriority(proto) && proto->Class != ITEM_CLASS_WEAPON) ||
        (proto->Class == ITEM_CLASS_WEAPON && proto->SubClass == ITEM_SUBCLASS_WEAPON_MISC))
        return false;
    uint8 slot = ai->FindEquipSlot(proto, NULL_SLOT, true);
    if (PrefersCompanionShield(ai) && IsCompanionShieldMainHand(proto->InventoryType))
        slot = EQUIPMENT_SLOT_MAINHAND;
    if (slot == NULL_SLOT)
        return false;
    Item* current = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
    if (!current || current == item || (current->GetUInt32Value(ITEM_FIELD_MAXDURABILITY) &&
        !current->GetUInt32Value(ITEM_FIELD_DURABILITY)))
        return false;
    auto const* equipped = current->GetTemplate();
    if (proto->Class == ITEM_CLASS_WEAPON && proto->InventoryType != equipped->InventoryType)
        return false;
    if (PrefersCompanionShield(ai) && UsesCompanionArmorPriority(proto))
        return CompareCompanionArmor(equipped, proto) > 0;
    StatsWeightCalculator calculator(bot);
    calculator.SetItemSetBonus(false);
    calculator.SetOverflowPenalty(false);
    return calculator.CalculateItem(current->GetEntry(), current->GetItemRandomPropertyId()) >
        calculator.CalculateItem(item->GetEntry(), item->GetItemRandomPropertyId()) *
            sPlayerbotAIConfig.equipUpgradeThreshold;
}

namespace
{
    uint64 InventorySignature(PlayerbotAI* ai)
    {
        uint64 signature = 14695981039346656037ULL;
        for (Item* item : ai->GetInventoryItems())
        {
            signature ^= uint64(item->GetEntry()) * 31 + item->GetCount();
            signature *= 1099511628211ULL;
        }
        return signature;
    }

    bool HasCompanionSale(PlayerbotAI* ai)
    {
        for (Item* item : ai->GetInventoryItems())
            if (!ai->companionErrands->failedSales.count(item->GetGUID()) && CanSellCompanionItem(ai, item))
                return true;
        return false;
    }

    CompanionInventory::Step InventoryCleanupStep(PlayerbotAI* ai, bool salePending)
    {
        Player* bot = ai->GetBot();
        Player* master = ai->GetMaster();
        bool full = FreeGeneralBagSlots(bot) == 0;
        bool depositPending = full && !salePending && CompanionGuildBankEnabled() && master &&
            bot->GetGuildId() && bot->GetGuildId() == master->GetGuildId() &&
            CompanionGoalAllowed(ai, GoalDomain::Guild) &&
            ai->companionErrands->storageAttemptSignature != InventorySignature(ai) && HasCompanionGuildDeposit(ai);
        return CompanionInventory::Next(full, salePending, depositPending);
    }
}

void NotifyCompanionInventoryChanged(PlayerbotAI* ai)
{
    if (!IsCompanionInventoryManaged(ai) || !ai->companionErrands)
        return;
    auto& state = *ai->companionErrands;
    state.bagInventoryDue = true;
    state.inventoryDue = true;
    state.townDue = true;
    state.professionDue = true;
}

void FinishCompanionInventoryStorage(PlayerbotAI* ai, bool retry)
{
    if (!IsCompanionInventoryManaged(ai) || !ai->companionErrands)
        return;
    NotifyCompanionInventoryChanged(ai);
    ai->companionErrands->storageAttemptSignature = retry ? 0 : InventorySignature(ai);
}

bool IsManagedCompanion(PlayerbotAI* ai)
{
    return ai && ai->GetBot() && ai->GetMaster() && ai->GetMaster() != ai->GetBot() &&
        !IsSelfBot(ai->GetBot()) && (sPlayerbotAIConfig.companionProfessionPlans.count(ai->GetBot()->GetName()) ||
        CompanionVocation::Get(ai->GetBot()).professions[0] != 0);
}

namespace
{
    void StopTrip(PlayerbotAI* ai, bool reject)
    {
        auto& state = *ai->companionErrands;
        if (reject)
        {
            LOG_INFO("playerbots.companion", "[Training] {} abandons errand={} spawn={} distance={} learned={}",
                ai->GetBot()->GetName(), uint32(state.kind), state.spawn,
                ai->GetBot()->GetDistance(state.x, state.y, state.z), state.learned);
            if (state.kind == Errand::Trainer)
            {
                state.rejectedTrainers.insert(state.spawn);
                RecordTraining(ai->GetBot(), "interrupted_or_unreachable", 0, state.pendingLesson, state.learned);
            }
            if (state.kind == Errand::Bags)
                state.rejectedBagVendors.insert(state.spawn);
            if (state.kind == Errand::ProfessionVendor || state.kind == Errand::MountVendor ||
                state.kind == Errand::EquipmentVendor || state.kind == Errand::BagVendor)
                state.rejectedSupplyVendors.insert(state.spawn);
            if (state.kind == Errand::PoisonVendor)
                state.rejectedVendors.insert(state.spawn);
            if (state.kind == Errand::Herb)
                state.rejectedHerbs.insert(state.target);
            if (state.kind == Errand::Craft)
            {
                state.rejectedCrafts.insert(state.craftSpell);
                state.professionDue = true;
            }
        }
        if (state.kind != Errand::None)
        {
            Player* bot = ai->GetBot();
            if (state.kind == Errand::Craft && state.awaitingCraft)
                bot->InterruptNonMeleeSpells(false);
            if (state.kind == Errand::Herb)
            {
                if (state.gathering)
                    bot->InterruptNonMeleeSpells(false);
                auto* context = ai->GetAiObjectContext();
                if (context->GetValue<LootObject>("loot target")->Get().guid == state.target)
                    context->GetValue<LootObject>("loot target")->Set(LootObject());
                context->GetValue<LootObjectStack*>("available loot")->Get()->Remove(state.target);
            }
            bot->StopMoving();
            bot->GetMotionMaster()->Clear();
        }
        if (state.goal != GoalDomain::Count)
            ObserveCompanionGoal(ai, state.goal, reject ? GoalState::Failed : GoalState::Interrupted,
                reject ? "unreachable_or_native_failure" : "activity_interrupted");
        state.goal = GoalDomain::Count;
        state.kind = Errand::None;
        state.awaitingCraft = false;
        state.pendingLesson = 0;
        state.verificationDue = false;
        state.events.CancelEvent(VerifyLesson);
        state.target.Clear();
        state.gathering = false;
        state.events.CancelEvent(Deadline);
        state.events.CancelEvent(Progress);
    }

    bool HasLearnedLesson(Player* bot, uint32 id)
    {
        if (bot->HasSpell(id))
            return true;
        auto const* info = sSpellMgr->GetSpellInfo(id);
        if (!info)
            return false;
        bool teachesSpell = false;
        for (auto const& effect : info->GetEffects())
        {
            if (!effect.IsEffect(SPELL_EFFECT_LEARN_SPELL))
                continue;
            teachesSpell = true;
            if (!bot->HasSpell(effect.TriggerSpell))
                return false;
        }
        return teachesSpell;
    }

    bool CanVisitTrainer(PlayerbotAI* ai)
    {
        // Class lessons and existing professions do not require a PBC vocation.
        return IsCompanionInventoryManaged(ai) && sPlayerbotAIConfig.allowLearnTrainerSpells;
    }

    void Returning(PlayerbotAI* ai)
    {
        auto& state = *ai->companionErrands;
        StopTrip(ai, false);
        // Continue local errands while the master remains in the same settlement.
        if ((IsManagedCompanion(ai) || CanVisitTrainer(ai)) && Eligible(ai) && state.settlement &&
            Settlement(ai->GetMaster()) == state.settlement)
        {
            state.townDue = true;
            return;
        }
        state.kind = Errand::Return;
        state.closest = std::numeric_limits<float>::max();
        state.events.RescheduleEvent(Deadline, Milliseconds(180000));
        state.events.RescheduleEvent(Progress, Milliseconds(30000));
    }

    void FinishVendorVisit(PlayerbotAI* ai, bool purchaseFailed)
    {
        auto& state = *ai->companionErrands;
        if (state.kind == Errand::PoisonVendor)
            state.rejectedVendors.insert(state.spawn);
        else
            state.rejectedSupplyVendors.insert(state.spawn);
        if (state.goal != GoalDomain::Count)
            ObserveCompanionGoal(ai, state.goal, purchaseFailed ? GoalState::Failed : GoalState::Opportunity,
                purchaseFailed ? "native_purchase_not_confirmed" : "no_available_vendor_purchase");
        state.goal = GoalDomain::Count;
        // Confirmed purchases already have a factual notice; an empty visit is not a completed resupply.
        state.events.RescheduleEvent(TownScan, Milliseconds(5000));
        Returning(ai);
    }

    Trainer::Spell const* ChooseLesson(PlayerbotAI* ai, Trainer::Trainer* trainer, float discount, bool budget = true, GoalDomain requestedDomain = GoalDomain::Count)
    {
        Player* bot = ai->GetBot();
        if (!CanVisitTrainer(ai) || !trainer || !trainer->IsTrainerValidForPlayer(bot))
            return nullptr;
        bool const classTrainer = trainer->GetTrainerType() == Trainer::Type::Class;
        if (budget && classTrainer && !CompanionGoalAllowed(ai, GoalDomain::ClassTraining))
            return nullptr;
        auto const focus = GetCompanionNarrativeFocus(bot->GetGUID());
        if ((focus == "training_class" && !classTrainer) || (focus == "training_professions" && classTrainer))
            return nullptr;
        if (!classTrainer && trainer->GetTrainerType() != Trainer::Type::Tradeskill &&
            trainer->GetTrainerType() != Trainer::Type::Mount)
            return nullptr;
        auto skills = CompanionVocation::Get(bot).professions;
        if (!skills[0])
            if (auto plan = sPlayerbotAIConfig.companionProfessionPlans.find(bot->GetName());
                plan != sPlayerbotAIConfig.companionProfessionPlans.end())
                skills = plan->second;
        Trainer::Spell const* best = nullptr;
        for (auto const& spell : trainer->GetSpells())
        {
            if (ai->companionErrands && ai->companionErrands->rejectedLessons.count(spell.SpellId))
                continue;
            bool riding = spell.ReqSkillLine == SKILL_RIDING || TeachesRiding(spell.SpellId);
            if ((requestedDomain == GoalDomain::Mount && !riding) ||
                (requestedDomain == GoalDomain::ProfessionTraining && riding) ||
                (budget && !classTrainer && !CompanionGoalAllowed(ai, riding ? GoalDomain::Mount : GoalDomain::ProfessionTraining)))
                continue;
            if (riding && focus == "training_professions")
                continue;
            if (!classTrainer && !riding)
            {
                uint32 skill = IsProfession(spell.ReqSkillLine) ? spell.ReqSkillLine :
                    ProfessionForSpell(spell.SpellId);
                if (!skill || (!bot->HasSkill(skill) && skill != skills[0] && skill != skills[1] &&
                    !(IsManagedCompanion(ai) && IsSecondaryProfession(skill))))
                    continue;
            }
            uint32 cost = uint32(std::floor(spell.MoneyCost * discount));
            if (trainer->CanTeachSpell(bot, &spell) &&
                (!budget || CompanionErrands::CanSpend(bot->GetMoney(), cost,
                    ai->companionErrands ? ai->companionErrands->reserve : bot->GetMoney() / 5)) &&
                (!best || spell.MoneyCost < best->MoneyCost ||
                    (spell.MoneyCost == best->MoneyCost && spell.SpellId < best->SpellId)))
                best = trainer->GetSpell(spell.SpellId);
        }
        return best;
    }
}

namespace
{
    void RefreshGoalQuote(PlayerbotAI* ai, GoalDomain domain)
    {
        if (!CompanionGoalRequested(ai, domain))
            return;
        Player* bot = ai->GetBot();
        Player* master = ai->GetMaster();
        if (!InTown(master))
        {
            ObserveCompanionGoal(ai, domain, GoalState::Opportunity, "awaiting_town");
            return;
        }
        uint32 cheapest = std::numeric_limits<uint32>::max();
        bool foundOffer = false;
        for (auto const& [spawn, data] : sObjectMgr->GetAllCreatureData())
        {
            if (data.mapid != bot->GetMapId() || !(data.phaseMask & bot->GetPhaseMask()) ||
                master->GetDistance(data.posX, data.posY, data.posZ) > TownRadius)
                continue;
            Creature* npc = ObjectAccessor::GetSpawnedCreatureByDBGUID(bot->GetMapId(), uint32(spawn));
            if (!npc || !npc->IsAlive() || !npc->IsFriendlyTo(bot) || !bot->CanSeeOrDetect(npc) ||
                Settlement(npc->GetAreaId(), npc->GetZoneId()) != Settlement(master))
                continue;
            uint64 price = 0;
            bool eligible = false;
            float discount = bot->GetReputationPriceDiscount(npc);
            if (domain == GoalDomain::ClassTraining || domain == GoalDomain::ProfessionTraining ||
                (domain == GoalDomain::Mount && sObjectMgr->GetTrainer(npc->GetEntry())))
            {
                auto* trainer = sObjectMgr->GetTrainer(npc->GetEntry());
                bool classTrainer = trainer && trainer->GetTrainerType() == Trainer::Type::Class;
                if (!trainer || classTrainer != (domain == GoalDomain::ClassTraining))
                    continue;
                if (auto const* lesson = ChooseLesson(ai, trainer, discount, false, domain))
                {
                    price = uint32(std::floor(lesson->MoneyCost * discount));
                    eligible = true;
                }
            }
            else if (domain == GoalDomain::Bags)
            {
                auto bag = ChooseBag(bot, npc->GetVendorItems(), 0, npc, false);
                if (bag.item)
                {
                    price = uint32(std::floor(sObjectMgr->GetItemTemplate(bag.item)->BuyPrice * discount));
                    eligible = true;
                }
            }
            else
            {
                PoisonPurchase purchase;
                if (domain == GoalDomain::Mount)
                    purchase = ChooseMount(ai, npc->GetVendorItems(), 0, npc, false);
                else if (domain == GoalDomain::Equipment)
                    purchase = ChooseEquipment(ai, npc->GetVendorItems(), 0, npc, false);
                else if (domain == GoalDomain::Supplies)
                {
                    purchase = ChooseProfessionSupply(bot, npc->GetVendorItems(), 0, NeededProfessionSupplies(bot), npc, false);
                    if (!purchase.item)
                        purchase = ChooseCombatSupply(ai, npc->GetVendorItems(), 0, npc, false);
                }
                if (purchase.item)
                {
                    price = uint32(std::ceil(sObjectMgr->GetItemTemplate(purchase.item)->BuyPrice *
                        purchase.batches * discount));
                    eligible = true;
                }
            }
            if (!eligible)
                continue;
            foundOffer = true;
            uint64 required = domain == GoalDomain::Equipment ? price * 10 : (price * 5 + 3) / 4;
            cheapest = std::min(cheapest, uint32(std::min<uint64>(required, MAX_MONEY_AMOUNT)));
        }
        if (foundOffer && cheapest > bot->GetMoney())
            ObserveCompanionGoal(ai, domain, GoalState::Money, "verified_local_offer", cheapest);
        else if (!foundOffer)
            ObserveCompanionGoal(ai, domain, GoalState::Opportunity, "no_local_offer_or_prerequisite_missing");
        else
            ObserveCompanionGoal(ai, domain, GoalState::Pending, "local_offer_affordable");
    }
}

bool CanTrainCompanionAt(PlayerbotAI* ai, Creature* npc)
{
    if (!IsCompanionInventoryManaged(ai) || !npc || !Eligible(ai) ||
        GetCompanionNarrativeFocus(ai->GetBot()->GetGUID()) == "follow" ||
        !ai->GetBot()->GetNPCIfCanInteractWith(npc->GetGUID(), UNIT_NPC_FLAG_TRAINER))
        return false;
    if (ai->companionErrands && ai->companionErrands->paused)
        return false;
    return ChooseLesson(ai, sObjectMgr->GetTrainer(npc->GetEntry()),
        ai->GetBot()->GetReputationPriceDiscount(npc)) != nullptr;
}

bool RequestCompanionTrainerVisit(PlayerbotAI* ai, Creature* npc)
{
    if (!CanTrainCompanionAt(ai, npc) || !npc->GetSpawnId())
        return false;
    if (ai->companionErrands && ai->companionErrands->kind != Errand::None)
        return ai->companionErrands->kind == Errand::Trainer &&
            ai->companionErrands->spawn == npc->GetSpawnId();
    RequestCompanionTraining(ai);
    auto& state = *ai->companionErrands;
    state.kind = Errand::Trainer;
    auto* trainer = sObjectMgr->GetTrainer(npc->GetEntry());
    auto const* lesson = ChooseLesson(ai, trainer, ai->GetBot()->GetReputationPriceDiscount(npc));
    state.goal = trainer->GetTrainerType() == Trainer::Type::Class ? GoalDomain::ClassTraining :
        lesson && (lesson->ReqSkillLine == SKILL_RIDING || TeachesRiding(lesson->SpellId)) ? GoalDomain::Mount :
        GoalDomain::ProfessionTraining;
    state.spawn = uint32(npc->GetSpawnId());
    state.owner = ai->GetMaster()->GetGUID();
    state.map = ai->GetBot()->GetMapId();
    state.zone = ai->GetMaster()->GetZoneId();
    state.settlement = Settlement(ai->GetMaster());
    state.reserve = ai->GetBot()->GetMoney() / 5;
    state.learned = 0;
    state.x = npc->GetPositionX();
    state.y = npc->GetPositionY();
    state.z = npc->GetPositionZ();
    state.events.RescheduleEvent(Deadline, Milliseconds(180000));
    state.events.RescheduleEvent(Progress, Milliseconds(30000));
    RecordTraining(ai->GetBot(), "arrived", npc->GetEntry(), 0, 0);
    return true;
}

void SetCompanionNarrativeFocus(ObjectGuid guid, std::string const& focus)
{
    std::lock_guard<std::mutex> lock(narrativeFocusMutex);
    if (focus == "normal")
        narrativeFocus.erase(guid);
    else if (focus == "follow" || focus == "training" || focus == "training_class" ||
        focus == "training_professions" || focus == "craft")
        narrativeFocus[guid] = focus;
}

std::string GetCompanionNarrativeFocus(ObjectGuid guid)
{
    std::lock_guard<std::mutex> lock(narrativeFocusMutex);
    auto found = narrativeFocus.find(guid);
    return found == narrativeFocus.end() ? "normal" : found->second;
}

void CancelCompanionErrands(PlayerbotAI* ai, bool automatic)
{
    if (!ai->companionErrands)
        return;
    if (ai->companionErrands->kind == Errand::Trainer)
        RecordTraining(ai->GetBot(), "interrupted", 0, 0, ai->companionErrands->learned);
    StopTrip(ai, false);
    auto& state = *ai->companionErrands;
    state.paused = true;
    state.events.RescheduleEvent(Resume, Milliseconds(automatic ? 10000 : 60000));
}

namespace
{
    bool BagMaintenanceEligible(PlayerbotAI* ai)
    {
        if (!IsCompanionInventoryManaged(ai) || HasPendingCompanionFlight(ai) ||
            !CompanionGoalPermitted(ai, GoalDomain::Bags))
            return false;
        Player* bot = ai->GetBot();
        Player* master = ai->GetMaster();
        if (!bot->IsInWorld() || !master->IsInWorld() || !bot->GetSession() || !master->GetSession() ||
            bot->GetSession()->IsLoggingOut() || master->GetSession()->IsLoggingOut() ||
            !bot->IsAlive() || !master->IsAlive() || bot->IsBeingTeleported() || master->IsBeingTeleported() ||
            bot->GetMap() != master->GetMap() || !(bot->GetPhaseMask() & master->GetPhaseMask()) ||
            bot->IsInCombat() || master->IsInCombat() || bot->GetTradeData() || master->GetTradeData() ||
            bot->IsInFlight() || bot->IsFlying() || bot->IsFalling() || bot->GetTransport() || bot->GetVehicle() ||
            bot->IsNonMeleeSpellCast(false) || !bot->GetLootGUID().IsEmpty())
            return false;
        if (Group* group = bot->GetGroup())
            for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
                if (Player* member = ref->GetSource())
                    if (member->IsInCombat())
                        return false;
        return true;
    }

    void ConsolidateCompanionStacks(PlayerbotAI* ai)
    {
        Player* bot = ai->GetBot();
        std::map<uint32, std::vector<ObjectGuid>> stacks;
        for (Item* item : ai->GetInventoryItems())
            if (Player::IsInventoryPos(item->GetBagSlot(), item->GetSlot()) &&
                item->GetTemplate()->GetMaxStackSize() > 1 && !item->IsInTrade() && !item->IsWrapped() &&
                !item->IsRefundable() && !item->IsBOPTradable())
                stacks[item->GetEntry()].push_back(item->GetGUID());
        uint32 moves = 0;
        for (auto const& [entry, items] : stacks)
            for (size_t target = 0; target < items.size(); ++target)
                for (size_t source = target + 1; source < items.size() && moves < 256; ++source)
                {
                    Item* destination = bot->GetItemByGuid(items[target]);
                    Item* origin = bot->GetItemByGuid(items[source]);
                    if (!destination || !origin ||
                        destination->GetItemRandomPropertyId() != origin->GetItemRandomPropertyId() ||
                        destination->CanBeMergedPartlyWith(origin->GetTemplate()) != EQUIP_ERR_OK)
                        continue;
                    uint32 maximum = destination->GetTemplate()->GetMaxStackSize();
                    if (destination->GetCount() >= maximum)
                        break;
                    uint32 before = origin->GetCount();
                    uint32 amount = std::min(before, maximum - destination->GetCount());
                    uint16 from = uint16(origin->GetBagSlot() << 8) | origin->GetSlot();
                    uint16 to = uint16(destination->GetBagSlot() << 8) | destination->GetSlot();
                    if (amount == before)
                        bot->SwapItem(from, to);
                    else
                        bot->SplitItem(from, to, amount);
                    ++moves;
                    // Native merging can delete the source item. Resolve its GUID afresh.
                    Item* remaining = bot->GetItemByGuid(items[source]);
                    if (!remaining || remaining->GetCount() < before)
                        ai->companionErrands->townDue = true;
                }
    }

    void MaintainCompanionBags(PlayerbotAI* ai)
    {
        if (!BagMaintenanceEligible(ai))
            return;
        Player* bot = ai->GetBot();
        ConsolidateCompanionStacks(ai);
        uint32 before = CompanionBagCapacity(bot);
        // Four equipped slots bound the work; every successful exchange increases capacity.
        for (uint8 pass = INVENTORY_SLOT_BAG_START; pass < INVENTORY_SLOT_BAG_END; ++pass)
        {
            uint32 previous = CompanionBagCapacity(bot);
            EquipOwnedBagUpgrade(bot);
            if (CompanionBagCapacity(bot) <= previous)
                break;
        }
        uint32 after = CompanionBagCapacity(bot);
        if (after <= before)
            return;
        auto& state = *ai->companionErrands;
        state.inventoryDue = true;
        state.townDue = true;
        if (CompanionGoalRequested(ai, GoalDomain::Bags))
            ObserveCompanionGoal(ai, GoalDomain::Bags,
                after >= CompanionGoalTarget(ai, GoalDomain::Bags) ? GoalState::Done : GoalState::Verifying,
                "bag_equipped", 0, after - before);
        LOG_INFO("playerbots.companion", "[Bags] {} capacity_before={} capacity_after={} equipped=true",
            bot->GetName(), before, after);
    }
}

void RequestCompanionTraining(PlayerbotAI* ai)
{
    if (!IsCompanionInventoryManaged(ai))
        return;
    // Schedule a fresh decision; normal safety and purchasing checks remain authoritative.
    if (!ai->companionErrands)
    {
        ai->companionErrands = std::make_shared<CompanionErrandState>();
        ai->companionErrands->reserve = ai->GetBot()->GetMoney() / 5;
        ai->companionErrands->events.ScheduleEvent(ClearRejected, Milliseconds(300000));
    }
    auto& state = *ai->companionErrands;
    if (state.pendingLesson)
        return;
    StopTrip(ai, false);
    state.paused = false;
    state.townDue = true;
    state.ready = true;
    state.professionDue = true;
    state.professionDefaultsChecked = false;
    state.rejectedTrainers.clear();
    state.rejectedSupplyVendors.clear();
    state.rejectedBagVendors.clear();
    state.rejectedVendors.clear();
    state.events.CancelEvent(Resume);
    RecordTraining(ai->GetBot(), "requested", 0, 0, 0);
}

void UpdateCompanionErrands(PlayerbotAI* ai, uint32 elapsed)
{
    if (IsCompanionFarming(ai) || IsCompanionReturningFromFarm(ai))
        return;
    if (!ai->companionErrands)
    {
        if (!IsCompanionInventoryManaged(ai))
            return;
        ai->companionErrands = std::make_shared<CompanionErrandState>();
        ai->companionErrands->reserve = ai->GetBot()->GetMoney() / 5;
        ai->companionErrands->events.ScheduleEvent(ClearRejected, Milliseconds(300000));
    }
    auto& state = *ai->companionErrands;
    bool changed = ReconsiderCompanionGoals(ai);
    uint64 revision = CompanionGoalRevision(ai);
    uint32 money = ai->GetBot()->GetMoney();
    bool fundsAdded = money > state.observedMoney;
    if (changed || state.observedMoney != money || state.goalRevision != revision)
    {
        state.observedMoney = money;
        state.goalRevision = revision;
        state.townDue = true;
        state.herbDue = true;
        state.inventoryDue = true;
    }
    // Progress observations and our own spending must not reopen already exhausted or failed vendors.
    // Explicit requests clear these lists in RequestCompanionTraining; new funds can unlock a real offer.
    if (changed || fundsAdded)
    {
        state.rejectedTrainers.clear();
        state.rejectedSupplyVendors.clear();
        state.rejectedBagVendors.clear();
        state.rejectedVendors.clear();
    }
    Player* restingMaster = ai->GetMaster();
    if (!restingMaster || !Eligible(ai) || restingMaster->isMoving())
    {
        state.townRest = false;
        state.restWaiting = true;
        state.events.RescheduleEvent(TownRest, Milliseconds(6000));
    }
    else if (!state.townRest && !state.restWaiting)
    {
        state.restWaiting = true;
        state.events.ScheduleEvent(TownRest, Milliseconds(6000));
    }
    state.events.Update(elapsed);
    while (uint32 event = state.events.ExecuteEvent())
    {
        switch (event)
        {
            case Decision: state.ready = true; break;
            case InventoryScan: state.inventoryDue = true; break;
            case CraftCooldown: state.craftDue = true; state.townDue = true; break;
            case CraftReset:
                state.crafts = 0;
                state.craftActions = 0;
                state.secondaryCrafts = 0;
                state.craftDue = true;
                state.professionDue = true;
                state.townDue = true;
                break;
            case TownScan: state.townDue = true; break;
            case HerbScan: state.herbDue = true; break;
            case TownRest:
                state.restWaiting = false;
                state.townRest = restingMaster && Eligible(ai) && !restingMaster->isMoving();
                state.townDue = state.townDue || state.townRest;
                break;
            case Resume:
                state.paused = false;
                state.ready = true;
                state.townDue = true;
                state.herbDue = true;
                state.inventoryDue = true;
                break;
            case VerifyLesson: state.verificationDue = true; break;
            case ClearRejected:
                state.rejectedBagVendors.clear();
                state.failedSales.clear();
                state.rejectedTrainers.clear();
                state.rejectedLessons.clear();
                state.rejectedVendors.clear();
                state.rejectedSupplyVendors.clear();
                state.rejectedHerbs.clear();
                state.rejectedCrafts.clear();
                state.professionDue = true;
                state.events.ScheduleEvent(ClearRejected, Milliseconds(300000));
                break;
            case MountRetry:
                state.mountDue = true;
                break;
            case Deadline:
            case Progress:
                StopTrip(ai, true);
                state.paused = true;
                state.events.RescheduleEvent(Resume, Milliseconds(15000));
                break;
        }
    }
    if (state.bagInventoryDue && BagMaintenanceEligible(ai))
    {
        MaintainCompanionBags(ai);
        // Coalesce notifications generated by our own native swaps; retry unsafe states when they become safe.
        state.bagInventoryDue = false;
    }
    if (state.kind == Errand::None)
        return;
    Player* master = ai->GetMaster();
    Player* bot = ai->GetBot();
    bool townActivity = state.kind != Errand::Herb && state.kind != Errand::Return;
    if ((townActivity && master && CompanionActivity::ReturnToAdventure(master->isMoving(),
            bot->GetDistance(master), state.goal != GoalDomain::Count && CompanionGoalRequested(ai, state.goal),
            state.kind == Errand::Craft)) ||
        (state.goal != GoalDomain::Count && !CompanionGoalAllowed(ai, state.goal)) ||
        GetCompanionNarrativeFocus(bot->GetGUID()) == "follow" ||
        !Eligible(ai) || master->GetGUID() != state.owner || bot->GetMapId() != state.map ||
        ((state.kind == Errand::Trainer || state.kind == Errand::PoisonVendor || state.kind == Errand::Bags ||
            state.kind == Errand::ProfessionVendor || state.kind == Errand::MountVendor ||
            state.kind == Errand::EquipmentVendor || state.kind == Errand::Craft || state.kind == Errand::BagVendor) &&
            ((state.kind != Errand::Craft && !InTown(master)) || master->GetZoneId() != state.zone ||
            (state.settlement ? Settlement(master) != state.settlement :
                master->GetDistance(state.x, state.y, state.z) > TownRadius))) ||
        (state.kind == Errand::Herb && CompanionErrands::GatherReturn(master->isMoving(),
            bot->GetDistance(master), master->GetDistance(state.x, state.y, state.z))))
        CancelCompanionErrands(ai, true);
}

bool IsCompanionErrandActive(PlayerbotAI* ai)
{
    return ai && ai->companionErrands && !ai->companionErrands->paused &&
        ai->companionErrands->kind != Errand::None && Eligible(ai);
}

bool HasCompanionCraftPlan(PlayerbotAI* ai)
{
    return ai && ai->companionErrands && ai->companionErrands->kind == Errand::Craft &&
        !ai->companionErrands->awaitingCraft && ai->companionErrands->crafts < 5 &&
        ai->companionErrands->craftActions < 24;
}

bool IsCompanionGathering(PlayerbotAI* ai)
{
    return ai && ai->companionErrands && ai->companionErrands->kind == Errand::Herb;
}

bool IsCompanionGatherLoot(PlayerbotAI* ai, ObjectGuid guid)
{
    if (IsCompanionFarmLoot(ai, guid))
        return true;
    return ai->companionErrands && ai->companionErrands->kind == Errand::Herb &&
        ai->companionErrands->target == guid;
}

void FinishCompanionGather(PlayerbotAI* ai, ObjectGuid guid)
{
    if (FinishCompanionFarmLoot(ai, guid))
        return;
    if (IsCompanionGatherLoot(ai, guid))
    {
        ai->companionErrands->gathering = false;
        ai->companionErrands->rejectedHerbs.insert(guid);
        ObserveCompanionGoal(ai, GoalDomain::Gathering, GoalState::Opportunity, "node_visit_finished");
        ai->companionErrands->goal = GoalDomain::Count;
        Returning(ai);
    }
}

bool CompanionErrandAction::isUseful()
{
    if (IsCompanionFarming(botAI))
        return bot->IsAlive() && !bot->IsInCombat();
    if (IsCompanionReturningFromFarm(botAI))
        return false;
    if (!Eligible(botAI))
        return false;
    if (!botAI->companionErrands)
    {
        botAI->companionErrands = std::make_shared<CompanionErrandState>();
        botAI->companionErrands->reserve = bot->GetMoney() / 5;
        botAI->companionErrands->events.ScheduleEvent(ClearRejected, Milliseconds(300000));
    }
    auto const& state = *botAI->companionErrands;
    if (GetCompanionNarrativeFocus(bot->GetGUID()) == "follow")
        return state.inventoryDue;
    return !state.paused && (state.kind != Errand::None ||
        (state.ready && (state.inventoryDue || state.townDue || state.herbDue)));
}

bool CompanionErrandAction::Execute(Event)
{
    if (IsCompanionFarming(botAI))
        return ExecuteCompanionFarming(botAI);
    if (!isUseful())
        return false;
    auto& state = *botAI->companionErrands;
    Player* master = botAI->GetMaster();
    if (state.inventoryDue && !bot->IsNonMeleeSpellCast(false) && bot->GetLootGUID().IsEmpty())
    {
        state.inventoryDue = false;
        state.events.RescheduleEvent(InventoryScan, Milliseconds(15000));
        if (bot->GetFreeTalentPoints() && CompanionGoalPermitted(botAI, GoalDomain::Talents))
        {
            uint32 before = bot->GetFreeTalentPoints();
            if (before)
                PlayerbotFactory(bot, bot->GetLevel()).InitTalentsTree(true, true, false);
            uint32 after = bot->GetFreeTalentPoints();
            ObserveCompanionGoal(botAI, GoalDomain::Talents, after ? GoalState::Opportunity : GoalState::Done,
                after ? "talent_path_or_prerequisite_missing" : "talent_points_spent", 0, before - after);
        }
        if (!state.professionDefaultsChecked)
        {
            RestoreCompanionProfessionDefaults(botAI);
            state.professionDefaultsChecked = true;
            state.professionDue = true;
        }
        uint64 signature = InventorySignature(botAI);
        uint64 progression = bot->GetSpellMap().size();
        for (uint32 skill : {uint32(SKILL_BLACKSMITHING), uint32(SKILL_LEATHERWORKING), uint32(SKILL_ALCHEMY),
            uint32(SKILL_HERBALISM), uint32(SKILL_MINING), uint32(SKILL_TAILORING), uint32(SKILL_ENGINEERING),
            uint32(SKILL_ENCHANTING), uint32(SKILL_SKINNING), uint32(SKILL_JEWELCRAFTING), uint32(SKILL_INSCRIPTION),
            uint32(SKILL_FIRST_AID), uint32(SKILL_COOKING), uint32(SKILL_FISHING)})
        {
            progression ^= bot->GetSkillValue(skill);
            progression *= 1099511628211ULL;
        }
        if (state.progressionSignature != progression)
        {
            state.progressionSignature = progression;
            state.professionDue = true;
            state.rejectedTrainers.clear();
            state.rejectedLessons.clear();
            state.rejectedCrafts.clear();
            state.craftDue = true;
            state.townDue = true;
        }
        if (state.observedLevel != bot->GetLevel())
        {
            state.rejectedTrainers.clear();
            state.rejectedLessons.clear();
            state.observedLevel = bot->GetLevel();
            state.mountDue = true;
            state.townDue = true;
        }
        if (state.inventorySignature != signature)
        {
            state.inventorySignature = signature;
            state.professionDue = true;
            state.rejectedCrafts.clear();
            state.craftDue = true;
            state.townDue = true;
            state.herbDue = true;
            state.bagInventoryDue = true;
            // Failed vendors remain excluded; a loot change does not repair a native purchase refusal.
        }
        std::array<ObjectGuid, EQUIPMENT_SLOT_END> equipmentBefore;
        for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
            if (Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                equipmentBefore[slot] = item->GetGUID();
        if (CompanionGoalPermitted(botAI, GoalDomain::Equipment))
            botAI->DoSpecificAction("equip upgrade", Event(), true);
        uint32 changedEquipment = 0;
        for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
            if (Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                changedEquipment += item->GetGUID() != equipmentBefore[slot];
        if (changedEquipment && CompanionGoalRequested(botAI, GoalDomain::Equipment))
            ObserveCompanionGoal(botAI, GoalDomain::Equipment, GoalState::Done, "equipment_worn", 0, changedEquipment);
        if (state.bagInventoryDue && BagMaintenanceEligible(botAI))
        {
            MaintainCompanionBags(botAI);
            state.bagInventoryDue = false;
        }
        if (FreeGeneralBagSlots(bot))
            state.bagNotice = CompanionInventory::Step::None;
        LOG_INFO("playerbots.companion", "[Inventory] {} protection={} money={} scan_complete=true",
            bot->GetName(), PrefersCompanionShield(botAI), bot->GetMoney());
    }
    if (CompanionGoalRequested(botAI, GoalDomain::Bags) &&
        CompanionBagCapacity(bot) >= CompanionGoalTarget(botAI, GoalDomain::Bags))
        ObserveCompanionGoal(botAI, GoalDomain::Bags, GoalState::Done, "target_capacity_observed");
    if (GetCompanionNarrativeFocus(bot->GetGUID()) == "follow")
        return false;
    if (!state.ready || bot->IsNonMeleeSpellCast(false) || !bot->GetLootGUID().IsEmpty())
        return state.kind != Errand::None;
    state.ready = false;
    state.events.RescheduleEvent(Decision, Milliseconds(1000));

    if (state.pendingMount && (bot->HasSpell(state.pendingMount) || state.mountDue))
    {
        LOG_INFO("playerbots.companion", "[Mount] {} spell={} learned={}",
            bot->GetName(), state.pendingMount, bot->HasSpell(state.pendingMount));
        ObserveCompanionGoal(botAI, GoalDomain::Mount, bot->HasSpell(state.pendingMount) ? GoalState::Done : GoalState::Failed,
            bot->HasSpell(state.pendingMount) ? "mount_learned" : "mount_not_learned", 0, bot->HasSpell(state.pendingMount));
        state.pendingMount = 0;
    }

    if (state.kind == Errand::None)
    {
        if (state.professionDue)
        {
            state.professionPlan = PlanCompanionProfession(botAI, state.rejectedCrafts);
            state.professionDue = false;
        }
        state.owner = master->GetGUID();
        state.map = bot->GetMapId();
        state.zone = master->GetZoneId();
        state.settlement = Settlement(master);
        if (state.mountDue && InTown(master) && CompanionGoalAllowed(botAI, GoalDomain::Mount))
        {
            for (Item* item : botAI->GetInventoryItems())
            {
                auto mount = MountFromItem(item->GetTemplate());
                if (!mount.spell || bot->HasSpell(mount.spell) || item->IsInTrade() ||
                    bot->CanUseItem(item->GetTemplate()) != EQUIP_ERR_OK)
                    continue;
                if (bot->isMoving())
                {
                    bot->StopMoving();
                    return true;
                }
                if (bot->IsMounted())
                    bot->RemoveAurasByType(SPELL_AURA_MOUNTED);
                state.mountDue = false;
                state.pendingMount = mount.spell;
                state.events.RescheduleEvent(MountRetry, Milliseconds(60000));
                MountItemUse(botAI).Learn(item);
                return true;
            }
        }
        if (state.townDue)
        {
            state.townDue = false;
            state.events.RescheduleEvent(TownScan, Milliseconds(60000));
            if ((InTown(master) || state.townRest) &&
                (state.townRest || GetCompanionNarrativeFocus(bot->GetGUID()) != "normal" ||
                CompanionGoalRequested(botAI, GoalDomain::Bags) || CompanionGoalRequested(botAI, GoalDomain::Maintenance) ||
                CompanionGoalRequested(botAI, GoalDomain::Supplies) || CompanionGoalRequested(botAI, GoalDomain::Mount) ||
                CompanionGoalRequested(botAI, GoalDomain::Equipment) || CompanionGoalRequested(botAI, GoalDomain::Crafting) ||
                CompanionGoalRequested(botAI, GoalDomain::ClassTraining) ||
                CompanionGoalRequested(botAI, GoalDomain::ProfessionTraining) || CompanionGoalRequested(botAI, GoalDomain::Guild)) &&
                (IsManagedCompanion(botAI) || CanVisitTrainer(botAI) ||
                (!master->isMoving() && bot->GetDistance(master) < 30.0f)))
            {
                state.reserve = bot->GetMoney() / 5;
                for (auto domain : {GoalDomain::Bags, GoalDomain::ClassTraining, GoalDomain::ProfessionTraining,
                    GoalDomain::Equipment, GoalDomain::Mount, GoalDomain::Supplies})
                    RefreshGoalQuote(botAI, domain);
                double best = std::numeric_limits<double>::max();
                if (CompanionGoalPermitted(botAI, GoalDomain::Equipment))
                    botAI->DoSpecificAction("equip upgrade", Event(), true);
                auto const stock = PoisonStock(bot);
                auto const supplies = CompanionGoalAllowed(botAI, GoalDomain::Supplies) ?
                    NeededProfessionSupplies(bot) : ProfessionSupplies{};
                bool sell = false;
                for (Item* item : botAI->GetInventoryItems())
                    if (CompanionGoalAllowed(botAI, GoalDomain::Maintenance) &&
                        !state.failedSales.count(item->GetGUID()) && CanSellCompanionItem(botAI, item))
                        sell = true;
                bool repair = CompanionGoalAllowed(botAI, GoalDomain::Maintenance) && NeedsEquipmentRepair(bot);
                auto cleanup = InventoryCleanupStep(botAI, sell);
                bool buyBags = CompanionInventory::BuyBags(CompanionGoalRequested(botAI, GoalDomain::Bags), cleanup);
                bool budgetBlocked = false;
                Errand selected = Errand::None;
                GoalDomain selectedGoal = GoalDomain::Count;
                for (auto const& [spawn, data] : sObjectMgr->GetAllCreatureData())
                {
                    if (!InTown(master))
                        break;
                    if (data.mapid != state.map || !(data.phaseMask & bot->GetPhaseMask()))
                        continue;
                    auto const* creature = sObjectMgr->GetCreatureTemplate(data.id);
                    if (!creature || !(creature->npcflag &
                        (UNIT_NPC_FLAG_VENDOR | UNIT_NPC_FLAG_REPAIR | UNIT_NPC_FLAG_TRAINER)))
                        continue;
                    auto const* faction = sFactionTemplateStore.LookupEntry(creature->faction);
                    if (!faction || !bot->GetFactionTemplateEntry()->IsFriendlyTo(*faction))
                        continue;
                    uint32 areaId = bot->GetMap()->GetAreaId(bot->GetPhaseMask(), data.posX, data.posY, data.posZ);
                    uint32 zoneId = bot->GetMap()->GetZoneId(bot->GetPhaseMask(), data.posX, data.posY, data.posZ);
                    if (zoneId != state.zone)
                        continue;
                    if (state.settlement ? Settlement(areaId, zoneId) != state.settlement :
                        master->GetDistance(data.posX, data.posY, data.posZ) > TownRadius)
                        continue;
                    auto* trainer = sObjectMgr->GetTrainer(data.id);
                    bool offered = false;
                    auto consider = [&](Errand candidate, GoalDomain domain, double priority)
                    {
                        if (!CompanionGoalAllowed(botAI, domain))
                            return;
                        offered = true;
                        double score = CompanionActivity::Score(domain, priority, CompanionGoalPriority(botAI, domain),
                            bot->GetDistance(data.posX, data.posY, data.posZ),
                            repair && (creature->npcflag & UNIT_NPC_FLAG_REPAIR), NeedsBagSpace(bot), master->IsMounted());
                        if (score >= best)
                            return;
                        best = score;
                        selected = candidate;
                        selectedGoal = domain;
                        state.spawn = uint32(spawn);
                        state.x = data.posX;
                        state.y = data.posY;
                        state.z = data.posZ;
                    };
                    if (!state.rejectedBagVendors.count(uint32(spawn)) &&
                        ((sell && (creature->npcflag & UNIT_NPC_FLAG_VENDOR)) ||
                        (repair && (creature->npcflag & UNIT_NPC_FLAG_REPAIR))))
                        consider(Errand::Bags, GoalDomain::Maintenance, -10000.0);
                    if (!state.rejectedTrainers.count(uint32(spawn)))
                        if (auto const* lesson = ChooseLesson(botAI, trainer, 1.0f))
                        {
                            bool profession = trainer->GetTrainerType() != Trainer::Type::Class;
                            GoalDomain domain = !profession ? GoalDomain::ClassTraining :
                                (lesson->ReqSkillLine == SKILL_RIDING || TeachesRiding(lesson->SpellId)) ?
                                    GoalDomain::Mount : GoalDomain::ProfessionTraining;
                            bool craftFocus = GetCompanionNarrativeFocus(bot->GetGUID()) == "craft";
                            consider(Errand::Trainer, domain,
                                craftFocus ? (profession ? 0.0 : 10000.0) : (profession ? 10000.0 : 0.0));
                        }
                    if (!state.rejectedSupplyVendors.count(uint32(spawn)))
                    {
                        auto const* goods = sObjectMgr->GetNpcVendorItemList(data.id);
                        if (CompanionGoalAllowed(botAI, GoalDomain::Mount) && ChooseMount(botAI, goods, state.reserve).item)
                            consider(Errand::MountVendor, GoalDomain::Mount, 12000.0);
                        if (buyBags && CompanionGoalAllowed(botAI, GoalDomain::Bags) &&
                            ChooseBag(bot, goods, state.reserve).item)
                            consider(Errand::BagVendor, GoalDomain::Bags, 14000.0);
                        if (CompanionGoalAllowed(botAI, GoalDomain::Supplies) &&
                            (ChooseProfessionSupply(bot, goods, state.reserve, supplies).item ||
                                ChooseCombatSupply(botAI, goods, state.reserve).item))
                            consider(Errand::ProfessionVendor, GoalDomain::Supplies, 15000.0);
                        if (CompanionGoalAllowed(botAI, GoalDomain::Equipment) && ChooseEquipment(botAI, goods, state.reserve).item)
                            consider(Errand::EquipmentVendor, GoalDomain::Equipment, 17000.0);
                    }
                    if (IsManagedCompanion(botAI) && !state.rejectedVendors.count(uint32(spawn)) &&
                        CompanionGoalAllowed(botAI, GoalDomain::Supplies) &&
                        ChoosePoison(bot, sObjectMgr->GetNpcVendorItemList(data.id), state.reserve, stock).item)
                        consider(Errand::PoisonVendor, GoalDomain::Supplies, 20000.0);
                    if (!offered)
                        budgetBlocked = budgetBlocked || ChooseLesson(botAI, trainer, 1.0f, false);
                }
                if (best == std::numeric_limits<double>::max() &&
                    GetCompanionNarrativeFocus(bot->GetGUID()).starts_with("training"))
                {
                    auto const previous = GetCompanionTrainingSnapshot(bot->GetGUID());
                    if (previous.status.empty() || previous.status == "requested" ||
                        previous.status == "no_eligible_lesson" || previous.status == "budget_insufficient")
                        RecordTraining(bot, budgetBlocked ? "budget_insufficient" : "no_eligible_lesson", 0, 0, 0);
                }
                if (CompanionGoalAllowed(botAI, GoalDomain::Guild) &&
                    IsCompanionInventoryManaged(botAI) &&
                    (cleanup == CompanionInventory::Step::Deposit || CompanionGuildBankVisitDue(bot->GetGUID())) &&
                    !master->isMoving() &&
                    bot->GetGuildId() && bot->GetGuildId() == master->GetGuildId())
                {
                    float distance = 30.0f;
                    for (ObjectGuid guid : context->GetValue<GuidVector>("nearest game objects")->Get())
                    {
                        GameObject* bank = botAI->GetGameObject(guid);
                        if (!bank || bank->GetGoType() != GAMEOBJECT_TYPE_GUILD_BANK || !bank->isSpawned() ||
                            !bot->CanSeeOrDetect(bank) || !bot->IsWithinLOSInMap(bank) ||
                            master->GetDistance(bank) > 30.0f || bot->GetDistance(bank) >= distance)
                            continue;
                        double score = CompanionActivity::Score(GoalDomain::Guild,
                            cleanup == CompanionInventory::Step::Deposit ? -70000.0 : 16000.0,
                            CompanionGoalPriority(botAI, GoalDomain::Guild), bot->GetDistance(bank),
                            false, false, false);
                        if (score >= best)
                            continue;
                        distance = bot->GetDistance(bank);
                        best = score;
                        selected = Errand::GuildBank;
                        selectedGoal = GoalDomain::Guild;
                        state.guildDepositOnly = cleanup == CompanionInventory::Step::Deposit;
                        state.target = guid;
                        state.x = bank->GetPositionX();
                        state.y = bank->GetPositionY();
                        state.z = bank->GetPositionZ();
                    }
                }
                if (state.kind == Errand::None && cleanup != CompanionInventory::Step::Deposit &&
                    CompanionGoalAllowed(botAI, GoalDomain::Crafting) &&
                    14000.0 - CompanionGoalPriority(botAI, GoalDomain::Crafting) < best && state.craftDue &&
                    CompanionCraftBudgetAvailable(state.crafts, state.craftActions, state.secondaryCrafts,
                        state.professionPlan.primary) && !master->isMoving() &&
                    !bot->IsNonMeleeSpellCast(false) &&
                    bot->GetDistance(master) < (state.craftActions ? 180.0f : 30.0f) && HasBagRoom(bot))
                {
                    state.craftDue = false;
                    state.events.RescheduleEvent(CraftCooldown, Milliseconds(30000));
                    CraftRandomItemAction crafting(botAI);
                    uint32 priority = 0;
                    for (uint32 id : {state.professionPlan.next})
                    {
                        auto const* spell = sSpellMgr->GetSpellInfo(id);
                        if (!spell || !bot->HasSpell(id) ||
                            state.rejectedCrafts.count(id) || !crafting.AcceptSpell(spell))
                            continue;
                        GameObject* focus = nullptr;
                        if (!botAI->CanCastSpell(id, bot))
                        {
                            if (!spell->RequiresSpellFocus)
                                continue;
                            std::list<GameObject*> stations;
                            AnyGameObjectInObjectRangeCheck check(bot, 150.0f);
                            Acore::GameObjectListSearcher<AnyGameObjectInObjectRangeCheck> search(bot, stations, check);
                            Cell::VisitObjects(bot, search, 150.0f);
                            float closest = 150.0f;
                            for (GameObject* station : stations)
                                if (station->isSpawned() && station->GetGoType() == GAMEOBJECT_TYPE_SPELL_FOCUS &&
                                    station->GetGOInfo()->spellFocus.focusId == spell->RequiresSpellFocus &&
                                    bot->GetDistance(station) < closest && bot->CanSeeOrDetect(station) &&
                                    master->GetDistance(station) <= 180.0f && SafeHerb(bot, station))
                                {
                                    focus = station;
                                    closest = bot->GetDistance(station);
                                }
                            if (!focus)
                                continue;
                        }
                        priority = 1;
                        state.craftIntermediate = id != state.professionPlan.root;
                        auto const* skill = PlayerbotSpellRepository::Instance().GetSkillLine(id);
                        state.craftSkill = skill ? skill->SkillLine : state.professionPlan.skill;
                        state.craftPrimary = state.professionPlan.primary;
                        state.craftSpell = id;
                        state.craftOutput = spell->Effects[EFFECT_0].ItemType;
                        state.target = focus ? focus->GetGUID() : ObjectGuid::Empty;
                        state.x = focus ? focus->GetPositionX() : bot->GetPositionX();
                        state.y = focus ? focus->GetPositionY() : bot->GetPositionY();
                        state.z = focus ? focus->GetPositionZ() : bot->GetPositionZ();
                    }
                    if (priority)
                    {
                        state.kind = Errand::Craft;
                        state.goal = GoalDomain::Crafting;
                        best = std::numeric_limits<double>::max();
                    }
                    else if (CompanionGoalRequested(botAI, GoalDomain::Crafting))
                        ObserveCompanionGoal(botAI, GoalDomain::Crafting, GoalState::Opportunity,
                            "recipe_materials_tools_or_station_missing");
                }
                if (best != std::numeric_limits<double>::max())
                {
                    state.kind = selected;
                    state.goal = selectedGoal;
                    state.learned = 0;
                    if (selected == Errand::Trainer)
                        RecordTraining(bot, "travelling", 0, 0, 0);
                    state.sold = 0;
                    state.repairs = 0;
                    if (selected == Errand::GuildBank)
                        state.spawn = 0;
                    if (CompanionGoalRequested(botAI, selectedGoal) &&
                        selected != Errand::ProfessionVendor && selected != Errand::MountVendor &&
                        selected != Errand::EquipmentVendor && selected != Errand::BagVendor)
                        botAI->TellMaster(selected == Errand::Bags ?
                        "Je vais vendre mon equipement obsolete et mes objets inutiles, puis faire les reparations." :
                        selected == Errand::Trainer ?
                        "Je vais voir un maitre pour mes apprentissages, puis je vous rejoins." :
                        selected == Errand::GuildBank ?
                        "Je passe au coffre de guilde pour partager les ressources utiles." :
                        "Je vais acheter mes poisons, puis je vous rejoins.");
                }
                if ((cleanup == CompanionInventory::Step::Deposit || cleanup == CompanionInventory::Step::Expand) &&
                    state.bagNotice != cleanup && CompanionGoalAllowed(botAI, GoalDomain::Bags))
                {
                    botAI->TellMaster(cleanup == CompanionInventory::Step::Deposit ?
                        "Mes sacs sont pleins. J'ai des materiaux a deposer au coffre de guilde." :
                        "Mes sacs sont pleins apres le tri. Il me faut des sacs plus grands.");
                    state.bagNotice = cleanup;
                }
            }
        }
        if (state.kind == Errand::None && state.herbDue)
        {
            state.herbDue = false;
            state.events.RescheduleEvent(HerbScan, Milliseconds(5000));
            if (!GetCompanionNarrativeFocus(bot->GetGUID()).starts_with("training") &&
                IsCompanionInventoryManaged(botAI) && CompanionGoalAllowed(botAI, GoalDomain::Gathering) &&
                ((bot->HasSkill(SKILL_HERBALISM) && bot->HasSpell(2366)) ||
                    (bot->HasSkill(SKILL_MINING) && bot->HasSpell(2575))) && HasBagRoom(bot) &&
                bot->GetDistance(master) <= 40.0f)
            {
                state.target.Clear();
                float radius = CompanionErrands::SearchRadius(state.townRest,
                    CompanionGoalRequested(botAI, GoalDomain::Gathering));
                float best = std::numeric_limits<float>::max();
                std::list<GameObject*> nodes;
                AnyGameObjectInObjectRangeCheck check(bot, radius);
                Acore::GameObjectListSearcher<AnyGameObjectInObjectRangeCheck> search(bot, nodes, check);
                Cell::VisitObjects(bot, search, radius);
                for (GameObject* herb : nodes)
                {
                    if (!herb)
                        continue;
                    ObjectGuid guid = herb->GetGUID();
                    if (state.rejectedHerbs.count(guid) || !bot->CanSeeOrDetect(herb) ||
                        herb->HasFlag(GAMEOBJECT_FLAGS, GO_FLAG_NOT_SELECTABLE) ||
                        (herb->HasFlag(GAMEOBJECT_FLAGS, GO_FLAG_INTERACT_COND) && !herb->ActivateToQuest(bot)) ||
                        herb->GetGoState() != GO_STATE_READY ||
                        !herb->isSpawned() || herb->getLootState() != GO_READY ||
                        bot->GetDistance(herb) > radius || master->GetDistance(herb) > radius ||
                        !bot->IsWithinLOSInMap(herb))
                        continue;
                    LootObject loot(bot, guid);
                    if ((loot.skillId != SKILL_HERBALISM && loot.skillId != SKILL_MINING) ||
                        !bot->HasSpell(loot.skillId == SKILL_MINING ? 2575 : 2366) ||
                        !loot.IsLootPossible(bot, false) || !SafeHerb(bot, herb))
                        continue;
                    bool claimed = false;
                    for (GroupReference* ref = bot->GetGroup()->GetFirstMember(); ref; ref = ref->next())
                        if (Player* member = ref->GetSource(); member && member != bot && member->IsInWorld() &&
                            member->GetMap() == bot->GetMap())
                            if (PlayerbotAI* other = GET_PLAYERBOT_AI(member))
                                claimed = claimed || IsCompanionGatherLoot(other, guid);
                    if (claimed)
                        continue;
                    bool needed = false;
                    for (auto const& [item, quantity] : state.professionPlan.missing)
                        if (quantity && CompanionLootSources::Has(CompanionLootSourceIndex::Kind::Gameobject,
                            herb->GetGOInfo()->GetLootId(), item))
                            needed = true;
                    float score = CompanionErrands::GatherScore(bot->GetDistance(herb), needed);
                    if (score >= best)
                        continue;
                    best = score;
                    state.target = guid;
                    state.x = herb->GetPositionX();
                    state.y = herb->GetPositionY();
                    state.z = herb->GetPositionZ();
                }
                if (state.target)
                {
                    state.kind = Errand::Herb;
                    state.goal = GoalDomain::Gathering;
                    // The bounded detour itself is sufficient; avoid repeated gathering chatter.
                }
            }
        }
        if (state.kind == Errand::None)
            return false;
        if (state.goal != GoalDomain::Count)
            ObserveCompanionGoal(botAI, state.goal, GoalState::Travelling, "native_plan_started");
        LOG_INFO("playerbots.companion", "[Errand] {} started kind={} spawn={} craft={} distance={}",
            bot->GetName(), uint32(state.kind), state.spawn, state.craftSpell,
            bot->GetDistance(state.x, state.y, state.z));
        state.closest = std::numeric_limits<float>::max();
        state.events.RescheduleEvent(Deadline, Milliseconds(state.kind == Errand::Herb ? 60000 : 180000));
        state.events.RescheduleEvent(Progress, Milliseconds(30000));
    }

    if (state.kind == Errand::Craft)
    {
        if (state.awaitingCraft)
        {
            bool completed = bot->GetItemCount(state.craftOutput, false) > state.craftBefore;
            if (!completed)
            {
                state.rejectedCrafts.insert(state.craftSpell);
                ObserveCompanionGoal(botAI, GoalDomain::Crafting, GoalState::Failed, "product_not_created");
                state.goal = GoalDomain::Count;
            }
            else
            {
                ++state.craftActions;
                if (!state.craftIntermediate)
                    ++state.crafts;
                if (!state.craftPrimary)
                    ++state.secondaryCrafts;
                ObserveCompanionGoal(botAI, GoalDomain::Crafting, state.craftIntermediate ? GoalState::Pending : GoalState::Done,
                    state.craftIntermediate ? "intermediate_created" : "product_created", 0, 1);
                state.goal = GoalDomain::Count;
                if (state.craftActions == 1)
                    state.events.RescheduleEvent(CraftReset, Milliseconds(300000));
                state.inventoryDue = true;
                state.professionDue = true;
                state.events.RescheduleEvent(CraftCooldown, Milliseconds(5000));
                LOG_INFO("playerbots.companion", "[Craft] {} spell={} item={} completed={} skill={} before={} after={}",
                    bot->GetName(), state.craftSpell, state.craftOutput, state.crafts, state.craftSkill,
                    state.craftSkillBefore, bot->GetPureSkillValue(state.craftSkill));
            }
            Returning(botAI);
            return true;
        }
        if (GameObject* station = botAI->GetGameObject(state.target))
            if (bot->GetDistance(station) > INTERACTION_DISTANCE - 1.0f)
            {
                float distance = bot->GetDistance(station);
                if (distance + 2.0f < state.closest)
                {
                    state.closest = distance;
                    state.events.RescheduleEvent(Progress, Milliseconds(30000));
                }
                MoveTo(state.map, state.x, state.y, state.z, false, false, true, true);
                return true;
            }
        bot->StopMoving();
        if (bot->IsMounted())
        {
            bot->RemoveAurasByType(SPELL_AURA_MOUNTED);
            return true;
        }
        CraftRandomItemAction crafting(botAI);
        auto const* spell = sSpellMgr->GetSpellInfo(state.craftSpell);
        if (!spell || !crafting.isUseful() || !crafting.AcceptSpell(spell) ||
            !botAI->CanCastSpell(state.craftSpell, bot))
        {
            StopTrip(botAI, true);
            return false;
        }
        state.craftBefore = bot->GetItemCount(state.craftOutput, false);
        state.craftSkillBefore = bot->GetPureSkillValue(state.craftSkill);
        state.awaitingCraft = botAI->CastSpell(state.craftSpell, bot);
        if (!state.awaitingCraft)
            StopTrip(botAI, true);
        return true;
    }
    if (state.kind == Errand::Return)
    {
        state.x = master->GetPositionX();
        state.y = master->GetPositionY();
        state.z = master->GetPositionZ();
        if (bot->GetDistance(master) < 8.0f)
        {
            StopTrip(botAI, false);
            return false;
        }
    }
    if (state.kind == Errand::Herb)
    {
        GameObject* herb = botAI->GetGameObject(state.target);
        if (!herb || !herb->isSpawned() || !HasBagRoom(bot) || !SafeHerb(bot, herb) ||
            !bot->CanSeeOrDetect(herb) || herb->HasFlag(GAMEOBJECT_FLAGS, GO_FLAG_NOT_SELECTABLE) ||
            (herb->HasFlag(GAMEOBJECT_FLAGS, GO_FLAG_INTERACT_COND) && !herb->ActivateToQuest(bot)))
        {
            StopTrip(botAI, true);
            return false;
        }
        if (bot->GetDistance(herb) < INTERACTION_DISTANCE - 1.0f)
        {
            if (!state.gathering)
            {
                LootObject loot(bot, state.target);
                if (!loot.IsLootPossible(bot))
                {
                    StopTrip(botAI, true);
                    return false;
                }
                if (bot->isMoving())
                {
                    bot->StopMoving();
                    return true;
                }
                if (bot->IsMounted())
                {
                    bot->RemoveAurasByType(SPELL_AURA_MOUNTED);
                    return true;
                }
                context->GetValue<LootObject>("loot target")->Set(loot);
                state.gathering = botAI->CastSpell(loot.skillId == SKILL_MINING ? 2575 : 2366, bot);
                if (!state.gathering)
                    StopTrip(botAI, true);
            }
            return true;
        }
    }
    if (state.kind == Errand::GuildBank)
    {
        GameObject* bank = botAI->GetGameObject(state.target);
        if (!bank || !bank->isSpawned() || master->GetDistance(bank) > 30.0f)
        {
            StopTrip(botAI, false);
            return false;
        }
        if (bot->GetGameObjectIfCanInteractWith(state.target, GAMEOBJECT_TYPE_GUILD_BANK))
        {
            bot->StopMoving();
            bool queued = QueueCompanionGuildBank(botAI, state.target, state.guildDepositOnly);
            if (!queued && state.guildDepositOnly)
                FinishCompanionInventoryStorage(botAI);
            ObserveCompanionGoal(botAI, GoalDomain::Guild, queued ?
                GoalState::Verifying : GoalState::Failed, "guild_operation_queued");
            state.goal = GoalDomain::Count;
            Returning(botAI);
            return true;
        }
    }
    if (state.kind == Errand::Bags)
    {
        Creature* npc = ObjectAccessor::GetSpawnedCreatureByDBGUID(bot->GetMapId(), state.spawn);
        bool vendor = npc && bot->GetNPCIfCanInteractWith(npc->GetGUID(), UNIT_NPC_FLAG_VENDOR);
        bool repairer = npc && bot->GetNPCIfCanInteractWith(npc->GetGUID(), UNIT_NPC_FLAG_REPAIR);
        if (vendor || repairer)
        {
            bot->StopMoving();
            if (vendor)
                for (Item* item : botAI->GetInventoryItems())
                {
                    if (state.failedSales.count(item->GetGUID()) || !CanSellCompanionItem(botAI, item))
                        continue;
                    ObjectGuid guid = item->GetGUID();
                    uint32 before = item->GetCount();
                    uint32 entry = item->GetEntry();
                    WorldPacket packet(CMSG_SELL_ITEM);
                    packet << npc->GetGUID() << guid << before;
                    WorldPackets::Item::SellItem request(std::move(packet));
                    request.Read();
                    bot->GetSession()->HandleSellItemOpcode(request);
                    Item* remaining = bot->GetItemByGuid(guid);
                    uint32 after = remaining ? remaining->GetCount() : 0;
                    if (after >= before)
                        state.failedSales.insert(guid);
                    else
                    {
                        state.sold += before - after;
                        NotifyCompanionInventoryChanged(botAI);
                        LOG_INFO("playerbots.companion", "[CompanionBags] {} sold {} x {} at {}",
                            bot->GetName(), before - after, entry, npc->GetEntry());
                    }
                    return true;
                }
            state.repairs = RepairEquipment(bot, npc, state.reserve);
            state.rejectedBagVendors.insert(state.spawn);
            if (state.sold || state.repairs || CompanionGoalRequested(botAI, GoalDomain::Maintenance))
                botAI->TellMaster("Entretien termine : " + std::to_string(state.sold) +
                " objets devenus inutiles vendus, " + std::to_string(state.repairs) +
                " pieces de cuivre en reparations. Je vous rejoins.");
            if (state.goal == GoalDomain::Maintenance)
                ObserveCompanionGoal(botAI, state.goal, NeedsEquipmentRepair(bot) || !state.failedSales.empty() ?
                    GoalState::Opportunity : GoalState::Done, "maintenance_finished", 0, state.sold + (state.repairs != 0));
            state.goal = GoalDomain::Count;
            state.events.RescheduleEvent(TownScan, Milliseconds(5000));
            Returning(botAI);
            return true;
        }
        if (npc)
        {
            state.x = npc->GetPositionX();
            state.y = npc->GetPositionY();
            state.z = npc->GetPositionZ();
        }
    }
    if (state.kind == Errand::PoisonVendor || state.kind == Errand::ProfessionVendor ||
        state.kind == Errand::MountVendor || state.kind == Errand::EquipmentVendor || state.kind == Errand::BagVendor)
    {
        bool supplies = state.kind == Errand::ProfessionVendor;
        bool mount = state.kind == Errand::MountVendor;
        Creature* npc = ObjectAccessor::GetSpawnedCreatureByDBGUID(bot->GetMapId(), state.spawn);
        if (npc && bot->GetNPCIfCanInteractWith(npc->GetGUID(), UNIT_NPC_FLAG_VENDOR))
        {
            auto purchase = state.kind == Errand::EquipmentVendor ?
                ChooseEquipment(botAI, npc->GetVendorItems(), state.reserve, npc) : mount ? ChooseMount(botAI, npc->GetVendorItems(), state.reserve, npc) : supplies ?
                ChooseProfessionSupply(bot, npc->GetVendorItems(), state.reserve, NeededProfessionSupplies(bot), npc) :
                ChoosePoison(bot, npc->GetVendorItems(), state.reserve, PoisonStock(bot), npc);
            BagPurchase bag;
            if (state.kind == Errand::BagVendor)
                purchase = {};
            if (state.kind == Errand::BagVendor && CompanionGoalAllowed(botAI, GoalDomain::Bags) &&
                CompanionInventory::BuyBags(CompanionGoalRequested(botAI, GoalDomain::Bags),
                    InventoryCleanupStep(botAI, HasCompanionSale(botAI))))
            {
                bag = ChooseBag(bot, npc->GetVendorItems(), state.reserve, npc);
                purchase = {bag.item, bag.vendorSlot, 1};
            }
            if (supplies && !purchase.item && CompanionGoalAllowed(botAI, GoalDomain::Supplies))
                purchase = ChooseCombatSupply(botAI, npc->GetVendorItems(), state.reserve, npc);
            if (purchase.item)
            {
                bot->StopMoving();
                uint32 bagCapacityBefore = bag.item ? CompanionBagCapacity(bot) : 0;
                uint32 before = bot->GetItemCount(purchase.item, false);
                uint32 previousVendor = bot->GetSession()->GetCurrentVendor();
                bot->GetSession()->SetCurrentVendor(0);
                bot->BuyItemFromVendorSlot(npc->GetGUID(), purchase.slot, purchase.item,
                    uint8(purchase.batches), bag.item ? INVENTORY_SLOT_BAG_0 : NULL_BAG,
                    bag.item ? bag.storageSlot : NULL_SLOT);
                bot->GetSession()->SetCurrentVendor(previousVendor);
                uint32 after = bot->GetItemCount(purchase.item, false);
                if (after > before)
                {
                    if (bag.item && bag.storageSlot != bag.equipSlot)
                        bot->SwapItem(uint16(INVENTORY_SLOT_BAG_0 << 8) | bag.storageSlot,
                            uint16(INVENTORY_SLOT_BAG_0 << 8) | bag.equipSlot);
                    state.inventoryDue = true;
                    if (bag.item)
                    {
                        state.bagInventoryDue = true;
                        ObserveCompanionGoal(botAI, GoalDomain::Bags, GoalState::Verifying, "bag_acquired", 0, 1);
                        if (CompanionBagCapacity(bot) > bagCapacityBefore &&
                            CompanionBagCapacity(bot) >= CompanionGoalTarget(botAI, GoalDomain::Bags))
                            ObserveCompanionGoal(botAI, GoalDomain::Bags, GoalState::Done, "bag_equipped");
                        state.goal = GoalDomain::Count;
                    }
                    if (!bag.item && state.goal != GoalDomain::Count)
                    {
                        ObserveCompanionGoal(botAI, state.goal,
                            state.goal == GoalDomain::Equipment ? GoalState::Pending :
                                state.goal == GoalDomain::Mount ? GoalState::Verifying : GoalState::Done,
                            "item_acquired", 0, 1);
                        state.goal = GoalDomain::Count;
                    }
                    botAI->TellMaster("J'ai achete " +
                        ChatHelper::FormatItem(sObjectMgr->GetItemTemplate(purchase.item)));
                    LOG_INFO("playerbots.companion", "[CompanionErrands] {} bought {} x {} at {}",
                        bot->GetName(), after - before, purchase.item, npc->GetEntry());
                    return true;
                }
            }
            FinishVendorVisit(botAI, purchase.item != 0);
            return true;
        }
        if (npc)
        {
            state.x = npc->GetPositionX();
            state.y = npc->GetPositionY();
            state.z = npc->GetPositionZ();
        }
    }
    if (state.kind == Errand::Trainer)
    {
        // Triggered learning spells may finish after TeachSpell returns. Verify before any new purchase.
        if (state.pendingLesson)
        {
            bool learned = HasLearnedLesson(bot, state.pendingLesson);
            if (!learned && !state.verificationDue)
                return true;
            LOG_INFO("playerbots.companion", "[Training] {} trainer={} spell={} known={} moneyBefore={} moneyAfter={}",
                bot->GetName(), state.pendingTrainer, state.pendingLesson, learned,
                state.moneyBeforeLesson, bot->GetMoney());
            if (learned)
            {
                ++state.learned;
                state.professionDefaultsChecked = false;
                state.professionDue = true;
                ObserveCompanionGoal(botAI, state.goal, GoalState::Verifying, "lesson_confirmed", 0, 1);
                if (auto const* info = sSpellMgr->GetSpellInfo(state.pendingLesson))
                    botAI->TellMaster("J'ai appris " + ChatHelper::FormatSpell(info));
            }
            else
                state.rejectedLessons.insert(state.pendingLesson);
            RecordTraining(bot, learned ? "learned" : "learning_failed", state.pendingTrainer,
                state.pendingLesson, state.learned);
            state.pendingLesson = 0;
            state.verificationDue = false;
            state.events.CancelEvent(VerifyLesson);
            state.events.RescheduleEvent(Progress, Milliseconds(30000));
            return true;
        }
        Creature* npc = ObjectAccessor::GetSpawnedCreatureByDBGUID(bot->GetMapId(), state.spawn);
        if (npc && bot->GetNPCIfCanInteractWith(npc->GetGUID(), UNIT_NPC_FLAG_TRAINER))
        {
            auto* trainer = sObjectMgr->GetTrainer(npc->GetEntry());
            auto const* lesson = ChooseLesson(botAI, trainer, bot->GetReputationPriceDiscount(npc));
            if (lesson)
            {
                state.goal = trainer->GetTrainerType() == Trainer::Type::Class ? GoalDomain::ClassTraining :
                    (lesson->ReqSkillLine == SKILL_RIDING || TeachesRiding(lesson->SpellId)) ? GoalDomain::Mount : GoalDomain::ProfessionTraining;
                state.pendingLesson = lesson->SpellId;
                state.pendingTrainer = npc->GetEntry();
                state.moneyBeforeLesson = bot->GetMoney();
                state.verificationDue = false;
                state.events.RescheduleEvent(VerifyLesson, Milliseconds(3000));
                state.events.RescheduleEvent(Progress, Milliseconds(30000));
                bot->StopMoving();
                trainer->TeachSpell(npc, bot, state.pendingLesson);
                RecordTraining(bot, "verifying", npc->GetEntry(), state.pendingLesson, state.learned);
                ObserveCompanionGoal(botAI, state.goal, GoalState::Verifying, "lesson_pending");
                return true;
            }
            bool budgetBlocked = ChooseLesson(botAI, trainer, bot->GetReputationPriceDiscount(npc), false);
            auto const lastTraining = GetCompanionTrainingSnapshot(bot->GetGUID());
            bool const failed = lastTraining.status == "learning_failed";
            RecordTraining(bot, budgetBlocked ? "budget_insufficient" : failed ? "learning_failed" : "visit_finished",
                npc->GetEntry(), 0, state.learned);
            LOG_INFO("playerbots.companion", "[Training] {} finished trainer={} learned={} money={} reserve={} budget={}",
                bot->GetName(), npc->GetEntry(), state.learned, bot->GetMoney(), state.reserve, budgetBlocked);
            if (!budgetBlocked)
                botAI->TellMaster(failed ? "L'apprentissage a echoue. Je conserve mon argent restant et reprendrai plus tard." :
                    state.learned ? "Mes nouvelles competences sont apprises. Je poursuis mes preparatifs." :
                    "Ce maitre n'a plus de lecon accessible pour mon niveau et mes competences.");
            if (budgetBlocked)
            {
                auto const* blocked = ChooseLesson(botAI, trainer, bot->GetReputationPriceDiscount(npc), false);
                if (blocked)
                {
                    state.goal = trainer->GetTrainerType() == Trainer::Type::Class ? GoalDomain::ClassTraining :
                        (blocked->ReqSkillLine == SKILL_RIDING || TeachesRiding(blocked->SpellId)) ? GoalDomain::Mount : GoalDomain::ProfessionTraining;
                    uint32 cost = uint32(std::floor(blocked->MoneyCost * bot->GetReputationPriceDiscount(npc)));
                    ObserveCompanionGoal(botAI, state.goal, GoalState::Money, "verified_trainer_price",
                        uint32((uint64(cost) * 5 + 3) / 4));
                }
            }
            else
                ObserveCompanionGoal(botAI, state.goal, failed ? GoalState::Failed : GoalState::Opportunity,
                    failed ? "lesson_not_learned" : "local_trainer_exhausted");
            state.goal = GoalDomain::Count;
            state.events.RescheduleEvent(TownScan, Milliseconds(5000));
            Returning(botAI);
            return true;
        }
        if (npc)
        {
            state.x = npc->GetPositionX();
            state.y = npc->GetPositionY();
            state.z = npc->GetPositionZ();
        }
    }
    float distance = bot->GetDistance(state.x, state.y, state.z);
    if (distance + 2.0f < state.closest)
    {
        state.closest = distance;
        state.events.RescheduleEvent(Progress, Milliseconds(30000));
    }
    MoveTo(state.map, state.x, state.y, state.z, false, false, true, true);
    return true;
}
