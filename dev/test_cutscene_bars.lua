-- Mocked reflection only. lua test_cutscene_bars.lua mod/lua/wuwa_cutscene_bars.lua
local module_path = assert(arg[1])
local M = assert(loadfile(module_path))()
local TEXTURE = "Class /Script/LGUI.UITexture"

local function named(name) return {to_string = function() return name end} end
local KINDS = {displayName = "StrProperty", AttachParent = "ObjectProperty", Alpha = "FloatProperty",
    Height = "FloatProperty", bIsUIActive = "BoolProperty", GameViewport = "ObjectProperty", World = "ObjectProperty"}
local registry, next_address = {}, 0x1000
local set_alpha_ok = true
local function descriptor()
    return {
        find_property = function(_, name)
            local kind = KINDS[name]
            return kind and {get_class = function() return {get_fname = function() return named(kind) end} end} or nil
        end,
        find_function = function(_, name)
            if name ~= "SetAlpha" then return nil end
            local param = {get_fname = function() return named(set_alpha_ok and "newAlpha" or "Value") end,
                get_class = function() return {get_fname = function() return named("FloatProperty") end} end,
                get_next = function() return nil end}
            return {get_child_properties = function() return param end}
        end,
        get_full_name = function() return TEXTURE end,
    }
end
local function object(fields, outer)
    next_address = next_address + 0x10
    local o = fields or {}
    local address = next_address
    o.get_address = function() return address end
    o.get_class = function() return descriptor() end
    o.get_outer = function() return outer end
    o.get_full_name = function() return TEXTURE end
    o.SetAlpha = function(self, value) self.Alpha = value; self.sets = (self.sets or 0) + 1 end
    registry[address] = o
    return o
end

local world = object({})
local other_world = object({})
local viewport = object({World = world})
local engine = object({GameViewport = viewport})
local class = object({})
local function view(name, active, height) return object({displayName = name .. "\0", bIsUIActive = active, Height = height or 1440}, world) end
local fade = view("UiView_BlackFadeScreen_Prefab", true)
local stale_fade = view("UiView_BlackFadeScreen_Prefab", false)
local black_screen = view("UiView_BlackScreen_Prefab", true)
local function texture(name, parent, alpha, height, outer)
    return object({displayName = name .. "\0", AttachParent = parent, Alpha = alpha, Height = height or 180}, outer or world)
end
local bottom = texture("BlackTexture", fade, 1)
local top = texture("BlackTexture1", fade, 1)
local stale = texture("BlackTexture", stale_fade, 1)
local decoy = texture("BlackTexture", black_screen, 1, 1440)
local other = texture("SprPhoto", fade, 1)
local textures = {other, decoy, stale, bottom, top}
local scans = 0
local hook = {get_objects_by_class = function(c, _) assert(c == class); scans = scans + 1; return textures end}
local api = {
    get_engine = function() return engine end,
    to_uobject = function(_, address) return registry[address] end,
    find_uobject = function(_, path) assert(path == TEXTURE); return class end,
}
local values = {WuWaControls_Clock = "0", WuWaStereo_HideCutsceneBars = "true",
    WuWaControls_EffectiveScreen = "false", WuWaControls_EffectiveMonoTheatre = "false"}
local logs = {}
local s = M.new({api = api, types = {UObjectHook = hook}, get = function(k) return values[k] end,
    log = function(t) logs[#logs + 1] = t end})
local clock = 0
local function tick(n)
    for _ = 1, n or 1 do clock = clock + 0.25; values.WuWaControls_Clock = tostring(clock); s:tick() end
end

local passed = 0
local function check(cond, message) assert(cond, message); passed = passed + 1 end

-- Off: nothing is searched or touched.
values.WuWaStereo_HideCutsceneBars = "false"
tick(3)
check(scans == 0 and bottom.Alpha == 1 and top.Alpha == 1, "off searches nothing")
check(s.status == "Cutscene bars: off", "off status")

-- Full VR and on: the two letterbox bars of the active fade view are hidden, nothing else.
values.WuWaStereo_HideCutsceneBars = "true"
tick(2)
check(scans == 1, "one search")
check(bottom.Alpha == 0 and top.Alpha == 0, "both bars hidden")
check(stale.Alpha == 1 and decoy.Alpha == 1 and other.Alpha == 1, "stale view, other view and other names untouched")
check(s.status == "Cutscene bars: 2 hidden in full VR", "hidden status: " .. s.status)

-- The stereo screen keeps the game's bars; full VR hides them again without a new search.
values.WuWaControls_EffectiveScreen = "true"
tick()
check(bottom.Alpha == 1 and top.Alpha == 1, "screen restores the game's alpha")
values.WuWaControls_EffectiveScreen = "false"
values.WuWaControls_EffectiveMonoTheatre = "true"
tick()
check(bottom.Alpha == 1, "mono theatre keeps them")
values.WuWaControls_EffectiveMonoTheatre = "false"
tick()
check(bottom.Alpha == 0 and top.Alpha == 0 and scans == 1, "full VR hides again from the cached bars")

-- The game shows a bar again with its own alpha: hidden again, and that alpha is what comes back.
top.Alpha = 0.8
tick()
check(top.Alpha == 0, "re-shown bar hidden again")
values.WuWaControls_EffectiveScreen = "true"
tick()
check(top.Alpha == 0.8 and bottom.Alpha == 1, "restore gives the game's latest alpha")
values.WuWaControls_EffectiveScreen = "false"
tick()

-- A bar larger than a letterbox (a fade to black) is the game's: given back, then hidden once small again.
bottom.Height = 720
tick()
check(bottom.Alpha == 1 and top.Alpha == 0, "fade-sized bar restored")
bottom.Height = 180
tick()
check(bottom.Alpha == 0, "letterbox again: hidden")

-- If the game changed a hidden bar's alpha to something else, restore leaves it alone.
values.WuWaStereo_HideCutsceneBars = "false"
bottom.Alpha = 0.5 -- (impossible while hidden in one tick, but the game wins)
tick()
check(bottom.Alpha == 0.5 and top.Alpha == 0.8, "off restores only what we hid")
check(s.status == "Cutscene bars: off", "off again")

-- A new world: the cached bars are dropped and searched again.
values.WuWaStereo_HideCutsceneBars = "true"
bottom.Alpha, top.Alpha = 1, 1
viewport.World = other_world
tick(2)
check(bottom.Alpha == 1, "bars of another world are not touched")
viewport.World = world
tick(20)
check(scans >= 2 and bottom.Alpha == 0 and top.Alpha == 0, "back in the world: found and hidden")

-- A SetAlpha with another signature is never called.
values.WuWaStereo_HideCutsceneBars = "false"
tick()
set_alpha_ok = false
local sets = (bottom.sets or 0)
values.WuWaStereo_HideCutsceneBars = "true"
tick(3)
check(bottom.Alpha == 1 and (bottom.sets or 0) == sets, "wrong signature: no call")
set_alpha_ok = true

-- Polling: at most every 0.2 s of the native clock.
values.WuWaStereo_HideCutsceneBars = "true"
tick()
top.Alpha = 1
values.WuWaControls_Clock = tostring(clock + 0.05)
s:tick()
check(top.Alpha == 1, "no work inside the poll interval")
clock = clock + 0.3
values.WuWaControls_Clock = tostring(clock)
s:tick()
check(top.Alpha == 0, "work after the interval")

-- Reset gives everything back.
s:reset()
check(top.Alpha == 1 and bottom.Alpha == 1, "reset restores")
check(#logs > 0, "status changes are logged")
print(string.format("test_cutscene_bars: %d checks passed", passed))
