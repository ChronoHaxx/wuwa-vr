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
local function fixture(options)
    local f = {objects = {}, count = 0, queries = 0, writes = {}, cache_writes = 0, width = 1920, height = 1080}
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
        o.mem = {}
        local function bounds(self, offset, bytes)
            local size = self.class.get_properties_size and self.class:get_properties_size()
            if not size or offset + bytes > size then error("Offset out of bounds") end
        end
        function o:read_dword(offset) bounds(self, offset, 4); return self.mem[offset] or 0 end
        function o:read_qword(offset) bounds(self, offset, 8); return self.mem[offset] or 0 end
        function o:write_dword(offset, value) bounds(self, offset, 4); self.mem[offset] = value; f.cache_writes = f.cache_writes + 1 end
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
    -- LGUICanvasScaler layout read live 13 Oct: 336 bytes, Canvas at 0x130, then
    -- two FIntPoint copies of the viewport size it last heard about.
    f.scaler_bytes, f.canvas_offset = 336, 0x130
    function scaler_class:get_properties_size() return f.scaler_bytes end
    scaler_class.props.Canvas.get_offset = function() return f.canvas_offset end
    f.scaler.mem[0x130] = f.canvas.id
    function f:set_cache(w, h, w2, h2)
        local m = self.scaler.mem
        m[0x138], m[0x13C], m[0x148], m[0x14C] = w, h, w2 or w, h2 or h
    end
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
    if options and options.wuwa then
        -- WuWa's schema (13 Oct): no ForceUpdate or GetIsActiveAndEnable; the
        -- scaler sizes the root UI item of its canvas (reference 2560 x 1440,
        -- shrink), and after stereo screen the item kept 2560 x 1440 in full VR.
        scaler_class.functions.ForceUpdate, scaler_class.functions.GetIsActiveAndEnable = nil, nil
        local actor_class, item_class = class("UIContainerActor"), class("UIItem")
        item_class.id, item_class.get_address = 1002, function(self) return self.id end
        f.objects[1002], f.classes.item, f.classes.actor = item_class, item_class, actor_class
        f.actor = object(actor_class, f.world)
        f.item = object(item_class, f.actor)
        f.viewport_size, f.item_size, f.scale = {X = 2560, Y = 2560}, {2560, 1440}, 1
        f:set_cache(2560, 2560)
        local function adapted(w, h) return f.adapted_scale or math.min(w / 2560, h / 1440) end
        -- The scaler lays out from its cached viewport whenever a setting changes;
        -- a same-value setter returns early (both seen live).
        local function relayout()
            local w, h = f.scaler.mem[0x138], f.scaler.mem[0x13C]
            f.scale = adapted(w, h); f.item_size = {w / f.scale, h / f.scale}
        end
        -- The game's own viewport event (ESC, entering stereo screen).
        function f:game_viewport(w, h) self:set_cache(w, h); relayout() end
        f.scaler.MatchFromWidthToHeight = 0
        scaler_class.props.MatchFromWidthToHeight = field("MatchFromWidthToHeight", "FloatProperty")
        scaler_class.functions.SetMatchFromWidthToHeight = callable({{"Value", "FloatProperty"}}, function(o, value)
            f.writes[#f.writes + 1] = "match"
            if o.MatchFromWidthToHeight ~= value then
                o.MatchFromWidthToHeight = value
                if not f.ignore_match then relayout() end
            end
        end)
        -- Counted: in the game this call computes a view and re-enters UEVR's stereo callbacks.
        f.viewport_calls = 0
        getter(canvas_class, "GetViewportSize", "StructProperty", function() f.viewport_calls = f.viewport_calls + 1; return f.viewport_size end)
        getter(canvas_class, "GetCanvasScale", "FloatProperty", function() return f.scale end)
        getter(canvas_class, "GetOwner", "ObjectProperty", function() return f.actor end)
        getter(actor_class, "K2_GetRootComponent", "ObjectProperty", function() return f.item end)
        getter(item_class, "GetRootCanvas", "ObjectProperty", function() return f.item_canvas or f.canvas end)
        getter(item_class, "GetWidth", "FloatProperty", function() return f.item_size[1] end)
        getter(item_class, "GetHeight", "FloatProperty", function() return f.item_size[2] end)
        item_class.functions.SetWidth = callable({{"NewWidth", "FloatProperty"}}, function(_, value)
            f.item_size[1] = value; f.writes[#f.writes + 1] = "width" end)
        item_class.functions.SetHeight = callable({{"NewHeight", "FloatProperty"}}, function(_, value)
            f.item_size[2] = value; f.writes[#f.writes + 1] = "height" end)
        scaler_class.functions.CalculateAdaptedSizeAndScale = callable({{"Width", "FloatProperty"},
            {"Height", "FloatProperty"}, {"OutWidth", "FloatProperty", "out"}, {"OutHeight", "FloatProperty", "out"},
            {"OutScale", "FloatProperty", "out"}}, function(_, width, height, out_width, out_height, out_scale)
                local scale = adapted(width, height)
                out_width.result, out_height.result, out_scale.result = width / scale, height / scale, scale
            end)
    end
    f.list = {f.scaler}
    f.env = {api = {
        to_uobject = function(_, id) return f.objects[id] end,
        get_engine = function() return f.engine end, get_player_controller = function() return f.pc end,
        find_uobject = function(_, name)
            if f.missing_class then return nil end
            if name == "Class /Script/LGUI.LGUICanvasScaler" then return scaler_class end
            if name == "Class /Script/LGUI.LGUICanvas" then return canvas_class end
            if name == "Class /Script/LGUI.UIItem" then return f.classes.item end
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
test("loading and idle ticks never mutate; idle full VR looks the scaler up once per world", function()
    local f = fixture(); for _ = 1, 9 do f:tick() end
    assert(f.queries == 0 and #f.writes == 0)
    for _ = 1, 40 do f:tick() end
    assert(f.queries == 1 and #f.writes == 0 and f.cache_writes == 0)
    f = fixture(); for _ = 1, 40 do f:tick(true) end
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
    -- Mono overlays an existing stereo-screen choice without changing it.
    -- Both directions must refresh even when the effective screen stays true.
    values.VR_2DScreenMode = "true"
    cb.on_pre_engine_tick(nil, 0.1); cb.on_pre_engine_tick(nil, 0.1)
    assert(#f.writes == 6)
    values.VR_MonoTheatreMode = "true"
    cb.on_pre_engine_tick(nil, 0.1); cb.on_pre_engine_tick(nil, 0.1)
    assert(#f.writes == 8 and values.VR_2DScreenMode == "true")
    values.VR_MonoTheatreMode = "false"
    cb.on_pre_engine_tick(nil, 0.1); cb.on_pre_engine_tick(nil, 0.1)
    assert(#f.writes == 10 and values.VR_2DScreenMode == "true")
    values.WuWaControls_ResetHudAspect = "true"; cb.on_pre_engine_tick(nil, 0.1)
    cb.on_script_reset(); cb.on_pre_engine_tick(nil, 0.1); assert(#f.writes == 10)
end)
test("unavailable Comfort helper explains repeated native reset clicks without idle status writes", function()
    local old_loaded, old_preload = package.loaded.wuwa_hud_refresh, package.preload.wuwa_hud_refresh
    local comfort = module_path:gsub("wuwa_hud_refresh.lua$", "01_WuWaVR_Comfort.lua")
    for _, phase in ipairs({"load", "initialization"}) do
        local marker = "fixture HUD " .. phase .. " failure"
        local cb, errors, status_writes = {}, {}, 0
        local values = {VR_2DScreenMode = "false", WuWaControls_ResetHudAspect = "false", WuWaControls_HudAspectStatus = "Ready"}
        uevr = {api = {}, types = {}, params = {vr = {
            get_mod_value = function(_, key) return values[key] end,
            set_mod_value = function(key, value)
                assert(key == "WuWaControls_ResetHudAspect" or key == "WuWaControls_HudAspectStatus")
                if key == "WuWaControls_HudAspectStatus" then status_writes = status_writes + 1 end
                values[key] = value
            end,
        }, functions = {log_info = function() end, log_error = function(error) errors[#errors + 1] = error end}},
            sdk = {callbacks = setmetatable({}, {__index = function(_, key) return function(fn) cb[key] = fn end end})}}
        json = nil
        if phase == "load" then
            package.loaded.wuwa_hud_refresh = nil
            package.preload.wuwa_hud_refresh = function() error(marker) end
        else
            package.loaded.wuwa_hud_refresh = {new = function() error(marker) end}
        end
        assert(loadfile(comfort))()
        cb.on_pre_engine_tick(nil, 0.1)
        local failure = values.WuWaControls_HudAspectStatus
        assert(failure:find("module unavailable", 1, true) and failure:find(marker, 1, true))
        assert(#errors == 1 and errors[1]:find(marker, 1, true))
        for _ = 1, 3 do
            local before = status_writes
            for _ = 1, 12 do cb.on_pre_engine_tick(nil, 0.1) end
            assert(status_writes == before, "unchanged native status was written every frame")
            values.WuWaControls_HudAspectStatus = "Refresh queued; requires the supplied Comfort script."
            values.WuWaControls_ResetHudAspect = "true"
            cb.on_pre_engine_tick(nil, 0.1)
            assert(values.WuWaControls_ResetHudAspect == "false")
            assert(values.WuWaControls_HudAspectStatus == failure and status_writes == before + 1,
                "unchanged module failure left the native button queued")
        end
        assert(#errors == 1, "module failure was logged repeatedly")
    end
    package.loaded.wuwa_hud_refresh, package.preload.wuwa_hud_refresh = old_loaded, old_preload
end)
test("unsupported HUD saves one read-only audit without repeated idle or reset work", function()
    local old_module, old_json = package.loaded.wuwa_hud_schema, json
    local audits, saves, saved = 0, 0, nil
    package.loaded.wuwa_hud_schema = function()
        audits = audits + 1
        return {status = "complete", scalers = {}}
    end
    json = {dump_file = function(path, report)
        assert(path:find("diagnostics/hud-schema-auto-", 1, true) == 1)
        saves, saved = saves + 1, report; return true
    end, load_file = function() return saved end}
    local f = fixture(); f.active = false
    f:manual()
    assert(audits == 1 and saves == 1 and #f.writes == 0 and f.state.schema_report)
    assert(f.state.status:find("HUD refresh unavailable", 1, true))
    for _ = 1, 10 do f:tick() end
    f:manual(); f.state:reset(); f:manual()
    assert(audits == 1 and saves == 1 and #f.writes == 0)
    package.loaded.wuwa_hud_schema, json = old_module, old_json
end)
test("audit failure cannot turn a rejected HUD request into success or repeated work", function()
    local old_module, old_json = package.loaded.wuwa_hud_schema, json
    local audits = 0
    package.loaded.wuwa_hud_schema = function() audits = audits + 1; error("audit fixture failure") end
    json = {dump_file = function() error("must not write") end}
    local f = fixture(); f.active = false
    f:manual(); f:manual()
    assert(audits == 1 and #f.writes == 0 and not f.state.schema_report)
    assert(f.state.status:find("HUD refresh unavailable", 1, true))
    package.loaded.wuwa_hud_schema, json = old_module, old_json
end)
test("WuWa: a 16:9 root item left from stereo screen is fitted to the eye view", function()
    local f = fixture({wuwa = true}); f:manual()
    assert(table.concat(f.writes, ",") == "height" and f.item_size[1] == 2560 and f.item_size[2] == 2560)
    assert(f.state.status:find("HUD root fitted (1) 2560x1440 -> 2560x2560", 1, true), f.state.status)
end)
test("WuWa: a matching root item is left alone without the unsupported-HUD audit", function()
    local old_module = package.loaded.wuwa_hud_schema
    package.loaded.wuwa_hud_schema = function() error("audit must not run") end
    for _, case in ipairs({{{X = 2560, Y = 2560}, {2560, 2560}}, {{X = 2560, Y = 1440}, {2560, 1440}}}) do
        local f = fixture({wuwa = true}); f.viewport_size, f.item_size = case[1], case[2]
        f:set_cache(case[1].X, case[1].Y); f:manual()
        assert(#f.writes == 0 and f.state.status:find("already matches", 1, true), f.state.status)
    end
    package.loaded.wuwa_hud_schema = old_module
end)
test("WuWa: returning to full VR fits once; the follow-ups find it fitted", function()
    local f = fixture({wuwa = true}); f:tick(true); f:tick(false); f:tick(false)
    assert(table.concat(f.writes, ",") == "height")
    for _ = 1, 40 do f:tick(false) end
    assert(table.concat(f.writes, ",") == "height" and f.state.followups == nil)
    assert(f.state.status:find("already matches", 1, true), f.state.status)
end)
test("WuWa: scale change, foreign item, world-space canvas and bad sizes are not written", function()
    for _, change in ipairs({
        {"canvas scale differs", function(f) f.adapted_scale = 2 end},
        {"root item of another canvas", function(f) f.item_canvas = f.world end},
        {"not a screen-space canvas", function(f) f.mode = 1 end},
        {"not a root canvas", function(f) f.root = f.world end},
        {"viewport size unavailable", function(f) f.viewport_size = {X = 0 / 0, Y = 2560} end},
        {"viewport size unavailable", function(f) f.viewport_size = {X = 2560} end},
        {"viewport size unavailable", function(f) f.viewport_size = nil end},
        {"no root UI item", function(f) f.actor = nil end},
        {"no root UI item", function(f) f.item = f.world end},
        {"no root UI item", function(f) f.objects[f.item.id] = nil end},
        {"UIItem class unavailable", function(f) f.classes.item = nil end},
        {"UI item schema differs", function(f)
            f.classes.item.functions.SetHeight = callable({{"NewHeight", "FloatProperty", "out"}}, function() error("unsafe") end) end},
        {"scaler schema differs", function(f) f.classes.scaler.functions.CalculateAdaptedSizeAndScale = nil end},
    }) do
        local f = fixture({wuwa = true}); change[2](f); f:manual()
        assert(#f.writes == 0 and f.state.status:find("No active HUD", 1, true), f.state.status)
        assert(f.state.last_rejections:find("fit: " .. change[1], 1, true) or
            f.state.last_rejections:find(change[1], 1, true), change[1] .. " / " .. f.state.last_rejections)
    end
end)
local function stale(f)
    -- 3292-pixel eyes after stereo screen: the cache kept the monitor size.
    f.viewport_size = {X = 3292, Y = 3292}; f:game_viewport(2560, 1440)
    assert(f.scale == 1 and f.item_size[1] == 2560 and f.item_size[2] == 1440)
end
local function synced(f)
    local m = f.scaler.mem
    return m[0x138] == 3292 and m[0x13C] == 3292 and m[0x148] == 3292 and m[0x14C] == 3292
        and math.abs(f.scale - 3292 / 2560) < 1e-6 and f.item_size[1] == 2560 and f.item_size[2] == 2560
        and f.scaler.MatchFromWidthToHeight == 0
end
test("WuWa: in full VR a stale viewport cache is synced and the scaler re-lays out the HUD", function()
    local f = fixture({wuwa = true}); stale(f); f:manual()
    assert(synced(f) and table.concat(f.writes, ",") == "match,match" and f.cache_writes == 4, table.concat(f.writes, ","))
    assert(f.state.status:find("HUD viewport resynced 2560x1440 -> 3292x3292", 1, true), f.state.status)
end)
test("WuWa: returning to full VR syncs the cache; stereo screen and mono never touch it", function()
    local f = fixture({wuwa = true}); f:tick(false); f:tick(true)
    f.viewport_size = {X = 2560, Y = 1440}; f:game_viewport(3292, 3292)
    for _ = 1, 60 do f:tick(true) end
    assert(f.cache_writes == 0 and f.scaler.mem[0x138] == 3292, "cache written outside full VR")
    stale(f); f:tick(false); f:tick(false)
    assert(synced(f) and f.cache_writes == 4)
end)
test("WuWa: in dialogue (no view switch) the full VR check resyncs a cache the game left stale", function()
    local f = fixture({wuwa = true}); f.viewport_size = {X = 3292, Y = 3292}; f:game_viewport(3292, 3292)
    for _ = 1, 40 do f:tick() end
    assert(#f.writes == 0 and f.cache_writes == 0 and f.queries == 1)
    stale(f)
    for _ = 1, 9 do f:tick() end
    assert(f.cache_writes == 0, "one mismatch is not enough")
    for _ = 1, 12 do f:tick() end
    assert(synced(f) and f.state.status:find("(full VR check)", 1, true), f.state.status)
    for _ = 1, 40 do f:tick() end
    assert(f.cache_writes == 4)
end)
test("WuWa: a cache the game keeps undoing is retried three times, then waits for a view switch", function()
    local f = fixture({wuwa = true}); f.viewport_size = {X = 3292, Y = 3292}; f:game_viewport(3292, 3292)
    for _ = 1, 400 do
        if f.scaler.mem[0x138] == 3292 then f:game_viewport(2560, 1440) end
        f:tick()
    end
    assert(f.cache_writes == 12 and f.state.status:find("keeps changing", 1, true), f.state.status)
    f:tick(true); f:tick(false); f:tick(false)
    assert(f.cache_writes == 16)
end)
test("WuWa: an unexpected scaler layout is never written; an ignored sync is restored", function()
    for _, case in ipairs({
        {"scaler layout differs", function(f) f.scaler_bytes = 344 end},
        {"scaler layout differs", function(f) f.canvas_offset = 0x128 end},
        {"scaler layout differs", function(f) f.scaler.mem[0x130] = f.world.id end},
        {"scaler class differs", function(f)
            local sub = setmetatable({id = 1003, get_address = function(self) return self.id end}, {__index = f.classes.scaler})
            f.objects[1003], f.scaler.class = sub, sub
            f.scaler.is_a = function(_, klass) return klass == f.classes.scaler end
        end},
        {"viewport cache differs", function(f) f:set_cache(2560, 1440, 3292, 3292) end},
        {"viewport cache differs", function(f) f:set_cache(0, 0) end},
        {"canvas viewport size unavailable", function(f) f.viewport_size = {X = 3292.5, Y = 3292} end},
        {"match value differs", function(f) f.scaler.MatchFromWidthToHeight = 2 end},
        {"scaler schema differs", function(f) f.classes.scaler.functions.SetMatchFromWidthToHeight = nil end},
    }) do
        local f = fixture({wuwa = true}); stale(f); case[2](f)
        local before = {}
        for at, v in pairs(f.scaler.mem) do before[at] = v end
        f:manual()
        assert(f.cache_writes == 0, case[1])
        for at, v in pairs(before) do assert(f.scaler.mem[at] == v, case[1]) end
        assert(not f.state.status:find("resynced", 1, true), case[1] .. " / " .. f.state.status)
        assert(f.state.last_cache == case[1] or (f.state.last_rejections or ""):find(case[1], 1, true),
            case[1] .. " / " .. tostring(f.state.last_cache))
    end
    local f = fixture({wuwa = true}); stale(f); f.ignore_match = true; f:manual()
    assert(f.cache_writes == 8 and f.scaler.mem[0x138] == 2560 and f.scaler.mem[0x14C] == 1440)
    assert(f.state.last_rejections:find("cache: cache sync did not take", 1, true), f.state.last_rejections)
end)
test("WuWa: idle full VR reads memory each second and asks the canvas only when the viewport changes", function()
    local f = fixture({wuwa = true}); f.viewport_size = {X = 3292, Y = 3292}; f:game_viewport(3292, 3292)
    for _ = 1, 60 do f:tick() end
    assert(f.viewport_calls == 1 and f.queries == 1 and #f.writes == 0, "canvas asked " .. f.viewport_calls .. " times")
    f.width = 1800
    for _ = 1, 30 do f:tick() end
    assert(f.viewport_calls == 2 and f.queries == 1 and #f.writes == 0, "viewport change not rechecked once")
end)
test("WuWa: after the game rebuilds its UI the check finds the new scaler within 10 s", function()
    local f = fixture({wuwa = true}); f.viewport_size = {X = 3292, Y = 3292}; f:game_viewport(3292, 3292)
    for _ = 1, 30 do f:tick() end
    assert(f.queries == 1)
    -- Same scaler class and layout at a new address, the old one gone.
    f.objects[f.scaler.id] = nil; f.scaler.id = 5000; f.objects[5000] = f.scaler
    stale(f)
    for _ = 1, 140 do f:tick() end
    assert(synced(f) and f.queries <= 3, "rebuilt UI not found or searched too often: " .. f.queries)
end)
print("HUD refresh tests passed: " .. count)
