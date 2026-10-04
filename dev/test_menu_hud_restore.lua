-- Offline Lua 5.4 checks; never connects to a game or writes a player profile.
-- lua test_menu_hud_restore.lua mod/lua/01_WuWaVR_Comfort.lua
local source = assert(arg[1], "Pass the Comfort script path")
local original_open = io.open
local layout_toggle = "Use saved Menu HUD temporarily / 临时使用已保存的菜单 HUD"
local visibility_toggle = "Temporarily show hidden UI in game menus / 游戏菜单中临时显示隐藏的界面"
local hud = {"UI_X_Offset", "UI_Y_Offset", "UI_Distance", "UI_Size", "UI_FollowView"}
local allowed = {VR_EnableGUI=true}
for _, key in ipairs(hud) do allowed[key] = true end
local initial = {UI_X_Offset="0.25", UI_Y_Offset="-0.3", UI_Distance="2.1", UI_Size="1.2",
    UI_FollowView="false", VR_EnableGUI="false"}
local saved = {UI_X_Offset=0, UI_Y_Offset=0, UI_Distance=2.5, UI_Size=1.6, UI_FollowView=true}
local function equal(a, b)
    return a == b or tonumber(a) and tonumber(b) and math.abs(tonumber(a)-tonumber(b)) < 0.00001
end
local function clone(values)
    local result = {}; for k,v in pairs(values) do result[k]=v end; return result
end

local function fixture(has_menu)
    local s = {values=clone(initial), callbacks={}, writes={}, changes={}, buttons={}, errors={}, text={}, ui=false}
    s.values.WuWaControls_NativeMenu="false"
    s.values.WuWaControls_Focused="true"
    s.values.WuWaControls_AdjustMode="false"
    s.values.VR_ToggleSlateGUIKey="119"
    io.open = function(name, mode)
        if name == "WuWaVR-layouts.json" then
            if has_menu ~= false then return {close=function() end} end
            return nil
        end
        return original_open(name, mode)
    end
    json = {load_file=function() return {version=1, Menu=clone(saved)} end}
    uevr = {
        params={vr={
            get_mod_value=function(_, key)
                if s.unavailable == key then return nil end
                return s.values[key]
            end,
            set_mod_value=function(key, value)
                assert(allowed[key], "Unexpected camera/render/input write: " .. key)
                s.writes[#s.writes+1] = {key=key, value=value}
                if s.fail_key == key then
                    s.fail_key = nil
                    if s.partial then s.values[key]=s.partial end
                    return
                end
                s.values[key]=value
            end,
        }, functions={
            is_drawing_ui=function() return s.ui end,
            log_info=function() end,
            log_error=function(value) s.errors[#s.errors+1]=value end,
        }},
        sdk={callbacks=setmetatable({}, {__index=function(_, key)
            return function(fn) s.callbacks[key]=fn end
        end})},
    }
    imgui = {
        text=function(value) s.text[#s.text+1]=value end,
        checkbox=function(key, current)
            if s.changes[key] ~= nil then
                local result = s.changes[key]; s.changes[key]=nil; return true, result
            end
            return false, current
        end,
        button=function(key) local result=s.buttons[key]; s.buttons[key]=nil; return result end,
        drag_float=function(_, current) return false, current end,
    }
    assert(loadfile(source))()
    function s:tick(detected, delta)
        self.values.WuWaControls_NativeMenu = detected == nil and nil or tostring(detected)
        self.callbacks.on_pre_engine_tick(nil, delta or 0.1)
        assert(#self.errors==0, self.errors[1])
    end
    function s:draw(changes)
        self.changes=changes or {}; self.ui=true; self.callbacks.on_draw_ui(); self.ui=false
    end
    function s:enable(placement, visibility)
        self:draw({[layout_toggle]=placement, [visibility_toggle]=visibility})
        self:tick(false); self:tick(false)
    end
    function s:original(except)
        for key,value in pairs(initial) do
            if not except or except ~= key then assert(equal(self.values[key], value), "Not restored: "..key.." = "..tostring(self.values[key])) end
        end
    end
    function s:active()
        for key,value in pairs(saved) do assert(equal(self.values[key], tostring(value)), "Menu layout absent: "..key) end
        assert(self.values.VR_EnableGUI == "true", "Hidden UI not temporarily shown")
    end
    return s
end

local passed = 0
local function test(name, run)
    run(); passed=passed+1; print("PASS: " .. name)
end

test("default off preserves all settings and does not react to unknown signals", function()
    local s=fixture(); s:tick(true); s:tick(nil); s:tick(false)
    s.values.WuWaControls_Focused="false"; s:tick(true)
    assert(#s.writes==0); s:original()
end)

test("entry applies once, repeated callbacks do not write, stable exit restores exact values", function()
    local s=fixture(); s:enable(true,true); s:tick(true); s:active()
    local writes=#s.writes
    for _=1,20 do s:tick(true) end
    assert(#s.writes==writes)
    s:tick(false); s:active(); s:tick(true); s:active() -- one false tick is ignored
    s:tick(false); s:tick(false); s:original()
    s:tick(true); s:active(); s.callbacks.on_script_reset(); s:original()
    s:tick(true); s:original() -- reset disables both options
end)

test("missing saved Menu never invents a layout; independent visibility remains useful", function()
    local s=fixture(false); s:enable(true,false); s:tick(true); assert(#s.writes==0); s:original()
    s:draw({[visibility_toggle]=true}); s:tick(false); s:tick(false); s:tick(true)
    s:original("VR_EnableGUI"); assert(s.values.VR_EnableGUI=="true")
    s:tick(false); s:tick(false); s:original()
end)

test("manual edits are kept and the same menu visit cannot overwrite them", function()
    local s=fixture(); s:enable(true,true); s:tick(true); s:active()
    s.values.UI_Distance="4.25"; s:tick(true); s:original("UI_Distance")
    assert(s.values.UI_Distance=="4.25")
    local count=#s.writes
    for _=1,5 do s:tick(true) end
    assert(#s.writes==count)
    s:tick(false); s:tick(false); s:tick(true); s:active()
    s:tick(false); s:tick(false); s:original("UI_Distance")
    assert(s.values.UI_Distance=="4.25", "New visit did not snapshot the manual setting")
end)

test("manual adjustment, focus loss and UEVR editing release ownership until next entry", function()
    for _, key in ipairs({"WuWaControls_AdjustMode", "WuWaControls_Focused", "draw_ui"}) do
        local s=fixture(); s:enable(true,true); s:tick(true)
        if key=="draw_ui" then s:draw()
        else s.values[key]=key=="WuWaControls_Focused" and "false" or "true"; s:tick(true) end
        s:original()
        s.values.WuWaControls_AdjustMode="false"; s.values.WuWaControls_Focused="true"
        s:tick(true); s:original()
        s:tick(false); s:tick(false); s:tick(true); s:active()
    end
end)

test("unknown native menu, focus or adjustment signals cannot activate or retain an override", function()
    for _,key in ipairs({"WuWaControls_NativeMenu", "WuWaControls_Focused", "WuWaControls_AdjustMode"}) do
        local s=fixture(); s:enable(true,true); s:tick(true); s:active()
        s.unavailable=key; s:tick(true); s:original()
        s.unavailable=nil; s:tick(true); s:original()
        s:tick(false); s:tick(false); s:tick(true); s:active()
    end
end)

test("partial failed apply restores every attempted field without touching render settings", function()
    local s=fixture(); s:enable(true,true)
    s.fail_key="UI_Size"; s.partial="1.4"; s:tick(true); s:original()
    s:tick(true); s:original()
    s:tick(false); s:tick(false); s:tick(true); s:active()
end)

test("interrupted restore is retained and retried before any reentry", function()
    local s=fixture(); s:enable(true,true); s:tick(true)
    s.fail_key="UI_Distance"; s:tick(false); s:tick(false)
    s:original("UI_Distance"); assert(equal(s.values.UI_Distance,2.5))
    s:tick(true); s:original()
end)

test("unavailable read during restore is not treated as a manual edit", function()
    local s=fixture(); s:enable(true,true); s:tick(true)
    s.unavailable="UI_Distance"; s:tick(true)
    s:original("UI_Distance"); assert(equal(s.values.UI_Distance,2.5))
    s.unavailable=nil; s:tick(true); s:original()
end)

test("disabling inside UEVR restores before editing and options are never persisted", function()
    local s=fixture(); s:enable(true,true); s:tick(true)
    s:draw({[layout_toggle]=false,[visibility_toggle]=false}); s:original()
    local count=#s.writes; s:tick(false); s:tick(false); s:tick(true)
    assert(#s.writes==count); s:original()
    local fresh=fixture(); fresh:tick(true); assert(#fresh.writes==0)
end)

io.open=original_open
print(string.format("PASS: %d offline menu HUD scenarios; headset/menu signal acceptance remains pending", passed))
