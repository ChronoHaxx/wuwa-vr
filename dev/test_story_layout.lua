-- "Story scenes in 16:9": the detector also runs for the story layout, with automatic cinema off.
-- lua dev/test_story_layout.lua mod/lua/wuwa_auto_cinema.lua
-- Reuses test_auto_cinema.lua's offline fixture (everything before its first test).
local module_path = arg[1] or "mod/lua/wuwa_auto_cinema.lua"
local file = assert(io.open("dev/test_auto_cinema.lua", "r")); local text = file:read("*a"); file:close()
local boundary = assert(text:find("local passed = 0", 1, true))
local env = setmetatable({arg = {module_path}}, {__index = _G})
local fixture = assert(load(text:sub(1, boundary - 1) .. "return fixture", "auto cinema fixture", "t", env))()

local passed = 0
local function test(name, fn) fn(); passed = passed + 1; print("PASS: " .. name) end
local function story_layout(f) f.values.WuWaControls_StoryLayoutDetect = "true"; f:tick() end

test("both off is inert, as before", function()
    local f = fixture(); f.values.WuWaControls_StoryLayoutDetect = "false"
    for _ = 1, 50 do f:tick() end
    assert(f.queries == 0 and f.lookups == 0 and #f.writes == 0)
end)
test("story layout alone starts the producer and reports dialogue, automatic cinema off", function()
    local f = fixture(); story_layout(f)
    assert(f.values.VR_AutoCinema == "false" and f.module.token == "1" and f:last().known == 3)
    f.story_active = true; f:tick(); assert(f:last().active == 2 and f:last().generation == "1")
end)
test("switching automatic cinema keeps the story producer (native clears, generation unchanged)", function()
    local f = fixture(); story_layout(f); local starts = 0
    for _, w in ipairs(f.writes) do if w[2] == "start" then starts = starts + 1 end end
    f.values.VR_AutoCinema = "true"; f:tick(); f.values.VR_AutoCinema = "false"; f:tick()
    for _, w in ipairs(f.writes) do if w[2] == "start" then starts = starts - 1 end end
    assert(starts == 0 and not f.module.superseded and f.module.token == "1" and f:last().generation == "1")
end)
test("turning story layout off stops the producer when automatic cinema is off", function()
    local f = fixture(); story_layout(f)
    f.values.WuWaControls_StoryLayoutDetect = "false"; f:tick()
    assert(f.module.token == nil and f.writes[#f.writes][2] == "stop:1")
end)
test("a fault stays latched while either user stays on; both off clears it", function()
    local f = fixture(); f.lookup_error = true; story_layout(f)
    local calls = f.lookups; for _ = 1, 30 do f:tick() end
    assert(f.module.faulted and f.lookups == calls)
    f.values.WuWaControls_StoryLayoutDetect = "false"; f:tick(); assert(not f.module.faulted)
    f.lookup_error = false; story_layout(f); assert(f:last().known == 3)
end)
test("an older backend without the key behaves as before", function()
    local f = fixture(); f.values.WuWaControls_StoryLayoutDetect = nil
    for _ = 1, 20 do f:tick() end; assert(#f.writes == 0)
    f:enable(); assert(f:last().known == 3)
end)

-- The Comfort script starts the detector for the story layout too (test 17c started it
-- only for automatic cinema, so "Story scenes in 16:9" never ran). Reuses the
-- auto-cinema test's Comfort fixture.
local comfort_text = text:sub((assert(text:find("local function comfort_fixture()", 1, true))))
comfort_text = comfort_text:sub(1, assert(comfort_text:find('test("', 1, true)) - 1)
local comfort_fixture = assert(load("local module_path = ...\n" .. comfort_text .. "return comfort_fixture",
    "comfort fixture", "t", env))(module_path)
test("the Comfort script starts the detector for Story scenes in 16:9 with automatic cinema off", function()
    -- As UEVR runs the script: its folder is on the search path while it loads.
    local old_path, old_loaded = package.path, package.loaded.wuwa_auto_cinema
    package.loaded.wuwa_auto_cinema = nil
    package.path = (module_path:match("^(.*)[/\\]") or ".") .. "/?.lua;" .. old_path
    local f = comfort_fixture(); f:load()
    package.path = old_path
    f:tick(); assert(#f.writes == 0, "nothing before either setting")
    f.values.WuWaControls_StoryLayoutDetect = "true"; f:tick()
    package.loaded.wuwa_auto_cinema = old_loaded
    assert(#f.errors == 0, f.errors[1])
    assert(f.writes[1] and f.writes[1][1] == "WuWaControls_AutoCinemaProducer", "detector not started")
end)

-- The plot system's own state (lease-v2 only): IsInPerformingPlot(world) on the plot library's default object.
local PLOT = "TypeScriptGeneratedClass /Game/Aki/TypeScript/Game/Module/Sequence/PlotBlueprintFunctionLibrary.PlotBlueprintFunctionLibrary_C"
local function named(s) return {to_string = function() return s end} end
local function prop(name, kind_name, nxt)
    return {get_fname = function() return named(name) end, get_next = function() return nxt end,
        get_class = function() return {get_fname = function() return named(kind_name) end} end}
end
local PICTURE = "Class /Script/LGUI.UITexture"
local function plot_fixture(protocol)
    local f = {objects = {}, lookups = {}, calls = 0, performing = true, samples = {}, clock = 1, generation = 0,
        textures = {}, scans = 0}
    local function object(name, outer)
        local o = {name = name, outer = outer}; f.objects[#f.objects + 1] = o; local id = #f.objects
        function o:get_address() return id end
        function o:get_full_name() return self.name end
        function o:get_outer() return self.outer end
        function o:get_fname() return named(self.name) end
        return o
    end
    f.world = object("World")
    local viewport, engine = object("Viewport"), object("Engine")
    viewport.World, engine.GameViewport = f.world, viewport
    local function described(o, props)
        o.get_class = function() return {find_property = function(_, key)
            return props[key] and prop(key, "ObjectProperty") or nil end} end
    end
    described(engine, {GameViewport = true}); described(viewport, {World = true})
    f.cdo = object("Default__PlotBlueprintFunctionLibrary_C")
    function f.cdo:IsInPerformingPlot(context) f.calls = f.calls + 1; f.context = context; return f.performing end
    f.signature = prop("__WorldContext", "ObjectProperty", prop("ReturnValue", "BoolProperty"))
    f.plot_class = object(PLOT)
    function f.plot_class:find_function(name)
        return name == "IsInPerformingPlot" and {get_child_properties = function() return f.signature end} or nil
    end
    function f.plot_class:get_class_default_object() return f.cdo end
    f.object, f.picture_class = object, object(PICTURE)
    f.values = {VR_AutoCinema = "false", WuWaControls_StoryLayoutDetect = "true",
        WuWaControls_AutoCinemaSample = protocol or "lease-v2", WuWaControls_AutoCinemaStatus = "test", WuWaControls_AutoCinemaProducer = "0"}
    local module = assert(loadfile(module_path))()
    f.module = module.new({api = {
            to_uobject = function(_, id) return f.objects[id] end,
            get_engine = function() return engine end,
            find_uobject = function(_, name)
                f.lookups[#f.lookups + 1] = name
                if name == PLOT then return f.plot_class end
                return name == PICTURE and f.picture_class or nil
            end},
        types = {UObjectHook = {get_objects_by_class = function(c)
            if c ~= f.picture_class then return {} end
            f.scans = f.scans + 1; return f.textures
        end}},
        get = function(key) if key == "WuWaControls_Clock" then return tostring(f.clock) end; return f.values[key] end,
        set = function(key, value)
            if key == "WuWaControls_AutoCinemaProducer" then
                if value == "start" then f.generation = f.generation + 1 end
                f.values[key] = tostring(f.generation)
            else f.samples[#f.samples + 1] = value end
        end, encode = function(v) return v end, log = function() end})
    function f:tick() self.clock = self.clock + .1; self.module:tick() end
    function f:last() return self.samples[#self.samples] end
    return f
end
test("lease-v2: a performing plot is reported (bit 4) with the current world as context", function()
    local f = plot_fixture(); f:tick()
    assert(f:last().known & 4 == 4 and f:last().active & 4 == 4 and f.context == f.world, f:last().detail)
    assert(f:last().detail:find("plot performing", 1, true))
    f.performing = false; f:tick(); assert(f:last().known & 4 == 4 and f:last().active & 4 == 0)
end)
test("lease-v1 backends never look up or call the plot library", function()
    local f = plot_fixture("lease-v1"); for _ = 1, 5 do f:tick() end
    for _, name in ipairs(f.lookups) do assert(name ~= PLOT, "plot lookup on lease-v1") end
    assert(f.calls == 0 and f:last().known & 4 == 0)
end)
test("an unexpected plot query signature is unknown and never called", function()
    local f = plot_fixture()
    f.signature = prop("__WorldContext", "ObjectProperty", prop("Extra", "IntProperty", prop("ReturnValue", "BoolProperty")))
    f:tick(); assert(f.calls == 0 and f:last().known & 4 == 0 and f:last().detail:find("signature", 1, true))
end)

-- Story pictures (lease-v3, automatic cinema only): the plot view's SprPhoto, found again per dialogue.
local WIDGET = {displayName = "StrProperty", AttachParent = "ObjectProperty", Alpha = "FloatProperty",
    texture = "ObjectProperty", bIsUIActive = "BoolProperty"}
local function widget(f, name, parent)
    local o = f.object(name, f.world)
    o.displayName, o.AttachParent, o.Alpha, o.bIsUIActive = name, parent, 0, true
    o.get_class = function() return {find_property = function(_, key) return WIDGET[key] and prop(key, WIDGET[key]) or nil end} end
    return o
end
-- ScreenSpaceUIRoot > ... > UiView_Plot > PnlSafeZone > PnlAdjust > SprPhoto, after `fillers` other widgets.
local function plot_view(f, fillers, view_name)
    for i = 1, fillers or 0 do f.textures[#f.textures + 1] = widget(f, "SprIcon" .. i) end
    local view = widget(f, view_name or "UiView_Plot", widget(f, "LayerItem"))
    local photo = widget(f, "SprPhoto", widget(f, "PnlAdjust", widget(f, "PnlSafeZone", view)))
    photo.texture = f.object("T_CommonDefault_UI")
    f.textures[#f.textures + 1] = photo
    return photo
end
local function show(f, photo) photo.Alpha, photo.texture = 1, f.object("T_M1_8_01_Seq_p1_Female") end
local function hide(f, photo) photo.Alpha, photo.texture = 0, f.object("T_CommonDefault_UI") end
local function picture_fixture()
    local f = plot_fixture("lease-v3"); f.values.VR_AutoCinema = "true"; return f
end
local function bit8(sample) return sample.known & 8 == 8, sample.active & 8 == 8 end

test("lease-v3: a story picture on screen is bit 8; an empty or default picture is clear", function()
    local f = picture_fixture(); local photo = plot_view(f); f:tick()
    local known, active = bit8(f:last()); assert(known and not active, f:last().detail)
    show(f, photo); f:tick(); known, active = bit8(f:last())
    assert(known and active and f:last().detail:find("story picture on screen", 1, true), f:last().detail)
    photo.texture = f.object("T_CommonDefault_UI"); f:tick(); assert(not select(2, bit8(f:last())), "default texture")
    show(f, photo); photo.Alpha = 0; f:tick(); assert(not select(2, bit8(f:last())), "faded out")
    show(f, photo); f:tick(); hide(f, photo); f:tick(); assert(not select(2, bit8(f:last())))
    assert(f.scans == 1, "the found widget is read directly")
end)
test("the search spreads over ticks (at most 150 widgets each) and stops once found", function()
    local f = picture_fixture(); local photo = plot_view(f, 400); show(f, photo)
    f:tick(); assert(not bit8(f:last()) and f:last().detail:find("looking for the story picture", 1, true))
    f:tick(); assert(not bit8(f:last()))
    f:tick(); assert(select(2, bit8(f:last())), f:last().detail)
    for _ = 1, 10 do f:tick() end; assert(f.scans == 1)
end)
test("no plot is a known no-picture, with no search", function()
    local f = picture_fixture(); plot_view(f); f.performing = false
    for _ = 1, 5 do f:tick() end
    local known, active = bit8(f:last()); assert(known and not active and f.scans == 0)
end)
test("lease-v2 backends, or the story layout alone, never look for pictures", function()
    local f = plot_fixture("lease-v2"); f.values.VR_AutoCinema = "true"; show(f, plot_view(f)); f:tick()
    for _, name in ipairs(f.lookups) do assert(name ~= PICTURE, "picture lookup on lease-v2") end
    assert(not bit8(f:last()) and f.scans == 0)
    f = plot_fixture("lease-v3"); show(f, plot_view(f)); f:tick()
    for _, name in ipairs(f.lookups) do assert(name ~= PICTURE, "picture lookup without automatic cinema") end
    assert(not bit8(f:last()) and f.scans == 0)
end)
test("a SprPhoto in another view is ignored; the search retries 2 s later", function()
    local f = picture_fixture(); show(f, plot_view(f, 0, "UiView_Album")); f:tick()
    local known, active = bit8(f:last()); assert(known and not active and f:last().detail:find("no story picture view", 1, true))
    for _ = 1, 15 do f:tick() end; assert(f.scans == 1)
    for _ = 1, 6 do f:tick() end; assert(f.scans == 2)
end)
test("the next dialogue's rebuilt view is found again", function()
    local f = picture_fixture(); local old = plot_view(f); show(f, old); f:tick()
    assert(select(2, bit8(f:last())))
    f.objects[old:get_address()] = nil; f.textures = {}
    local new = plot_view(f); show(f, new); f:tick()
    assert(select(2, bit8(f:last())) and f.scans == 2, f:last().detail)
end)
test("a closed view still in memory loses to the live one; two live views are unknown", function()
    local f = picture_fixture(); local stale = plot_view(f); show(f, stale); stale.bIsUIActive = false
    plot_view(f); f:tick()
    local known, active = bit8(f:last()); assert(known and not active, f:last().detail)
    f = picture_fixture(); show(f, plot_view(f)); plot_view(f); f:tick()
    assert(not bit8(f:last()) and f:last().detail:find("ambiguous story picture", 1, true))
end)
print("Story layout detection tests passed (with plot and pictures): " .. passed)
