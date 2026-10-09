local WC = WoWCompagnon
local french = GetLocale() == "frFR"
local data = WC.Data[french and "frFR" or "enUS"]
local function T(fr, en) return french and fr or en end
local state = {sequence = 0, mode = "form", values = {qualities = {}, flaws = {}}}

local frame = CreateFrame("Frame", "WoWCompagnonForm", UIParent)
frame:SetWidth(620)
frame:SetHeight(550)
frame:SetPoint("CENTER")
frame:SetFrameStrata("DIALOG")
frame:SetMovable(true)
frame:EnableMouse(true)
frame:RegisterForDrag("LeftButton")
frame:SetScript("OnDragStart", frame.StartMoving)
frame:SetScript("OnDragStop", frame.StopMovingOrSizing)
frame:SetClampedToScreen(true)
frame:SetBackdrop({bgFile = "Interface\\DialogFrame\\UI-DialogBox-Background",
    edgeFile = "Interface\\DialogFrame\\UI-DialogBox-Border", tile = true, tileSize = 32,
    edgeSize = 32, insets = {left = 11, right = 12, top = 12, bottom = 11}})
frame:Hide()
table.insert(UISpecialFrames, "WoWCompagnonForm")

local function Label(parent, text, x, y, width, template)
    local label = parent:CreateFontString(nil, "OVERLAY", template or "GameFontNormal")
    label:SetPoint("TOPLEFT", x, y)
    label:SetWidth(width or 260)
    label:SetJustifyH("LEFT")
    label:SetText(text)
    return label
end
local title = Label(frame, T("Fiche du compagnon", "Companion sheet"), 28, -25, 550, "GameFontNormalLarge")
local identity = Label(frame, "", 28, -61, 550, "GameFontHighlight")
local close = CreateFrame("Button", nil, frame, "UIPanelCloseButton")
close:SetPoint("TOPRIGHT", -6, -6)
local form = CreateFrame("Frame", nil, frame)
form:SetAllPoints(frame)
Label(form, T("Spécialisation", "Specialization"), 28, -106, 550)
Label(form, T("Premier métier", "First profession"), 28, -167)
Label(form, T("Second métier", "Second profession"), 322, -167)
Label(form, T("Trois qualités", "Three qualities"), 28, -238)
Label(form, T("Trois défauts", "Three flaws"), 322, -238)
local summary = Label(frame, "", 28, -106, 550, "GameFontHighlight")
summary:SetJustifyV("TOP")
summary:SetHeight(255)
summary:Hide()
local status = Label(frame, "", 28, -402, 550, "GameFontHighlightSmall")
status:SetHeight(32)
local footer = Label(frame, T("Les choix confirmés seront définitifs.", "Confirmed choices are permanent."),
    28, -504, 550, "GameFontDisableSmall")

local check = CreateFrame("CheckButton", "WoWCompagnonPermanentCheck", frame, "UICheckButtonTemplate")
check:SetPoint("TOPLEFT", 25, -363)
Label(check, T("Je comprends que cette fiche sera définitive.", "I understand this sheet will be permanent."),
    34, -9, 500, "GameFontHighlightSmall")
check:Hide()
local primary = CreateFrame("Button", "WoWCompagnonSubmit", frame, "UIPanelButtonTemplate")
primary:SetWidth(260)
primary:SetHeight(30)
primary:SetPoint("BOTTOMRIGHT", -27, 67)
local back = CreateFrame("Button", "WoWCompagnonBack", frame, "UIPanelButtonTemplate")
back:SetWidth(150)
back:SetHeight(30)
back:SetPoint("BOTTOMLEFT", 27, 67)
back:SetText(T("Modifier", "Edit"))
back:Hide()
local dropdowns = {}

-- One bounded scrolling popup shared by all fields (WoW 3.3.5 frame APIs).
local menu = CreateFrame("Frame", "WoWCompagnonChoiceMenu", frame)
menu:SetWidth(276)
menu:SetFrameStrata("TOOLTIP")
menu:SetClampedToScreen(true)
menu:EnableMouse(true)
menu:SetBackdrop({bgFile = "Interface\\DialogFrame\\UI-DialogBox-Background",
    edgeFile = "Interface\\Tooltips\\UI-Tooltip-Border", tile = true, tileSize = 16,
    edgeSize = 16, insets = {left = 4, right = 4, top = 4, bottom = 4}})
menu:Hide()
local scroll = CreateFrame("ScrollFrame", "WoWCompagnonChoiceScroll", menu, "UIPanelScrollFrameTemplate")
scroll:SetPoint("TOPLEFT", 8, -8)
scroll:SetPoint("BOTTOMRIGHT", -30, 8)
local content = CreateFrame("Frame", nil, scroll)
content:SetWidth(236)
scroll:SetScrollChild(content)
scroll:EnableMouseWheel(true)
local rows = {}
local scrollLimit = 0
scroll:SetScript("OnMouseWheel", function(self, delta)
    self:SetVerticalScroll(math.max(0, math.min(scrollLimit, self:GetVerticalScroll() - delta * 24)))
end)
local function CloseMenu() menu:Hide() end
local function OpenMenu(drop, options)
    if menu:IsShown() and menu.owner == drop then CloseMenu(); return end
    menu.owner = drop
    menu:ClearAllPoints()
    menu:SetPoint("TOPLEFT", drop, "BOTTOMLEFT", 0, -2)
    local visible = math.min(8, #options)
    menu:SetHeight(visible * 24 + 16)
    content:SetHeight(math.max(1, #options * 24))
    scrollLimit = math.max(0, (#options - visible) * 24)
    for i, option in ipairs(options) do
        local row = rows[i]
        if not row then
            row = CreateFrame("Button", "WoWCompagnonChoice" .. i, content)
            row:SetWidth(236)
            row:SetHeight(24)
            row:SetPoint("TOPLEFT", 0, -(i - 1) * 24)
            row:SetNormalFontObject("GameFontHighlightSmall")
            row:SetDisabledFontObject("GameFontDisableSmall")
            row:SetHighlightTexture("Interface\\QuestFrame\\UI-QuestTitleHighlight")
            rows[i] = row
        end
        row:SetText((option.checked and "|cff00ff00> |r" or "") .. option.text)
        row:SetScript("OnClick", function()
            if option.disabled then return end
            CloseMenu()
            option.func()
        end)
        if option.disabled then row:Disable() else row:Enable() end
        row:Show()
    end
    for i = #options + 1, #rows do rows[i]:Hide() end
    menu:Show()
    scroll:SetVerticalScroll(0)
end

local function LabelFor(category, id)
    local item = WC.Find(data[category], id)
    if not item then return T("Choisir…", "Choose…") end
    return item.label or item[state.header and state.header.gender == 1 and "female" or "male"]
end

local function Complete()
    return state.header and WC.Valid(state.values, data, state.header.class)
end

local function UpdateButtons()
    primary:Disable()
    if state.mode == "locked" then
        primary:SetText(T("Fiche définitive", "Permanent sheet"))
    elseif state.mode == "review" then
        primary:SetText(T("Confirmer définitivement", "Confirm permanently"))
        if not state.pending and check:GetChecked() and state.nonce and GetTime() < state.expires then
            primary:Enable()
        end
    else
        primary:SetText(T("Vérifier ma fiche", "Review my sheet"))
        if not state.pending and Complete() then primary:Enable() end
    end
    if state.pending then back:Disable() else back:Enable() end
end

local function Summary()
    local v = state.values
    local qualities, flaws = {}, {}
    for i = 1, 3 do
        qualities[i] = LabelFor("qualities", v.qualities[i])
        flaws[i] = LabelFor("flaws", v.flaws[i])
    end
    local spec = data.specializations[state.header.class][v.tab + 1]
    summary:SetText(T("Spécialisation : ", "Specialization: ") .. spec .. "\n\n" ..
        T("Métiers : ", "Professions: ") .. LabelFor("professions", v.first) .. " / " ..
        LabelFor("professions", v.second) .. "\n\n" .. T("Qualités : ", "Qualities: ") ..
        table.concat(qualities, ", ") .. "\n\n" .. T("Défauts : ", "Flaws: ") ..
        table.concat(flaws, ", "))
end

local function Refresh()
    CloseMenu()
    if state.header then
        local h = state.header
        identity:SetText(h.name .. " — " .. data.races[h.race] .. " / " .. data.classes[h.class] ..
            " / " .. (h.gender == 1 and T("Féminin", "Female") or
                h.gender == 0 and T("Masculin", "Male") or T("Non précisé", "Unspecified")))
    end
    if state.mode == "form" then
        form:Show(); summary:Hide(); check:Hide(); back:Hide()
        for _, drop in ipairs(dropdowns) do
            local value = drop.get()
            local text = drop.category == "specializations" and state.header and value ~= nil and
                data.specializations[state.header.class][value + 1] or
                (drop.category ~= "specializations" and LabelFor(drop.category, value)) or T("Choisir…", "Choose…")
            drop:SetText(text)
            if state.pending or not state.header then
                drop:Disable()
            else
                drop:Enable()
            end
        end
    else
        form:Hide(); summary:Show(); Summary()
        if state.mode == "review" then check:Show(); back:Show() else check:Hide(); back:Hide() end
    end
    UpdateButtons()
end

local function Dropdown(category, x, y, get, set, index)
    local drop = CreateFrame("Button", "WoWCompagnonDrop" .. (#dropdowns + 1), form, "UIPanelButtonTemplate")
    drop:SetPoint("TOPLEFT", x + 18, y + 2)
    drop:SetWidth(254)
    drop:SetHeight(24)
    drop.category, drop.get = category, get
    drop:SetScript("OnClick", function()
        if not state.header or state.pending or state.mode ~= "form" then return end
        local entries = {}
        local options = data[category]
        if category == "specializations" then
            options = {}
            for i, label in ipairs(data.specializations[state.header.class]) do
                options[i] = {id = i - 1, label = label}
            end
        end
        for _, item in ipairs(options) do
            local chosen = item
            local info = {}
            info.text = item.label or item[state.header.gender == 1 and "female" or "male"]
            info.value, info.checked = item.id, get() == item.id
            if category == "professions" then
                info.disabled = (index == 1 and state.values.second or state.values.first) == item.id
            elseif category == "qualities" or category == "flaws" then
                for i = 1, 3 do
                    if i ~= index and state.values[category][i] == item.id then info.disabled = true end
                end
            end
            info.func = function()
                if state.pending or state.mode ~= "form" then return end
                set(chosen.id)
                state.nonce = nil
                status:SetText("")
                Refresh()
            end
            entries[#entries + 1] = info
        end
        OpenMenu(drop, entries)
    end)
    dropdowns[#dropdowns + 1] = drop
    return drop
end

Dropdown("specializations", 10, -125, function() return state.values.tab end,
    function(value) state.values.tab = value end)
Dropdown("professions", 10, -186, function() return state.values.first end,
    function(value) state.values.first = value end, 1)
Dropdown("professions", 304, -186, function() return state.values.second end,
    function(value) state.values.second = value end, 2)
for i = 1, 3 do
    local index = i
    Dropdown("qualities", 10, -257 - (i - 1) * 40, function() return state.values.qualities[index] end,
        function(value) state.values.qualities[index] = value end, index)
    Dropdown("flaws", 304, -257 - (i - 1) * 40, function() return state.values.flaws[index] end,
        function(value) state.values.flaws[index] = value end, index)
end

local errors = {
    INELIGIBLE = T("Ce personnage est le main ou n'est pas disponible hors combat.",
        "This character is the main or is not available out of combat."),
    TARGET = T("Le personnage sélectionné a changé. Rouvre le formulaire.",
        "The selected character changed. Reopen the form."),
    NOTREADY = T("L'atelier n'est pas disponible sur ce serveur.", "The workshop is unavailable on this server."),
    INVALID = T("Choix invalides ou catalogue incompatible. Vérifie la fiche.",
        "Invalid choices or incompatible catalogue. Check the sheet."),
    EXPIRED = T("Le récapitulatif a expiré. Vérifie à nouveau la fiche.",
        "The review expired. Review the sheet again."),
    BUSY = T("Patiente un instant, puis réessaie.", "Wait a moment and try again."),
    SAVE = T("La sauvegarde n'a pas été confirmée. Vérifie auprès du serveur.",
        "Saving was not confirmed. Check with the server."),
}

local function Send(action, build)
    if state.pending then return end
    state.sequence = state.sequence + 1
    local request = tostring(state.sequence)
    local payload = build(request)
    if #payload > 248 then status:SetText(errors.INVALID); return end
    state.pending = {id = request, action = action, sent = GetTime()}
    status:SetText(T("Échange avec le serveur…", "Waiting for the server…"))
    Refresh()
    SendAddonMessage(WC.Prefix, payload, "WHISPER", UnitName("player"))
end

local function Open(automatic)
    if state.pending then return end
    state.automatic, state.header, state.nonce = automatic, nil, nil
    state.mode = "form"
    state.values = {qualities = {}, flaws = {}}
    identity:SetText(T("Chargement du personnage…", "Loading character…"))
    if not automatic then frame:Show() end
    Send("H", function(id) return "H|" .. id .. "|" .. (automatic and "self" or "target") end)
end

primary:SetScript("OnClick", function()
    if state.pending or not state.header then return end
    if state.mode == "form" and Complete() then
        Send("P", function(id) return WC.PreviewPayload(id, state.header.key, state.values) end)
    elseif state.mode == "review" and state.nonce and check:GetChecked() and GetTime() < state.expires then
        Send("C", function(id) return WC.ConfirmPayload(id, state.header.key, state.nonce) end)
    end
end)
back:SetScript("OnClick", function()
    if state.pending then return end
    state.mode, state.nonce = "form", nil
    check:SetChecked(false)
    status:SetText("")
    Refresh()
end)
check:SetScript("OnClick", UpdateButtons)

local events = CreateFrame("Frame")
events:RegisterEvent("PLAYER_LOGIN")
events:RegisterEvent("CHAT_MSG_ADDON")
events:SetScript("OnEvent", function(self, event, ...)
    if event == "PLAYER_LOGIN" then state.loginAt = GetTime() + 2; return end
    local prefix, message, channel, sender = ...
    if prefix ~= WC.Prefix or channel ~= "WHISPER" or sender ~= UnitName("player") or not state.pending then return end
    local result = WC.Parse(message, data, state.header and state.header.class)
    if not result or result.request ~= state.pending.id then return end
    local action = state.pending.action
    if action == "H" and result.kind ~= "header" and result.kind ~= "error" then return end
    if action == "C" and result.kind ~= "error" and
        (result.kind ~= "header" or not result.locked) then return end
    if action == "P" and result.kind == "header" and not result.locked then return end
    if result.kind ~= "error" and action ~= "H" and result.key ~= state.header.key then return end
    state.pending = nil
    if result.kind == "error" then
        state.mode, state.nonce = "form", nil
        check:SetChecked(false)
        status:SetText(errors[result.code] or errors.INVALID)
        Refresh()
        if not state.automatic then frame:Show() end
        return
    end
    if result.kind == "header" then
        state.header = result
        state.values = result.locked and result.values or {qualities = {}, flaws = {}}
        state.mode = result.locked and "locked" or "form"
        state.nonce = nil
        status:SetText(result.locked and T("Fiche confirmée, consultable uniquement.",
            "Confirmed sheet, available for viewing only.") or
            T("Choisis les orientations et six traits de ce compagnon.",
                "Choose this companion's paths and six traits."))
        Refresh()
        if not state.automatic or not result.locked then frame:Show() end
    else
        state.values, state.nonce, state.mode = result.values, result.nonce, "review"
        state.expires = GetTime() + 300
        check:SetChecked(false)
        status:SetText(T("Vérifie tes choix avant la confirmation définitive.",
            "Review your choices before permanent confirmation."))
        Refresh()
        frame:Show()
    end
end)
events:SetScript("OnUpdate", function()
    local now = GetTime()
    if state.loginAt and now >= state.loginAt then state.loginAt = nil; Open(true) end
    if state.pending and now - state.pending.sent > 8 then
        state.pending = nil
        status:SetText(T("Le serveur ne répond pas. Rouvre le formulaire pour réessayer.",
            "The server did not respond. Reopen the form to try again."))
        Refresh()
    end
    if state.mode == "review" and state.expires and now >= state.expires then
        state.mode, state.nonce = "form", nil
        check:SetChecked(false)
        status:SetText(errors.EXPIRED)
        Refresh()
    end
end)
frame:SetScript("OnHide", function()
    CloseMenu()
    state.pending = nil
    if state.mode == "review" then state.mode, state.nonce = "form", nil end
end)
SLASH_WOWCOMPAGNON1 = "/compagnon"
SLASH_WOWCOMPAGNON2 = "/companion"
SlashCmdList.WOWCOMPAGNON = function() Open(false) end
Refresh()
