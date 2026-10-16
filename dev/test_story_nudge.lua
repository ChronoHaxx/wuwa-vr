-- Story camera nudge (1.2.4): L3 + Y / X (or Page Up / Page Down) moves the eye along the game camera
-- in story shots, L3 + Y + X (Home) resets it, and a cut or the end of the story shots puts it back.
-- Pure callbacks; no game, device or native calls. Lua 5.4:
-- lua dev/test_story_nudge.lua mod/lua/02_WuWaVR_PolarControls.lua
-- Reuses test_close_up_near_clip.lua's fixture (everything before its world builder).
local source=assert(arg[1], 'Pass the PolarControls script path')
local file=assert(io.open('dev/test_close_up_near_clip.lua','r')); local text=file:read('*a'); file:close()
local boundary=assert(text:find('-- Characters around a game camera',1,true))
local env=setmetatable({arg={source}},{__index=_G})
local fixture=assert(load(text:sub(1,boundary-1)..'return fixture','near clip fixture','t',env))()
local L3,X,Y=0x40,0x4000,0x8000
local passed=0
local function test(name,fn) fn(); passed=passed+1; print('PASS: '..name) end
local function story(f)
    f.values.WuWaControls_StoryNudge='true'
    return f
end
-- A neutral sample arms the pad first (as after plugging in); returns what the game received last.
local function hold(f,buttons,frames)
    if not f.armed then f:pad(); f.armed=true end
    local seen
    for _=1,frames do seen=f:pad(buttons); f:frame() end
    return seen
end
local function nudge(f) return tonumber(f.values.WuWaControls_StoryNudgeNow or '') end
-- Where the eye goes for a game camera at the origin looking along +X.
local function eye(f)
    local position,rotation={x=0,y=0,z=0},{x=0,y=0,z=0}
    f.callbacks.on_pre_calculate_stereo_view_offset(nil,nil,nil,position,rotation)
    return position.x
end
local function cut(f,x) f.pcm.GetCameraLocation=function() return {X=x,Y=223,Z=41} end end

test('in a close-up L3 + Y moves nearer, L3 + X back, on top of Close-up match', function()
    local f=story(fixture()); f:frames(3)
    local base=eye(f); assert(base>0,'Close-up match moved the eye')
    local seen=hold(f,L3|Y,20); local near=nudge(f)
    assert(near and near>0,'nearer: '..tostring(f.values.WuWaControls_StoryNudgeNow))
    assert(math.abs(eye(f)-(base+near))<1,'the eye moved by the nudge')
    assert(seen.wButtons&(L3|Y)==0,'L3 + Y never reach the game')
    hold(f,0,2); hold(f,L3|X,60); local back=nudge(f)
    assert(back<0,'back past the game spot: '..back)
    assert(f.logs[#f.logs]:find('%[WuWaStory%] nudge'),'the release is logged')
end)
test('L3 + Y + X goes back to the game spot, once per press', function()
    local f=story(fixture()); f:frames(3)
    hold(f,L3|Y,20); assert(nudge(f)>0)
    hold(f,L3|Y|X,3); assert(nudge(f)==0,'reset')
    hold(f,L3|Y,10); assert(nudge(f)>0,'Y after the reset moves again')
end)
test('a tap is a couple of cm; holding speeds up', function()
    local f=story(fixture()); f:frames(3)
    hold(f,L3|Y,5); local tap=nudge(f); assert(tap>=1 and tap<=3,'tap '..tap)
    hold(f,0,2); hold(f,L3|Y,60); assert(nudge(f)>20,'held for two seconds: '..nudge(f))
end)
test('it never goes through the person in focus', function()
    local f=story(fixture()); f:frames(3)
    local base=eye(f)
    hold(f,L3|Y,400)
    assert(eye(f)<=145-10+0.5,'eye at '..eye(f)..' cm with the focus at 145 cm')
    assert(eye(f)>base)
end)
test('a cut puts it back, unless it is kept for the whole scene', function()
    local f=story(fixture()); f:frames(3)
    -- Released first: the fixture's last sample stays fresh for a quarter second.
    hold(f,L3|Y,20); hold(f,0,2); cut(f,400); f:frames(2); assert(nudge(f)==0,'reset at the cut')
    f.values.WuWaControls_StoryNudgeKeep='1'
    hold(f,L3|Y,20); hold(f,0,2); local kept=nudge(f); cut(f,900); f:frames(2)
    assert(nudge(f)==kept,'kept across the cut')
end)
test('outside story shots: nothing moves, and the nudge ends after half a second', function()
    local f=story(fixture()); f.values.WuWaControls_StoryNudgeKeep='1'; f:frames(3)
    hold(f,L3|Y,20); assert(nudge(f)>0)
    f.pcm.fov=75; f:frames(40); assert(nudge(f)==nil,'no nudge shown in a wide shot')
    assert(eye(f)==0,'the wide shot is the game camera')
    hold(f,L3|Y,20); f.pcm.fov=37.5; f:frames(2); assert(nudge(f)==0,'input in the wide shot did nothing')
end)
test('the plot flag makes a wide story shot count', function()
    local f=story(fixture()); f.values.WuWaControls_StoryScene='true'; f.pcm.fov=75; f:frames(3)
    hold(f,L3|Y,20); assert(nudge(f)>0,'nudged in a wide story shot')
    assert(eye(f)>0,'applied without Close-up match')
end)
test('keyboard: Page Up nearer, Page Down back, Home reset', function()
    local f=story(fixture()); f:frames(3)
    f.values.WuWaControls_StoryNudgeKeys='1'; f:frames(20); assert(nudge(f)>0,'Page Up')
    f.values.WuWaControls_StoryNudgeKeys='2'; f:frames(2); assert(nudge(f)==0,'Home')
    f.values.WuWaControls_StoryNudgeKeys='-1'; f:frames(20); assert(nudge(f)<0,'Page Down')
end)
test('off, the stereo screen and other camera modes leave the camera alone', function()
    local f=story(fixture()); f:frames(3); local base=eye(f)
    f.values.WuWaControls_StoryNudge='false'; hold(f,L3|Y,20); assert(nudge(f)==nil and eye(f)==base,'off')
    f.values.WuWaControls_StoryNudge='true'; hold(f,L3|Y,20); assert(nudge(f)>0)
    f.values.VR_2DScreenMode='true'; f:frames(2); assert(nudge(f)==nil,'stereo screen')
    f.values.VR_2DScreenMode='false'; f:frames(2); assert(nudge(f)==0,'back in full VR at the game spot')
end)
test('with the game cursor shown, L3 + Y still nudge and never reach the game', function()
    local f=story(fixture()); f:frames(3); f.pc.bShowMouseCursor=true
    local seen=hold(f,L3|Y,20)
    assert(nudge(f)>0,'nudged with the cursor shown')
    assert(seen.wButtons&Y==0,'Y reached the game')
end)
print('Story nudge tests passed: '..passed)
