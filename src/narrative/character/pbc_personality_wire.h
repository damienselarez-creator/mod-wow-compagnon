#ifndef PBC_PERSONALITY_WIRE_H
#define PBC_PERSONALITY_WIRE_H

#include "pbc_json.h"
#include <cstdint>
#include <charconv>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

inline constexpr std::string_view PBC_PersonalityPrefix = "WoWCmp\t";

inline std::vector<std::string> PBC_PersonalitySplit(std::string_view text, char separator)
{
    std::vector<std::string> fields;
    do
    {
        auto end = text.find(separator);
        fields.emplace_back(text.substr(0, end));
        if (end == std::string_view::npos)
            return fields;
        text.remove_prefix(end + 1);
    } while (fields.size() < 16);
    return {};
}

inline std::optional<uint32_t> PBC_PersonalityNumber(std::string const& text)
{
    if (text.empty() || text.size() > 10)
        return {};
    uint32_t value = 0;
    auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
        return {};
    return value;
}

inline std::vector<std::string> PBC_PersonalityRequest(std::string_view message)
{
    if (!message.starts_with(PBC_PersonalityPrefix) || message.size() > 255)
        return {};
    auto fields = PBC_PersonalitySplit(message.substr(PBC_PersonalityPrefix.size()), '|');
    if (fields.size() < 3 || !PBC_PersonalityNumber(fields[1]))
        return {};
    if ((fields[0] == "H" && fields.size() == 3 && (fields[2] == "self" || fields[2] == "target")) ||
        (fields[0] == "P" && fields.size() == 8) || (fields[0] == "C" && fields.size() == 4))
        return fields;
    return {};
}

inline std::string PBC_PersonalityIds(pbc_json const& ids)
{
    std::string text;
    for (auto const& id : ids)
    {
        if (!text.empty())
            text += ',';
        text += id.get<std::string>();
    }
    return text;
}

#endif
