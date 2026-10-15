-- Offline Lua 5.4 callbacks; no game, native device, profile or runtime access.
-- lua dev/test_view_chord.lua mod/lua/02_WuWaVR_PolarControls.lua
-- R3 view chord (test 17k): hold both triggers; each R3 click toggles full VR /
-- stereo screen, and the triggers can stay held for the next click. Holding R3
-- 0.8 s toggles mono theatre, back to the view it was entered from. A click
-- fires when R3 is released with both triggers still held. The fixture is the
-- one in dev/test_mono_theatre.lua (test 3i), copied so this file stands alone.
local source=assert(arg[1], 'Pass the PolarControls script path')
local L3,R3,A,Y=64,128,4096,32768
local MONO,SCREEN='VR_MonoTheatreMode','VR_2DScreenMode'
local TOGGLE_MONO,TOGGLE_SCREEN='WuWaControls_ToggleMonoTheatre','WuWaControls_ToggleStereoScreen'
local EFFECTIVE_MONO,EFFECTIVE_SCREEN='WuWaControls_EffectiveMonoTheatre','WuWaControls_EffectiveScreen'
local VIEW_KEYS={[MONO]=true,[SCREEN]=true,[TOGGLE_MONO]=true,[TOGGLE_SCREEN]=true}
local passed,failures,fixtures=0,{},{}
local function consumed(p)
    assert(p.wButtons&R3==0 and p.bLeftTrigger==0 and p.bRightTrigger==0,'View chord leaked')
end
-- Native view model (WuWaAutoCinema.hpp resolve/manual_screen): an automatic
-- lease shows its own view, otherwise the effective screen is screen or mono.
-- A v2 backend reports "effective-v2" for both toggle keys, applies on/off/
-- toggle to the live options and ends any lease; screen off also leaves mono.
-- Older backends lack the keys, so the script writes the options itself.
local function fixture(view,legacy)
    local f={callbacks={},writes={},errors={},logs={},requests={},applied=0,clock=1,ui=false,legacy=legacy,
        pc={bShowMouseCursor=false,get_class=function() return {find_function=function() end} end}}
    f.values={WuWaControls_Enabled='true',WuWaControls_Focused='true',WuWaControls_MouseAssist='true',
        WuWaControls_CameraMode='0',WuWaControls_NativeMenu='false',WuWaControls_AdjustMode='false',
        VR_MonoTheatreMode=tostring(view=='mono'),VR_2DScreenMode=tostring(view=='stereo'),VR_WorldScale='1.75',
        WindowMode_Enabled='true',WuWaDiorama_Enabled='true',VR_EnableGUI='true'}
    if not legacy then f.values[TOGGLE_MONO]='effective-v2'; f.values[TOGGLE_SCREEN]='effective-v2' end
    local function effective(key)
        if f.auto then return tostring(key==EFFECTIVE_SCREEN or f.auto=='mono') end
        if key==EFFECTIVE_MONO then return f.values[MONO] end
        return tostring(f.values[SCREEN]=='true' or f.values[MONO]=='true')
    end
    local env=setmetatable({uevr={api={get_player_controller=function()
        assert(not f.in_xinput,'XInput accessed a UObject'); return f.pc
    end,get_local_pawn=function() end},params={vr={
        get_mod_value=function(_,key)
            if key==EFFECTIVE_MONO or key==EFFECTIVE_SCREEN then return effective(key) end
            return f.values[key] or 'false'
        end,
        set_mod_value=function(key,value)
            assert(not f.in_xinput,'XInput callback wrote '..key)
            f.writes[key]=(f.writes[key] or 0)+1
            if VIEW_KEYS[key] then f.requests[#f.requests+1]=key..'='..value; f.view_request=true end
            if (key==TOGGLE_MONO or key==TOGGLE_SCREEN) and f.values[key]=='effective-v2' then
                if value~='on' and value~='off' and value~='toggle' then return end
                local option=key==TOGGLE_MONO and MONO or SCREEN
                local now=effective(key==TOGGLE_MONO and EFFECTIVE_MONO or EFFECTIVE_SCREEN)=='true'
                local want=value=='on' or (value=='toggle' and not now)
                f.auto=nil; f.values[option]=tostring(want)
                if option==SCREEN and not want then f.values[MONO]='false' end
                return
            end
            f.values[key]=value
        end},
        functions={is_drawing_ui=function() return f.ui end,dispatch_custom_event=function() end,
            log_info=function(e) f.logs[#f.logs+1]=e end,
            log_warn=function(e) f.errors[#f.errors+1]=e end,log_error=function(e) f.errors[#f.errors+1]=e end}},
        sdk={callbacks=setmetatable({},{__index=function(_,key) return function(fn) f.callbacks[key]=fn end end})}},
        json={dump_string=function() return '{}' end}},{__index=_G})
    assert(loadfile(source,'t',env))()
    function f:advance(dt) self.clock=self.clock+(dt or .016); self.values.WuWaControls_Clock=tostring(self.clock) end
    -- At most one view shortcut applies per engine tick; count the ticks that
    -- asked the backend for a view, whichever key route the backend supports.
    function f:tick(dt)
        if dt then self:advance(dt) end
        self.view_request=false
        self.callbacks.on_pre_engine_tick(nil,.016); assert(#self.errors==0,self.errors[1])
        if self.view_request then self.applied=self.applied+1 end
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
    function f:click(index) consumed(self:sample(R3,255,255,index)); consumed(self:sample(0,255,255,index)) end
    function f:hold(seconds)
        local remaining=seconds
        while remaining>1e-9 do
            local step=math.min(.1,remaining); self:advance(step)
            consumed(self:input(R3,255,255)); self:tick(); remaining=remaining-step
        end
    end
    function f:count(n) assert(self.applied==n,'View shortcuts '..self.applied..', expected '..n) end
    function f:view() return self.values[MONO]=='true' and 'mono' or self.values[SCREEN]=='true' and 'stereo' or 'full' end
    function f:expect(view) assert(self:view()==view,'View '..self:view()..', expected '..view) end
    fixtures[#fixtures+1]=f; return f
end
local function test(name,fn)
    local ok,e=pcall(fn)
    if ok then passed=passed+1; print('PASS: '..name)
    else failures[#failures+1]=name..': '..tostring(e); print('FAIL: '..failures[#failures]) end
end

test('with both triggers held, each R3 click toggles full VR / stereo screen',function()
    for _,legacy in ipairs({false,true}) do
        local f=fixture('full',legacy); local p=f:arm()
        assert(p.bLeftTrigger==255 and p.bRightTrigger==255,'Pre-click game input changed')
        consumed(f:sample(R3,255,255)); f:count(0)
        consumed(f:sample(0,255,255)); f:count(1); f:expect('stereo')
        f:click(); f:count(2); f:expect('full')
        f:click(); f:count(3); f:expect('stereo')
        -- Held R3 between clicks does nothing more until it is released.
        for _=1,3 do consumed(f:sample(R3,255,255)) end; consumed(f:sample(0,255,255)); f:count(4); f:expect('full')
    end
end)
test('holding R3 0.8 s enters mono and holding again returns to the view before it',function()
    for _,start in ipairs({'full','stereo'}) do
        for _,legacy in ipairs({false,true}) do
            local f=fixture(start,legacy); f:arm()
            f:hold(1.0); f:count(1); f:expect('mono')
            consumed(f:sample(0,255,255)); f:count(1) -- releasing a fired hold is not a click
            f:hold(1.0); f:count(2); f:expect(start)
            consumed(f:sample(0,255,255)); f:count(2)
        end
    end
end)
test('a click from mono goes to full VR; mono then returns to the new view',function()
    local f=fixture('stereo'); f:arm()
    f:hold(1.0); consumed(f:sample(0,255,255)); f:expect('mono')
    f:click(); f:expect('full')
    f:hold(1.0); consumed(f:sample(0,255,255)); f:expect('mono')
    f:hold(1.0); consumed(f:sample(0,255,255)); f:expect('full')
end)
test('a hold shorter than 0.8 s is a click',function()
    local f=fixture('full'); f:arm()
    f:hold(.5); consumed(f:sample(0,255,255)); f:count(1); f:expect('stereo')
end)
test('a click needs both full triggers through the R3 release; a dip cancels until neutral',function()
    local f=fixture(); f:arm(); consumed(f:sample(R3,255,255)); consumed(f:sample()); f:count(0)
    f:arm(); consumed(f:sample(R3,255,255)); consumed(f:sample(0,255,179)); consumed(f:sample(0,255,255)); f:count(0)
    consumed(f:sample(R3,255,255)); consumed(f:sample(0,255,255)); f:count(0)
    f:sample(); f:arm(); f:click(); f:count(1); f:expect('stereo')
end)
test('a trigger dip or polling pause during an R3 hold cancels it until neutral',function()
    for _,reason in ipairs({'dip','pause'}) do
        local f=fixture('stereo'); f:arm(); consumed(f:sample(R3,255,255)); f:hold(.4)
        if reason=='dip' then consumed(f:sample(R3,255,179))
        else f:advance(.251); consumed(f:input(R3,255,255)); f:tick() end
        f:hold(1); consumed(f:sample(0,255,255)); f:count(0); f:expect('stereo')
        f:click(); f:count(0); f:expect('stereo') -- still cancelled while the triggers stay held
        f:sample(); f:arm(); f:click(); f:count(1); f:expect('full')
    end
end)
test('a click or hold starts from the live view, including an automatic cinema view',function()
    local f=fixture('full'); f.auto='stereo'; f:arm(); f:click(); f:expect('full')
    assert(f.requests[#f.requests]=='WuWaControls_ToggleStereoScreen=off',f.requests[#f.requests])
    f=fixture('full'); f.auto='mono'; f:arm(); f:hold(1.0); consumed(f:sample(0,255,255)); f:expect('full')
    f=fixture('full'); f.auto='mono'; f:arm(); f:click(); f:expect('full')
end)
test('R3 first or simultaneous input cannot start the chord',function()
    local f=fixture(); f:sample(); f:sample(R3); f:sample(R3,255,255); f:sample(0,255,255); f:count(0)
    f:sample(); f:arm(); f:click(); f:count(1)
end)
test('extra buttons cancel the chord until neutral; L3+R3 still reaches the native menu',function()
    local f=fixture(); f:arm(); consumed(f:sample(R3,255,255))
    local p=f:input(R3+L3,255,255); assert(p.wButtons==L3+R3,'Native menu chord changed'); f:tick()
    consumed(f:sample(0,255,255)); f:count(0)
    f:sample(); f:arm(); f:click(); f:count(1)
end)

for _,f in ipairs(fixtures) do
    for _,key in ipairs({'VR_WorldScale','VR_EnableGUI'}) do
        assert(not f.writes[key],'View workflow changed '..key)
    end
    assert(f.legacy or not (f.writes[MONO] or f.writes[SCREEN]),'v2 fixture wrote a view option directly')
    assert(#f.errors==0,f.errors[1])
end
assert(#failures==0,table.concat(failures,'\n'))
print(string.format('PASS: %d R3 view-chord groups, %d isolated fixtures; no game/device/native calls',passed,#fixtures))
