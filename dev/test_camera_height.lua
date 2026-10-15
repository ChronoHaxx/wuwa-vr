-- L3 + Y / X camera height in first person and the fixed camera, and L3 + Y + X back to the default.
-- Pure callbacks; no game, device or native calls. Lua 5.4:
-- lua dev/test_camera_height.lua mod/lua/02_WuWaVR_PolarControls.lua
-- Reuses test_close_up_near_clip.lua's fixture (everything before its world builder).
local source=assert(arg[1], 'Pass the PolarControls script path')
local file=assert(io.open('dev/test_close_up_near_clip.lua','r')); local text=file:read('*a'); file:close()
local boundary=assert(text:find('-- Characters around a game camera',1,true))
local env=setmetatable({arg={source}},{__index=_G})
local fixture=assert(load(text:sub(1,boundary-1)..'return fixture','near clip fixture','t',env))()
local L3,X,Y=0x40,0x4000,0x8000
local passed=0
local function test(name,fn) fn(); passed=passed+1; print('PASS: '..name) end
-- A neutral sample arms the pad first (as after plugging in), as in the near clip tests.
local function hold(f,buttons,frames)
    if not f.armed then f:pad(); f.armed=true end
    for _=1,frames do f:pad(buttons); f:frame() end
end
local function height(f,key) return tonumber(f.values[key] or 'nan') end

test('fixed camera: L3 + Y raises, L3 + X lowers, L3 + Y + X puts 30 back', function()
    local f=fixture(); f.values.WuWaControls_CameraMode='1'; f.values.WuWaControls_FixedHeight='30'
    hold(f,L3|Y,20); local up=height(f,'WuWaControls_FixedHeight'); assert(up>40,'Y raised to '..up)
    hold(f,L3|X,5); assert(height(f,'WuWaControls_FixedHeight')<up,'X lowered')
    hold(f,L3|Y|X,3); assert(height(f,'WuWaControls_FixedHeight')==30,'reset to '..height(f,'WuWaControls_FixedHeight'))
end)
test('first person: the same buttons move and reset its own height (default 0)', function()
    local f=fixture(); f.values.WuWaControls_CameraMode='3'; f.values.WuWaControls_FirstUp='0'
    f.values.WuWaControls_FixedHeight='120'
    hold(f,L3|Y,20); assert(height(f,'WuWaControls_FirstUp')>0,'Y raised')
    hold(f,L3|Y|X,2); assert(height(f,'WuWaControls_FirstUp')==0,'reset')
    assert(height(f,'WuWaControls_FixedHeight')==120,'the fixed camera height is left alone')
end)
test('the reset happens once per press: Y still held afterwards moves again', function()
    local f=fixture(); f.values.WuWaControls_CameraMode='1'; f.values.WuWaControls_FixedHeight='200'
    hold(f,L3|Y|X,10); assert(height(f,'WuWaControls_FixedHeight')==30)
    hold(f,L3|Y,10); assert(height(f,'WuWaControls_FixedHeight')>30,'Y after the reset')
end)
test('the game camera and Y + X without L3 change nothing', function()
    local f=fixture(); f.values.WuWaControls_CameraMode='0'; f.values.WuWaControls_FixedHeight='200'; f.values.WuWaControls_FirstUp='50'
    hold(f,L3|Y|X,5); hold(f,L3|Y,5)
    assert(height(f,'WuWaControls_FixedHeight')==200 and height(f,'WuWaControls_FirstUp')==50)
    f.values.WuWaControls_CameraMode='1'; hold(f,Y|X,5)
    assert(height(f,'WuWaControls_FixedHeight')==200,'no L3, no reset')
end)
print('Camera height tests passed: '..passed)
