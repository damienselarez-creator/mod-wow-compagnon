/*
 * WoW Compagnon, GNU GPL v2 or later. See the preserved component author notices.
 */
#ifndef COMPANION_FARM_DIALOGUE_H
#define COMPANION_FARM_DIALOGUE_H
#include "CompanionFarmPolicy.h"
#include "pbc_vocation_policy.h"
namespace CompanionFarm
{
    inline Intent Parse(std::string const& message)
    {
        using PBCVocationPolicy::Has;
        auto words = PBCVocationPolicy::Words(message);
        if (message.find('"') != std::string::npos ||
            message.find("«") != std::string::npos || Has(words, "exemple") || Has(words, "example") ||
            Has(words, "si") || Has(words, "if"))
            return Intent::None;
        if (Has(words, "ou en es tu avec le farming") || Has(words, "tu en es ou du farming") ||
            Has(words, "tu farmes") || Has(words, "how is the farming") || Has(words, "are you farming"))
            return Intent::Status;
        if (PBCVocationPolicy::Hesitation(words) || Has(words, "quand") || Has(words, "when") ||
            Has(words, "pourquoi") || Has(words, "why") ||
            Has(words, "comment") || Has(words, "how"))
            return Intent::None;
        for (auto phrase : {"reviens", "reviens avec moi", "reviens me rejoindre", "rejoins moi", "suis moi",
            "reviens pres de moi", "viens avec moi", "on repart ensemble", "arrete de farmer",
            "arret du farming", "stop farming", "come back to me", "come with me", "follow me",
            "on repart a l aventure", "retour a l aventure", "revenir avec moi", "tu peux me rejoindre",
            "peux tu me rejoindre"})
            if (Has(words, phrase) && !Has(words, "pas") && !Has(words, "not") && !Has(words, "ne"))
                return Intent::Return;
        bool farming = Has(words, "farmer") || Has(words, "farming") || Has(words, "farm") ||
            Has(words, "recolter seul") || Has(words, "recolte de ton cote") || Has(words, "gather on your own");
        if (!farming)
            return Intent::None;
        if (Has(words, "ou en es tu") || Has(words, "tu farmes") || Has(words, "tu en es ou") ||
            Has(words, "how is") || Has(words, "are you farming"))
            return Intent::Status;
        bool question = message.find('?') != std::string::npos;
        bool request = Has(words, "va") || Has(words, "vas") || Has(words, "aller") ||
            Has(words, "occupe toi") || Has(words, "peux tu") || Has(words, "tu peux") ||
            Has(words, "go") || Has(words, "can you") || Has(words, "could you");
        if (request && !Has(words, "ne") && !Has(words, "pas") && !Has(words, "not") &&
            !Has(words, "pourquoi") && !Has(words, "comment") && !Has(words, "why") && !Has(words, "how") &&
            (!question || Has(words, "peux tu") || Has(words, "tu peux") || Has(words, "can you") ||
                Has(words, "could you")))
            return Intent::Start;
        return Intent::None;
    }
}
#endif
