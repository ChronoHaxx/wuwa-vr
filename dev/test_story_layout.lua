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
print("Story layout detection tests passed: " .. passed)
