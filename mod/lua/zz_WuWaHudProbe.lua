-- Read-only HUD probe for private tests (3h). After a switch from a screen view
-- to full VR the HUD stays squashed until the ESC menu has been opened once,
-- and the automatic refresh has nothing to work with: test 3g found a single
-- LGUICanvasScaler, without the expected GetIsActiveAndEnable. This logs what
-- the game's UI actually is, so the layout change ESC makes can be found:
-- viewport size, LGUI root canvases (size, render mode), the scaler's settings.
-- It only reads and calls getters. "[WuWaHudProbe]" lines: at load, 0.3 s and
-- 2 s after each view switch, 0.5 s and 2 s after the ESC menu closes; at most
-- 40 dumps per script session. Not part of a release.
local api, types, log, vr = uevr.api, uevr.types, uevr.params.functions, uevr.params.vr
local function L(s) log.log_info("[WuWaHudProbe] " .. s) end
local function addr(o) local ok, v = pcall(function() return o:get_address() end) return ok and v or nil end
local function alive(o) local a = o and addr(o) return a ~= nil and api:to_uobject(a) ~= nil end
local function try(f) local ok, v = pcall(f) if ok then return v end return "err:" .. tostring(v):sub(1, 80) end
local function name_of(o) return try(function() return o:get_fname():to_string() end) end
local function class_of(o) return try(function() return o:get_class():get_fname():to_string() end) end
local function full_of(o) return try(function() return o:get_full_name() end) end
local function value(v)
    if type(v) == "userdata" then
        local ok, x = pcall(function() return v.X end)
        local ok2, y = pcall(function() return v.Y end)
        if ok and ok2 and x ~= nil and y ~= nil then return string.format("(%s,%s)", tostring(x), tostring(y)) end
        local ok3, n = pcall(function() return v:get_full_name() end)
        if ok3 and n then return n end
    end
    return tostring(v)
end
local function props(o, limit)
    local out, count = {}, 0
    local cls = o:get_class()
    while cls and count < (limit or 60) do
        local p = cls:get_child_properties()
        while p and count < (limit or 60) do
            local n, t = name_of(p), class_of(p)
            if t == "FloatProperty" or t == "IntProperty" or t == "BoolProperty" or t == "ByteProperty" or
                t == "EnumProperty" or t == "StructProperty" or t == "DoubleProperty" or t == "ObjectProperty" then
                out[#out + 1] = n .. "=" .. try(function() return value(o[n]) end)
                count = count + 1
            end
            p = p:get_next()
        end
        cls = cls:get_super_struct()
        if cls and name_of(cls) == "Object" then break end
    end
    return table.concat(out, " ")
end
local function functions(cls, limit)
    local out = {}
    local f = try(function() return cls:get_children() end)
    while type(f) == "userdata" and #out < (limit or 80) do
        out[#out + 1] = name_of(f)
        f = try(function() return f:get_next() end)
    end
    return table.concat(out, " ")
end
local function call(o, name)
    local fn = try(function() return o:get_class():find_function(name) end)
    if type(fn) ~= "userdata" then return "n/a" end
    return try(function() return value(o[name](o)) end)
end
local function viewport_world()
    return try(function() return api:get_engine().GameViewport.World end)
end
local function in_world(o, world)
    for _ = 1, 12 do
        if not alive(o) then return false end
        if alive(world) and addr(o) == addr(world) then return true end
        o = try(function() return o:get_outer() end)
        if type(o) ~= "userdata" then return false end
    end
    return false
end
local function objects(path)
    local class = api:find_uobject(path)
    if not class then return nil end
    return types.UObjectHook.get_objects_by_class(class, false) or {}
end

local dumps, described = 0, false
local function dump(reason)
    if dumps >= 40 then return end
    dumps = dumps + 1
    local screen = try(function() return vr:get_mod_value("WuWaControls_EffectiveScreen") end)
    local mono = try(function() return vr:get_mod_value("WuWaControls_EffectiveMonoTheatre") end)
    local pc = api:get_player_controller(0)
    local vx, vy = {}, {}
    try(function() pc:GetViewportSize(vx, vy) end)
    L(string.format("dump %d (%s): screen=%s mono=%s viewport=%sx%s hud_shape=%s", dumps, reason, tostring(screen), tostring(mono),
        tostring(vx.result), tostring(vy.result), tostring(try(function() return vr:get_mod_value("UI_WuWaHudShape") end))))
    local world = viewport_world()
    local canvases = objects("Class /Script/LGUI.LGUICanvas")
    local scalers = objects("Class /Script/LGUI.LGUICanvasScaler")
    if not canvases or not scalers then L("LGUI classes not found") return end
    if not described then
        described = true
        local scaler_class = api:find_uobject("Class /Script/LGUI.LGUICanvasScaler")
        local canvas_class = api:find_uobject("Class /Script/LGUI.LGUICanvas")
        L("viewport world: " .. full_of(world))
        L("LGUICanvasScaler functions: " .. functions(scaler_class))
        L("LGUICanvas functions: " .. functions(canvas_class, 140))
    end
    local roots, modes, in_vp = 0, {}, 0
    for i, c in ipairs(canvases) do
        if alive(c) then
            local w = in_world(c, world)
            if w then in_vp = in_vp + 1 end
            local root = try(function() return addr(c:GetRootCanvas()) == addr(c) end)
            local mode = tostring(try(function() return c:GetRenderMode() end))
            modes[mode] = (modes[mode] or 0) + 1
            if root == true and roots < 24 then
                roots = roots + 1
                -- 3h: GetUIItem / GetIsActiveAndEnable do not exist in WuWa's LGUI.
                L(string.format("root canvas %d: mode=%s actual=%s visible=%s in_viewport_world=%s viewport=%s scale=%s | %s", i, mode,
                    call(c, "GetActualRenderMode"), call(c, "IsUIVisible"), tostring(w), call(c, "GetViewportSize"),
                    call(c, "GetCanvasScale"), full_of(c):sub(1, 160)))
            end
        end
    end
    local mode_list = {}
    for k, v in pairs(modes) do mode_list[#mode_list + 1] = k .. ":" .. v end
    L(string.format("canvases=%d (in viewport world %d, roots listed %d, render modes %s) scalers=%d", #canvases, in_vp, roots,
        table.concat(mode_list, " "), #scalers))
    for i, s in ipairs(scalers) do
        if i > 4 then break end
        if alive(s) then
            L(string.format("scaler %d: in_viewport_world=%s | %s", i, tostring(in_world(s, world)), full_of(s):sub(1, 160)))
            L("scaler " .. i .. " settings: " .. props(s, 12))
        end
    end
end

local elapsed, last_mode, pending, menu_open = 0, nil, {}, false
local function queue(delay, why) pending[#pending + 1] = {at = elapsed + delay, why = why} end
uevr.sdk.callbacks.on_pre_engine_tick(function(_, delta)
    if dumps >= 40 then return end
    elapsed = elapsed + (type(delta) == "number" and delta or 0)
    local ok, mode = pcall(function()
        return tostring(vr:get_mod_value("WuWaControls_EffectiveScreen")) .. "/" .. tostring(vr:get_mod_value("WuWaControls_EffectiveMonoTheatre"))
    end)
    if ok and last_mode ~= nil and mode ~= last_mode then
        queue(0.3, "0.3 s after switch to " .. mode); queue(2.0, "2 s after switch to " .. mode)
    end
    if ok then last_mode = mode end
    -- The ESC menu shows the mouse cursor; dump after it closes.
    local okm, open = pcall(function() local pc = api:get_player_controller(0) return pc ~= nil and pc.bShowMouseCursor == true end)
    if okm then
        if menu_open and not open then queue(0.5, "0.5 s after a menu closed"); queue(2.0, "2 s after a menu closed") end
        menu_open = open
    end
    if dumps == 0 and elapsed > 3 then pcall(dump, "load") end
    for i = #pending, 1, -1 do
        if elapsed >= pending[i].at then
            local why = pending[i].why
            table.remove(pending, i)
            local done, err = pcall(dump, why)
            if not done then L("dump failed: " .. tostring(err):sub(1, 160)) end
        end
    end
end)
L("loaded (read-only); dumps after view switches and after the ESC menu closes")
