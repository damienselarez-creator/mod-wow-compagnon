-- Isolated Lua 5.1 test: no game client, server, real character or permanent write.
local WC = WoWCompagnon
local data = WC.Data[GetLocale() == "frFR" and "frFR" or "enUS"]
local events
for _, candidate in ipairs(TestFrames) do
    if candidate.events and candidate.events.CHAT_MSG_ADDON then events = candidate end
end
assert(events, "Addon event handler missing")
local function event(name, ...)
    return events.scripts.OnEvent(events, name, ...)
end
local function incoming(payload, sender)
    return event("CHAT_MSG_ADDON", WC.Prefix, payload, "WHISPER", sender or "Example")
end
local function request()
    return WC.Split(TestSent[#TestSent][2], "|")
end
local function choose(number, value)
    local drop = _G["WoWCompagnonDrop" .. number]
    TestMenu = {}
    drop.initialize()
    for _, option in ipairs(TestMenu) do
        if option.value == value then
            assert(not option.disabled, "Duplicate option must be disabled")
            option.func()
            return
        end
    end
    error("Missing dropdown option: " .. tostring(value))
end
local function header(id, status, fields)
    return "H|" .. id .. "|40|Example|8|5|1|" .. status .. "|" .. fields
end

assert(not WoWCompagnonForm:IsShown(), "Form opened before server eligibility")
event("PLAYER_LOGIN")
TestNow = 3
events.scripts.OnUpdate(events)
assert(request()[1] == "H" and request()[3] == "self")
incoming(header("1", "NEW", "-1|0|0||"), "OtherPlayer")
assert(not WoWCompagnonForm:IsShown(), "Foreign sender opened a private sheet")
incoming(header("999", "NEW", "-1|0|0||"))
assert(not WoWCompagnonForm:IsShown(), "Stale reply accepted")
incoming(header("1", "NEW", "-1|0|0||"))
assert(WoWCompagnonForm:IsShown(), "Eligible character form did not open")
assert(not WoWCompagnonSubmit.enabled, "Incomplete form enabled preview")
choose(1, 1)
choose(2, 171)
choose(3, 182)
choose(4, "quality_benevolent")
assert(WoWCompagnonDrop4.text == (GetLocale() == "frFR" and "Bienveillante" or "Benevolent"),
    "Trait label does not follow the target's gender and client locale")
choose(5, "flaw_stubborn")
choose(6, "quality_protective")
choose(7, "flaw_indiscreet")
choose(8, "quality_patient")
choose(9, "flaw_impulsive")
assert(WoWCompagnonSubmit.enabled, "Complete form cannot be reviewed")
WoWCompagnonSubmit.scripts.OnClick()
local preview = request()
assert(preview[1] == "P" and preview[2] == "2")
local values = table.concat({preview[4], preview[5], preview[6], preview[7], preview[8]}, "|")
incoming("P|2|41|1234|" .. values)
assert(not WoWCompagnonSubmit.enabled, "Wrong target preview accepted")
incoming("P|2|40|1234|" .. values)
assert(not WoWCompagnonSubmit.enabled, "Confirmation enabled before explicit checkbox")
WoWCompagnonPermanentCheck:SetChecked(true)
WoWCompagnonPermanentCheck.scripts.OnClick()
assert(WoWCompagnonSubmit.enabled, "Reviewed sheet cannot be confirmed")
WoWCompagnonSubmit.scripts.OnClick()
assert(request()[1] == "C" and request()[4] == "1234")
local count = #TestSent
WoWCompagnonSubmit.scripts.OnClick()
assert(#TestSent == count, "Double click sent two confirmations")
incoming(header("3", "LOCKED", values))
assert(not WoWCompagnonSubmit.enabled, "Locked sheet became editable")
assert(not WoWCompagnonBack:IsShown(), "Locked sheet exposes an edit button")
assert(not WoWCompagnonPermanentCheck:IsShown(), "Locked sheet offers confirmation again")

-- The main stays silent on login; the command explains why it cannot be configured.
WoWCompagnonForm:Hide()
event("PLAYER_LOGIN")
TestNow = 10
events.scripts.OnUpdate(events)
incoming("E|4|INELIGIBLE")
assert(not WoWCompagnonForm:IsShown(), "Main auto-opened")
SlashCmdList.WOWCOMPAGNON()
incoming("E|5|INELIGIBLE")
assert(WoWCompagnonForm:IsShown() and not WoWCompagnonSubmit.enabled)

SlashCmdList.WOWCOMPAGNON()
incoming(header("6", "NEW", "-1|0|0||"))
choose(1, 1); choose(2, 171); choose(3, 182)
choose(4, "quality_benevolent"); choose(5, "flaw_stubborn")
choose(6, "quality_protective"); choose(7, "flaw_indiscreet")
choose(8, "quality_patient"); choose(9, "flaw_impulsive")
WoWCompagnonSubmit.scripts.OnClick()
incoming("P|7|40|1234|" .. values)
WoWCompagnonPermanentCheck:SetChecked(true)
WoWCompagnonPermanentCheck.scripts.OnClick()
assert(WoWCompagnonSubmit.enabled)
TestNow = TestNow + 301
events.scripts.OnUpdate(events)
assert(not WoWCompagnonPermanentCheck:IsShown(), "Expired review retained acknowledgement")
assert(WoWCompagnonSubmit.enabled, "Expired form cannot be reviewed again")
WoWCompagnonSubmit.scripts.OnClick()
assert(request()[1] == "P", "Expired review sent a permanent confirmation")
WoWCompagnonForm:Hide()
incoming("P|8|40|1234|" .. values)
assert(not WoWCompagnonForm:IsShown(), "Closing the form allowed a late reply to reopen it")

-- Missing traits, duplicates and mirror associations are handled independently.
local good = {tab = 1, first = 171, second = 182,
    qualities = {"quality_benevolent", "quality_protective", "quality_patient"},
    flaws = {"flaw_stubborn", "flaw_indiscreet", "flaw_impulsive"}}
assert(WC.Valid(good, data, 5))
good.qualities[2] = good.qualities[1]
assert(not WC.Valid(good, data, 5))
good.qualities[2] = nil
assert(not WC.Valid(good, data, 5))
assert(not WC.Parse("P|4|key|0|1|171|182|a,b,c|d,e,f", data, 5))
assert(not WC.Parse(string.rep("x", 249), data, 5))
assert(#data.qualities == 30 and #data.flaws == 30 and #data.professions == 11)
assert(#WC.PreviewPayload("12345", "40", WC.Values(WC.Split("1|171|182|" ..
    "quality_benevolent,quality_protective,quality_patient|flaw_stubborn,flaw_indiscreet,flaw_impulsive", "|"), 1)) <= 248)
print("PASS: form eligibility, dropdowns, independent choices, review, checkbox, immutable view, reply isolation")
