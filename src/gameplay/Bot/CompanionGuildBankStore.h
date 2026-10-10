/*
 * This file is part of the mod-playerbots module for AzerothCore. See AUTHORS file for Copyright
 * information; released under GNU GPL v2 license, redistribute/modify under version 2 of the License,
 * or (at your option) any later version.
 */
#ifndef COMPANION_GUILD_BANK_STORE_H
#define COMPANION_GUILD_BANK_STORE_H

#include "CompanionGuildBankPolicy.h"
#include "pbc_json.h"
#include <filesystem>
#include <fstream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

class CompanionGuildBankStore
{
public:
    CompanionSharing::Quotas Quotas;
    std::filesystem::path Path;
    bool Ready = false;

    bool Save()
    {
        try
        {
            auto temporary = Path;
            temporary += ".tmp";
            pbc_json data = {{"version", 1}, {"next", Quotas.Next}};
            {
                std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
                output << data.dump() << '\n';
                output.flush();
                if (!output)
                    return false;
            }
            std::filesystem::permissions(temporary, std::filesystem::perms::owner_read |
                std::filesystem::perms::owner_write, std::filesystem::perm_options::replace);
#ifdef _WIN32
            return MoveFileExW(temporary.c_str(), Path.c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
            int file = open(temporary.c_str(), O_RDONLY);
            if (file < 0)
                return false;
            bool durable = fsync(file) == 0;
            close(file);
            if (!durable)
                return false;
            std::filesystem::rename(temporary, Path);
            auto parent = Path.parent_path();
            if (parent.empty())
                parent = ".";
            int directory = open(parent.c_str(), O_RDONLY | O_DIRECTORY);
            if (directory < 0)
                return false;
            durable = fsync(directory) == 0;
            close(directory);
            return durable;
#endif
        }
        catch (...)
        {
            return false;
        }
    }

    bool Load(std::filesystem::path const& path)
    {
        Path = path;
        Ready = false;
        Quotas.Next.clear();
        try
        {
            if (!std::filesystem::exists(path))
                return Ready = Save();
            if (!std::filesystem::is_regular_file(path) || std::filesystem::file_size(path) > 2 * 1024 * 1024)
                return false;
            std::ifstream input(path, std::ios::binary);
            auto data = pbc_json::parse(input, [](int depth, pbc_json::parse_event_t, pbc_json&)
            {
                if (depth > 8)
                    throw std::runtime_error("Invalid guild sharing history depth");
                return true;
            });
            if (data.at("version") != 1 || !data.at("next").is_object())
                return false;
            for (auto const& [key, value] : data.at("next").items())
            {
                if (key.size() > 64 || !value.is_number_unsigned())
                    return false;
                Quotas.Next[key] = value.get<uint64_t>();
            }
            return Ready = true;
        }
        catch (...)
        {
            return false;
        }
    }

    bool Reserve(uint32_t character, CompanionSharing::Kind kind, uint64_t now)
    {
        if (!Ready || !Quotas.Reserve(CompanionSharing::Key(character, kind), now))
            return false;
        // Persist BEFORE the native withdrawal: a crash cannot grant a second weekly allowance.
        if (!Save())
        {
            Ready = false;
            return false;
        }
        return true;
    }

    void Cancel(uint32_t character, CompanionSharing::Kind kind)
    {
        Quotas.Next.erase(CompanionSharing::Key(character, kind));
        if (!Save())
            Ready = false;
    }
};
#endif
