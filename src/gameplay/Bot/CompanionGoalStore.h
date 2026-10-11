#ifndef COMPANION_GOAL_STORE_H
#define COMPANION_GOAL_STORE_H

#include "CompanionGoalPolicy.h"
#include "pbc_json.h"
#include <filesystem>
#include <fstream>
#include <map>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

class CompanionGoalStore
{
public:
    std::map<std::string, CompanionGoals::Book> Books;
    std::filesystem::path Path;
    bool Ready = false;
    bool Dirty = false;

    bool Save()
    {
        try
        {
            pbc_json characters = pbc_json::object();
            for (auto const& [key, book] : Books)
            {
                pbc_json goals = pbc_json::array();
                for (auto const& goal : book.goals)
                    goals.push_back({goal.requested, goal.forbidden, goal.quiet, goal.deferredUntil,
                        goal.since, goal.updated, uint8_t(goal.state), goal.reason, goal.requiredMoney,
                        goal.target, goal.confirmed});
                characters[key] = {{"goals", goals}};
            }
            auto temporary = Path;
            temporary += ".tmp";
            {
                std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
                output << pbc_json({{"version", 1}, {"characters", characters}}).dump() << '\n';
                output.flush();
                if (!output)
                    return false;
            }
            std::filesystem::permissions(temporary, std::filesystem::perms::owner_read |
                std::filesystem::perms::owner_write, std::filesystem::perm_options::replace);
#ifdef _WIN32
            bool durable = MoveFileExW(temporary.c_str(), Path.c_str(),
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
            int directory = open(Path.parent_path().c_str(), O_RDONLY | O_DIRECTORY);
            if (directory < 0)
                return false;
            durable = fsync(directory) == 0;
            close(directory);
#endif
            if (durable)
                Dirty = false;
            return durable;
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
        Dirty = false;
        Books.clear();
        try
        {
            if (!path.is_absolute())
                return false;
            if (!std::filesystem::exists(path))
                return Ready = Save();
            if (!std::filesystem::is_regular_file(path) || std::filesystem::file_size(path) > 8 * 1024 * 1024)
                return false;
            std::ifstream input(path);
            auto data = pbc_json::parse(input, [](int depth, pbc_json::parse_event_t, pbc_json&)
            {
                if (depth > 8)
                    throw std::runtime_error("Goal history nesting exceeds limit");
                return true;
            });
            if (data.at("version") != 1 || !data.at("characters").is_object() ||
                data.at("characters").size() > 4096)
                return false;
            std::map<std::string, CompanionGoals::Book> loaded;
            for (auto const& [key, character] : data.at("characters").items())
            {
                if (key.empty() || key.size() > 80 || !character.at("goals").is_array() ||
                    character.at("goals").size() != size_t(CompanionGoals::Domain::Count))
                    return false;
                CompanionGoals::Book book;
                size_t index = 0;
                for (auto const& row : character.at("goals"))
                {
                    if (!row.is_array() || row.size() != 11 || !row[0].is_boolean() ||
                        !row[1].is_boolean() || !row[2].is_boolean() || !row[7].is_string())
                        return false;
                    for (auto column : {3, 4, 5, 6, 8, 9, 10})
                        if (!row[column].is_number_unsigned())
                            return false;
                    if (row[6].get<uint64_t>() > uint64_t(CompanionGoals::State::Done) ||
                        row[7].get<std::string>().size() > 96)
                        return false;
                    for (auto column : {8, 9, 10})
                        if (row[column].get<uint64_t>() > UINT32_MAX)
                            return false;
                    auto& goal = book.goals[index++];
                    goal = {row[0].get<bool>(), row[1].get<bool>(), row[2].get<bool>(),
                        row[3].get<uint64_t>(), row[4].get<uint64_t>(), row[5].get<uint64_t>(),
                        CompanionGoals::State(row[6].get<uint8_t>()), row[7].get<std::string>(),
                        row[8].get<uint32_t>(), row[9].get<uint32_t>(), row[10].get<uint32_t>()};
                    if (goal.state == CompanionGoals::State::Travelling || goal.state == CompanionGoals::State::Verifying)
                    {
                        goal.state = CompanionGoals::State::Pending;
                        goal.reason = "restart_revalidate";
                    }
                }
                loaded.emplace(key, book);
            }
            Books.swap(loaded);
            return Ready = true;
        }
        catch (...)
        {
            return false;
        }
    }
};
#endif
