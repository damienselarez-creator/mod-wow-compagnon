#ifndef PBC_COMPANION_LANGUAGE_H
#define PBC_COMPANION_LANGUAGE_H

#include <cstdint>
#include <string>

class Player;

// Native client locale 2 is frFR; both English clients use the English fallback.
inline bool PBC_IsFrenchClient(uint8_t locale) { return locale == 2; }

inline uint8_t PBC_SelectCompanionClientLocale(int listenerLocale, int masterLocale, int companionLocale)
{
    int locale = listenerLocale >= 0 ? listenerLocale : masterLocale >= 0 ? masterLocale : companionLocale;
    return locale == 2 ? 2 : 0;
}

inline std::string PBC_CompanionLanguageInstruction(uint8_t locale)
{
    return PBC_IsFrenchClient(locale)
        ? "\n[LANGUE DU CLIENT] Reponds en francais naturel. La langue du client du joueur prime "
          "sur la langue des sources, des souvenirs et des citations. Conserve la meme personnalite, "
          "les memes capacites et les memes engagements. N'invente aucun souvenir pour traduire.\n"
        : "\n[CLIENT LANGUAGE] Reply in natural English. The player's client language takes precedence "
          "over the language of source material, memories and quotations. Keep the same personality, "
          "abilities and commitments. Never invent a memory while translating.\n";
}

uint8_t PBC_CompanionClientLocale(Player* companion, Player* listener = nullptr);

#endif
