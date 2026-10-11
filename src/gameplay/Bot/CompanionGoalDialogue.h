#ifndef COMPANION_GOAL_DIALOGUE_H
#define COMPANION_GOAL_DIALOGUE_H

#include "CompanionGoalPolicy.h"
#include "pbc_vocation_policy.h"
#include <optional>

namespace CompanionGoals
{
    struct Intent
    {
        Domain domain = Domain::Count;
        Directive directive = Directive::Status;
    };

    inline std::optional<Intent> Parse(std::string const& message, Domain topic)
    {
        using PBCVocationPolicy::Has;
        auto words = PBCVocationPolicy::Words(message);
        if (message.find('"') != std::string::npos || message.find("«") != std::string::npos ||
            Has(words, "exemple") || Has(words, "example") || Has(words, "si") || Has(words, "if"))
            return {};
        Domain domain = Domain::Count;
        auto any = [&](std::initializer_list<char const*> phrases)
        {
            for (auto phrase : phrases)
                if (Has(words, phrase))
                    return true;
            return false;
        };
        if (any({"talent", "talents", "points de talent", "talent points"}))
            domain = Domain::Talents;
        else if (any({"sac", "sacs", "bag", "bags", "bagages"}))
            domain = Domain::Bags;
        else if (any({"maitre de metier", "maitres de metier", "maitres de metiers", "maitre de profession", "maitres de profession", "metiers", "professions", "formation des metiers", "profession trainer"}))
            domain = Domain::ProfessionTraining;
        else if (any({"maitre de classe", "maitres de classe", "sorts", "formation", "maitres", "class trainer", "class training", "spells"}))
            domain = Domain::ClassTraining;
        else if (any({"equipement", "armure", "bouclier", "arme", "armes", "equipment", "armor", "shield", "weapon", "weapons"}))
            domain = Domain::Equipment;
        else if (any({"reparation", "reparations", "repare", "vendre", "vente", "repair", "sell"}))
            domain = Domain::Maintenance;
        else if (any({"fournitures", "outils", "reagents", "supplies", "tools"}))
            domain = Domain::Supplies;
        else if (any({"monture", "montures", "equitation", "mount", "mounts", "riding"}))
            domain = Domain::Mount;
        else if (any({"minage", "miner", "herboristerie", "recolte", "recolter", "mining", "herbalism", "gather", "gathering", "farm", "farmer"}))
            domain = Domain::Gathering;
        else if (any({"forge", "forger", "fabriquer", "fabrication", "artisanat", "craft", "crafting", "blacksmithing"}))
            domain = Domain::Crafting;
        else if (any({"coffre", "guilde", "guild", "guild bank"}))
            domain = Domain::Guild;

        Directive directive = Directive::Status;
        bool control = true;
        if (any({"n achete pas", "n achete rien", "ne depense pas", "ne fais pas", "ne fabrique pas", "ne mine pas", "ne recolte pas", "interdit",
            "do not buy", "don t buy", "do not craft", "do not gather", "never"}))
            directive = Directive::Forbid;
        else if (any({"laisse tomber", "abandonne", "annule", "forget it", "cancel", "drop it"}))
            directive = Directive::Cancel;
        else if (any({"pas d argent", "pas l argent", "je n ai pas les moyens", "je peux pas t aider",
            "je ne peux pas t aider", "no money", "can t afford", "cannot afford", "can t help"}))
            directive = Directive::NoHelp;
        else if (any({"pas maintenant", "plus tard", "attends", "not now", "later", "wait"}))
            directive = Directive::Postpone;
        else if (any({"reprends", "tu peux recommencer", "resume", "you may resume"}))
            directive = Directive::Resume;
        else if (any({"achete", "acheter", "achetes", "apprends", "va voir", "occupe toi", "forger",
            "fabrique", "fabriquer", "recolte", "recolter", "mine", "miner", "repare", "vends",
            "debrouille toi", "repartis", "depense", "buy", "learn", "go to", "craft", "gather", "repair", "sell", "handle it"}))
            directive = Directive::Request;
        else
            control = false;
        if (directive == Directive::Request &&
            (any({"ne", "pas", "non", "not", "no", "pourquoi", "why", "comment", "how"}) ||
                (message.find('?') != std::string::npos &&
                    !any({"peux tu", "pourrais tu", "tu peux", "can you", "could you"}))))
            return {};
        if (domain == Domain::Count)
        {
            if (!control || topic == Domain::Count)
                return {};
            if (directive == Directive::Request && !any({"en acheter", "en achete", "achete les", "achete le", "buy them", "buy it", "handle it", "occupe toi", "debrouille toi"}))
                return {}; // An unrelated object must not inherit the previous shopping topic.
            domain = topic;
        }
        else if (!control && !any({"tu", "tes", "ton", "ta", "tes", "you", "your", "quoi", "combien", "what", "how"}))
            return {};
        return Intent{domain, directive};
    }
}
#endif
