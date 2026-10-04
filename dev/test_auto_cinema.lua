-- Offline reflection/lease-producer tests. No game, device or profile access.
local module_path = arg[1] or "mod/lua/wuwa_auto_cinema.lua"
local M = assert(loadfile(module_path))()
local MOVIE = "MediaPlayer /Game/Aki/UI/UIResources/UiPlot/VideoPlayer/CommonVideoPlayer.CommonVideoPlayer"
local STORY = "Class /Script/LGUI.UIExtendToggleSpriteTransition"
local function named(s) return {to_string = function() return s end} end
local function field(name, kind)
    return {get_fname = function() return named(name) end,
        get_class = function() return {get_fname = function() return named(kind) end} end}
end
local function fixture()
    local f = {objects = {}, queries = 0, lookups = 0, clock = 1, generation = 0, writes = {}, samples = {}, logs = {},
        playing = false, paused = false, preparing = false, story_active = false}
    local function object(class, name, outer)
        local id = #f.objects + 1
        local o = {class = class, outer = outer}
        function o:get_address() return id end
        function o:get_class() return self.class end
        function o:get_full_name() return name end
        function o:get_fname() return named(name) end
        function o:get_outer() return self.outer end
        f.objects[id] = o
        return o
    end
    local function class(name)
        local c = object(nil, name)
        c.properties, c.functions = {}, {}
        function c:find_property(key) return self.properties[key] end
        function c:find_function(key) return self.functions[key] end
        return c
    end
    local function getter(c, name, callback)
        local p = field("ReturnValue", "BoolProperty")
        function p:get_next() return nil end
        function p:is_param() return true end
        function p:is_return_param() return true end
        function p:is_out_param() return true end
        function p:is_reference_param() return false end
        c.functions[name] = setmetatable({get_child_properties = function() return p end,
            find_property = function(_, key) return key == "ReturnValue" and p or nil end},
            {__call = function(_, o) f.getter_calls = (f.getter_calls or 0) + 1; return callback(o) end})
    end
    local engine_class, viewport_class = class("Engine"), class("Viewport")
    f.movie_class, f.story_class = class("Class /Script/MediaAssets.MediaPlayer"), class(STORY)
    f.world = object(nil, "Current world")
    f.engine, f.viewport = object(engine_class, "Engine"), object(viewport_class, "Viewport")
    f.engine.GameViewport, f.viewport.World = f.viewport, f.world
    engine_class.properties.GameViewport = field("GameViewport", "ObjectProperty")
    viewport_class.properties.World = field("World", "ObjectProperty")
    f.movie, f.story = object(f.movie_class, MOVIE), object(f.story_class, "Story toggle", f.world)
    getter(f.movie_class, "IsPlaying", function() return f.playing end)
    getter(f.movie_class, "IsPaused", function() return f.paused end)
    getter(f.movie_class, "IsPreparing", function() return f.preparing end)
    getter(f.story_class, "GetIsActiveAndEnable", function() return f.story_active end)
    f.story_class.properties.TransitionState = field("TransitionState", "StructProperty")
    local transition, hover = class("Transition"), class("Hover")
    transition.properties.CheckedHoverState = field("CheckedHoverState", "StructProperty")
    hover.properties.Sprite = field("Sprite", "ObjectProperty")
    f.sprite = object(nil, "SP_PlotSkipBgIcon1")
    f.story.TransitionState = {get_struct = function() return transition end,
        CheckedHoverState = {get_struct = function() return hover end, Sprite = f.sprite}}
    f.movie_list, f.story_list = {f.movie}, {f.story}
    f.values = {VR_AutoCinema = "false", WuWaControls_AutoCinemaSample = "lease-v1",
        WuWaControls_AutoCinemaStatus = "native lease test", WuWaControls_AutoCinemaProducer = "0"}
    local env = {api = {
        to_uobject = function(_, id) return f.objects[id] end,
        get_engine = function() return f.engine end,
        find_uobject = function(_, name)
            f.lookups = f.lookups + 1
            assert(name:sub(1, 6) == "Class ", "Asset/global object lookup was attempted")
            if f.lookup_error then error("lookup failed") end
            if f.missing_classes then return nil end
            return name == STORY and f.story_class or f.movie_class
        end}, types = {UObjectHook = {get_objects_by_class = function(c, defaults)
            f.queries = f.queries + 1; assert(defaults == false)
            if f.slow then f.clock = f.clock + .06 end
            return c == f.movie_class and f.movie_list or f.story_list
        end}},
        get = function(key)
            if key == "WuWaControls_Clock" then return tostring(f.clock) end
            return f.values[key]
        end,
        set = function(key, value)
            assert(key == "WuWaControls_AutoCinemaProducer" or key == "WuWaControls_AutoCinemaSample", "Preference write")
            f.writes[#f.writes + 1] = {key, value}
            if key == "WuWaControls_AutoCinemaProducer" then
                if value == "start" then f.generation = f.generation + 1
                elseif value == "stop:" .. f.generation then f.generation = f.generation + 1 end
                f.values[key] = tostring(f.generation)
            else
                assert(type(value.generation) == "string" and value.version == 1)
                f.samples[#f.samples + 1] = value
            end
        end, encode = function(sample) return sample end,
        log = function(text) f.logs[#f.logs + 1] = text end}
    f.module = M.new(env)
    function f:tick(dt) self.clock = self.clock + (dt or .1); self.module:tick() end
    function f:enable() self.values.VR_AutoCinema = "true"; self:tick() end
    function f:last() return self.samples[#self.samples] end
    return f
end
local passed = 0
local function test(name, fn) fn(); passed = passed + 1; print("PASS: " .. name) end
test("OFF is inert: no reflection, class search, bridge writes or logs", function()
    local f = fixture(); for _ = 1, 100 do f:tick() end
    assert(f.queries == 0 and f.lookups == 0 and #f.writes == 0 and #f.logs == 0)
end)
test("exact movie and active current-world story signals stay independent", function()
    local f = fixture(); f:enable(); assert(f:last().known == 3 and f:last().active == 0)
    f.playing = true; f:tick(); assert(f:last().active == 1)
    f.story_active = true; f:tick(); assert(f:last().active == 3)
    f.playing = false; f:tick(); assert(f:last().active == 2)
    f.paused = true; f:tick(); assert(f:last().hold == 1 and f:last().active == 2)
end)
test("unrelated media, inactive/stale-world/similarly named story widgets cannot trigger", function()
    local f = fixture(); f.playing, f.story_active = true, true
    f.movie.get_full_name = function() return MOVIE .. "_Other" end
    f.sprite.get_fname = function() return named("SP_PlotSkipBgIcon11") end
    f:enable(); assert(f:last().active == 0)
    f.sprite.get_fname = function() return named("SP_PlotSkipBgIcon1") end
    f.story.outer = nil; f:tick(); assert(f:last().active == 0)
end)
test("unknown nested schema or callable signature never invokes an unverified getter", function()
    local f = fixture(); f.story_class.properties.TransitionState = field("TransitionState", "IntProperty")
    f.movie_class.functions.IsPlaying.find_property = function() return nil end
    f:enable(); assert(f:last().known == 0 and f:last().active == 0 and (f.getter_calls or 0) == 0)
    assert(f:last().detail:find("unavailable"))
end)
test("missing/ambiguous asset and oversized indexed sets fail closed without recurring global lookup", function()
    local f = fixture(); f.movie_list = {}; f:enable()
    for _ = 1, 100 do f:tick() end
    assert(f.lookups == 2 and f:last().known == 2 and #f.logs < 8)
    f.movie_list = {f.movie, f.movie}; f:tick(); assert(f:last().active == 0 and f:last().detail:find("ambiguous"))
    f.story_list = {}; for i = 1, 65 do f.story_list[i] = f.story end
    f:tick(); assert(f:last().known == 0)
end)
test("class lookup failure stops until explicit disable/re-enable", function()
    local f = fixture(); f.lookup_error = true; f:enable()
    local calls = f.lookups; for _ = 1, 50 do f:tick() end
    assert(f.lookups == calls and f.module.faulted)
    f.values.VR_AutoCinema = "false"; f:tick(); f.lookup_error = false; f:enable()
    assert(f:last().known == 3)
end)
test("missing lazy classes receive only three spaced attempts, never a 10Hz global scan", function()
    local f = fixture(); f.missing_classes = true; f:enable()
    for _ = 1, 49 do f:tick() end; assert(f.lookups == 2)
    for _ = 1, 220 do f:tick() end; assert(f.lookups == 6 and f.queries == 0)
end)
test("100ms cadence, over-budget suspension and producer reset are bounded", function()
    local f = fixture(); f:enable(); local queries = f.queries
    for _ = 1, 9 do f:tick(.001) end; assert(f.queries == queries)
    f.slow = true; f:tick(.1); assert(f.module.faulted and f:last().world == "0")
    queries = f.queries; for _ = 1, 50 do f:tick() end; assert(f.queries == queries)
    assert(f.module.token == nil)
end)
test("obsolete script cannot steal producer ownership or cancel the replacement", function()
    local f = fixture(); f:enable(); local before = #f.writes
    f.generation = f.generation + 1; f.values.WuWaControls_AutoCinemaProducer = tostring(f.generation)
    f:tick(); f:tick(); assert(f.module.superseded and #f.writes == before)
    f.module:reset(); assert(#f.writes == before)
end)
test("lost world releases the lease; a missing stop method is unknown rather than false", function()
    local f = fixture(); f:enable(); f.movie_class.functions.IsPreparing = nil
    f:tick(); assert(f:last().known == 2)
    f.viewport.World = nil; f:tick(); assert(f:last().world == "0" and f:last().known == 0)
end)
test("real Polar gestures request effective native toggles instead of overwriting saved preferences", function()
    -- Reuse the established callback fixture, before its tests execute. This
    -- exercises the production Polar script; preference semantics live in the
    -- production pure C++ lease/resolve helper tested by test_auto_cinema.cpp.
    local file = assert(io.open("dev/test_mono_theatre.lua", "r")); local text = file:read("*a"); file:close()
    local boundary = assert(text:find("local function consumed", 1, true))
    local env = setmetatable({arg = {"mod/lua/02_WuWaVR_PolarControls.lua"}}, {__index = _G})
    local make = assert(load(text:sub(1, boundary - 1) .. "return fixture", "shared mono fixture", "t", env))()
    local mono = make(true); mono.values.WuWaControls_ToggleMonoTheatre = "effective-v1"
    mono:arm(); mono:sample(128, 255, 255)
    assert(mono.values.WuWaControls_ToggleMonoTheatre == "toggle" and not mono.writes.VR_MonoTheatreMode)
    assert(mono.values.VR_2DScreenMode == "true")
    local screen = make(false); screen.values.WuWaControls_ToggleStereoScreen = "effective-v1"
    screen:arm(); screen:sample(64, 255, 255)
    for _ = 1, 55 do screen:sample(64, 255, 255) end
    assert(screen.values.WuWaControls_ToggleStereoScreen == "toggle" and not screen.writes.VR_2DScreenMode)
    assert(not screen.writes.VR_MonoTheatreMode)
end)
local function comfort_fixture()
    local f = {callbacks = {}, values = {VR_AutoCinema = "false", VR_2DScreenMode = "false", VR_MonoTheatreMode = "false",
        WuWaControls_Clock = "1", WuWaControls_AutoCinemaProducer = "0", WuWaControls_AutoCinemaSample = "lease-v1",
        WuWaControls_AutoCinemaStatus = "mock lease"}, errors = {}, writes = {}, queries = 0}
    local env = setmetatable({io = {open = function() return nil end},
        json = {dump_string = function(value) return value end},
        uevr = {api = {get_engine = function() f.queries = f.queries + 1 end,
            find_uobject = function() f.queries = f.queries + 1 end}, types = {},
            params = {vr = {get_mod_value = function(_, key) return f.values[key] end,
                set_mod_value = function(key, value)
                    assert(key == "WuWaControls_AutoCinemaProducer" or key == "WuWaControls_AutoCinemaSample", "Unexpected native write")
                    f.writes[#f.writes + 1] = {key, value}
                    if key == "WuWaControls_AutoCinemaProducer" and value == "start" then
                        f.values[key] = tostring(tonumber(f.values[key]) + 1)
                    end
                end}, functions = {log_info = function() end,
                    log_error = function(value) f.errors[#f.errors + 1] = value end}},
            sdk = {callbacks = setmetatable({}, {__index = function(_, key)
                return function(fn) f.callbacks[key] = fn end
            end})}}}, {__index = _G})
    function f:load()
        assert(loadfile(module_path:gsub("wuwa_auto_cinema.lua$", "01_WuWaVR_Comfort.lua"), "t", env))()
    end
    function f:tick() self.callbacks.on_pre_engine_tick(nil, .1) end
    return f
end
test("UEVR temporary script search path: callback activation survives path restoration", function()
    local old_path = package.path
    local old_cinema, old_hud = package.loaded.wuwa_auto_cinema, package.loaded.wuwa_hud_refresh
    local old_preload = package.preload.wuwa_auto_cinema
    package.loaded.wuwa_auto_cinema, package.loaded.wuwa_hud_refresh, package.preload.wuwa_auto_cinema = nil, nil, nil
    package.path = "missing-runtime-root/?.lua"
    -- UEVR standalone autorun discards the chunk result and does not seed require.
    assert(loadfile(module_path))(); assert(package.loaded.wuwa_auto_cinema == nil)
    -- ScriptState::run_script appends siblings only for top-level execution.
    local pristine = package.path
    package.path = pristine .. ";" .. (module_path:match("^(.*)[/\\]") or ".") .. "/?.lua"
    local f = comfort_fixture(); f:load()
    package.path = pristine
    f:tick(); assert(#f.errors == 0 and f.queries == 0 and #f.writes == 0, "OFF was not inert")
    f.values.VR_AutoCinema = "true"; f:tick()
    assert(#f.errors == 0 and f.queries > 0 and #f.writes == 2, "Deferred detector failed after path restoration")
    assert(f.writes[1][1] == "WuWaControls_AutoCinemaProducer" and f.writes[2][1] == "WuWaControls_AutoCinemaSample")
    package.path = old_path
    package.loaded.wuwa_auto_cinema, package.loaded.wuwa_hud_refresh, package.preload.wuwa_auto_cinema = old_cinema, old_hud, old_preload
end)
test("optional require/factory failures retain their actual cause and log once only when enabled", function()
    local old_cinema, old_hud, old_preload = package.loaded.wuwa_auto_cinema, package.loaded.wuwa_hud_refresh, package.preload.wuwa_auto_cinema
    package.loaded.wuwa_hud_refresh = {new = function() return {status = "Ready", request = function() end,
        tick = function() end, reset = function() end} end}
    for _, phase in ipairs({"require", "factory"}) do
        local marker = "cinema fixture " .. phase .. " exact failure"
        package.loaded.wuwa_auto_cinema = nil
        package.preload.wuwa_auto_cinema = function()
            if phase == "require" then error(marker) end
            return {new = function() error(marker) end}
        end
        local f = comfort_fixture(); f:load(); f:tick()
        assert(#f.errors == 0 and #f.writes == 0 and f.queries == 0)
        f.values.VR_AutoCinema = "true"
        for _ = 1, 12 do f:tick() end
        assert(#f.errors == 1 and f.errors[1]:find(marker, 1, true), "Original module error was lost or repeated")
        assert(#f.writes == 0 and f.queries == 0)
    end
    package.loaded.wuwa_auto_cinema, package.loaded.wuwa_hud_refresh, package.preload.wuwa_auto_cinema = old_cinema, old_hud, old_preload
end)
print("PASS: " .. passed .. " auto-cinema detector/integration groups; no native/game writes")
