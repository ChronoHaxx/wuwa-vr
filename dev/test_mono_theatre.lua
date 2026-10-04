-- Offline Lua 5.4 callbacks; no game, native device, profile or runtime access.
-- lua dev/test_mono_theatre.lua mod/lua/02_WuWaVR_PolarControls.lua
local source=assert(arg[1], 'Pass the PolarControls script path')
local L3,R3,A,Y=64,128,4096,32768
local MONO,SCREEN='VR_MonoTheatreMode','VR_2DScreenMode'
local passed,failures,fixtures=0,{},{}
local function fixture(screen)
    local f={callbacks={},writes={},errors={},clock=1,ui=false,
        pc={bShowMouseCursor=false,get_class=function() return {find_function=function() end} end}}
    f.values={WuWaControls_Enabled='true',WuWaControls_Focused='true',WuWaControls_MouseAssist='true',
        WuWaControls_CameraMode='0',WuWaControls_NativeMenu='false',WuWaControls_AdjustMode='false',
        VR_MonoTheatreMode='false',VR_2DScreenMode=tostring(screen==true),VR_WorldScale='1.75',
        WindowMode_Enabled='true',WuWaDiorama_Enabled='true',VR_EnableGUI='true'}
    local env=setmetatable({uevr={api={get_player_controller=function()
        assert(not f.in_xinput,'XInput accessed a UObject'); return f.pc
    end,get_local_pawn=function() end},params={vr={
        get_mod_value=function(_,key) return f.values[key] or 'false' end,
        set_mod_value=function(key,value) f.values[key]=value; f.writes[key]=(f.writes[key] or 0)+1 end},
        functions={is_drawing_ui=function() return f.ui end,dispatch_custom_event=function() end,
            log_warn=function(e) f.errors[#f.errors+1]=e end,log_error=function(e) f.errors[#f.errors+1]=e end}},
        sdk={callbacks=setmetatable({},{__index=function(_,key) return function(fn) f.callbacks[key]=fn end end})}},
        json={dump_string=function() return '{}' end}},{__index=_G})
    assert(loadfile(source,'t',env))()
    function f:advance(dt) self.clock=self.clock+(dt or .016); self.values.WuWaControls_Clock=tostring(self.clock) end
    function f:tick(dt)
        if dt then self:advance(dt) end
        self.callbacks.on_pre_engine_tick(nil,.016); assert(#self.errors==0,self.errors[1])
    end
    function f:input(buttons,lt,rt,index,result)
        local state={Gamepad={wButtons=buttons or 0,bLeftTrigger=lt or 0,bRightTrigger=rt or 0,
            sThumbLX=0,sThumbLY=0,sThumbRX=0,sThumbRY=0}}
        self.in_xinput=true
        local ok,e=pcall(self.callbacks.on_xinput_get_state,nil,index or 0,state,result or 0)
        self.in_xinput=false; assert(ok,e); return state.Gamepad
    end
    function f:sample(buttons,lt,rt,index)
        self:advance(); local p=self:input(buttons,lt,rt,index); self:tick(); return p
    end
    function f:arm(index) self:sample(0,0,0,index); return self:sample(0,255,255,index) end
    function f:count(n) assert((self.writes[MONO] or 0)==n,'Mono toggles '..(self.writes[MONO] or 0)..', expected '..n) end
    fixtures[#fixtures+1]=f; return f
end
local function consumed(p)
    assert(p.wButtons&R3==0 and p.bLeftTrigger==0 and p.bRightTrigger==0,'Mono chord leaked')
end
local function test(name,fn)
    local ok,e=pcall(fn)
    if ok then passed=passed+1; print('PASS: '..name)
    else failures[#failures+1]=name..': '..tostring(e); print('FAIL: '..failures[#failures]) end
end

test('triggers first then R3 click toggles once, preserving original stereo-screen choice',function()
    for _,screen in ipairs({false,true}) do
        local f=fixture(screen); local p=f:arm()
        assert(p.bLeftTrigger==255 and p.bRightTrigger==255,'Pre-click game input changed')
        consumed(f:sample(R3,255,255)); f:count(1); assert(f.values[MONO]=='true')
        for _=1,20 do consumed(f:sample(R3,255,255)) end
        f:count(1); consumed(f:sample(0,255,255)); consumed(f:sample(R3,255,255)); f:count(1)
        consumed(f:sample(R3,0,0)); consumed(f:sample(R3,255,255)); f:count(1)
        f:arm(); consumed(f:sample(R3,255,255)); f:count(2); assert(f.values[MONO]=='false')
        assert(f.values[SCREEN]==tostring(screen) and not f.writes[SCREEN],'Stereo-screen preference overwritten')
    end
end)
test('simultaneous and R3-first input cannot fire; full release is required',function()
    for _,first in ipairs({'simultaneous','r3','partial','initial'}) do
        local f=fixture()
        if first~='initial' then f:sample() end
        if first=='r3' then f:sample(R3)
        elseif first=='partial' then f:sample(0,179,255) end
        f:sample(R3,255,255); f:count(0)
        local released=f:sample(0,255,255); local pressed=f:sample(R3,255,255)
        -- Until the first neutral sample this script owns no input at all.
        if first~='initial' then consumed(released); consumed(pressed) end
        f:count(0)
        f:arm(); consumed(f:sample(R3,255,255)); f:count(1)
    end
end)
test('fresh arming requires both full triggers and rejects stale or reversed clocks',function()
    for _,dt in ipairs({.249,.251,-.01}) do
        local f=fixture(); f:arm(); f:advance(dt)
        consumed(f:input(R3,255,255)); f:tick(); f:count(dt>=0 and dt<=.25 and 1 or 0)
    end
    for _,drop in ipairs({0,29,30,179}) do
        local f=fixture(); f:arm(); f:sample(0,drop,255)
        consumed(f:sample(R3,255,255)); f:count(0)
    end
end)
test('quick press/release before engine tick is a click, not a cancelled hold',function()
    local f=fixture(); f:arm(); consumed(f:input(R3,255,255)); consumed(f:input())
    f:count(0); f:tick(); f:count(1)
end)
test('queued click has a bounded lifetime and clock reversal fails closed',function()
    for _,age in ipairs({.249,.251,-.01}) do
        local f=fixture(); f:arm(); consumed(f:input(R3,255,255)); f:tick(age)
        local n=age>=0 and age<=.25 and 1 or 0
        f:count(n); for _=1,20 do consumed(f:sample(R3,255,255)) end; f:count(n)
    end
end)
test('extra buttons and L3+R3 cancel queued mono without hijacking native menu',function()
    for _,extra in ipairs({A,Y,L3}) do
        local f=fixture(); f:arm(); consumed(f:input(R3,255,255))
        local p=f:input(R3+extra,255,255)
        if extra==L3 then assert(p.wButtons==L3+R3,'Native menu chord changed')
        else consumed(p); assert(p.wButtons&extra~=0,'Unrelated button swallowed') end
        f:tick(); f:count(0); consumed(f:sample(R3,255,255)); f:count(0)
        f:arm(); consumed(f:sample(R3,255,255)); f:count(1)
    end
end)
test('neither portal nor diorama nor stereo-screen gesture rolls into mono',function()
    for _,triggers in ipairs({{255,0},{0,255},{255,255}}) do
        local f=fixture(); f:sample(); f:sample(L3,triggers[1],triggers[2])
        f:sample(0,255,255); f:sample(R3,255,255); f:count(0)
        f:arm(); consumed(f:sample(R3,255,255)); f:count(1)
    end
end)
test('mono requires neutral release before another view shortcut can take ownership',function()
    local f=fixture(); f:arm(); consumed(f:sample(R3,255,255)); f:count(1)
    f:sample(A,0,0); f:sample(L3,255,0)
    assert(not f.writes.WindowMode_Enabled,'Held non-neutral input rolled mono into portal')
    f:sample(); f:sample(L3,255,0)
    assert(f.writes.WindowMode_Enabled==1,'Fresh portal gesture stopped working')
end)
test('mono remains available in dialogue and game-menu cursor transitions',function()
    for _,signal in ipairs({'native','cursor'}) do
        local f=fixture(); f:arm()
        if signal=='native' then f.values.WuWaControls_NativeMenu='true' else f.pc.bShowMouseCursor=true end
        f:tick(); consumed(f:sample(R3,255,255)); f:count(1)
        f:arm(); consumed(f:sample(R3,255,255)); f:count(2)
    end
end)
test('focus, UEVR, adjustment, disabled controls and passthrough discard armed/queued input',function()
    for _,reason in ipairs({'focus','ui','adjust','disabled','passthrough'}) do
        for _,queued in ipairs({false,true}) do
            local f=fixture(); f:arm(); if queued then consumed(f:input(R3,255,255)) end
            local function block(on)
                if reason=='focus' then f.values.WuWaControls_Focused=tostring(not on)
                elseif reason=='ui' then f.ui=on
                elseif reason=='adjust' then f.values.WuWaControls_AdjustMode=tostring(on)
                elseif reason=='disabled' then f.values.WuWaControls_Enabled=tostring(not on)
                else f.values.VR_WuWaGamepadPassthrough=tostring(on) end
                f:tick()
            end
            block(true); block(false); f:sample(R3,255,255); f:count(0)
            f:arm(); consumed(f:sample(R3,255,255)); f:count(1)
        end
    end
end)
test('passthrough does not consume or toggle any chord component',function()
    local f=fixture(); f:arm(); f.values.VR_WuWaGamepadPassthrough='true'
    local p=f:sample(R3,255,255); f:count(0)
    assert(p.wButtons==R3 and p.bLeftTrigger==255 and p.bRightTrigger==255)
end)
test('disconnect, controller handover and reset cancel pending clicks',function()
    for _,reason in ipairs({'disconnect','handover','reset','unrelated'}) do
        local f=fixture(); f:sample(0,0,0,1); f:arm(); consumed(f:input(R3,255,255))
        if reason=='disconnect' then f:input(0,0,0,0,1167)
        elseif reason=='handover' then f:advance(1.01); f:input(A,0,0,1)
        elseif reason=='reset' then f.callbacks.on_script_reset()
        else f:input(0,0,0,1,1167) end
        f:tick(); f:count(reason=='unrelated' and 1 or 0)
    end
end)
test('mono clicks do not seed the double-R3 free-camera action',function()
    local f=fixture(); f:arm(); consumed(f:sample(R3,255,255)); consumed(f:sample())
    f:sample(R3); f:sample(); assert(not f.writes.WuWaControls_CameraMode,'Mono click seeded freecam tap')
    f:sample(R3); f:sample(); assert(f.values.WuWaControls_CameraMode=='2','Ordinary double-R3 stopped working')
end)
test('leaving mono honours a stereo-screen preference changed while mono was active',function()
    local f=fixture(); f:arm(); consumed(f:sample(R3,255,255)); f.values[SCREEN]='true'
    f:arm(); consumed(f:sample(R3,255,255)); f:count(2)
    assert(f.values[SCREEN]=='true' and not f.writes[SCREEN],'External stereo preference reverted')
end)

for _,f in ipairs(fixtures) do
    for _,key in ipairs({'VR_WorldScale','VR_EnableGUI'}) do
        assert(not f.writes[key],'Mono workflow changed '..key)
    end
    assert(#f.errors==0,f.errors[1])
end
assert(#failures==0,table.concat(failures,'\n'))
print(string.format('PASS: %d mono-theatre groups, %d isolated fixtures; no game/device/native calls',passed,#fixtures))
