#include "pbc_dialogue_style.h"
#include <cassert>
#include <string>

int main()
{
    for (uint8_t locale : {uint8_t(0), uint8_t(1), uint8_t(2), uint8_t(8), uint8_t(255)})
        for (std::string const& mission : {std::string{}, std::string("\n[QUEST] Keep 5-8 sentences. {literal_data}\n")})
        {
            std::string custom = "[CUSTOM SYSTEM] Identity and setting from the existing custom template.\n";
            std::string prefix = custom;
            PBC_AppendDialogueInstructions(custom, mission, locale);
            assert(custom.compare(0, prefix.size(), prefix) == 0);
            assert(custom.compare(prefix.size(), mission.size(), mission) == 0);
            auto const language = PBC_CompanionLanguageInstruction(locale);
            assert(custom.compare(custom.size() - language.size(), language.size(), language) == 0);
            if (locale == 2)
            {
                assert(custom.find("[PAROLE NATURELLE ET JUSTE]") != std::string::npos);
                assert(custom.find("[NATURAL AND GROUNDED SPEECH]") == std::string::npos);
            }
            else
            {
                assert(custom.find("[NATURAL AND GROUNDED SPEECH]") != std::string::npos);
                assert(custom.find("[PAROLE NATURELLE ET JUSTE]") == std::string::npos);
            }
            assert(custom.find("{literal_data}") == (mission.empty() ? std::string::npos :
                prefix.size() + mission.find("{literal_data}")));
        }
}
