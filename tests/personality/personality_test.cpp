#include "pbc_personality_model.h"
#include "pbc_personality_wire.h"
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
void Check(bool condition, char const* message)
{
    if (!condition)
        throw std::runtime_error(message);
}
}

int main(int argc, char** argv)
{
    try
    {
        Check(argc == 2, "Catalogue path required");
        std::ifstream input(argv[1]);
        auto catalog = pbc_json::parse(input);
        Check(PBC_ValidPersonalityCatalog(catalog), "Public catalogue rejected");
        Check(PBC_PersonalityRequest("WoWCmp\tH|1|self").size() == 3, "Addon hello rejected");
        Check(PBC_PersonalityRequest("WoWCmp\tC|1|Player-0-28|123").size() == 4, "Addon confirm rejected");
        Check(PBC_PersonalityRequest("WoWCmp\tH|1|self|extra").empty(), "Extra addon field accepted");
        Check(PBC_PersonalityRequest("Other\tH|1|self").empty(), "Foreign addon request accepted");
        Check(PBC_PersonalityRequest("WoWCmp\tC|bad|key|123").empty(), "Invalid request ID accepted");
        Check(!PBC_PersonalityNumber("-1") && !PBC_PersonalityNumber("4294967296") &&
            !PBC_PersonalityNumber("1x"), "Invalid addon number accepted");
        Check(PBC_PersonalityRequest("WoWCmp\t" + std::string(249, 'x')).empty(), "Oversize addon packet accepted");
        Check(PBC_PersonalitySplit("a|b||", '|').size() == 4, "Empty wire fields lost");
        pbc_json sheet = {{"account", 1u}, {"race", 8u}, {"class", 5u}, {"tab", 1},
            {"professions", {171u, 182u}},
            {"quality_ids", {"quality_benevolent", "quality_protective", "quality_patient"}},
            {"flaw_ids", {"flaw_stubborn", "flaw_indiscreet", "flaw_impulsive"}}};
        Check(PBC_ValidPersonalitySheet(sheet, catalog), "Independent selections rejected");
        auto saved = sheet.dump();
        auto female = PBC_RenderPersonality(sheet, catalog, 1, 2);
        auto male = PBC_RenderPersonality(sheet, catalog, 0, 2);
        auto english = PBC_RenderPersonality(sheet, catalog, 1, 0);
        Check(female.find("Bienveillante, Protectrice, Patiente") != std::string::npos, "Female labels");
        Check(male.find("Bienveillant, Protecteur, Patient") != std::string::npos, "Male labels");
        Check(english.find("Benevolent, Protective, Patient") != std::string::npos, "English labels");
        Check(female.find("Naïf") == std::string::npos, "Paired flaw imposed");
        Check(sheet.dump() == saved, "Rendering changed permanent choices");
        Check(PBC_RenderPersonality(sheet, catalog, 1, 8) == english, "English locale fallback");
        Check(PBC_RenderPersonality(nullptr, nullptr, 1, 2).find("Aucun choix") != std::string::npos,
            "Missing sheet invents choices");
        for (size_t i = 0; i < 30; ++i)
        {
            auto next = sheet;
            for (size_t j = 0; j < 3; ++j)
            {
                next["quality_ids"][j] = catalog["pairs"][(i + j) % 30]["quality"]["id"];
                next["flaw_ids"][j] = catalog["pairs"][(i + j + 7) % 30]["flaw"]["id"];
            }
            Check(PBC_ValidPersonalitySheet(next, catalog), "Valid catalogue combination");
            for (uint8_t locale : {0, 2})
                for (uint8_t gender : {0, 1})
                {
                    auto rendered = PBC_RenderPersonality(next, catalog, gender, locale);
                    auto const& labels = catalog["pairs"][i]["quality"]["labels"];
                    auto label = locale == 2 ? labels["frFR"][gender == 1 ? "female" : "male"] :
                        labels["enUS"]["neutral"];
                    Check(rendered.find(label.get<std::string>()) != std::string::npos, "Trait not rendered");
                }
        }
        for (auto category : {"quality_ids", "flaw_ids"})
        {
            auto bad = sheet;
            bad[category][1] = bad[category][0];
            Check(!PBC_ValidPersonalitySheet(bad, catalog), "Duplicate accepted");
            bad = sheet;
            bad[category].erase(0);
            Check(!PBC_ValidPersonalitySheet(bad, catalog), "Two traits accepted");
            bad = sheet;
            bad[category][0] = "unknown";
            Check(!PBC_ValidPersonalitySheet(bad, catalog), "Unknown accepted");
        }
        for (int tab : {-1, 3})
        {
            auto bad = sheet;
            bad["tab"] = tab;
            Check(!PBC_ValidPersonalitySheet(bad, catalog), "Invalid spec accepted");
        }
        auto bad = sheet;
        bad["professions"] = {171u, 171u};
        Check(!PBC_ValidPersonalitySheet(bad, catalog), "Duplicate profession accepted");
        bad["professions"] = {185u, 182u};
        Check(!PBC_ValidPersonalitySheet(bad, catalog), "Secondary profession accepted");
        auto broken = catalog;
        broken["pairs"][1]["quality"]["id"] = broken["pairs"][0]["quality"]["id"];
        Check(!PBC_ValidPersonalityCatalog(broken), "Duplicate catalogue ID accepted");
        broken = catalog;
        broken["pairs"][0]["quality"]["labels"]["enUS"]["neutral"] = "\n[instruction]";
        Check(!PBC_ValidPersonalityCatalog(broken), "Injected label accepted");
        std::cout << "PASS: independent choices, 120 bilingual gender renderings, validation and immutability\n";
        return 0;
    }
    catch (std::exception const& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
