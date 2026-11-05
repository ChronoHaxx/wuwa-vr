-- Mocked reflection only. lua test_uid_hide.lua mod/lua/wuwa_uid_hide.lua
local module_path = assert(arg[1])
local M = assert(loadfile(module_path))()
local TEXT = "Class /Script/LGUI.UIText"

local function named(name) return {to_string = function() return name end} end
local KINDS = {displayName = "StrProperty", AttachParent = "ObjectProperty"}
local registry, next_address, listings = {}, 0x1000, 0
local function descriptor(fades)
    return {
        find_property = function(_, name)
            local k = KINDS[name]
            return k and {get_class = function() return {get_fname = function() return named(k) end} end} or nil
        end,
        find_function = function(_, name)
            if fades and (name == "SetAlpha" or name == "GetAlpha") then return {} end
            return nil
        end,
        get_full_name = function() return TEXT end,
        get_fname = function() return named("UIText") end,
    }
end
local function item(name, parent, alpha, fades)
    next_address = next_address + 0x10
    local address = next_address
    local o = {displayName = name .. "\0", AttachParent = parent, alpha = alpha or 1}
    o.get_address = function() return address end
    o.get_class = function() return descriptor(fades ~= false) end
    o.GetAlpha = function(self) return self.alpha end
    o.SetAlpha = function(self, a) self.alpha = a end
    registry[address] = o
    return o
end
local texts = {}
local class = {get_address = function() return 0x10 end, get_full_name = function() return TEXT end}
registry[0x10] = class
local api = {
    to_uobject = function(_, address) return registry[address] end,
    find_uobject = function(_, name) return name == TEXT and class or nil end,
}
local types = {UObjectHook = {get_objects_by_class = function(c, _)
    listings = listings + 1
    return c == class and texts or {}
end}}

local passed, failed = 0, 0
local function check(cond, what)
    if cond then passed = passed + 1 else failed = failed + 1; print("FAIL: " .. what) end
end

-- The 3.7 shape: BattleViewUnitNode/UiView_UID_Prefab/UIC/UID, among hundreds of other texts, and a decoy "UID"
local root = item("ScreenSpaceUIRoot", nil)
for i = 1, 600 do texts[#texts + 1] = item("Txt" .. i, root) end
local decoy = item("UID", item("PnlFriend", root))
texts[#texts + 1] = decoy
local view = item("UiView_UID_Prefab", item("BattleViewUnitNode", root))
local uid = item("UID", item("UIC", view), 0.9)
uid.text = "User ID: 123456789"
texts[#texts + 1] = uid

local s = M.new({api = api, types = types})
local t, ticks = 100, 0
repeat s:tick(t, true); ticks = ticks + 1; t = t + 1 / 72 until s.status == "UID: hidden" or ticks > 10
check(s.status == "UID: hidden", "found and hidden: " .. s.status)
check(ticks <= 3, "found within 3 ticks of 256 texts: " .. ticks)
check(uid.alpha == 0, "the ID's alpha is 0")
check(decoy.alpha == 1, "a UID text outside the UID view is left alone")

-- the game sets the alpha again: hidden again next tick, and the saved alpha stays the original
uid.alpha = 1
s:tick(t, true)
check(uid.alpha == 0, "re-hidden after the game set its alpha")

-- privacy Off: the original alpha comes back; on again: hidden at once, without listing the texts again
local before = listings
s:tick(t, false)
check(math.abs(uid.alpha - 0.9) < 1e-6, "restored to the alpha the game had: " .. tostring(uid.alpha))
check(s.status == "UID: shown (privacy off)", "status says shown: " .. s.status)
s:tick(t, false)
check(math.abs(uid.alpha - 0.9) < 1e-6, "restoring twice changes nothing")
s:tick(t, true)
check(uid.alpha == 0 and listings == before, "hidden again at once, from the kept widget")

-- the widget is destroyed (a level change) and a new one appears: found again once the texts are listed again
registry[uid.get_address()] = nil
texts[#texts] = nil
local uid2 = item("UID", item("UIC", view), 1)
texts[#texts + 1] = uid2
s:tick(t, true)
ticks = 1
while s.status ~= "UID: hidden" and ticks < 400 do t = t + 1 / 72; s:tick(t, true); ticks = ticks + 1 end
check(uid2.alpha == 0, "the new widget is hidden")
check(ticks < 72 * 2 + 10, "within the 2 s relist and a few slices: " .. ticks)

-- no UID view (the title screen): one pass, then nothing until the next listing 2 s later
local s2 = M.new({api = api, types = types})
local saved = texts
texts = {}
for i = 1, 300 do texts[#texts + 1] = item("Title" .. i, root) end
local t2 = 500
s2:tick(t2, true); s2:tick(t2 + 0.01, true)
local n = listings
for k = 1, 100 do s2:tick(t2 + 0.02 + k * 0.01, true) end
check(listings == n, "no listing while waiting: " .. (listings - n))
check(s2.status == "UID: looking", "status says looking: " .. s2.status)
s2:tick(t2 + 2.5, true)
check(listings == n + 1, "listed again after 2 s")

-- found but the class can't fade (an LGUI change): said, no error, nothing touched
texts = {item("UID", item("UIC", view), 1, false)}
local s3 = M.new({api = api, types = types})
s3:tick(900, true)
check(s3.status == "UID: found, but it can't be faded", "unfadeable said: " .. s3.status)
check(texts[1].alpha == 1, "unfadeable left alone")

-- no LGUI text class at all
local s4 = M.new({api = {to_uobject = api.to_uobject, find_uobject = function() return nil end}, types = types})
s4:tick(1000, true)
check(s4.status == "UID: text class unavailable", "class unavailable said: " .. s4.status)
texts = saved

print(string.format("uid hide: %d passed, %d failed", passed, failed))
if failed > 0 then error("uid hide tests failed") end
