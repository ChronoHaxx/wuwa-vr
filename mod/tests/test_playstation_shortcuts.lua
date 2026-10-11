-- Uses actual PolarControls callback/state machine, no engine/device I/O.
local source=assert(arg[1],'Pass PolarControls path')
local L3,R3=64,128
local passed=0
local function fixture()
    local f={clock=10,cb={},writes={},ui=false,errors={}}
    f.values={WuWaControls_Enabled='true',WuWaControls_Focused='true',WuWaControls_MouseAssist='true',
        WuWaControls_CameraMode='0',VR_MonoTheatreMode='false',VR_2DScreenMode='false',
        WindowMode_Enabled='false',WuWaDiorama_Enabled='false',VR_EnableGUI='true'}
    local env=setmetatable({uevr={api={get_player_controller=function()
        assert(not f.in_xinput,'XInput touched Unreal state')
        return {bShowMouseCursor=false,get_class=function() return {find_function=function() end} end}
    end,get_local_pawn=function() end},params={vr={
        get_mod_value=function(_,k) return k=='WuWaControls_Clock' and tostring(f.clock) or f.values[k] or 'false' end,
        set_mod_value=function(k,v) f.values[k]=v; f.writes[k]=(f.writes[k] or 0)+1 end},functions={
        is_drawing_ui=function() return f.ui end,dispatch_custom_event=function() end,
        log_info=function() end,log_warn=function(e) f.errors[#f.errors+1]=e end,log_error=function(e) f.errors[#f.errors+1]=e end}},
        sdk={callbacks=setmetatable({},{__index=function(_,k) return function(fn) f.cb[k]=fn end end})}},
        json={dump_string=function(t) if t.screenshot then f.screenshots=(f.screenshots or 0)+1 end return '{}' end}},{__index=_G})
    assert(loadfile(source,'t',env))()
    function f:tick() self.cb.on_pre_engine_tick(nil,.016); assert(#self.errors==0,self.errors[1]) end
    function f:ps(b,lt,rt,gen)
        self.clock=self.clock+.02
        self.values.WuWaControls_PlayStationState=string.format('ps-v1,%d,%.0f,%d,%d,%d,0,0,0,0',gen or 1,self.clock*1000,b or 0,lt or 0,rt or 0)
        self:tick()
    end
    function f:xp(b,lt,rt)
        self.clock=self.clock+.02; self.values.WuWaControls_PlayStationState='unavailable'
        local p={Gamepad={wButtons=b or 0,bLeftTrigger=lt or 0,bRightTrigger=rt or 0,sThumbLX=0,sThumbLY=0,sThumbRX=0,sThumbRY=0}}
        self.in_xinput=true; self.cb.on_xinput_get_state(nil,0,p,0); self.in_xinput=false; self:tick()
    end
    function f:count(k,n) assert((self.writes[k] or 0)==n,k..' count '..(self.writes[k] or 0)..' expected '..n) end
    return f
end
local function test(name,body) body(); passed=passed+1; print('PASS '..name) end
test('native report applies existing portal and diorama trigger thresholds once',function()
    for _,side in ipairs({'left','right'}) do
        local f=fixture(); local key=side=='left' and 'WindowMode_Enabled' or 'WuWaDiorama_Enabled'
        local function chord(v) f:ps(L3,side=='left' and v or 0,side=='right' and v or 0) end
        chord(255); f:count(key,0); f:ps(); chord(179); f:count(key,0); chord(180); f:count(key,1)
        for i=1,100 do chord(255) end
        f:count(key,1); f:ps(); chord(255); f:count(key,2)
    end
end)
test('native next view (R3 click) preserves triggers-first and fresh edge requirements',function()
    -- Since test 3i: with both triggers held first, an R3 click is the next view.
    local f=fixture(); f:ps(); f:ps(R3,255,255); f:ps(0,255,255); f:count('VR_2DScreenMode',0)
    f:ps(); f:ps(0,255,255); f:ps(R3,255,255); f:ps(0,255,255); f:count('VR_2DScreenMode',1)
    for i=1,60 do f:ps(R3,255,255) end
    f:count('VR_2DScreenMode',1)
end)
test('native stereo screen requires continuous 0.8 second hold',function()
    local f=fixture(); f:ps(); f:ps(L3,255,255)
    for i=1,39 do f:ps(L3,255,255) end
    f:count('VR_2DScreenMode',0); f:ps(L3,255,255); f:ps(L3,255,255); f:count('VR_2DScreenMode',1)
end)
test('Steam Input appearing while native chord held cannot toggle twice',function()
    local f=fixture(); f:ps(); f:ps(L3,255,0); f:count('WindowMode_Enabled',1)
    for i=1,60 do f:xp(L3,255,0) end
    f:count('WindowMode_Enabled',1); f:xp(); f:xp(L3,255,0); f:count('WindowMode_Enabled',2)
end)
test('mapped input disappearing while held cannot transfer gesture to HID',function()
    local f=fixture(); f:xp(); f:xp(L3,0,255); f:count('WuWaDiorama_Enabled',1)
    for i=1,60 do f:ps(L3,0,255) end
    f:count('WuWaDiorama_Enabled',1); f:ps(); f:ps(L3,0,255); f:count('WuWaDiorama_Enabled',2)
end)
test('reconnect generation requires release even with same shortcut held',function()
    local f=fixture(); f:ps(); f:ps(L3,255,0); f:ps(L3,255,0,2); f:count('WindowMode_Enabled',1)
    f:ps(0,0,0,2); f:ps(L3,255,0,2); f:count('WindowMode_Enabled',2)
end)
test('stale malformed and unavailable snapshots discard held source',function()
    for _,raw in ipairs({'unavailable','ps-v1,1,0,64,255,0,0,0,0,0','ps-v1,1,10000,64,999,0,0,0,0,0','ps-v1,bad'}) do
        local f=fixture(); f:ps(); f:ps(L3,255,0); f.values.WuWaControls_PlayStationState=raw; f:tick()
        f:ps(L3,255,0); f:count('WindowMode_Enabled',1); f:ps(); f:ps(L3,255,0); f:count('WindowMode_Enabled',2)
    end
end)
test('native focus loss and UEVR menu opening require released controls',function()
    for _,gate in ipairs({'WuWaControls_Focused','ui','VR_WuWaGamepadPassthrough'}) do
        local f=fixture(); f:ps(); f:ps(L3,255,0)
        if gate=='ui' then f.ui=true else f.values[gate]=gate=='WuWaControls_Focused' and 'false' or 'true' end
        f:ps(L3,255,0)
        if gate=='ui' then f.ui=false else f.values[gate]=gate=='WuWaControls_Focused' and 'true' or 'false' end
        f:ps(L3,255,0); f:count('WindowMode_Enabled',1); f:ps(); f:ps(L3,255,0); f:count('WindowMode_Enabled',2)
    end
end)
test('native L3+R3 priority cancels other view shortcut',function()
    local f=fixture(); f:ps(); f:ps(0,255,255); f:ps(L3+R3,255,255)
    f:count('VR_MonoTheatreMode',0); f:count('WindowMode_Enabled',0); f:count('VR_2DScreenMode',0)
end)
test('native PS face and option buttons use common shortcut vocabulary',function()
    local f=fixture(); f:ps(); f:ps(L3+8192); f:count('VR_EnableGUI',1)
    f:ps(); f:ps(L3+16); f:count('WuWaControls_PinCard',1)
end)
test('holding L3 for half a second calls up the controls card and lights buttons',function()
    local f=fixture(); f:xp()
    for i=1,24 do f:xp(L3) end
    assert(f.values.WuWaControls_Card==nil or f.values.WuWaControls_Card=='','card before half a second')
    f:xp(L3); f:xp(L3); assert(f.values.WuWaControls_Card=='l3:64:0:0',tostring(f.values.WuWaControls_Card))
    f:xp(L3+8192); assert(f.values.WuWaControls_Card=='l3:8256:0:0',tostring(f.values.WuWaControls_Card))
    f:xp(); assert(f.values.WuWaControls_Card=='','card after release')
end)
test('a long L3 hold never counts toward double L3',function()
    local f=fixture(); f:xp()
    for i=1,30 do f:xp(L3) end
    -- A click right after the long hold must not complete a double click.
    f:xp(); f:xp(L3); f:xp()
    assert((f.screenshots or 0)==0,'long hold counted as a click')
    -- Two quick clicks still take a screenshot.
    for i=1,20 do f:xp() end
    f:xp(L3); f:xp(); f:xp(L3); f:xp()
    assert((f.screenshots or 0)==1,'double L3 '..tostring(f.screenshots))
end)
test('holding both triggers first calls up the views card; L3 then keeps it',function()
    local f=fixture(); f:xp()
    for i=1,26 do f:xp(0,255,255) end
    assert(f.values.WuWaControls_Card=='views:0:1:1',tostring(f.values.WuWaControls_Card))
    f:xp(L3,255,255); assert(f.values.WuWaControls_Card=='views:64:1:1',tostring(f.values.WuWaControls_Card))
    f:xp(); assert(f.values.WuWaControls_Card=='','views card after release')
end)
test('portal still toggles while the L3 card is up',function()
    local f=fixture(); f:xp()
    for i=1,30 do f:xp(L3) end
    f:xp(L3,255,0); f:count('WindowMode_Enabled',1)
    assert(f.values.WuWaControls_Card:sub(1,3)=='l3:',tostring(f.values.WuWaControls_Card))
end)
print('PASS '..passed..' PlayStation shared-shortcut integration groups; no device/game access')
