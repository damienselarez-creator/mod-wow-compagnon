#ifndef PBC_PERSONALITY_MODEL_H
#define PBC_PERSONALITY_MODEL_H

#include "pbc_json.h"
#include <array>
#include <set>
#include <string>

inline bool PBC_ValidPersonalityCatalog(pbc_json const& catalog)
{
    try
    {
        if (catalog.at("schema") != "wow-compagnon.personality-traits.v1" ||
            catalog.at("selection").at("mode") != "independent" ||
            catalog.at("selection").at("qualities") != 3 || catalog.at("selection").at("flaws") != 3 ||
            !catalog.at("pairs").is_array() || catalog.at("pairs").size() != 30)
            return false;
        std::set<std::string> ids;
        for (auto const& pair : catalog.at("pairs"))
            for (std::string const category : {"quality", "flaw"})
            {
                auto const& trait = pair.at(category);
                auto id = trait.at("id").get<std::string>();
                if (!id.starts_with(category + "_") || id.size() > 64 || !ids.insert(id).second)
                    return false;
                for (char c : id)
                    if ((c < 'a' || c > 'z') && c != '_')
                        return false;
                for (auto const& label : {trait.at("labels").at("frFR").at("male"),
                    trait.at("labels").at("frFR").at("female"), trait.at("labels").at("enUS").at("neutral")})
                {
                    auto text = label.get<std::string>();
                    if (text.empty() || text.size() > 100 || text.find_first_of("\r\n[]{}") != std::string::npos)
                        return false;
                }
            }
        return true;
    }
    catch (pbc_json::exception const&)
    {
        return false;
    }
}

inline bool PBC_ValidPersonalitySheet(pbc_json const& sheet, pbc_json const& catalog)
{
    try
    {
        for (auto field : {"account", "race", "class"})
            if (!sheet.at(field).is_number_unsigned() || sheet.at(field).get<uint32_t>() == 0)
                return false;
        if (!sheet.at("tab").is_number_integer() || sheet.at("tab").get<int>() < 0 ||
            sheet.at("tab").get<int>() > 2 || !sheet.at("professions").is_array() ||
            sheet.at("professions").size() != 2)
            return false;
        std::set<uint32_t> const primary = {164, 165, 171, 182, 186, 197, 202, 333, 393, 755, 773};
        auto skills = sheet.at("professions").get<std::array<uint32_t, 2>>();
        if (!primary.contains(skills[0]) || !primary.contains(skills[1]) || skills[0] == skills[1])
            return false;
        for (auto category : {"quality", "flaw"})
        {
            auto const& choices = sheet.at(std::string(category) + "_ids");
            if (!choices.is_array() || choices.size() != 3)
                return false;
            std::set<std::string> distinct;
            for (auto const& choice : choices)
            {
                auto id = choice.get<std::string>();
                bool found = false;
                for (auto const& pair : catalog.at("pairs"))
                    found = found || pair.at(category).at("id") == id;
                if (!found || !distinct.insert(id).second)
                    return false;
            }
        }
        return true;
    }
    catch (pbc_json::exception const&)
    {
        return false;
    }
}

inline std::string PBC_RenderPersonality(pbc_json const& sheet, pbc_json const& catalog,
    uint8_t gender, uint8_t locale)
{
    bool french = locale == 2;
    std::string out = french ? "\n[PERSONNALITE DEFINITIVE]\nGenre du personnage : " :
        "\n[PERMANENT PERSONALITY]\nCharacter gender: ";
    out += gender == 1 ? (french ? "feminin" : "female") :
        gender == 0 ? (french ? "masculin" : "male") : (french ? "non precise" : "unspecified");
    out += ".\n";
    if (!sheet.is_null() && PBC_ValidPersonalitySheet(sheet, catalog))
    {
        for (auto category : {"quality", "flaw"})
        {
            out += std::string(category) == "quality" ? (french ? "Qualites : " : "Qualities: ") :
                (french ? "Defauts : " : "Flaws: ");
            bool first = true;
            for (auto const& id : sheet.at(std::string(category) + "_ids"))
                for (auto const& pair : catalog.at("pairs"))
                    if (pair.at(category).at("id") == id)
                    {
                        if (!first)
                            out += ", ";
                        auto const& labels = pair.at(category).at("labels");
                        out += french ? labels.at("frFR").at(gender == 1 ? "female" : "male").get<std::string>() :
                            labels.at("enUS").at("neutral").get<std::string>();
                        first = false;
                    }
            out += ".\n";
        }
        out += french ?
            "Ces six traits guident tes reactions, tes priorites et ta voix, avec nuance. "
            "Ne les recite pas a chaque reponse. Tes defauts ne sont pas des qualites imposees en miroir. "
            "Les souvenirs et les relations peuvent evoluer sans remplacer ces traits.\n" :
            "These six traits guide your reactions, priorities and voice with nuance. "
            "Do not recite them in every reply. Flaws are selected independently of qualities. "
            "Memories and relationships may evolve without replacing these traits.\n";
    }
    else
        out += french ? "Aucun choix definitif de traits enregistre ; n'en invente pas.\n" :
            "No permanent trait choices recorded; do not invent any.\n";
    out += french ? "Le genre ne determine pas automatiquement le caractere ou le role.\n" :
        "Gender does not automatically determine personality or role.\n";
    return out;
}

#endif
