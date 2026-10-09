local WC = WoWCompagnon
WC.Prefix = "WoWCmp"

function WC.Split(text, separator)
    local result, start = {}, 1
    while true do
        local pos = string.find(text, separator, start, true)
        if not pos then
            result[#result + 1] = string.sub(text, start)
            return result
        end
        result[#result + 1] = string.sub(text, start, pos - 1)
        start = pos + #separator
        if #result > 16 then return nil end
    end
end

function WC.Find(list, id)
    for _, item in ipairs(list) do
        if item.id == id then return item end
    end
end

function WC.Valid(values, data, class)
    if not data.specializations[class] or type(values.tab) ~= "number" or
        values.tab < 0 or values.tab > 2 or values.tab ~= math.floor(values.tab) then return false end
    if not WC.Find(data.professions, values.first) or not WC.Find(data.professions, values.second) or
        values.first == values.second then return false end
    for _, category in ipairs({"qualities", "flaws"}) do
        local choices, seen = values[category], {}
        if type(choices) ~= "table" or #choices ~= 3 then return false end
        for i = 1, 3 do
            local id = choices[i]
            if seen[id] or not WC.Find(data[category], id) then return false end
            seen[id] = true
        end
    end
    return true
end

function WC.Values(fields, start)
    return {tab = tonumber(fields[start]), first = tonumber(fields[start + 1]),
        second = tonumber(fields[start + 2]), qualities = WC.Split(fields[start + 3], ","),
        flaws = WC.Split(fields[start + 4], ",")}
end

function WC.Parse(message, data, class)
    if type(message) ~= "string" or #message > 248 then return nil end
    local fields = WC.Split(message, "|")
    if not fields or not fields[2] or not string.match(fields[2], "^%d+$") then return nil end
    if fields[1] == "E" and #fields == 3 then
        return {kind = "error", request = fields[2], code = fields[3]}
    end
    if fields[1] == "H" and #fields == 13 then
        local header = {kind = "header", request = fields[2], key = fields[3], name = fields[4],
            race = tonumber(fields[5]), class = tonumber(fields[6]), gender = tonumber(fields[7]),
            locked = fields[8] == "LOCKED", values = WC.Values(fields, 9)}
        if not data.races[header.race] or not data.classes[header.class] or
            (header.gender ~= 0 and header.gender ~= 1 and header.gender ~= 2) or
            (fields[8] ~= "LOCKED" and fields[8] ~= "NEW") or header.name == "" or
            not string.match(header.key, "^[%w%-:]+$") then return nil end
        if header.locked and not WC.Valid(header.values, data, header.class) then return nil end
        return header
    end
    if fields[1] == "P" and #fields == 9 then
        local result = {kind = "preview", request = fields[2], key = fields[3], nonce = fields[4],
            values = WC.Values(fields, 5)}
        if not string.match(result.nonce, "^%d+$") or tonumber(result.nonce) == 0 or
            not WC.Valid(result.values, data, class) then return nil end
        return result
    end
end

function WC.PreviewPayload(request, key, values)
    return table.concat({"P", request, key, values.tab, values.first, values.second,
        table.concat(values.qualities, ","), table.concat(values.flaws, ",")}, "|")
end

function WC.ConfirmPayload(request, key, nonce)
    return table.concat({"C", request, key, nonce}, "|")
end
