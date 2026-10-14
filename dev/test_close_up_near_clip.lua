-- Close-up near clip (test 15): what the camera script asks for, shot by shot.
-- Pure callbacks; no game, device or native calls. Lua 5.4:
-- lua dev/test_close_up_near_clip.lua mod/lua/02_WuWaVR_PolarControls.lua
local source=assert(arg[1], 'Pass the PolarControls script path')
local NOW='WuWaControls_NearClipNow'
local failures,passed={},0

-- A player camera: lens in degrees, the game's focus in cm (nil: no focus), at a fixed spot.
local function fixture()
    local f={callbacks={},errors={},logs={},sends={},clock=0}
    f.pp={bOverride_DepthOfFieldFocalDistance=true,DepthOfFieldFocalDistance=145,
        bOverride_DepthOfFieldFstop=true,DepthOfFieldFstop=2.3}
    f.pcm={fov=37.5,CameraCachePrivate={POV={PostProcessSettings=f.pp}}}
    function f.pcm:GetFOVAngle() return self.fov end
    function f.pcm:GetCameraLocation() return {X=-203,Y=223,Z=41} end
    function f.pcm:GetCameraRotation() return {Pitch=0,Yaw=5.5,Roll=0} end
    f.pc={bShowMouseCursor=false,PlayerCameraManager=f.pcm,get_class=function() return {find_function=function() end} end}
    f.values={WuWaControls_Enabled='true',WuWaControls_Focused='true',WuWaControls_CameraMode='0',
        WuWaControls_LensMatch='75',WuWaControls_LensNearest='10',WuWaControls_CloseUpNearClip='20',
        VR_2DScreenMode='false',VR_MonoTheatreMode='false',VR_EnableGUI='true',VR_WorldScale='1'}
    local env=setmetatable({uevr={
        api={get_player_controller=function() return f.pc end,get_local_pawn=function() end},
        params={vr={
            get_mod_value=function(_,key)
                if key=='WuWaControls_EffectiveMonoTheatre' then return f.values.VR_MonoTheatreMode end
                if key=='WuWaControls_EffectiveScreen' then return tostring(f.values.VR_2DScreenMode=='true' or f.values.VR_MonoTheatreMode=='true') end
                return f.values[key] or 'false'
            end,
            set_mod_value=function(key,value)
                if key==NOW then f.sends[#f.sends+1]=value end
                f.values[key]=value
            end},functions={is_drawing_ui=function() return false end,dispatch_custom_event=function() end,
                log_info=function(s) f.logs[#f.logs+1]=s end,
                log_warn=function(s) f.errors[#f.errors+1]=s end,log_error=function(s) f.errors[#f.errors+1]=s end}},
        sdk={callbacks=setmetatable({},{__index=function(_,name) return function(fn) f.callbacks[name]=fn end end})}},
        json={dump_string=function() return '{}' end}},{__index=_G})
    assert(loadfile(source,'t',env))()
    -- One rendered frame: the engine tick (camera mode, view), then the draw callback where the lens runs.
    function f:frame()
        self.clock=self.clock+.016; self.values.WuWaControls_Clock=tostring(self.clock)
        self.callbacks.on_pre_engine_tick(nil,.016)
        self.callbacks.on_pre_viewport_client_draw()
        assert(#self.errors==0,self.errors[1])
        assert(not tostring(self.values.WuWaControls_LensStatus or ''):find('Unavailable'),self.values.WuWaControls_LensStatus)
        return tonumber(self.values[NOW] or '0')
    end
    function f:frames(n) local v; for _=1,n or 3 do v=self:frame() end; return v end
    return f
end
local function test(name,body)
    local ok,message=pcall(body)
    if ok then passed=passed+1; print('PASS: '..name)
    else failures[#failures+1]=name..': '..tostring(message); print('FAIL: '..failures[#failures]) end
end
local function eq(got,want,what) assert(got==want,what..': got '..tostring(got)..', expected '..tostring(want)) end

test('a dialogue close-up asks for the setting every frame and logs it with the shot',function()
    local f=fixture()
    eq(f:frame(),20,'first frame of the shot')
    local before=#f.sends; f:frames(5)
    eq(#f.sends-before,5,'requests in five frames (a request lapses after a second)')
    local logged=false
    for _,line in ipairs(f.logs) do if line:find('%[WuWaLens%] shot=1') and line:find('near_clip=20') then logged=true end end
    assert(logged,'shot log line has no near_clip=20')
    assert(f.values.WuWaControls_LensStatus:find('hiding anything nearer than 20 cm',1,true),f.values.WuWaControls_LensStatus)
end)
test('it stays 10 cm short of the person the game focuses on, after the close-up move',function()
    local f=fixture()
    f.pp.DepthOfFieldFocalDistance=60      -- move 33 cm: 27 cm left, so 17 cm at most
    eq(f:frames(),16,'tight shot')
    f=fixture(); f.pp.DepthOfFieldFocalDistance=35   -- 15.5 cm left: no room above 10 cm
    eq(f:frames(),0,'very tight shot')
end)
test('the game own plane outside close-ups, on screens, in other camera modes and when set to 10',function()
    local f=fixture(); eq(f:frames(),20,'close-up')
    f.pcm.fov=75; eq(f:frames(),0,'wide shot')
    local sends=#f.sends; f:frames(4); eq(#f.sends,sends,'repeats of the off request')
    f.pcm.fov=37.5; eq(f:frames(),20,'next close-up')
    f.values.VR_2DScreenMode='true'; eq(f:frames(),0,'stereo screen')
    f.values.VR_2DScreenMode='false'; eq(f:frames(),20,'back in full VR')
    f.values.WuWaControls_CameraMode='1'; eq(f:frames(),0,'another camera mode')
    f.values.WuWaControls_CameraMode='0'; eq(f:frames(),20,'game camera again')
    f.values.WuWaControls_CloseUpNearClip='10'; eq(f:frames(),0,'setting at the game own 10 cm')
end)
test('it works with Close-up match off and in shots without a focus',function()
    local f=fixture(); f.values.WuWaControls_LensMatch='0'
    eq(f:frames(),20,'Close-up match off: no move, plane still raised')
    f=fixture(); f.pp.bOverride_DepthOfFieldFocalDistance=false
    eq(f:frames(),20,'no focus')
    local logged=false
    for _,line in ipairs(f.logs) do if line:find('subject=none') and line:find('near_clip=20') then logged=true end end
    assert(logged,'a shot without a focus is not logged')
end)
test('a raised setting is capped at 50 cm and the lens rows never ask below the game own plane',function()
    local f=fixture(); f.values.WuWaControls_CloseUpNearClip='80'; f.pp.DepthOfFieldFocalDistance=400
    eq(f:frames(),50,'capped setting')
    f.values.WuWaControls_CloseUpNearClip='3'; eq(f:frames(),0,'below the range')
end)

print(string.format('Close-up near clip tests passed: %d', passed))
if #failures>0 then error(#failures..' failed') end
