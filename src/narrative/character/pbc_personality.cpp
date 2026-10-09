#include "pbc_personality.h"
#include "pbc_personality_model.h"
#include "pbc_companion_language.h"
#include "pbc_config.h"
#include "Chat.h"
#include "ChatCommand.h"
#include "DatabaseEnv.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "WorldSession.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>

namespace
{
std::mutex stateMutex;
pbc_json catalog;
pbc_json sheets = pbc_json::object();
std::filesystem::path stateFile;
bool ready = false;
struct Draft
{
    std::string target;
    pbc_json sheet;
    std::chrono::steady_clock::time_point expires;
};
std::map<std::string, Draft> drafts;

pbc_json ReadFile(std::filesystem::path const& path, uintmax_t limit)
{
    if (!std::filesystem::is_regular_file(path) || std::filesystem::file_size(path) > limit)
        throw std::runtime_error("Invalid personality file size or type");
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("Unreadable personality file");
    return pbc_json::parse(input, [](int depth, pbc_json::parse_event_t, pbc_json&)
    {
        if (depth > 16)
            throw std::runtime_error("Personality nesting limit");
        return true;
    });
}

bool Matches(pbc_json const& sheet, Player* target)
{
    return target && target->GetSession() && sheet.at("account") == target->GetSession()->GetAccountId() &&
        sheet.at("race") == target->getRace() && sheet.at("class") == target->getClass();
}

// The oldest surviving character is the main. Re-evaluate at preview and confirmation.
bool Eligible(Player* actor, Player* target)
{
    if (!g_PBC_Enable || !actor || !target || !actor->GetSession() || !target->GetSession() ||
        actor->GetSession()->IsHeadless() || actor->GetSession()->IsLoggingOut() ||
        target->GetSession()->IsLoggingOut() || !actor->IsInWorld() || !target->IsInWorld() ||
        actor->GetSession()->GetAccountId() != target->GetSession()->GetAccountId() ||
        actor->IsInCombat() || target->IsInCombat())
        return false;
    auto* statement = CharacterDatabase.GetPreparedStatement(CHAR_SEL_CHAR_GUID_NAME_BY_ACC);
    statement->SetData(0, actor->GetSession()->GetAccountId());
    auto result = CharacterDatabase.Query(statement);
    if (!result)
        return false;
    uint32 oldest = UINT32_MAX;
    bool found = false;
    do
    {
        uint32 guid = result->Fetch()[0].Get<uint32>();
        oldest = std::min(oldest, guid);
        found = found || guid == target->GetGUID().GetCounter();
    } while (result->NextRow());
    return found && oldest != target->GetGUID().GetCounter();
}

bool Save(std::string const& key, pbc_json const& sheet)
{
    if (!ready || sheets.contains(key) || sheets.size() >= 4096)
        return false;
    try
    {
        auto next = sheets;
        next[key] = sheet;
        auto temporary = stateFile;
        temporary += ".tmp";
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output)
            return false;
        std::filesystem::permissions(temporary, std::filesystem::perms::owner_read |
            std::filesystem::perms::owner_write, std::filesystem::perm_options::replace);
        output << pbc_json({{"version", 1}, {"characters", next}}).dump(2) << '\n';
        output.flush();
        if (!output.good())
            return false;
        output.close();
        if (output.fail())
            return false;
        std::filesystem::rename(temporary, stateFile);
        sheets.swap(next);
        return true;
    }
    catch (std::exception const& error)
    {
        LOG_ERROR("module.pbc", "Personality save failed: {}", error.what());
        return false;
    }
}

bool Command(ChatHandler* handler, Acore::ChatCommands::Tail args)
{
    auto* actor = handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
    if (!actor)
        return false;
    uint8_t locale = PBC_CompanionClientLocale(actor, actor);
    bool french = locale == 2;
    std::istringstream input{std::string(args)};
    std::string action;
    input >> action;
    std::lock_guard<std::mutex> lock(stateMutex);
    if (!ready)
    {
        handler->SendSysMessage(french ? "Atelier indisponible : configuration serveur requise." :
            "Workshop unavailable: server configuration required.");
        return true;
    }
    if (action == "list")
    {
        for (auto const& pair : catalog.at("pairs"))
            for (auto category : {"quality", "flaw"})
            {
                auto const& trait = pair.at(category);
                auto const& labels = trait.at("labels");
                handler->SendSysMessage(trait.at("id").get<std::string>() + " : " +
                    (french ? labels.at("frFR").at(actor->getGender() == 1 ? "female" : "male").get<std::string>() :
                        labels.at("enUS").at("neutral").get<std::string>()));
            }
        return true;
    }
    auto* target = handler->getSelectedPlayer();
    if (!target)
        target = actor;
    if (!Eligible(actor, target))
    {
        handler->SendSysMessage(french ? "Choisis un personnage de ton compte autre que le main, hors combat." :
            "Choose a character from your account other than the main, out of combat.");
        return true;
    }
    std::string key = target->GetGUID().ToString();
    std::string actorKey = actor->GetGUID().ToString();
    auto existing = sheets.find(key);
    if (existing != sheets.end())
    {
        handler->SendSysMessage(french ? "Fiche definitive deja enregistree, non modifiable." :
            "Permanent sheet already recorded; it cannot be changed.");
        if (Matches(*existing, target))
            handler->SendSysMessage(PBC_RenderPersonality(*existing, catalog, target->getGender(), locale));
        return true;
    }
    if (action == "confirm")
    {
        auto draft = drafts.find(actorKey);
        if (draft == drafts.end() || draft->second.target != key ||
            draft->second.expires < std::chrono::steady_clock::now() || !Matches(draft->second.sheet, target))
        {
            handler->SendSysMessage(french ? "Apercu absent ou expire : recommence avec preview." :
                "Preview missing or expired: start again with preview.");
            return true;
        }
        bool saved = Save(key, draft->second.sheet);
        drafts.erase(draft);
        handler->SendSysMessage(saved ? (french ? "Fiche confirmee definitivement." : "Sheet permanently confirmed.") :
            (french ? "Echec de sauvegarde ; aucun choix confirme." : "Save failed; no choices confirmed."));
        return true;
    }
    if (action == "preview")
    {
        drafts.erase(actorKey);
        int tab = -1;
        uint32 first = 0;
        uint32 second = 0;
        std::array<std::string, 3> qualities;
        std::array<std::string, 3> flaws;
        std::string extra;
        if (input >> tab >> first >> second >> qualities[0] >> qualities[1] >> qualities[2] >>
            flaws[0] >> flaws[1] >> flaws[2] && !(input >> extra))
        {
            pbc_json sheet = {{"account", actor->GetSession()->GetAccountId()}, {"race", uint32(target->getRace())},
                {"class", uint32(target->getClass())}, {"tab", tab}, {"professions", {first, second}},
                {"quality_ids", qualities}, {"flaw_ids", flaws}};
            if (PBC_ValidPersonalitySheet(sheet, catalog))
            {
                for (auto it = drafts.begin(); it != drafts.end();)
                    if (it->second.expires < std::chrono::steady_clock::now())
                        it = drafts.erase(it);
                    else
                        ++it;
                if (drafts.size() >= 4096)
                    return false;
                drafts[actorKey] = {key, sheet, std::chrono::steady_clock::now() + std::chrono::minutes(5)};
                handler->SendSysMessage(target->GetName() + " : tab=" + std::to_string(tab) + " professions=" +
                    std::to_string(first) + "," + std::to_string(second));
                handler->SendSysMessage(PBC_RenderPersonality(sheet, catalog, target->getGender(), locale));
                handler->SendSysMessage(french ? "Verifie les six traits et orientations. .companion confirm est "
                    "definitif (5 minutes). Aucun changement de talents ou metiers applique par cet apercu." :
                    "Review the six traits and paths. .companion confirm is permanent (5 minutes). "
                    "This preview does not change talents or professions.");
                return true;
            }
        }
    }
    handler->SendSysMessage(".companion list | .companion preview <tab 0..2> <profession1> <profession2> "
        "<quality_id1> <quality_id2> <quality_id3> <flaw_id1> <flaw_id2> <flaw_id3> | .companion confirm");
    return true;
}

class PersonalityCommands : public CommandScript
{
public:
    PersonalityCommands() : CommandScript("PBC_PersonalityCommands") { }
    Acore::ChatCommands::ChatCommandTable GetCommands() const override
    {
        using namespace Acore::ChatCommands;
        return {{"companion", Command, SEC_PLAYER, Console::No}};
    }
};
}

bool PBC_LoadPersonality(std::string const& catalogPath, std::string const& statePath, std::string& status)
{
    std::lock_guard<std::mutex> lock(stateMutex);
    try
    {
        if (catalogPath.empty() || statePath.empty())
        {
            status = "not configured";
            return false;
        }
        if (!std::filesystem::path(statePath).is_absolute())
            throw std::runtime_error("PersonalityFile must be absolute");
        if (ready && stateFile != statePath)
            throw std::runtime_error("PersonalityFile cannot change during runtime");
        auto nextCatalog = ReadFile(catalogPath, 256 * 1024);
        if (!PBC_ValidPersonalityCatalog(nextCatalog))
            throw std::runtime_error("Invalid personality catalogue");
        auto next = sheets;
        if (!ready && std::filesystem::exists(statePath))
        {
            auto saved = ReadFile(statePath, 4 * 1024 * 1024);
            if (saved.at("version") != 1 || !saved.at("characters").is_object())
                throw std::runtime_error("Invalid personality store");
            next = saved.at("characters");
        }
        if (next.size() > 4096)
            throw std::runtime_error("Personality sheet limit");
        for (auto const& [key, sheet] : next.items())
            if (key.empty() || key.size() > 64 || !PBC_ValidPersonalitySheet(sheet, nextCatalog))
                throw std::runtime_error("Invalid saved personality sheet");
        std::filesystem::create_directories(std::filesystem::path(statePath).parent_path());
        stateFile = statePath;
        catalog.swap(nextCatalog);
        sheets.swap(next);
        drafts.clear();
        ready = true;
        status = "30 qualities, 30 flaws, " + std::to_string(sheets.size()) + " permanent sheets";
        return true;
    }
    catch (std::exception const& error)
    {
        status = error.what();
        return false;
    }
}

pbc_json PBC_PersonalitySheet(Player* companion)
{
    std::lock_guard<std::mutex> lock(stateMutex);
    if (!ready || !companion)
        return nullptr;
    auto it = sheets.find(companion->GetGUID().ToString());
    return it != sheets.end() && Matches(*it, companion) ? *it : pbc_json();
}

std::string PBC_PersonalityContext(pbc_json const& sheet, uint8_t gender, uint8_t locale)
{
    std::lock_guard<std::mutex> lock(stateMutex);
    return PBC_RenderPersonality(sheet, catalog, gender, locale);
}

void AddPBCPersonalityScripts()
{
    new PersonalityCommands();
}
