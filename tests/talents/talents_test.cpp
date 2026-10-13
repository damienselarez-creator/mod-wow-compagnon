#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

using uint8 = uint8_t;
using uint32 = uint32_t;
constexpr uint32 MAX_TALENT_RANK = 5;
constexpr int MAX_SPECNO = 4;
constexpr int CLASS_DRUID = 11;
constexpr int CLASS_SHAMAN = 7;
constexpr int DRUID_TAB_FERAL = 1;
constexpr int SPELL_DRUID_THICK_HIDE = 100;
constexpr int SPELL_SHAMAN_DUAL_WIELD = 101;
constexpr int SKILL_DUAL_WIELD = 1;
#define GET_PLAYERBOT_AI(bot) (bot)
#define LOG_ERROR(...)
uint32 urand(uint32 first, uint32) { return first; }

struct TalentEntry
{
    uint32 TalentID = 0;
    uint32 TalentTab = 0;
    uint32 Row = 0;
    uint32 Col = 0;
    uint32 DependsOn = 0;
    uint32 DependsOnRank = 0;
    std::array<uint32, MAX_TALENT_RANK> RankID{};
};

struct TalentTabEntry
{
    uint32 tabpage = 0;
    uint32 ClassMask = 0;
};

template <class Entry>
struct Store
{
    std::vector<Entry> entries;
    uint32 GetNumRows() const { return entries.size(); }
    Entry const* LookupEntry(uint32 id) const { return id < entries.size() ? &entries[id] : nullptr; }
};

Store<TalentEntry> sTalentStore;
Store<TalentTabEntry> sTalentTabStore;

struct Vocation
{
    bool managed = true;
    int tab = 0;
};

struct Player
{
    uint8 cls = 1;
    uint32 level = 80;
    uint32 freePoints = 71;
    Vocation vocation;
    std::map<uint32, uint32> learned;
    bool allowed = true;
    bool dualWield = false;
    uint32 resets = 0;
    uint32 sent = 0;
    uint32 denied = 0;

    uint8 getClass() const { return cls; }
    uint32 getClassMask() const { return 1u << (cls - 1); }
    uint32 GetLevel() const { return level; }
    uint32 GetFreeTalentPoints() const { return freePoints; }
    uint32 GetActiveSpec() const { return 0; }
    bool HasAura(uint32) const { return false; }
    bool HasSpell(uint32) const { return false; }
    void SetSkill(uint32, uint32, uint32, uint32) { }
    void SetCanDualWield(bool enabled) { dualWield = enabled; }
    void SendTalentsInfoData(bool) { ++sent; }
    void resetTalents(bool)
    {
        for (auto const& [id, count] : learned)
            freePoints += count;
        learned.clear();
        ++resets;
    }
    bool HasTalent(uint32 spell, uint32) const
    {
        for (auto const& [id, count] : learned)
            if (count && sTalentStore.entries[id].RankID[count - 1] == spell)
                return true;
        return false;
    }
    std::map<uint8, uint32> Points() const
    {
        std::map<uint8, uint32> points = {{0, 0}, {1, 0}, {2, 0}};
        for (auto const& [id, count] : learned)
            points[sTalentTabStore.entries[sTalentStore.entries[id].TalentTab].tabpage] += count;
        return points;
    }
    void LearnTalent(uint32 id, uint32 rank)
    {
        auto const* talent = sTalentStore.LookupEntry(id);
        auto const* tab = talent ? sTalentTabStore.LookupEntry(talent->TalentTab) : nullptr;
        uint32 oldRank = learned.contains(id) ? learned.at(id) : 0;
        if (!tab || !(tab->ClassMask & getClassMask()) || rank >= MAX_TALENT_RANK ||
            !talent->RankID[rank] || rank + 1 <= oldRank || rank + 1 - oldRank > freePoints ||
            level < 10 || Points()[tab->tabpage] < 5 * talent->Row ||
            (talent->DependsOn && (!learned.contains(talent->DependsOn) ||
                learned.at(talent->DependsOn) <= talent->DependsOnRank)))
        {
            ++denied;
            return;
        }
        freePoints -= rank + 1 - oldRank;
        learned[id] = rank + 1;
    }
};

namespace AiFactory
{
    std::map<uint8, uint32> GetPlayerSpecTabs(Player* bot) { return bot->Points(); }
    uint32 GetPlayerSpecTab(Player* bot)
    {
        auto points = bot->Points();
        uint32 chosen = 0;
        for (uint32 tab = 1; tab < 3; ++tab)
            if (points[tab] > points[chosen])
                chosen = tab;
        return chosen;
    }
}

namespace CompanionVocation
{
    Vocation Get(Player* bot) { return bot->vocation; }
}

namespace CompanionGoals
{
    enum class Domain { Talents };
}

bool CompanionGoalAllowed(Player* bot, CompanionGoals::Domain) { return bot->allowed; }

struct Config
{
    std::array<std::array<uint32, 4>, 12> randomClassSpecProb{};
    std::array<std::array<uint32, 4>, 12> randomClassSpecIndex{};
    std::map<uint32, std::map<uint32, std::map<int, std::vector<std::vector<uint32>>>>> parsedSpecLinkOrder;
    bool limitTalentsExpansion = false;
} sPlayerbotAIConfig;

struct PlayerbotFactory
{
    Player* bot;
    std::vector<uint32> fallbackTrees;
    uint32 InitTalentsTree(bool increment = false, bool use_template = true, bool reset = false);
    void InitTalentsByTemplate(uint32 specTab, bool primaryOnly = false);
    void InitCompanionTalents(uint32 specNo);
    void InitTalents(uint32 specNo) { fallbackTrees.push_back(specNo); }
};

#include "tree.inc"
#include "allocation.inc"

void Fixtures(uint8 cls, uint32 chosen, bool withTemplate = true)
{
    sTalentStore.entries.clear();
    sTalentTabStore.entries.clear();
    sPlayerbotAIConfig = {};
    for (uint32 tab = 0; tab < 3; ++tab)
    {
        sTalentTabStore.entries.push_back({tab, 1u << (cls - 1)});
        sPlayerbotAIConfig.randomClassSpecProb[cls][tab] = 1;
        sPlayerbotAIConfig.randomClassSpecIndex[cls][tab] = tab;
        for (uint32 index = 0; index < 15; ++index)
        {
            TalentEntry talent;
            talent.TalentID = sTalentStore.entries.size();
            talent.TalentTab = tab;
            talent.Row = index / 2;
            talent.Col = index % 2;
            if (index == 3)
            {
                talent.DependsOn = talent.TalentID - 1;
                talent.DependsOnRank = 2;
            }
            for (uint32 rank = 0; rank < MAX_TALENT_RANK; ++rank)
                talent.RankID[rank] = 1000 + talent.TalentID * 10 + rank;
            sTalentStore.entries.push_back(talent);
        }
    }
    if (withTemplate)
    {
        auto& path = sPlayerbotAIConfig.parsedSpecLinkOrder[cls][chosen][80];
        // A mixed endgame template deliberately starts with an off-tree talent.
        path.push_back({(chosen + 1) % 3, 0, 0, 5});
        for (uint32 index = 0; index < 11; ++index)
            path.push_back({chosen, index / 2, index % 2, 5});
        path.push_back({(chosen + 2) % 3, 0, 0, 5});
    }
}

void ExclusivePoints(Player const& bot, uint32 chosen, uint32 expected)
{
    auto points = bot.Points();
    assert(points[chosen] == expected);
    assert(points[(chosen + 1) % 3] == 0);
    assert(points[(chosen + 2) % 3] == 0);
    assert(bot.resets == 0);
}

int main()
{
    for (uint8 cls : {1, 2, 3, 4, 5, 6, 7, 8, 9, 11})
    {
        for (uint32 chosen = 0; chosen < 3; ++chosen)
        {
            Fixtures(cls, chosen);
            Player bot;
            bot.cls = cls;
            bot.vocation.tab = chosen;
            PlayerbotFactory factory{&bot};
            assert(factory.InitTalentsTree(true, true, true) == chosen);
            ExclusivePoints(bot, chosen, 71);
            assert(bot.freePoints == 0 && bot.sent == 1 && factory.fallbackTrees.empty());
            factory.InitTalentsTree(true);
            ExclusivePoints(bot, chosen, 71);
            bot.freePoints = 9;
            factory.InitTalentsTree(true);
            ExclusivePoints(bot, chosen, 75);
            assert(bot.freePoints == 5); // Exhausted tree never spills into another tree.
        }
    }

    Fixtures(1, 2, false);
    Player growing;
    growing.vocation.tab = 2;
    growing.level = 10;
    growing.freePoints = 1;
    PlayerbotFactory growingFactory{&growing};
    growingFactory.InitTalentsTree(true);
    ExclusivePoints(growing, 2, 1);
    growing.level = 40;
    growing.freePoints = 30;
    growingFactory.InitTalentsTree(true, false);
    ExclusivePoints(growing, 2, 31);

    // Missing/invalid selected-tree data keeps the points and terminates without a secondary fallback.
    Fixtures(1, 2, false);
    sTalentStore.entries.erase(sTalentStore.entries.begin() + 30, sTalentStore.entries.end());
    Player missing;
    missing.vocation.tab = 2;
    PlayerbotFactory missingFactory{&missing};
    missingFactory.InitTalentsTree(true);
    assert(missing.freePoints == 71 && missingFactory.fallbackTrees.empty());

    Fixtures(1, 2, false);
    Player blocked;
    blocked.vocation.tab = 2;
    for (auto& talent : sTalentStore.entries)
        if (talent.TalentTab == 2)
            talent.Row = 1;
    PlayerbotFactory blockedFactory{&blocked};
    blockedFactory.InitTalentsTree(true);
    assert(blocked.freePoints == 71 && blocked.denied > 0);

    Fixtures(1, 2);
    Player undecided;
    undecided.vocation.tab = -1;
    PlayerbotFactory{&undecided}.InitTalentsTree(true);
    assert(undecided.freePoints == 71 && undecided.sent == 0);
    Player paused;
    paused.allowed = false;
    PlayerbotFactory{&paused}.InitTalentsTree(true);
    assert(paused.freePoints == 71 && paused.sent == 0);
    Player novice;
    novice.vocation.tab = 2;
    novice.level = 9;
    novice.freePoints = 0;
    PlayerbotFactory{&novice}.InitTalentsTree(true);
    ExclusivePoints(novice, 2, 0);

    // Existing manual choices survive; only the remaining points change policy.
    Player hybrid;
    hybrid.vocation.tab = 2;
    hybrid.learned[30] = 5;
    hybrid.learned[0] = 1;
    hybrid.freePoints = 65;
    PlayerbotFactory{&hybrid}.InitTalentsTree(true, true, true);
    assert(hybrid.Points()[2] == 70 && hybrid.Points()[0] == 1 && hybrid.resets == 0);
    Player divergent;
    divergent.vocation.tab = 2;
    divergent.learned[0] = 5;
    PlayerbotFactory{&divergent}.InitTalentsTree(true);
    assert(divergent.freePoints == 71 && divergent.sent == 0);

    Player unmanaged;
    unmanaged.vocation.managed = false;
    unmanaged.learned[30] = 5;
    PlayerbotFactory unmanagedFactory{&unmanaged};
    unmanagedFactory.InitTalentsTree(true, false);
    assert((unmanagedFactory.fallbackTrees == std::vector<uint32>{0, 1}));
    std::cout << "Single-tree talents: ten classes, three trees, growth, exhaustion and safeguards PASS\n";
}
