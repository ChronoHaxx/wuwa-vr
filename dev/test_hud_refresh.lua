-- Mocked reflection only. lua test_hud_refresh.lua mod/lua/wuwa_hud_refresh.lua
local module_path = assert(arg[1])
local M = assert(loadfile(module_path))()
local function named(name) return {to_string = function() return name end} end
local function field(name, kind, direction, next_field)
    return {get_fname = function() return named(name) end,
        get_class = function() return {get_fname = function() return named(kind) end} end,
        get_next = function() return next_field end}
end
local function callable(parameters, run)
    local first, properties = nil, {}
    for i = #parameters, 1, -1 do
        local p = parameters[i]
        local property = field(p[1], p[2], p[3], first)
        property.is_param = function() return true end
        property.is_reference_param = function() return false end
        property.is_return_param = function() return p[1] == "ReturnValue" end
        property.is_out_param = function() return p[3] == "out" or p[1] == "ReturnValue" end
        properties[p[1]] = property
        first = field(p[1], p[2], p[3], first) -- FField deliberately has no flags.
    end
    return setmetatable({get_child_properties = function() return first end,
        find_property = function(_, name) return properties[name] end,
        get_properties_size = function() return #parameters * 4 end}, {__call = function(_, ...) return run(...) end})
end
local function fixture()
    local f = {objects = {}, count = 0, queries = 0, writes = {}, width = 1920, height = 1080}
    local function class(name)
        local c = {name = name, functions = {}, props = {}}
        function c:find_function(key) return self.functions[key] end
        function c:find_property(key) return self.props[key] end
        return c
    end
    local function object(c, outer)
        f.count = f.count + 1
        local o = {id = f.count, class = c, outer = outer}
        function o:get_address() return self.id end
        function o:get_class() return self.class end
        function o:get_outer() return self.outer end
        function o:is_a(klass) return self.class == klass end
        f.objects[o.id] = o
        return o
    end
    local base = class("Object")
    f.world = object(base)
    local engine_class, viewport_class, pc_class = class("Engine"), class("Viewport"), class("PC")
    local scaler_class, canvas_class = class("Scaler"), class("Canvas")
    scaler_class.id, canvas_class.id = 1000, 1001
    scaler_class.get_address, canvas_class.get_address = function(self) return self.id end, function(self) return self.id end
    f.objects[1000], f.objects[1001] = scaler_class, canvas_class
    f.engine, f.viewport, f.pc = object(engine_class), object(viewport_class), object(pc_class, f.world)
    f.engine.GameViewport, f.viewport.World = f.viewport, f.world
    engine_class.props.GameViewport = field("GameViewport", "ObjectProperty")
    viewport_class.props.World = field("World", "ObjectProperty")
    f.scaler, f.canvas = object(scaler_class, f.world), object(canvas_class, f.world)
    f.scaler.Canvas = f.canvas
    scaler_class.props.Canvas = field("Canvas", "ObjectProperty")
    f.active, f.mode, f.projection, f.fov, f.near, f.far = true, 0, 0, 90, 1, 10000
    local function getter(c, name, kind, run) c.functions[name] = callable({{"ReturnValue", kind}}, run) end
    getter(scaler_class, "GetIsActiveAndEnable", "BoolProperty", function() return f.active end)
    getter(canvas_class, "GetRootCanvas", "ObjectProperty", function() return f.root or f.canvas end)
    getter(canvas_class, "GetRenderMode", "EnumProperty", function() return f.mode end)
    getter(scaler_class, "GetProjectionType", "ByteProperty", function() return f.projection end)
    getter(scaler_class, "GetFovAngle", "FloatProperty", function() return f.fov end)
    getter(scaler_class, "GetNearClipPlane", "FloatProperty", function() return f.near end)
    getter(scaler_class, "GetFarClipPlane", "FloatProperty", function() return f.far end)
    scaler_class.functions.ForceUpdate = callable({}, function()
        f.writes[#f.writes + 1] = "force"
        if f.after_force then f.after_force() end
    end)
    canvas_class.functions.SetProjectionParameters = callable({{"InProjectionType", "ByteProperty"},
        {"InFovAngle", "FloatProperty"}, {"InNearClipPlane", "FloatProperty"}, {"InFarClipPlane", "FloatProperty"}},
        function(_, projection, fov, near, far)
            assert(projection == f.projection and fov == f.fov and near == f.near and far == f.far)
            f.writes[#f.writes + 1] = "projection"
        end)
    pc_class.functions.GetViewportSize = callable({{"SizeX", "IntProperty", "out"}, {"SizeY", "IntProperty", "out"}},
        function(_, x, y) x.result, y.result = f.width, f.height end)
    f.classes = {scaler = scaler_class, canvas = canvas_class, pc = pc_class}
    f.list = {f.scaler}
    f.env = {api = {
        to_uobject = function(_, id) return f.objects[id] end,
        get_engine = function() return f.engine end, get_player_controller = function() return f.pc end,
        find_uobject = function(_, name)
            if f.missing_class then return nil end
            if name == "Class /Script/LGUI.LGUICanvasScaler" then return scaler_class end
            if name == "Class /Script/LGUI.LGUICanvas" then return canvas_class end
            error("Unexpected lookup")
        end,
    }, types = {UObjectHook = {get_objects_by_class = function(c, defaults)
        assert(c == scaler_class and defaults == false); f.queries = f.queries + 1; return f.list
    end}}}
    f.state = M.new(f.env)
    function f:tick(mode, delta)
        if mode == nil then mode = false end
        self.state:tick(mode, delta or 0.1)
    end
    function f:manual() self.state:request(); self:tick(); self:tick() end
    return f
end
local count = 0
local function test(name, run) run(); count = count + 1; print("PASS: " .. name) end
test("loading and idle ticks never query or mutate objects", function()
    local f = fixture(); for _ = 1, 20 do f:tick() end
    assert(f.queries == 0 and #f.writes == 0)
end)
test("manual request is deferred and preserves exact projection values", function()
    local f = fixture(); f.state:request(); assert(#f.writes == 0)
    f:tick(); assert(#f.writes == 0); f:tick()
    assert(f.queries == 1 and table.concat(f.writes, ",") == "force,projection")
    assert(f.state.status:find("check the headset", 1, true))
end)
test("legacy paused fallback is bounded and requires twelve stable samples", function()
    local f = fixture(); f.state:request()
    for _ = 1, 11 do f.state:tick(false, 0) end; assert(#f.writes == 0)
    f.state:tick(false, 0); assert(#f.writes == 2)
end)
test("monotonic clock waits 200ms after latest dimensions while simulation is paused", function()
    local f = fixture(); local clock = 100; f.env.clock = function() return clock end
    f.state:request(); f.state:tick(false, 0)
    for _ = 1, 10 do clock = clock + 0.01; f.state:tick(false, 0) end
    assert(#f.writes == 0)
    f.width = 1800; clock = 100.11; f.state:tick(false, 0)
    clock = 100.30; f.state:tick(false, 0); assert(#f.writes == 0)
    clock = 100.31; f.state:tick(false, 0); assert(#f.writes == 2)
end)
test("lost/reversed native clock and mismatched property descriptor fail closed", function()
    for _, bad in ipairs({false, 99}) do
        local f = fixture(); local clock = 100; f.env.clock = function() return clock end
        f.state:request(); f:tick(); clock = bad; f:tick()
        assert(#f.writes == 0 and f.state.pending == nil)
    end
    local f = fixture()
    f.classes.scaler.functions.GetFovAngle.find_property = function() return field("Wrong", "FloatProperty") end
    f:manual(); assert(#f.writes == 0)
end)
test("both transitions refresh once and rapid toggles coalesce", function()
    local f = fixture(); f:tick(false); f:tick(true); f:tick(false); assert(#f.writes == 0)
    f:tick(false); assert(#f.writes == 2)
    for _ = 1, 8 do f:tick(false) end; assert(#f.writes == 2)
    f:tick(true); f:tick(true); assert(#f.writes == 4)
end)
test("changing dimensions defer and timeout without object enumeration", function()
    local f = fixture(); f:tick(false); f:tick(true)
    for i = 1, 22 do f.width = 1900 + i; f:tick(true) end
    assert(f.queries == 0 and #f.writes == 0 and f.state.pending == nil)
end)
test("unavailable dimensions disable automatic but permit explicit manual refresh", function()
    local f = fixture(); f.classes.pc.functions.GetViewportSize = nil
    f:tick(false); f:tick(true); assert(f.queries == 0 and f.state.status:find("Automatic"))
    f.state:request(); f:tick(true); f:tick(true); assert(#f.writes == 2)
end)
test("known invalid viewport dimensions never permit manual fallback", function()
    local f = fixture(); f.width = 0; f:manual()
    assert(#f.writes == 0 and f.queries == 0)
    f.width = 1920; f:tick(); f:tick(); assert(#f.writes == 2)
    f = fixture(); f.width = 0; f.state:request()
    for _ = 1, 22 do f:tick() end
    assert(f.queries == 0 and f.state.pending == nil)
end)
test("missing schema, inactive, uninitialized, non-root and world canvases are skipped", function()
    for _, change in ipairs({
        function(f) f.classes.scaler.functions.ForceUpdate = nil end,
        function(f) f.classes.canvas.functions.SetProjectionParameters = callable({{"Wrong", "FloatProperty"}}, function() error("unsafe") end) end,
        function(f) f.active = false end,
        function(f) f.scaler.Canvas = nil end,
        function(f) f.root = f.world end,
        function(f) f.mode = 1 end,
        function(f) f.scaler.outer = nil end,
        function(f) f.missing_class = true end,
        function(f) f.fov = 0/0 end,
    }) do local f = fixture(); change(f); f:manual(); assert(#f.writes == 0) end
end)
test("stale scaler and canvas cannot be invoked", function()
    for _, name in ipairs({"scaler", "canvas"}) do
        local f = fixture(); f.objects[f[name].id] = nil; f:manual(); assert(#f.writes == 0)
    end
end)
test("world change while queued cancels without writes", function()
    local f = fixture(); f.state:request(); f:tick(); f.viewport.World = f.engine; f:tick()
    assert(#f.writes == 0 and f.queries == 0 and f.state.status:find("World changed"))
end)
test("ForceUpdate invalidation prevents second mutation", function()
    local f = fixture(); f.after_force = function() f.objects[f.canvas.id] = nil end
    f:manual(); assert(table.concat(f.writes, ",") == "force")
    assert(f.state.status:find("HUD changed"))
end)
test("oversized index rejected, pending reset cancelled, unknown mode cancels", function()
    local f = fixture(); for i = 1, 65 do f.list[i] = f.scaler end
    f:manual(); assert(#f.writes == 0)
    f = fixture(); f.state:request(); f.state:reset(); f:tick(); f:tick(); assert(f.queries == 0)
    f.state:request(); f.state:tick(nil, 0.1); assert(f.state.pending == nil and #f.writes == 0)
end)
test("Comfort button/native request run only on tick and publish changed status", function()
    local f = fixture(); local cb, writes, buttons = {}, {}, {}
    local values = {VR_2DScreenMode = "false", WuWaControls_ResetHudAspect = "false", WuWaControls_HudAspectStatus = "Ready"}
    uevr = {api = f.env.api, types = f.env.types, params = {vr = {
        get_mod_value = function(_, key) return values[key] end,
        set_mod_value = function(key, value)
            assert(key == "WuWaControls_ResetHudAspect" or key == "WuWaControls_HudAspectStatus")
            values[key] = value; writes[#writes + 1] = {key, value}
        end,
    }, functions = {log_info = function() end, log_error = function() end}},
        sdk = {callbacks = setmetatable({}, {__index = function(_, key) return function(fn) cb[key] = fn end end})}}
    imgui = {text = function() end, checkbox = function(_, v) return false, v end,
        drag_float = function(_, v) return false, v end,
        button = function(label) local value = buttons[label]; buttons[label] = nil; return value end}
    json = nil
    package.loaded.wuwa_hud_refresh = M
    local comfort = module_path:gsub("wuwa_hud_refresh.lua$", "01_WuWaVR_Comfort.lua")
    assert(loadfile(comfort))()
    cb.on_pre_engine_tick(nil, 0.1)
    local before = #writes; cb.on_pre_engine_tick(nil, 0.1); assert(#writes == before)
    buttons["Reset HUD aspect / 重置 HUD 比例"] = true; cb.on_draw_ui(); assert(#f.writes == 0)
    cb.on_pre_engine_tick(nil, 0.1); cb.on_pre_engine_tick(nil, 0.1); assert(#f.writes == 2)
    values.WuWaControls_ResetHudAspect = "true"
    cb.on_pre_engine_tick(nil, 0.1); assert(values.WuWaControls_ResetHudAspect == "false")
    cb.on_pre_engine_tick(nil, 0.1); assert(#f.writes == 4)
    assert(values.WuWaControls_HudAspectStatus:find("HUD layout refreshed", 1, true))
    for _, entry in ipairs(writes) do assert(#entry[2] <= 256 and utf8.len(entry[2])) end
    values.WuWaControls_ResetHudAspect = "true"; cb.on_pre_engine_tick(nil, 0.1)
    cb.on_script_reset(); cb.on_pre_engine_tick(nil, 0.1); assert(#f.writes == 4)
end)
print("HUD refresh tests passed: " .. count)
