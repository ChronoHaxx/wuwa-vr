-- Keyboard camera shortcuts (1.2.7): the plugin counts VR key + C / F / T / G presses in
-- WuWaControls_Keyboard and the camera script acts on each new one, as on L3 / R3 shortcuts.
-- Pure callbacks; no game, device or native calls. Lua 5.4:
-- lua dev/test_keyboard_camera.lua mod/lua/02_WuWaVR_PolarControls.lua
-- Reuses test_close_up_near_clip.lua's fixture (everything before its world builder).
local source=assert(arg[1], 'Pass the PolarControls script path')
local file=assert(io.open('dev/test_close_up_near_clip.lua','r')); local text=file:read('*a'); file:close()
local boundary=assert(text:find('-- Characters around a game camera',1,true))
local env=setmetatable({arg={source}},{__index=_G})
local fixture=assert(load(text:sub(1,boundary-1)..'return fixture','near clip fixture','t',env))()
local passed=0
local function test(name,fn) fn(); passed=passed+1; print('PASS: '..name) end
-- kb1,<generation>,<flags>,<flight keys>,<mouse x>,<mouse y>,<freecam>,<first person>,<fixed>,<animation>
local function keys(f,generation,freecam,first,fixed,animation)
    f.values.WuWaControls_Keyboard=string.format('kb1,%d,0,0,0,0,%d,%d,%d,%d',generation,freecam,first or 0,fixed or 0,animation or 0)
    f:frame()
end
local function mode(f) return tonumber(f.values.WuWaControls_CameraMode) end

test('VR key + C: freecam on, and again back to the camera before it', function()
    local f=fixture(); f.values.WuWaControls_CameraMode='1'
    keys(f,1,0); assert(mode(f)==1,'the first report only syncs')
    keys(f,1,1); assert(mode(f)==2,'freecam on, got '..mode(f))
    keys(f,1,1); assert(mode(f)==2,'the same count does nothing')
    keys(f,1,2); assert(mode(f)==1,'back to the fixed camera, got '..mode(f))
end)
test('VR key + F and + T: first person, then the fixed camera from the game camera', function()
    local f=fixture()
    keys(f,1,0,0,0); keys(f,1,0,1,0); assert(mode(f)==3,'first person')
    keys(f,1,0,2,0); assert(mode(f)==0,'first person off')
    keys(f,1,0,2,1); assert(mode(f)==1,'fixed camera')
    keys(f,1,0,2,2); assert(mode(f)==0,'game camera')
end)
test('VR key + G in first person: full animation, and back to the motion before', function()
    local f=fixture(); f.values.WuWaControls_CameraMode='3'; f.values.WuWaControls_FirstMotion='1'
    keys(f,1,0,0,0,0); keys(f,1,0,0,0,1)
    assert(f.values.WuWaControls_FirstMotion=='3','full animation, got '..tostring(f.values.WuWaControls_FirstMotion))
    keys(f,1,0,0,0,2); assert(f.values.WuWaControls_FirstMotion=='1','previous motion')
end)
test('a new generation (focus came back) resyncs without acting', function()
    local f=fixture()
    keys(f,1,0); keys(f,2,5); assert(mode(f)==0,'nothing fired')
    keys(f,2,6); assert(mode(f)==2,'the next press fires')
end)
test('in a game menu the camera shortcuts do nothing and are not replayed after it', function()
    local f=fixture()
    keys(f,1,0); f.pc.bShowMouseCursor=true
    keys(f,1,1); assert(mode(f)==0,'no freecam in a menu')
    f.pc.bShowMouseCursor=false; keys(f,1,1); assert(mode(f)==0,'not replayed')
end)
test('no keyboard report, or a malformed one, changes nothing', function()
    local f=fixture(); f.values.WuWaControls_Keyboard='kb9,1'
    f:frame(); assert(mode(f)==0)
    f.values.WuWaControls_Keyboard=nil; f:frame(); assert(mode(f)==0)
end)
print('Keyboard camera tests passed: '..passed)
