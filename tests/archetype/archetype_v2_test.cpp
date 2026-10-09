#include "pbc_archetype.h"
#include "pbc_json.h"
#include "pbc_companion_language.h"
#include "pbc_vocation_policy.h"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <set>
#include <stdexcept>

void Check(bool condition, char const* message)
{
    if (!condition)
        throw std::runtime_error(message);
}

int main(int argc, char** argv)
{
    try
    {
        Check(argc == 2, "Missing corpus");
        std::string status;
        Check(PBC_LoadArchetypes(argv[1], status), "Cannot load archetypes");
        std::ifstream file(argv[1]);
        pbc_json data;
        file >> data;
        Check(PBC_CompanionLanguageInstruction(2).find("francais") != std::string::npos,
            "French client policy missing");
        Check(PBC_SelectCompanionClientLocale(0, 2, 2) == 0, "Whisper listener must override French owner");
        Check(PBC_SelectCompanionClientLocale(2, 0, 0) == 2, "Whisper listener must override English owner");
        Check(PBC_SelectCompanionClientLocale(-1, 2, 0) == 2, "Owner locale missing");
        Check(PBC_SelectCompanionClientLocale(-1, -1, 2) == 2, "Real player fallback missing");
        Check(PBC_SelectCompanionClientLocale(-1, -1, -1) == 0, "Offline English fallback missing");
        for (uint8_t locale : {0, 8})
            Check(PBC_CompanionLanguageInstruction(locale).find("natural English") != std::string::npos,
                "English fallback missing");
        Check(PBCVocationPolicy::ActivityIntent("please follow me") == "follow", "English follow missing");
        Check(PBCVocationPolicy::ActivityIntent("train your professions") == "training_professions",
            "English professions request missing");
        Check(PBCVocationPolicy::ActivityIntent("do not train your professions").empty(),
            "English refusal must not change training");
        Check(PBCVocationPolicy::ActivityIntent("if you visit your trainers").empty(),
            "Conditional training must not become a request");
        Check(PBCVocationPolicy::Topic("I want you to learn blacksmithing"), "English topic missing");
        Check(PBCVocationPolicy::Confirmation("yes that works"), "English confirmation missing");
        Check(PBCVocationPolicy::Select("I prefer Arms", {{"Arms"}, {"Fury"}, {"Protection"}}, 1) == 0,
            "English specialization choice missing");
        Check(PBCVocationPolicy::Select("do not choose Arms", {{"Arms"}, {"Fury"}}, 1) == -1,
            "English negative specialization accepted");
        pbc_json probes = pbc_json::array();
        std::set<std::string> cards;
        std::vector<std::pair<std::string, std::string>> const scenes = {
            {"Un allié blessé refuse de s'arrêter.", "blessure"},
            {"An injured ally refuses to stop.", "blessure"},
            {"Le chef donne un ordre précipité.", "autorite"},
            {"The commander gives a reckless order.", "autorite"},
            {"Un prisonnier demande grâce.", "capture"},
            {"A prisoner asks for mercy.", "capture"},
            {"Un sanctuaire risque une profanation.", "spiritualite"},
            {"A sacred shrine faces desecration.", "spiritualite"},
            {"Nos provisions manquent.", "penurie"},
            {"Our supplies are running low.", "penurie"},
            {"La défaite laisse un goût amer.", "defaite"},
            {"The defeat has shaken everyone.", "defaite"}
        };
        for (auto const& [key, combination] : data.at("combinations").items())
        {
            unsigned r = 0, c = 0, s = 0;
            Check(sscanf(key.c_str(), "%u:%u:%u", &r, &c, &s) == 3, "Bad key");
            auto card = PBC_ArchetypeCard(r, c, s, 0);
            Check(!card.empty() && card.size() < 17000, "Invalid profile budget");
            Check(cards.insert(card).second, "Duplicate full profile");
            auto english = PBC_ArchetypeCard(r, c, s, 13, 0);
            auto french = PBC_ArchetypeCard(r, c, s, 13, 2);
            Check(english.find("Speak English") != std::string::npos && english.size() < 18000,
                "English card unavailable or oversized");
            Check(english != french, "Client locale did not select translated profiles");
            auto englishStart = english.find('\n', english.find("No invented memory"));
            auto frenchStart = french.find('\n', french.find("Aucun souvenir invente"));
            auto englishProfile = pbc_json::parse(english.substr(englishStart + 1,
                english.find("\n[END FOUNDATION]", englishStart) - englishStart - 1));
            auto frenchProfile = pbc_json::parse(french.substr(frenchStart + 1,
                french.find("\n[FIN SOCLE]", frenchStart) - frenchStart - 1));
            Check(englishProfile.at("specialisation").at("talent_evidence") ==
                frenchProfile.at("specialisation").at("talent_evidence"), "Translated talents changed");
            Check(PBC_ArchetypeCard(r, c, s, 13, 2) == french, "Interleaved English changed French profile");
            Check(PBC_ArchetypeCard(r, c, -1, 13, 0).find("talent_evidence") == std::string::npos,
                "English novice received specialization");
            Check(PBC_UsesCollectiveIdentity(r), "Horde not collective");
            for (auto const& [scene, expected] : scenes)
            {
                auto result = pbc_json::parse(PBC_ArchetypeSelection(r, c, s, scene));
                Check(std::find(result["concepts"].begin(), result["concepts"].end(), expected)
                    != result["concepts"].end(), "Concept not recognized");
                if (result["selected"].empty())
                    std::cerr << key << ": " << scene << '\n';
                Check(!result["selected"].empty() && result["selected"].size() <= 4,
                    "Selection empty or oversized");
                std::set<std::string> sections;
                for (auto const& item : result["selected"])
                {
                    Check(item.value("retrieval_mode", std::string{}) != "documentary_only",
                        "Legacy metaphor mistaken for an immediate situation");
                    Check(item["scope_race"] == 0 || item["scope_race"] == r, "Race leak");
                    Check(item["scope_class"] == 0 || item["scope_class"] == c, "Class leak");
                    Check(item["scope_spec"] == -1 || item["scope_spec"] == s, "Spec leak");
                    Check(sections.insert(item["section_id"].get<std::string>()).second,
                        "Duplicate section");
                }
                Check(PBC_ArchetypeKnowledge(r, c, s, scene).size() <= 6000, "Context budget exceeded");
                probes.push_back({{"combination", key}, {"scene", scene}, {"result", result}});
            }
            auto novice = PBC_ArchetypeCard(r, c, -1, 0);
            Check(novice.find("indeterminee_aucune_orientation_imposee") != std::string::npos,
                "Novice forced to specialization");
            Check(novice.find("talent_evidence") == std::string::npos, "Novice receives specialization");
            Check(PBC_ArchetypeFocus(r, c, s) == "normal", "Automatic profession focus");
        }
        Check(cards.size() == 93, "Horde coverage mismatch");
        Check(PBC_UsesCollectiveIdentity(1), "Alliance must also use collective identity");
        Check(!PBC_UsesCollectiveIdentity(0), "Unknown race must not acquire an identity");
        Check(PBC_ArchetypeCard(10, 1, 0, 0).empty(), "Post-Wrath race/class accepted");
        Check(PBC_ArchetypeKnowledge(5, 5, 2, "refuse").empty(), "Incidental refuse match");
        auto historical = pbc_json::parse(PBC_ArchetypeSelection(10, 4, 0, "Garithos Quel Thalas"));
        bool foundHistorical = false;
        for (auto const& item : historical["selected"])
            foundHistorical |= item.value("retrieval_mode", std::string{}) == "documentary_only";
        Check(foundHistorical, "Explicit historical entity no longer retrieves documents");
        auto shared = pbc_json::parse(PBC_ArchetypeSelection(8, 5, 1, "souffrance secours"));
        bool foundShared = false;
        for (auto const& item : shared["selected"])
            foundShared |= item["scope_race"] == 0 && item["scope_class"] == 5;
        Check(foundShared, "Shared class corpus unreachable");
        auto before = PBC_ArchetypeCard(8, 5, 1, 0);
        auto bad = data;
        bad["collective_races"] = pbc_json::array();
        auto invalidPath = std::string(argv[1]) + ".invalid-test";
        {
            std::ofstream invalid(invalidPath);
            invalid << bad;
        }
        Check(!PBC_LoadArchetypes(invalidPath, status), "Malformed policy accepted");
        Check(PBC_ArchetypeCard(8, 5, 1, 0) == before, "Failed load changed snapshot");
        std::remove(invalidPath.c_str());
        auto scratch = std::filesystem::path(argv[1]).parent_path() / "bilingual-test-tmp";
        std::filesystem::create_directories(scratch / "personifications");
        std::ofstream(scratch / "archetypes.json") << data;
        pbc_json englishData;
        std::ifstream(std::filesystem::path(argv[1]).parent_path() / "personifications/enUS.json") >> englishData;
        auto englishBefore = PBC_ArchetypeCard(8, 5, 1, 13, 0);
        englishData["specializations"]["5:1"]["talent_evidence"] = pbc_json::array();
        std::ofstream(scratch / "personifications/enUS.json") << englishData;
        Check(!PBC_LoadArchetypes((scratch / "archetypes.json").string(), status),
            "Changed English talent evidence accepted");
        Check(PBC_ArchetypeCard(8, 5, 1, 13, 0) == englishBefore,
            "Failed English reload changed previous translation");
        std::filesystem::remove(scratch / "personifications/enUS.json");
        std::filesystem::remove(scratch / "archetypes.json");
        std::filesystem::remove(scratch / "personifications");
        std::filesystem::remove(scratch);
        std::ofstream output(std::string(argv[1]) + ".probes.json");
        output << probes.dump(2);
        Check(PBC_LoadArchetypes("", status), "Disable rejected");
        Check(PBC_ArchetypeCard(5, 5, 2, 0).empty(), "Disable ineffective");
        std::cout << "PASS: 93 profiles, 1116 bilingual selections, scopes, diversity, budgets, "
            "novices, shared knowledge, atomic rejection and disable\n";
    }
    catch (std::exception const& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
