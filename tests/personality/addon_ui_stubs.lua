TestFrames, TestSent, TestMenu, TestNow = {}, {}, {}, 0
UISpecialFrames, SlashCmdList = {}, {}
UIParent = {}
local methods = {}
local function stub() end
for _, name in ipairs({"SetPoint", "SetFrameStrata", "SetMovable", "EnableMouse", "RegisterForDrag",
    "SetClampedToScreen", "SetBackdrop", "SetAllPoints", "SetJustifyH", "SetJustifyV",
    "StartMoving", "StopMovingOrSizing"}) do methods[name] = stub end
function methods:SetWidth(width) self.width = width end
function methods:SetHeight(height) self.height = height end
function methods:SetText(text) self.text = text end
function methods:SetScript(name, callback) self.scripts[name] = callback end
function methods:Show() self.shown = true end
function methods:Hide()
    local wasShown = self.shown
    self.shown = false
    if wasShown and self.scripts.OnHide then self.scripts.OnHide(self) end
end
function methods:IsShown() return self.shown end
function methods:Enable() self.enabled = true end
function methods:Disable() self.enabled = false end
function methods:GetChecked() return self.checked end
function methods:SetChecked(checked) self.checked = checked end
function methods:RegisterEvent(name) self.events[name] = true end
function methods:CreateFontString() return CreateFrame("FontString", nil, self) end
function CreateFrame(kind, name, parent, template)
    local result = setmetatable({scripts = {}, events = {}, shown = true, enabled = true,
        kind = kind, parent = parent, template = template}, {__index = methods})
    if name then _G[name] = result end
    TestFrames[#TestFrames + 1] = result
    return result
end
function GetLocale() return TestLocale end
function GetTime() return TestNow end
function UnitName() return "Example" end
function SendAddonMessage(...) TestSent[#TestSent + 1] = {...} end
function UIDropDownMenu_SetWidth(frame, width) frame.width = width end
function UIDropDownMenu_SetText(frame, text) frame.text = text end
function UIDropDownMenu_DisableDropDown(frame) frame:Disable() end
function UIDropDownMenu_EnableDropDown(frame) frame:Enable() end
function UIDropDownMenu_Initialize(frame, callback) frame.initialize = callback end
function UIDropDownMenu_CreateInfo() return {} end
function UIDropDownMenu_AddButton(info) TestMenu[#TestMenu + 1] = info end
