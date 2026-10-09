-- Pure callbacks; no game, device or native calls. Lua 5.4:
-- lua dev/test_portal_shortcut.lua mod/lua/02_WuWaVR_PolarControls.lua
local source=assert(arg[1], 'Pass the PolarControls script path')
local L3,R3,A,B,Y=64,128,4096,8192,32768
local PORTAL,DIORAMA,SCREEN='WindowMode_Enabled','WuWaDiorama_Enabled','VR_2DScreenMode'
local fixtures,failures,passed={},{},0
local function fixture()
    local f={callbacks={},writes={},errors={},clock=0,ui=false,pc={bShowMouseCursor=false,
        get_class=function() return {find_function=function() end} end}}
    f.values={WuWaControls_Enabled='true',WuWaControls_Focused='true',WuWaControls_MouseAssist='true',
        WuWaControls_CameraMode='0',WuWaControls_NativeMenu='false',WuWaControls_AdjustMode='false',
        WuWaDiorama_Enabled='false',VR_2DScreenMode='false',VR_WorldScale='1.75',WindowMode_Enabled='false',
        WindowMode_PlaneWidth='2.4',WindowMode_PlaneHeight='1.35',WindowMode_Curvature='0.5',VR_EnableGUI='true'}
    local env=setmetatable({uevr={
        api={get_player_controller=function()
            assert(not f.in_xinput,'XInput callback read a player-controller UObject')
            return f.pc
        end,get_local_pawn=function() end},
        params={vr={
            get_mod_value=function(_,key) return f.values[key] or 'false' end,
            set_mod_value=function(key,value)
                assert(key~='VR_WorldScale','Shortcut wrote normal VR_WorldScale')
                f.values[key]=value; f.writes[key]=(f.writes[key] or 0)+1
            end},functions={is_drawing_ui=function() return f.ui end,dispatch_custom_event=function() end,
                log_warn=function(s) f.errors[#f.errors+1]=s end,log_error=function(s) f.errors[#f.errors+1]=s end}},
        sdk={callbacks=setmetatable({},{__index=function(_,name) return function(fn) f.callbacks[name]=fn end end})}},
        json={dump_string=function(value)
            if value.motion_only then f.motion=value.motion end
            return '{}'
        end}},{__index=_G})
    assert(loadfile(source,'t',env))()
    function f:advance(seconds) self.clock=self.clock+(seconds or .016); self.values.WuWaControls_Clock=tostring(self.clock) end
    function f:clean() assert(#self.errors==0,self.errors[1]) end
    function f:tick(seconds)
        if seconds then self:advance(seconds) end
        self.callbacks.on_pre_engine_tick(nil,.016); self:clean()
    end
    -- Deliberately separate XInput from the tick to cover queued-action races.
    function f:input(buttons,lt,rt,index,result)
        local state={Gamepad={wButtons=buttons or 0,bLeftTrigger=lt or 0,bRightTrigger=rt or 0,
            sThumbLX=0,sThumbLY=0,sThumbRX=0,sThumbRY=0}}
        self.in_xinput=true
        local ok,message=pcall(self.callbacks.on_xinput_get_state,nil,index or 0,state,result or 0)
        self.in_xinput=false; assert(ok,message)
        self:clean(); return state.Gamepad
    end
    function f:sample(buttons,lt,rt,index)
        self:advance(); local pad=self:input(buttons,lt,rt,index); self:tick(); return pad
    end
    function f:counts(portal,diorama,screen)
        assert((self.writes[PORTAL] or 0)==portal,'Portal count '..tostring(self.writes[PORTAL] or 0)..', expected '..portal)
        assert((self.writes[DIORAMA] or 0)==diorama,'Diorama count '..tostring(self.writes[DIORAMA] or 0)..', expected '..diorama)
        assert((self.writes[SCREEN] or 0)==(screen or 0),'Screen count '..tostring(self.writes[SCREEN] or 0)..', expected '..(screen or 0))
    end
    function f:motion_sample()
        self.motion=nil; self.values.WuWaControls_Recording='true'
        self.callbacks.on_pre_viewport_client_draw()
        self.values.WuWaControls_Recording='false'; self:clean()
        return assert(self.motion,'Recording callback emitted no camera eligibility sample')
    end
    function f:preserved()
        assert(not self.writes.VR_WorldScale,'Normal world scale was written')
        for key,value in pairs({WindowMode_PlaneWidth='2.4',WindowMode_PlaneHeight='1.35',WindowMode_Curvature='0.5'}) do
            assert(self.values[key]==value and not self.writes[key],'Portal geometry changed: '..key)
        end
    end
    fixtures[#fixtures+1]=f; return f
end
local function consumed(p)
    assert(p.wButtons & L3==0 and p.bLeftTrigger==0 and p.bRightTrigger==0,'Latched chord leaked L3 or a trigger')
end
local function screen_hold(f,seconds,index,tick)
    local remaining=seconds
    while remaining>1e-9 do
        local step=math.min(.1,remaining); f:advance(step)
        consumed(f:input(L3,255,255,index))
        if tick~=false then f:tick() end
        remaining=remaining-step
    end
end
local function begin_screen(f,index)
    f:sample(0,255,255,index); consumed(f:sample(L3,255,255,index))
end
local function test(name,body)
    local ok,message=pcall(body)
    if ok then passed=passed+1; print('PASS: '..name)
    else failures[#failures+1]=name..': '..tostring(message); print('FAIL: '..failures[#failures]) end
end

test('initial neutral rearm, thresholds and one fire per portal/diorama gesture',function()
    for _,right in ipairs({false,true}) do
        local f=fixture()
        local function chord(v) return f:sample(L3,right and 0 or v,right and v or 0) end
        chord(255); f:counts(0,0); f:sample(); f:sample(L3)
        local below=chord(29); assert((right and below.bRightTrigger or below.bLeftTrigger)==29,'Below-deadzone input changed')
        consumed(chord(30)); consumed(chord(179)); f:counts(0,0)
        consumed(chord(180)); f:counts(right and 0 or 1,right and 1 or 0)
        for _=1,20 do consumed(chord(255)) end
        f:counts(right and 0 or 1,right and 1 or 0)
        f:sample(); consumed(chord(255)); f:counts(right and 0 or 2,right and 2 or 0)
        assert(f.values[right and DIORAMA or PORTAL]=='false','Second gesture did not toggle off')
    end
end)
test('both release orders wait for L3 and both triggers',function()
    for _,right in ipairs({false,true}) do
        local f=fixture(); f:sample(); local lt,rt=right and 0 or 255,right and 255 or 0
        consumed(f:sample(L3,lt,rt)); consumed(f:sample(0,lt,rt)); consumed(f:sample(L3,lt,rt))
        consumed(f:sample(L3,0,0)); consumed(f:sample(L3,lt,rt))
        consumed(f:sample(0,rt,lt)); consumed(f:sample(L3,lt,rt))
        f:counts(right and 0 or 1,right and 1 or 0)
        f:sample(); consumed(f:sample(L3,lt,rt)); f:counts(right and 0 or 2,right and 2 or 0)
    end
end)
test('short two-trigger holds and trigger rolls do not toggle until release',function()
    local f=fixture(); f:sample()
    consumed(f:sample(L3,255,255)); consumed(f:sample(L3,0,255)); consumed(f:sample(L3,255,0)); f:counts(0,0)
    f:sample(); consumed(f:sample(L3,0,255)); f:counts(0,1)
    for _,right in ipairs({false,true}) do
        for _,overlap in ipairs({false,true}) do
            local g=fixture(); g:sample()
            consumed(g:sample(L3,right and 0 or 30,right and 30 or 0))
            if overlap then consumed(g:sample(L3,30,30)) end
            consumed(g:sample(L3,right and 255 or 0,right and 0 or 255)); g:counts(0,0)
            consumed(g:sample(0,255,255)); consumed(g:sample(L3,255,0)); g:counts(0,0)
            g:sample(); consumed(g:sample(L3,255,0)); g:counts(1,0)
        end
    end
end)
test('extra buttons latch a cancelled gesture until full release',function()
    local f=fixture(); f:sample()
    local p=f:sample(L3+Y,0,255); consumed(p); assert(p.wButtons & Y~=0,'Unrelated button was swallowed')
    consumed(f:sample(L3,0,255)); f:counts(0,0)
    f:sample(); consumed(f:sample(L3,0,255)); f:counts(0,1)
end)
test('ambiguity cancels a queued action before its first engine tick',function()
    local f=fixture(); f:sample()
    f:sample(); consumed(f:input(L3,0,255)); consumed(f:input(L3,255,255)); f:tick(); f:counts(0,0)
    consumed(f:sample(L3,255,0)); f:counts(0,0)
    f:sample(); consumed(f:sample(L3,0,255)); f:counts(0,1)
end)
test('normal triggers, HUD shortcut and UEVR menu chord remain intact',function()
    local f=fixture(); f:sample()
    assert(f:sample(0,255,0).bLeftTrigger==255,'Normal LT swallowed')
    assert(f:sample(0,0,255).bRightTrigger==255,'Normal RT swallowed')
    local both=f:sample(0,255,255); assert(both.bLeftTrigger==255 and both.bRightTrigger==255,'Ordinary two-trigger input changed')
    f:sample(); f:sample(L3+B); assert(f.values.VR_EnableGUI=='false','HUD chord changed')
    f:sample(); assert(f:sample(L3+R3).wButtons==L3+R3,'UEVR menu chord changed'); f:counts(0,0)
end)
test('game menu/cursor blocks diorama and cancels held and queued transitions',function()
    for _,signal in ipairs({'native','cursor'}) do
        local f=fixture(); f:sample()
        local function menu(on)
            if signal=='native' then f.values.WuWaControls_NativeMenu=tostring(on) else f.pc.bShowMouseCursor=on end
        end
        menu(true); f:tick(); consumed(f:sample(L3,0,255)); f:counts(0,0)
        menu(false); f:tick(); consumed(f:sample(L3,0,255)); f:counts(0,0)
        f:sample(); consumed(f:sample(L3,0,255)); f:counts(0,1)
        f:sample(); consumed(f:sample(L3,0,30)); menu(true); f:tick(); menu(false); f:tick()
        consumed(f:sample(L3,0,255)); f:counts(0,1)
        f:sample(); consumed(f:sample(L3,0,255)); f:counts(0,2)
        f:sample(); consumed(f:input(L3,0,255)); menu(true); f:tick(); f:counts(0,2)
        -- Portal remains usable in a game menu; only diorama is gated by this signal.
        f:sample(); consumed(f:sample(L3,255,0)); f:counts(1,2)
    end
end)
test('adjustment, focus, disabled controls, UEVR and passthrough require fresh rearm',function()
    for _,reason in ipairs({'adjust','focus','enabled','ui','passthrough'}) do
        local f=fixture(); f:sample()
        local function blocked(on)
            if reason=='adjust' then f.values.WuWaControls_AdjustMode=tostring(on)
            elseif reason=='focus' then f.values.WuWaControls_Focused=tostring(not on)
            elseif reason=='enabled' then f.values.WuWaControls_Enabled=tostring(not on)
            elseif reason=='passthrough' then f.values.VR_WuWaGamepadPassthrough=tostring(on)
            else f.ui=on end
        end
        blocked(true); f:tick(); f:sample(L3,0,255); f:counts(0,0)
        blocked(false); f:tick(); f:sample(L3,0,255); f:counts(0,0)
        f:sample(); consumed(f:sample(L3,0,255)); f:counts(0,1)
        f:sample(); consumed(f:input(L3,0,255)); blocked(true); f:tick(); f:counts(0,1)
        blocked(false); f:tick(); f:sample(L3,0,255); f:counts(0,1)
        f:sample(); consumed(f:sample(L3,0,255)); f:counts(0,2)
    end
end)
test('portal preserves focus and UEVR rearm behavior',function()
    local f=fixture(); f:sample(); consumed(f:sample(L3,255,0)); f:counts(1,0)
    f.values.WuWaControls_Focused='false'; f:sample(L3,255,0)
    f.values.WuWaControls_Focused='true'; f:sample(L3,255,0); f:counts(1,0)
    f:sample(); consumed(f:sample(L3,255,0)); f:counts(2,0)
    f.ui=true; f:sample(L3,255,0); f.ui=false; f:sample(L3,255,0); f:counts(2,0)
    f:sample(); consumed(f:sample(L3,255,0)); f:counts(3,0)
end)
test('passthrough clears queued shortcuts while preserving focused camera eligibility',function()
    local f=fixture()
    f.values.WuWaControls_KeepCameraOnFocusLoss='false'; f.values.WuWaControls_CameraMode='1'
    f:sample(); local before=f:motion_sample()
    assert(before.camera_active==true and before.focused==true,'Camera was not initially eligible')
    consumed(f:input(L3,0,255)); f.values.VR_WuWaGamepadPassthrough='true'; f:tick(); f:counts(0,0)
    local during=f:motion_sample()
    assert(during.camera_active==true and during.focused==true,'Passthrough disabled focused camera eligibility')
    assert(during.input==nil and during.slot==nil,'Passthrough kept queued controller ownership')
    local raw=f:sample(A,12,255)
    assert(raw.wButtons==A and raw.bLeftTrigger==12 and raw.bRightTrigger==255,'Passthrough altered ordinary input')
    assert(f:motion_sample().camera_active==true,'A passthrough sample disabled camera eligibility')
    f.values.VR_WuWaGamepadPassthrough='false'; f:tick(); f:sample(L3,0,255); f:counts(0,0)
    f:sample(); consumed(f:sample(L3,0,255)); f:counts(0,1)
    -- Prove the telemetry reflects real eligibility transitions, not a fixture constant.
    f.values.WuWaControls_Focused='false'; f:tick(); local unfocused=f:motion_sample()
    assert(unfocused.camera_active==false and unfocused.focused==false,'Focus-loss camera preference was ignored')
end)
test('queued actions expire after .25 seconds and cannot replay while held',function()
    for _,right in ipairs({false,true}) do
        for _,age in ipairs({.249,.251}) do
            local f=fixture(); f:sample(); local lt,rt=right and 0 or 255,right and 255 or 0
            consumed(f:input(L3,lt,rt)); f:tick(age); local n=age<.25 and 1 or 0
            f:counts(right and 0 or n,right and n or 0)
            consumed(f:sample(L3,lt,rt)); f:counts(right and 0 or n,right and n or 0)
            f:sample(); consumed(f:sample(L3,lt,rt)); f:counts(right and 0 or n+1,right and n+1 or 0)
        end
    end
end)
test('disconnect before tick clears action and reconnect requires neutral',function()
    for _,right in ipairs({false,true}) do
        local f=fixture(); f:sample(); local lt,rt=right and 0 or 255,right and 255 or 0
        consumed(f:input(L3,lt,rt)); f:input(L3,255,255,0,1167); f:tick(); f:counts(0,0)
        f:sample(L3,lt,rt); f:counts(0,0)
        f:sample(); consumed(f:sample(L3,lt,rt)); f:counts(right and 0 or 1,right and 1 or 0)
    end
end)
test('handover drops old pending actions without unrelated-slot disconnect loss',function()
    local f=fixture(); f:sample(); f:sample(0,0,0,1)
    consumed(f:input(L3,0,255,0)); f:advance(1.01); f:input(A,0,0,1); f:tick(); f:counts(0,0)
    f:sample(0,0,0,1); consumed(f:sample(L3,255,0,1)); f:counts(1,0)
    f:sample(0,0,0,1); consumed(f:input(L3,0,255,1)); f:input(0,0,0,0,1167); f:tick(); f:counts(1,1)
end)
test('reset clears queued actions and synthetic toggles preserve changed normal scale',function()
    local f=fixture(); f:sample(); consumed(f:input(L3,0,255)); f.callbacks.on_script_reset(); f:tick(); f:counts(0,0)
    f:sample(L3,0,255); f:counts(0,0)
    f:sample(); consumed(f:sample(L3,0,255)); f:counts(0,1)
    assert(f.values.VR_WorldScale=='1.75','Diorama changed normal scale')
    f.values.VR_WorldScale='2.25' -- Models a deliberate normal-scale setting change.
    f:sample(); consumed(f:sample(L3,0,255)); f:counts(0,2)
    assert(f.values.VR_WorldScale=='2.25','Diorama off reverted the new normal scale')
end)
test('screen needs fresh neutral, both full triggers, then an L3 click; fires once',function()
    local f=fixture()
    f:sample(L3,255,255)
    for _=1,10 do f:advance(.1); f:input(L3,255,255); f:tick() end
    f:counts(0,0,0)
    f:sample()
    -- Both triggers first is a reliable deliberate acquisition; ordinary
    -- trigger-only game input remains unchanged until L3 is pressed as well.
    local normal=f:sample(0,255,255)
    assert(normal.bLeftTrigger==255 and normal.bRightTrigger==255,'Pre-L3 triggers changed')
    begin_screen(f); f:counts(0,0,1)
    assert(f.values[SCREEN]=='true','Screen mode was not enabled')
    screen_hold(f,1.1); f:counts(0,0,1)
    consumed(f:sample(0,255,255)); begin_screen(f); screen_hold(f,1); f:counts(0,0,1)
    consumed(f:sample(L3,0,0)); begin_screen(f); screen_hold(f,1); f:counts(0,0,1)
    f:sample(); begin_screen(f); f:counts(0,0,2)
    assert(f.values[SCREEN]=='false','Second fresh click did not disable screen mode')
    assert(not f.writes.VR_EnableGUI,'Screen toggle changed HUD visibility')
end)

test('screen click released before the engine tick still toggles once',function()
    local f=fixture(); f:sample(); f:sample(0,255,255)
    f:advance(); consumed(f:input(L3,255,255)); f:advance(); consumed(f:input(0,255,255))
    f:tick(); f:counts(0,0,1)
    begin_screen(f); f:counts(0,0,1)
    f:sample(); begin_screen(f); f:counts(0,0,2)
end)

test('screen needs both triggers at 180 before the L3 click',function()
    local f=fixture(); f:sample(); f:sample(0,179,179); consumed(f:sample(L3,179,179))
    consumed(f:sample(L3,255,255)); f:counts(0,0,0)
    f:sample(); f:sample(0,180,180); consumed(f:sample(L3,180,180)); f:counts(0,0,1)
end)

test('screen click is rejected unless it follows a fresh triggers-only sample',function()
    for _,reason in ipairs({'extra','menu_escape','partial','l3_first','stale','clock'}) do
        local f=fixture(); f:sample()
        if reason=='extra' then f:sample(0,255,255); consumed(f:sample(L3+Y,255,255))
        elseif reason=='menu_escape' then f:sample(0,255,255); assert(f:sample(L3+R3,255,255).wButtons==L3+R3,'Menu escape changed')
        elseif reason=='partial' then f:sample(0,255,179); consumed(f:sample(L3,255,255))
        elseif reason=='l3_first' then f:sample(L3,0,0); consumed(f:sample(L3,255,255))
        elseif reason=='stale' then f:sample(0,255,255); f:advance(.251); consumed(f:input(L3,255,255))
        else f:sample(0,255,255); f:advance(-.1); consumed(f:input(L3,255,255)) end
        screen_hold(f,1); f:counts(0,0,0)
        f:sample(); begin_screen(f); f:counts(0,0,1)
    end
end)

test('single-trigger gestures never promote to screen mode when the other joins',function()
    for _,right in ipairs({false,true}) do
        for _,fired in ipairs({false,true}) do
            local f=fixture(); f:sample(); local value=fired and 255 or 30
            consumed(f:sample(L3,right and 0 or value,right and value or 0))
            begin_screen(f); screen_hold(f,1.2)
            f:counts(not right and fired and 1 or 0,right and fired and 1 or 0,0)
            f:sample(); begin_screen(f)
            f:counts(not right and fired and 1 or 0,right and fired and 1 or 0,1)
        end
    end
end)

test('screen toggle remains available during game dialogue and cursor transitions',function()
    for _,signal in ipairs({'native','cursor'}) do
        local f=fixture(); f:sample()
        f.values[PORTAL]='true'; f.values[DIORAMA]='true'
        local function menu(on)
            if signal=='native' then f.values.WuWaControls_NativeMenu=tostring(on) else f.pc.bShowMouseCursor=on end
            f:tick()
        end
        f:sample(0,255,255); menu(true); consumed(f:sample(L3,255,255)); f:counts(0,0,1)
        f:sample(); begin_screen(f); f:counts(0,0,2)
        assert(f.values[PORTAL]=='true' and f.values[DIORAMA]=='true','Screen changed another view mode')
        assert(f.values.VR_EnableGUI=='true' and not f.writes.VR_EnableGUI,'Screen changed the dialogue HUD')
    end
end)

test('screen focus, UEVR, adjustment, disabled controls and passthrough require rearm',function()
    for _,reason in ipairs({'adjust','focus','enabled','ui','passthrough'}) do
        local f=fixture(); f:sample(); f:sample(0,255,255)
        local function blocked(on)
            if reason=='adjust' then f.values.WuWaControls_AdjustMode=tostring(on)
            elseif reason=='focus' then f.values.WuWaControls_Focused=tostring(not on)
            elseif reason=='enabled' then f.values.WuWaControls_Enabled=tostring(not on)
            elseif reason=='passthrough' then f.values.VR_WuWaGamepadPassthrough=tostring(on)
            else f.ui=on end
            f:tick()
        end
        blocked(true); blocked(false)
        -- The transition was seen only by the tick, not an XInput callback.
        for _=1,12 do f:advance(.1); f:input(L3,255,255); f:tick() end
        f:counts(0,0,0)
        f:sample(); begin_screen(f); f:counts(0,0,1)
    end
end)

test('queued screen requests respect TTL and reject a reversed clock',function()
    for _,age in ipairs({.249,.251,-.01}) do
        local f=fixture(); f:sample(); f:sample(0,255,255); f:advance(); consumed(f:input(L3,255,255))
        f:counts(0,0,0); f:tick(age)
        local count=age>=0 and age<.25 and 1 or 0
        f:counts(0,0,count); screen_hold(f,1); f:counts(0,0,count)
    end
end)

test('screen disconnect, handover and reset discard pending requests',function()
    for _,reason in ipairs({'disconnect','handover','reset','unrelated'}) do
        local f=fixture(); f:sample(); f:sample(0,0,0,1)
        f:sample(0,255,255); f:advance(); consumed(f:input(L3,255,255))
        if reason=='disconnect' then f:input(L3,255,255,0,1167)
        elseif reason=='handover' then f:advance(1.01); f:input(A,0,0,1)
        elseif reason=='reset' then f.callbacks.on_script_reset()
        else f:input(0,0,0,1,1167) end
        f:tick(); f:counts(0,0,reason=='unrelated' and 1 or 0)
        if reason=='handover' then
            f:sample(0,0,0,1); begin_screen(f,1); f:counts(0,0,1)
        end
    end
end)
for index,f in ipairs(fixtures) do
    local ok,message=pcall(function() f:preserved(); f:clean() end)
    if not ok then failures[#failures+1]='fixture '..index..' preservation: '..tostring(message) end
end
assert(#failures==0,table.concat(failures,'\n'))
print(string.format('PASS: %d view-shortcut groups, %d isolated fixtures; no game/device/native calls',passed,#fixtures))
