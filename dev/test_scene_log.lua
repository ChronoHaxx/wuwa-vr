-- Mocked reflection only. lua test_scene_log.lua mod/lua/wuwa_scene_log.lua
local module_path = assert(arg[1])
local M = assert(loadfile(module_path))()
local TEXT = "Class /Script/LGUI.UIText"

local function named(name) return {to_string = function() return name end} end
local KINDS = {displayName = "StrProperty", AttachParent = "ObjectProperty", Alpha = "FloatProperty",
    text = "StrProperty", bIsUIActive = "BoolProperty", GameViewport = "ObjectProperty", World = "ObjectProperty"}
local registry, next_address = {}, 0x1000
local function descriptor(class_name)
    return {
        find_property = function(_, name)
            local kind = KINDS[name]
            return kind and {get_class = function() return {get_fname = function() return named(kind) end} end} or nil
        end,
        find_function = function(_, name)
            if name ~= "IsUIActiveInHierarchy" then return nil end
            local ret = {get_fname = function() return named("ReturnValue") end,
                get_class = function() return {get_fname = function() return named("BoolProperty") end} end,
                get_next = function() return nil end}
            return {get_child_properties = function() return ret end}
        end,
        get_full_name = function() return class_name or TEXT end,
        get_fname = function() return named(class_name or "UIText") end,
    }
end
local function object(fields, class_name, address)
    local o = fields or {}
    if not address then next_address = next_address + 0x10; address = next_address end
    o.get_address = function() return address end
    o.get_class = function() return descriptor(class_name) end
    o.get_fname = function() return named(o.fname or "?") end
    o.get_full_name = function() return class_name or TEXT end
    -- LGUI: active in hierarchy = this item and every parent active
    o.IsUIActiveInHierarchy = function(self)
        local p = self
        while p do if p.bIsUIActive ~= true then return false end; p = p.AttachParent end
        return true
    end
    registry[address] = o
    return o
end
local function item(name, parent, active, text, alpha)
    return object({displayName = name .. "\0", AttachParent = parent, bIsUIActive = active ~= false,
        text = text and (text .. "\0") or nil, Alpha = alpha or 1})
end

local world = object({fname = "WP_3_7_WXSG"}, "World")
local viewport = object({World = world}, "GameViewport")
local engine = object({GameViewport = viewport}, "Engine")
local pawn = object({}, "BP_Xinyuehu_C")
local root = item("ScreenSpaceUIRoot", nil)
local info = item("PnlInfo", item("PnlProcess", root))
local title = item("TxtTitle", item("PnlTitle", info), true, "Simulacrum of the Heart")
local step = item("TxtParent", item("PnlSubTitle", item("PnlParent", info)), true, "Return to the Heart of Bloom")
local distance = item("TxtParentDistance", item("PnlParent", info), true, "518m<texture=/Game/Arrow.Arrow>")
local uid_view = item("UiView_UID_Prefab", item("BattleViewUnitNode", root))
local uid = item("UID", item("UIC", uid_view), true, "User ID: 701123532")
local watermark = item("TxtTime", item("PnlInfo", item("PnlPos", item("PnlPosition", root))), true, "2026-10-21 09:23:25+1 *")
local hidden_view = item("UiView_Plot", root, false)
local hidden = item("TxtTalk", hidden_view, true, "A line nobody sees")
local faded = item("TxtFade", root, true, "Fading out", 0)
local list = {title, step, distance, uid, watermark, hidden, faded}
local class = object({}, "Class")
local hook = {get_objects_by_class = function(c, _) assert(c == class); return list end}
local api = {
    get_engine = function() return engine end,
    get_local_pawn = function() return pawn end,
    to_uobject = function(_, address) return registry[address] end,
    find_uobject = function(_, path) assert(path == TEXT); return class end,
}
class.get_full_name = function() return TEXT end
local s = M.new({api = api, types = {UObjectHook = hook}})

local passed = 0
local function check(cond, message) assert(cond, message); passed = passed + 1 end
local function count(t) local n = 0; for _ in pairs(t or {}) do n = n + 1 end; return n end

-- Cleaning: tags, numbers-only, distances, masks, NUL ends.
check(M.clean("<size=16>Lv.</size>60") == "Lv.60", "rich-text tags are removed")
check(M.clean("6568/6568") == nil and M.clean("518m<texture=/Game/A.A>") == nil and M.clean("299ms") == nil,
    "numbers, distances and ping readings are dropped")
check(M.clean("User ID: 701123532") == "User ID: #########", "7+ digit runs are masked")
check(M.clean("Talk to the Celestials\0garbage") == "Talk to the Celestials", "text ends at the first NUL")
check(M.clean("  Head   to\nthe  anomaly ") == "Head to the anomaly", "whitespace collapses")
check(M.clean("New Text") == nil, "LGUI's placeholder text is dropped")

-- One pass (7 widgets < one scan step): the quest title and step, nothing else.
local t = 0
s:tick(t)
check(s.shown["PnlTitle/TxtTitle"] == nil, "keys carry the last three names")
check(s.shown["PnlInfo/PnlTitle/TxtTitle"] == "Simulacrum of the Heart", "quest title read")
check(s.shown["PnlParent/PnlSubTitle/TxtParent"] == "Return to the Heart of Bloom", "quest step read")
check(count(s.shown) == 2, "UID, watermark, distance, hidden view and faded text are not read")
for _, text in pairs(s.shown) do check(not text:find("7011"), "no UID digits anywhere") end

-- First take: a full snapshot, repeated on three rows, then nothing.
local p = s:take(t)
check(p and p.full and p.texts["PnlInfo/PnlTitle/TxtTitle"] and p.map == "WP_3_7_WXSG" and p.pawn == "BP_Xinyuehu_C",
    "the first row carries everything shown, the map and the character")
check(s:take(t + 0.03) == p and s:take(t + 0.06) == p, "a payload repeats on the next two rows")
check(s:take(t + 0.09) == nil, "then rows carry nothing while nothing changes")

-- A dialogue line appears and the step changes: one change payload, repeated; then the line goes.
hidden_view.bIsUIActive = true
step.text = "Talk to Leyue\0"
t = 1
s:tick(t)
p = s:take(t)
check(p and not p.full and p.set["ScreenSpaceUIRoot/UiView_Plot/TxtTalk"] == "A line nobody sees"
    and p.set["PnlParent/PnlSubTitle/TxtParent"] == "Talk to Leyue" and p.gone == nil, "changes only")
check(s:take(t) == p and s:take(t) == p and s:take(t) == nil, "repeated three times")
hidden_view.bIsUIActive = false
s:tick(2)
p = s:take(2)
check(p and p.gone and p.gone[1] == "ScreenSpaceUIRoot/UiView_Plot/TxtTalk" and p.set == nil, "a closed line is reported gone")
-- A new change while the last one is still repeating merges into it.
title.text = "Cerulean Echoes\0"
s:tick(2.05)
local q = s:take(2.05)
check(q.gone[1] == "ScreenSpaceUIRoot/UiView_Plot/TxtTalk" and q.set["PnlInfo/PnlTitle/TxtTitle"] == "Cerulean Echoes", "merged payload")

-- Full snapshot every 30 s, so a reader can start anywhere.
for _ = 1, 4 do s:take(3) end
p = s:take(40)
check(p and p.full and p.texts["PnlInfo/PnlTitle/TxtTitle"] == "Cerulean Echoes", "periodic full snapshot")

-- A widget address reused by a new widget after a list refresh is described afresh (no stale key/skip).
local reused = object({displayName = "UID\0", AttachParent = item("UIC", uid_view), bIsUIActive = true,
    text = "User ID: 701123532\0", Alpha = 1}, nil, title.get_address())
list = {reused, step}
s:tick(50) -- list refresh (4 s passed)
check(s.shown["PnlInfo/PnlTitle/TxtTitle"] == nil, "the reused address is not read as the old title")
for _, text in pairs(s.shown) do check(not text:find("7011"), "no UID text through a reused address") end

-- Reset: the next recording starts with a full snapshot.
s:reset()
s:tick(60)
p = s:take(60)
check(p and p.full, "after reset the first payload is full")

-- Unavailable class: reported, no error.
local s2 = M.new({api = {get_engine = api.get_engine, get_local_pawn = api.get_local_pawn, to_uobject = api.to_uobject,
    find_uobject = function() return nil end}, types = {UObjectHook = hook}})
s2:tick(0)
check(s2.status == "Scene info: text class unavailable" and s2:take(0).full, "no class: status, empty snapshot")

print(("test_scene_log: %d checks passed"):format(passed))
